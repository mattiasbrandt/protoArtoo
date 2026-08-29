// =============================================================================
// src/web/api_console.cpp
//
// POST /api/console - browser adapter for the Controller Console (ADR 0034).
// The same command processor (consoleExecuteCommand) that the serial task
// uses, driven by a web-specific sink, over two response paths:
//
//  - Bounded path (every command except `operations`): accumulates records
//    into a fixed-size array + value arena, then serializes once into a
//    fixed response buffer. Every command answered this way today (help,
//    status queries, single-record guard results) is small; hitting the
//    capacity is handled explicitly (#240) rather than silently, as a
//    safety net for whatever lands here next.
//  - Streaming path (`operations` and `operations type=<t>` only): the
//    catalog listing has no fixed size (175 entries and growing) and cannot
//    fit the bounded path's array or response buffer. See that path's own
//    comment below for why only this one meta-command may stream.
//
// The bounded path's sink copies field values into owned storage because the
// module reuses a single stack buffer for successive field emissions.
// =============================================================================

#include "../../include/api_console.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <string.h>
#include <ctype.h>

#include "../../include/console_module.h"
#include "../../include/console_record.h"
#include "../../include/console_args.h"  // consoleSplitCommandLine() - the `operations` routing
                                          // check below (#221)
#include "../../include/web_json_slice_writer.h"

// Maximum number of records + value storage for a single BOUNDED response.
// `operations` never uses this path (see fillOperationsResponse below) -
// this cap sizes the small, fixed responses every other command produces.
#define CONSOLE_RESPONSE_RECORDS_MAX 32
#define CONSOLE_RECORD_VALUE_ARENA 2048

struct ConsoleRecord {
    const char* type;
    uint32_t requestId;
    const char* status;
    const char* outcome;
    const char* reason;
    const char* operation;
    const char* name;
    const char* value;
};

// Arena holds copied values; all record pointers reference this storage
struct ConsoleWebSink {
    ConsoleRecord records[CONSOLE_RESPONSE_RECORDS_MAX];
    size_t recordCount;
    char valueArena[CONSOLE_RECORD_VALUE_ARENA];
    size_t arenaUsed;
    // Set the moment a sink callback cannot record a record because the
    // array is full - including the `end`/`result` record that would have
    // closed the group. handleConsolePost() checks this before building any
    // response, so a command that outgrows this path answers with an
    // explicit failure instead of a JSON body that looks complete but is
    // missing its close (#240).
    bool overflowed;
};

static ConsoleWebSink* g_currentWebSink = nullptr;

// Helper: copy string into arena, return pointer.
// Truncates on overflow; never returns the module's buffer to avoid dangling pointers.
// Guard against overflow: if we cannot store even a NUL terminator, return nullptr.
static const char* arenaStoreString(const char* src) {
    if (g_currentWebSink == nullptr || src == nullptr) return src;
    size_t len = strlen(src);
    size_t available = CONSOLE_RECORD_VALUE_ARENA - g_currentWebSink->arenaUsed;
    // Need space for at least the NUL terminator
    if (available == 0) return "";
    if (len + 1 > available) len = available - 1;  // Truncate to fit, leaving room for NUL
    char* dest = &g_currentWebSink->valueArena[g_currentWebSink->arenaUsed];
    if (len > 0) strncpy(dest, src, len);
    dest[len] = '\0';
    g_currentWebSink->arenaUsed += len + 1;
    return dest;
}

static void webOnRecordBegin_impl(uint32_t requestId, const char* operationType) {
    if (g_currentWebSink == nullptr) return;
    if (g_currentWebSink->recordCount >= CONSOLE_RESPONSE_RECORDS_MAX) {
        g_currentWebSink->overflowed = true;
        return;
    }
    ConsoleRecord* rec = &g_currentWebSink->records[g_currentWebSink->recordCount++];
    rec->type = "begin";
    rec->requestId = requestId;
    rec->operation = arenaStoreString(operationType);
}

static void webOnRecordField_impl(uint32_t requestId, const char* name, const char* value) {
    if (g_currentWebSink == nullptr) return;
    if (g_currentWebSink->recordCount >= CONSOLE_RESPONSE_RECORDS_MAX) {
        g_currentWebSink->overflowed = true;
        return;
    }
    ConsoleRecord* rec = &g_currentWebSink->records[g_currentWebSink->recordCount++];
    rec->type = "field";
    rec->requestId = requestId;
    rec->name = arenaStoreString(name);
    rec->value = arenaStoreString(value);
}

