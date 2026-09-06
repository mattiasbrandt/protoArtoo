// =============================================================================
// src/web/api_console.cpp
//
// POST /api/console - browser adapter for the Controller Console (ADR 0036).
// The same command processor (consoleExecuteCommand) that the serial task
// uses, driven by a web-specific sink, over two response paths:
//
//  - Bounded path (every command except `operations`): accumulates records
//    into a fixed-size array + value arena, then serializes once into a
//    fixed response buffer. Every command answered this way today (help,
//    status queries, single-record guard results) is small; hitting the
//    capacity is handled explicitly (#240) rather than silently, as a
//    safety net for whatever lands here next.
//
//    `system.status.logs` (#239) is the one exception this path still
//    answers: its item count can exceed both the record-array cap and the
//    value arena, but its source (the log ring) is live/mutable, so it
//    cannot use the streaming path below the way `operations` does (see
//    handleConsolePost()'s system.status.logs branch for why). Instead the
//    bounded sink truncates it gracefully - keeping the NEWEST lines, never
//    a silent per-value truncation, never an HTTP 500 - and reports the
//    truncation on the JSON response envelope (`"truncated":true`), not as a
//    new Console Record field. See webOnRecordItem_impl() for the mechanism.
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
#include "../../include/web_server.h"   // getLogBufferCount() - #239's system.status.logs
                                         // peek, below
#include "../../include/log_buffer.h"   // LOG_LINE_MAX - the per-item arena reserve #239 needs
                                         // (pure header, no Arduino/FreeRTOS dependency)

// Maximum number of records + value storage for a single BOUNDED response.
// `operations` never uses this path (see fillOperationsResponse below) -
// this cap sizes the small, fixed responses every other command produces.
#define CONSOLE_RESPONSE_RECORDS_MAX 32
#define CONSOLE_RECORD_VALUE_ARENA 2048

// Worst-case bytes one item's value can consume in the arena. The longest
// item value any query emits today is a log line (system.status.logs, #239),
// up to LOG_LINE_MAX-1 bytes. Reserving this much before accepting an item
// guarantees arenaStoreString() never has to silently truncate a value on
// this path - an item this file refuses is visible on the wire
// (webSink.itemsTruncated -> "truncated":true); one arenaStoreString() quietly
// shortened would not be. Revisit if a future item-emitting query's values
// can be longer than a log line.
#define CONSOLE_ITEM_VALUE_RESERVE_BYTES LOG_LINE_MAX

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
    // missing its close (#240). system.status.logs (#239) never reaches this:
    // its item count is bounded up front by itemsToSkip below, precisely so
    // it degrades via itemsTruncated instead.
    bool overflowed;
    // #239: system.status.logs-style graceful item truncation.
    // itemsToSkip is set once, before consoleExecuteCommand() runs
    // (handleConsolePost()'s system.status.logs branch), to the number of
    // OLDEST items to discard so the KEPT items are the newest ones - a
    // discarded item never touches records[]/valueArena, so skipping costs
    // nothing, unlike storing-then-evicting an already-kept item would.
    // itemsTruncated is set the moment any item is skipped this way, or an
    // item is refused for arena headroom (webOnRecordItem_impl) - reported to
    // the client on the JSON response envelope ("truncated":true), never as
    // a new Console Record field; the wire protocol itself is unchanged.
    size_t itemsToSkip;
    bool itemsTruncated;
};

// #266: this struct alone was ~3.1 KB on the real 32-bit target, and living
// on handleConsolePost()'s stack (alongside two 256-byte command buffers) was
// enough by itself to overflow the httpd task's 8 KB stack on the very first
// POST, for every command including the smallest possible one - measured via
// `-fstack-usage` (handleConsolePost()'s frame: 3776 B before, 240 B after
// moving it and the buffers to static storage; see handleConsolePost()'s own
// comment). Guards against this struct growing back into stack-sized
// territory unnoticed: a future field addition that trips this budget must
// re-measure the static chain (this struct is now `static`, so it costs .bss
// - RAM budget in tools/build_budgets.json - not stack, but a large
// ADDITIONAL request-scoped struct like this one, declared as a stack local
// elsewhere in this file or a new adapter, would reopen exactly this defect)
// rather than silently raising the number here.
//
// PA_NATIVE_TEST_STUBS-gated out: the native test build compiles this same
// struct for the HOST'S pointer width, not the target's. ConsoleRecord holds
// seven `const char*` fields, so a 64-bit host makes this struct ~1 KB larger
// than the real ESP32/ESP32-P4 build (measured: 4136 B host vs 3092 B
// target) for a reason with zero bearing on either chip's actual stack
// (native tests never run on an httpd task) - asserting the target's number
// against a host-compiled size would either false-fail on every native build
// or have to be loosened past the point of guarding anything.
#if !defined(PA_NATIVE_TEST_STUBS)
static_assert(sizeof(ConsoleWebSink) <= 3200,
              "ConsoleWebSink grew past its #266 stack-safety budget - "
              "re-measure handleConsolePost()'s -fstack-usage frame before "
              "raising this number");
