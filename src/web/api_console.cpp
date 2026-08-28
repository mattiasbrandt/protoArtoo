// =============================================================================
// src/web/api_console.cpp
//
// POST /api/console - browser adapter for the Controller Console (ADR 0034).
// The same command processor (consoleExecuteCommand) that the serial task uses,
// driven by a web-specific sink that accumulates records into a JSON response.
// =============================================================================

#include "../../include/api_console.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <string.h>
#include <ctype.h>

#include "../../include/console_module.h"
#include "../../include/console_record.h"

// =============================================================================
// Web-Specific Console Record Sink
//
// The sink is bound at request time; state lives in the stack, so the handler
// must hold storage for all records in a command's response. This is practical:
// - T1 scope (system.status.health): ~6 records max
// - HTTP responses are already buffered and serialized to JSON
// - The sink executes inline, no async coordination needed
// =============================================================================

// Maximum number of records in a single response. Conservative upper bound
// for T1 (status queries) and early T2 (simple actions).
#define CONSOLE_RESPONSE_RECORDS_MAX 32

// Holder for a single record in the response. Fields are accumulated from
// sink callbacks and indexed by record type.
struct ConsoleRecord {
    // Record type (result, begin, field, item, end)
    const char* type;
    uint32_t requestId;

    // Single-record responses (result, end)
    const char* status;    // "ok" or "err"
    const char* outcome;   // "queued", "applied", etc
    const char* reason;    // optional: "unknown-operation", etc

    // Multi-record responses (begin, field, item, end)
    const char* operation;  // operation name for begin
    const char* name;       // field name (for type=field)
    const char* value;      // field value (for type=field or item)
};

// Accumulator for a command response
struct ConsoleWebSink {
    ConsoleRecord records[CONSOLE_RESPONSE_RECORDS_MAX];
    size_t recordCount;
    // Tracking state for multi-record sequences
    uint32_t currentRequestId;
    bool inMultiRecord;  // true between begin and end
    bool hasError;       // true if any record has status=err
};

// Forward declaration for global sink context (set during handleConsolePost)
static ConsoleWebSink* g_currentWebSink = nullptr;

// =============================================================================
// Sink Callback Implementations
// =============================================================================

static void webOnRecordBegin_impl(uint32_t requestId, const char* operationType) {
    if (g_currentWebSink == nullptr || g_currentWebSink->recordCount >= CONSOLE_RESPONSE_RECORDS_MAX) {
        return;
    }
    ConsoleRecord* rec = &g_currentWebSink->records[g_currentWebSink->recordCount++];
    rec->type = "begin";
    rec->requestId = requestId;
    rec->operation = operationType;
    g_currentWebSink->currentRequestId = requestId;
    g_currentWebSink->inMultiRecord = true;
}

static void webOnRecordField_impl(uint32_t requestId, const char* name, const char* value) {
    if (g_currentWebSink == nullptr || g_currentWebSink->recordCount >= CONSOLE_RESPONSE_RECORDS_MAX) {
        return;
    }
    ConsoleRecord* rec = &g_currentWebSink->records[g_currentWebSink->recordCount++];
    rec->type = "field";
    rec->requestId = requestId;
    rec->name = name;
    rec->value = value;
}

static void webOnRecordItem_impl(uint32_t requestId, const char* value) {
    if (g_currentWebSink == nullptr || g_currentWebSink->recordCount >= CONSOLE_RESPONSE_RECORDS_MAX) {
        return;
    }
    ConsoleRecord* rec = &g_currentWebSink->records[g_currentWebSink->recordCount++];
    rec->type = "item";
    rec->requestId = requestId;
    rec->value = value;
}

static void webOnRecordResult_impl(uint32_t requestId, ConsoleStatus status,
                                   ConsoleOutcome outcome, ConsoleReason reason) {
    if (g_currentWebSink == nullptr || g_currentWebSink->recordCount >= CONSOLE_RESPONSE_RECORDS_MAX) {
        return;
    }
    ConsoleRecord* rec = &g_currentWebSink->records[g_currentWebSink->recordCount++];
    rec->type = "result";
    rec->requestId = requestId;
    rec->status = consoleStatusString(status);
    rec->outcome = consoleOutcomeString(outcome);
    if (status == CONSOLE_STATUS_ERR) {
        rec->reason = consoleReasonString(reason);
    }
    if (status == CONSOLE_STATUS_ERR) {
        g_currentWebSink->hasError = true;
    }
}