static void webOnRecordItem_impl(uint32_t requestId, const char* value) {
    if (g_currentWebSink == nullptr) return;
    if (g_currentWebSink->recordCount >= CONSOLE_RESPONSE_RECORDS_MAX) {
        g_currentWebSink->overflowed = true;
        return;
    }
    ConsoleRecord* rec = &g_currentWebSink->records[g_currentWebSink->recordCount++];
    rec->type = "item";
    rec->requestId = requestId;
    rec->value = arenaStoreString(value);
}

static void webOnRecordResult_impl(uint32_t requestId, ConsoleStatus status,
                                   ConsoleOutcome outcome, ConsoleReason reason) {
    if (g_currentWebSink == nullptr) return;
    if (g_currentWebSink->recordCount >= CONSOLE_RESPONSE_RECORDS_MAX) {
        g_currentWebSink->overflowed = true;
        return;
    }
    ConsoleRecord* rec = &g_currentWebSink->records[g_currentWebSink->recordCount++];
    rec->type = "result";
    rec->requestId = requestId;
    rec->status = consoleStatusString(status);
    rec->outcome = consoleOutcomeString(outcome);
    // Present exactly when there is a reason, matching the serial adapter.
    if (consoleReasonIsPresent(reason)) rec->reason = consoleReasonString(reason);
}

static void webOnRecordEnd_impl(uint32_t requestId, ConsoleStatus status,
                               ConsoleOutcome outcome, ConsoleReason reason) {
    if (g_currentWebSink == nullptr) return;
    if (g_currentWebSink->recordCount >= CONSOLE_RESPONSE_RECORDS_MAX) {
        // The record that would have closed the group is exactly the one
        // that cannot fit: never let this be a silent drop (#240).
        g_currentWebSink->overflowed = true;
        return;
    }
    ConsoleRecord* rec = &g_currentWebSink->records[g_currentWebSink->recordCount++];
    rec->type = "end";
    rec->requestId = requestId;
    rec->status = consoleStatusString(status);
    rec->outcome = consoleOutcomeString(outcome);
    // Present exactly when there is a reason, matching the serial adapter.
    if (consoleReasonIsPresent(reason)) rec->reason = consoleReasonString(reason);
}

// =============================================================================
// Streaming path: the `operations` and `operations type=<t>` meta-command
// =============================================================================
// The bounded path above cannot answer this command: the unfiltered catalog
// is 175 entries (docs/action-registry.yaml) and growing, and no static
// array or the fixed response buffer below can hold it (#240).
//
// `operations` is architecturally guaranteed side-effect-free: console_module.cpp
// dispatches it as its own case, before the CONSOLE_OP_ACTION/CONFIG switch that
// runs real operations - it only enumerates the compile-time catalog table
// (consoleCatalogGetEntries()) and formats each entry's name into an item
// record. That is what makes it safe to re-run consoleExecuteCommand() once
// per HTTP chunk, which is what WebRequest::sendChunked()'s offset-replay
// contract requires (include/web_json_slice_writer.h: "each call re-walks the
// whole logical body"): every replay reads the same static table and produces
// byte-identical output.
//
// Do NOT extend this streaming path to any other operation name. The general
// switch in consoleExecuteCommand() is where CONSOLE_OP_ACTION/CONFIG will
// dispatch real actions and config writes once their executors land (#220 and
// later) - replaying one of those through an offset-replay filler would fire
// it more than once per response, which is a completely different, and much
// worse, defect than the one this file fixes.
static JsonSliceWriter* g_operationsWriter = nullptr;
static bool g_operationsFirstRecord = true;

// Opens `{"id":<n>,"type":"<type>"` - caller appends any further fields and
// the closing `}`. Handles the leading comma between array elements.
static void opsBeginRecord(uint32_t requestId, const char* type) {
    if (!g_operationsFirstRecord) {
        g_operationsWriter->append(',');
    }
    g_operationsFirstRecord = false;
    g_operationsWriter->append("{\"id\":");
    g_operationsWriter->appendUint(requestId);
    g_operationsWriter->append(",\"type\":\"");
    g_operationsWriter->append(type);
    g_operationsWriter->append('"');
}

static void opsOnRecordBegin(uint32_t requestId, const char* operationType) {
    if (g_operationsWriter == nullptr) return;
    opsBeginRecord(requestId, "begin");
    g_operationsWriter->append(",\"operation\":");
    g_operationsWriter->appendJsonString(operationType);
    g_operationsWriter->append('}');
}

static void opsOnRecordItem(uint32_t requestId, const char* value) {
    if (g_operationsWriter == nullptr) return;
    opsBeginRecord(requestId, "item");
    g_operationsWriter->append(",\"value\":");
    g_operationsWriter->appendJsonString(value);
    g_operationsWriter->append('}');
}