#endif

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

    // #239: discard the oldest items first, before they ever reach the array
    // or the arena - handleConsolePost()'s system.status.logs branch sets
    // this to keep the response's items the newest ones, not whichever the
    // executor's loop (oldest-first, src/console/console_module.cpp) happens
    // to reach first.
    if (g_currentWebSink->itemsToSkip > 0) {
        g_currentWebSink->itemsToSkip--;
        g_currentWebSink->itemsTruncated = true;
        return;
    }

    if (g_currentWebSink->recordCount >= CONSOLE_RESPONSE_RECORDS_MAX) {
        g_currentWebSink->overflowed = true;
        return;
    }

    // #239: refuse rather than let arenaStoreString() truncate this value's
    // bytes silently - a record slot may still be free while the arena is
    // not, once item values run long (e.g. near-full-length log lines).
    size_t arenaRemaining = CONSOLE_RECORD_VALUE_ARENA - g_currentWebSink->arenaUsed;
    if (arenaRemaining < CONSOLE_ITEM_VALUE_RESERVE_BYTES) {
        g_currentWebSink->itemsTruncated = true;
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
    // Static, not stack (#266): ConsoleWebSink alone is ~3.1 KB (records[32] +
    // the 2 KB value arena) and this function's other locals brought the
    // frame to 3776 B (measured via `-fstack-usage`) - on an 8 KB httpd task
    // stack that is also carrying the PsychicHttp/esp_http_server/lwIP
    // dispatch chain beneath every route AND consoleExecuteCommand()'s own
    // fixed ~1.9 KB frame (paid on every call regardless of which operation
    // runs - console_module.cpp's lineBuf[256] plus its dispatch switch),
    // that was enough on its own to overflow the task stack on the very
    // first POST, for every command including the smallest possible one
    // (#266's measurement). Same fix, same reasoning as api_status.cpp's
    // `body[3072]` comment on handleStatusGet(): one "httpd" task processes
    // one request at a time (PsychicHttp's default synchronous dispatch, the
    // same assumption g_currentWebSink/g_operationsCommand below already
    // make), so a static buffer here is race-free and costs .bss instead of
    // stack. Explicitly reset every field below rather than relying on
    // static zero-init, which only runs once at boot, not per request.
    static char command[256];
    memset(command, 0, sizeof(command));
    static ConsoleWebSink webSink;
    memset(&webSink, 0, sizeof(webSink));

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
            // Static for the same reason as `command`/`webSink` above (#266):
            // one httpd-task request in flight at a time, so no reset is
            // needed beyond the snprintf() below, which always fully
            // (re)terminates it before use.
            static char routeScratch[sizeof(command)];
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

            // system.status.logs (#239): the ring can hold up to
            // LOG_RING_MAX_LINES lines - chip-dependent, not one fixed number
            // (include/log_buffer.h:97-116): 48 on artoo-esp32, 112 on the
            // ESP32-P4 - more than the bounded path below can safely hold on
            // either board (webOnRecordItem_impl's
            // two independent limits: CONSOLE_RESPONSE_RECORDS_MAX's record
            // array, reserving 2 slots for begin/end, and
            // CONSOLE_RECORD_VALUE_ARENA's value arena, reserving
            // CONSOLE_ITEM_VALUE_RESERVE_BYTES per item worst-case). Peeking
            // at the ring's current count here, before consoleExecuteCommand()
            // runs, lets the sink discard the OLDEST lines up front (cheaply -
            // a skipped line never touches records[]/valueArena) so the kept
            // lines are the most recent ones, not whichever the executor's
            // oldest-first loop happens to reach before running out of room.
            //
            // This does NOT stream like `operations` above: the log ring is
            // live/mutable mid-response (unlike the compile-time catalog), so
            // replaying consoleExecuteCommand() once per HTTP chunk could see
            // a different ring state on each replay and corrupt
            // sendChunked()'s offset-based framing (include/web_json_slice_writer.h:
            // "the data a producer reads must be ... stable across the
            // calls"). The documented fix for that - snapshotting the source
            // into file-scope state before the send starts - is exactly the
            // second buffer #239 already ruled out on RAM grounds
            // (8,580 B free on artoo-esp32 at the ticket's base commit).
            if (routeName != nullptr && strcmp(routeName, "system.status.logs") == 0) {
                size_t total = getLogBufferCount();
                size_t recordCap = CONSOLE_RESPONSE_RECORDS_MAX - 2;  // reserve begin+end
                size_t arenaCap =
                    (CONSOLE_RECORD_VALUE_ARENA - CONSOLE_ITEM_VALUE_RESERVE_BYTES) /
                    CONSOLE_ITEM_VALUE_RESERVE_BYTES;  // one reserve's worth held back for
                                                        // the begin record's own value
                size_t cap = (arenaCap < recordCap) ? arenaCap : recordCap;
                webSink.itemsToSkip = (total > cap) ? (total - cap) : 0;
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
    // group was complete - with nothing on the wire to say so. This is
    // still the safety net for whatever hits it unexpectedly; system.status.logs
    // (#239) reaches this path's array/arena limits routinely (a 48-line ring
    // is at or near full in normal operation) but never sets `overflowed` -
    // see webOnRecordItem_impl() and handleConsolePost()'s system.status.logs
    // branch above, which keep it under both limits deliberately and report
    // the truncation on the response envelope below instead.
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

    // #239: present exactly when the bounded sink had to drop or refuse an
    // item (system.status.logs on a near-full ring) - absent on every other
    // response, matching the "reason= present exactly when there is one"
    // convention the Console Records themselves already use
    // (consoleReasonIsPresent(), include/console_record.h). This lives on the
    // JSON envelope, not as a new Console Record field: the wire protocol
    // (docs/console-protocol.md) is unchanged by this fix.
    if (webSink.itemsTruncated) {
        responseDoc["truncated"] = true;
    }

    size_t bodySize = serializeJson(responseDoc, responseBody, sizeof(responseBody));
    if (bodySize == 0 || bodySize >= sizeof(responseBody)) {
        req.send(500, "application/json", "{\"ok\":false,\"error\":\"response too large\"}");
        return;
    }

    req.send(200, "application/json", responseBody);
}