static void webOnRecordEnd_impl(uint32_t requestId, ConsoleStatus status,
                               ConsoleOutcome outcome, ConsoleReason reason) {
    if (g_currentWebSink == nullptr || g_currentWebSink->recordCount >= CONSOLE_RESPONSE_RECORDS_MAX) {
        return;
    }
    ConsoleRecord* rec = &g_currentWebSink->records[g_currentWebSink->recordCount++];
    rec->type = "end";
    rec->requestId = requestId;
    rec->status = consoleStatusString(status);
    rec->outcome = consoleOutcomeString(outcome);
    if (status == CONSOLE_STATUS_ERR) {
        rec->reason = consoleReasonString(reason);
    }
    g_currentWebSink->inMultiRecord = false;
    if (status == CONSOLE_STATUS_ERR) {
        g_currentWebSink->hasError = true;
    }
}

// =============================================================================
// Handler Implementation
// =============================================================================

void handleConsolePost(WebRequest& req) {
    // Extract the command line from the request (form parameter or JSON body)
    char command[256] = {};
    if (!req.param("command", command, sizeof(command)) || command[0] == '\0') {
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
            snprintf(command, sizeof(command), "%s", cmdVar.as<const char*>());
        }
    }

    // Trim and validate command
    // Remove leading/trailing whitespace
    size_t start = 0;
    while (command[start] && isspace(command[start])) start++;
    size_t end = strlen(command);
    while (end > start && isspace(command[end - 1])) end--;
    command[end] = '\0';
    if (start > 0) {
        memmove(command, command + start, end - start + 1);
    }

    if (command[0] == '\0') {
        req.send(400, "application/json", "{\"ok\":false,\"error\":\"empty command\"}");
        return;
    }

    // Initialize web sink for this request
    ConsoleWebSink webSink = {};
    g_currentWebSink = &webSink;

    // Create the console request
    // For T1: parse the command line to extract operation name
    // Simple parsing: first whitespace-delimited token is the operation name
    char operationName[256] = {};
    sscanf(command, "%255s", operationName);

    ConsoleRequest consoleReq = {
        .requestId = consoleGetNextRequestId(),
        .source = CONSOLE_SOURCE_WEB,
        .operationName = operationName,
    };

    // Create the sink callbacks
    ConsoleRecordSink sink = {
        .onRecordBegin = webOnRecordBegin_impl,
        .onRecordField = webOnRecordField_impl,
        .onRecordItem = webOnRecordItem_impl,
        .onRecordResult = webOnRecordResult_impl,
        .onRecordEnd = webOnRecordEnd_impl,
    };

    // Execute the command through the Console module
    consoleExecuteCommand(&consoleReq, &sink);

    // Clear the global sink reference
    g_currentWebSink = nullptr;

    // Build JSON response from accumulated records
    JsonDocument responseDoc;
    JsonArray recordsArray = responseDoc.createNestedArray("records");

    for (size_t i = 0; i < webSink.recordCount; i++) {
        const ConsoleRecord& rec = webSink.records[i];
        JsonObject recordObj = recordsArray.createNestedObject();

        recordObj["id"] = rec.requestId;
        recordObj["type"] = rec.type;

        // Type-specific fields
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
            if (rec.reason != nullptr) {
                recordObj["reason"] = rec.reason;
            }
        }
    }

    // Serialize response
    char responseBody[4096] = {};
    size_t bodySize = serializeJson(responseDoc, responseBody, sizeof(responseBody));
    if (bodySize == 0 || bodySize >= sizeof(responseBody)) {
        req.send(500, "application/json", "{\"ok\":false,\"error\":\"response too large\"}");
        return;
    }

    req.send(200, "application/json", responseBody);
}