// Shared by onRecordResult (the invalid type= filter guard path) and
// onRecordEnd (the normal close of the listing) - both carry the same
// status/outcome/reason shape.
static void opsWriteResultLike(uint32_t requestId, const char* type, ConsoleStatus status,
                               ConsoleOutcome outcome, ConsoleReason reason) {
    opsBeginRecord(requestId, type);
    g_operationsWriter->append(",\"status\":\"");
    g_operationsWriter->append(consoleStatusString(status));
    g_operationsWriter->append("\",\"outcome\":\"");
    g_operationsWriter->append(consoleOutcomeString(outcome));
    g_operationsWriter->append('"');
    if (consoleReasonIsPresent(reason)) {
        g_operationsWriter->append(",\"reason\":");
        g_operationsWriter->appendJsonString(consoleReasonString(reason));
    }
    g_operationsWriter->append('}');
}

static void opsOnRecordResult(uint32_t requestId, ConsoleStatus status, ConsoleOutcome outcome,
                              ConsoleReason reason) {
    if (g_operationsWriter == nullptr) return;
    opsWriteResultLike(requestId, "result", status, outcome, reason);
}

static void opsOnRecordEnd(uint32_t requestId, ConsoleStatus status, ConsoleOutcome outcome,
                           ConsoleReason reason) {
    if (g_operationsWriter == nullptr) return;
    opsWriteResultLike(requestId, "end", status, outcome, reason);
}

// Fixed once per HTTP request, before sendChunked() starts calling
// fillOperationsResponse() (see file-header comment for why every call must
// see the same command line and request id): sendChunked()'s contract calls
// the filler repeatedly with different byte offsets into the same logical
// body, so a value that changed between calls would make different replays
// disagree about what that body is.
static char g_operationsCommand[256];
static uint32_t g_operationsRequestId;

static size_t fillOperationsResponse(uint8_t* output, size_t capacity, size_t offset) {
    JsonSliceWriter writer(output, capacity, offset);
    g_operationsWriter = &writer;
    g_operationsFirstRecord = true;

    writer.append("{\"records\":[");

    ConsoleRequest request = {
        .requestId = g_operationsRequestId,
        .source = CONSOLE_SOURCE_WEB,
        .operationName = g_operationsCommand,
    };
    // `operations` never calls onRecordField (console_module.cpp only emits
    // begin/item/result/end for this meta-command) - left null rather than a
    // dead handler, so the sink contract stays honest about what this path
    // actually implements.
    ConsoleRecordSink sink = {
        .onRecordBegin = opsOnRecordBegin,
        .onRecordField = nullptr,
        .onRecordItem = opsOnRecordItem,
        .onRecordResult = opsOnRecordResult,
        .onRecordEnd = opsOnRecordEnd,
    };
    consoleExecuteCommand(&request, &sink);

    writer.append("]}");
    g_operationsWriter = nullptr;
    return writer.written();
}

void handleConsolePost(WebRequest& req) {
    char command[256] = {};
    ConsoleWebSink webSink = {};

    // Check for truncation in form parameter (D13: detect oversized command)
    const char* paramValue = req.paramRef("command");
    if (paramValue != nullptr) {
        if (strlen(paramValue) >= sizeof(command)) {
            // Line too long; emit single result record with error reason, then serialize
            g_currentWebSink = &webSink;
            uint32_t reqId = consoleGetNextRequestId();
            webOnRecordResult_impl(reqId, CONSOLE_STATUS_ERR, CONSOLE_OUTCOME_INVALID, CONSOLE_REASON_LINE_TOO_LONG);
            g_currentWebSink = nullptr;
        } else {
            snprintf(command, sizeof(command), "%s", paramValue);
        }
    }

    if (command[0] == '\0') {
        const char* body = req.body();
        if (body != nullptr) {
            JsonDocument bodyDoc;
            if (deserializeJson(bodyDoc, body)) {
                req.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid json body\"}");
                return;
            }
            JsonVariantConst cmdVar = bodyDoc["command"];
            if (!cmdVar.is<const char*>()) {
                req.send(400, "application/json", "{\"ok\":false,\"error\":\"missing command\"}");
                return;
            }
            // Also check JSON body command length
            const char* jsonCmd = cmdVar.as<const char*>();
            if (strlen(jsonCmd) >= sizeof(command)) {
                req.send(400, "application/json", "{\"ok\":false,\"error\":\"command too long\"}");
                return;
            }
            snprintf(command, sizeof(command), "%s", jsonCmd);
        }
    }

    // Only process command through module if we didn't get a truncation error
    if (webSink.recordCount == 0 && command[0] != '\0') {
        // Cast to (unsigned char) to avoid UB on high-bit chars
        size_t start = 0;
        while (command[start] && isspace((unsigned char)command[start])) start++;
        size_t end = strlen(command);
        while (end > start && isspace((unsigned char)command[end - 1])) end--;
        command[end] = '\0';
        if (start > 0) memmove(command, command + start, end - start + 1);

        if (command[0] != '\0') {
            // `operations` (and its `type=` filter form) streams instead of
            // going through the bounded sink below - see the streaming
            // path's comment above fillOperationsResponse() for why this is
            // the one operation name allowed to. Routed by the SAME name
            // split console_module.cpp uses (consoleSplitCommandLine(),
            // include/console_args.h) on a scratch copy - `command` itself
            // must stay intact, since both this streaming branch and the
            // bounded path below need the full, unmutated combined line -
            // so this file and the module can never disagree about which
            // command names it, including when whitespace between the name
            // and "type=" is more than one space.
            char routeScratch[sizeof(command)];
            snprintf(routeScratch, sizeof(routeScratch), "%s", command);
            char* routeName = nullptr;
            char* routeArgsUnused = nullptr;
            consoleSplitCommandLine(routeScratch, &routeName, &routeArgsUnused);
            if (routeName != nullptr && strcmp(routeName, "operations") == 0) {
                g_operationsRequestId = consoleGetNextRequestId();
                snprintf(g_operationsCommand, sizeof(g_operationsCommand), "%s", command);
                if (!req.sendChunked("application/json", fillOperationsResponse)) {
                    req.send(500, "application/json",
                             "{\"ok\":false,\"error\":\"response alloc failed\"}");
                }
                return;
            }

            g_currentWebSink = &webSink;

            // Pass FULL command line to module (not just first token, which enables "help system.status.health")
            ConsoleRequest consoleReq = {
                .requestId = consoleGetNextRequestId(),
                .source = CONSOLE_SOURCE_WEB,
                .operationName = command,
            };

            ConsoleRecordSink sink = {
                .onRecordBegin = webOnRecordBegin_impl,
                .onRecordField = webOnRecordField_impl,
                .onRecordItem = webOnRecordItem_impl,
                .onRecordResult = webOnRecordResult_impl,
                .onRecordEnd = webOnRecordEnd_impl,
            };

            consoleExecuteCommand(&consoleReq, &sink);
            g_currentWebSink = nullptr;
        }
    }

    if (webSink.recordCount == 0 && command[0] == '\0') {
        req.send(400, "application/json", "{\"ok\":false,\"error\":\"empty command\"}");
        return;
    }

    // The bounded path's capacity was exceeded (#240): answer with an
    // explicit failure rather than a JSON body that dropped records -
    // possibly including the `end` that would have told the client the
    // group was complete - with nothing on the wire to say so. No command
    // on this path is known to reach 32 records today; this is the safety
    // net for whatever does next, not a measured case.
    if (webSink.overflowed) {
        req.send(500, "application/json",
                 "{\"ok\":false,\"error\":\"response too large for this adapter\"}");
        return;
    }

    static char responseBody[4096];
    memset(responseBody, 0, sizeof(responseBody));

    JsonDocument responseDoc;
    JsonArray recordsArray = responseDoc["records"].to<JsonArray>();

    for (size_t i = 0; i < webSink.recordCount; i++) {
        const ConsoleRecord& rec = webSink.records[i];
        JsonObject recordObj = recordsArray.add<JsonObject>();
        recordObj["id"] = rec.requestId;
        recordObj["type"] = rec.type;
        if (strcmp(rec.type, "begin") == 0) {
            recordObj["operation"] = rec.operation;
        } else if (strcmp(rec.type, "field") == 0) {
            recordObj["name"] = rec.name;
            recordObj["value"] = rec.value;
        } else if (strcmp(rec.type, "item") == 0) {
            recordObj["value"] = rec.value;
        } else if (strcmp(rec.type, "result") == 0 || strcmp(rec.type, "end") == 0) {
            recordObj["status"] = rec.status;
            recordObj["outcome"] = rec.outcome;
            if (rec.reason != nullptr) recordObj["reason"] = rec.reason;
        }
    }

    size_t bodySize = serializeJson(responseDoc, responseBody, sizeof(responseBody));
    if (bodySize == 0 || bodySize >= sizeof(responseBody)) {
        req.send(500, "application/json", "{\"ok\":false,\"error\":\"response too large\"}");
        return;
    }

    req.send(200, "application/json", responseBody);
}
