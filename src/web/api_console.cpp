// =============================================================================
// src/web/api_console.cpp
//
// POST /api/console - browser adapter for the Controller Console (ADR 0034).
// The same command processor (consoleExecuteCommand) that the serial task uses,
// driven by a web-specific sink that accumulates records into a JSON response.
//
// The web sink copies field values into owned storage because the module
// reuses a single stack buffer for successive field emissions.
// =============================================================================

#include "../../include/api_console.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <string.h>
#include <ctype.h>

#include "../../include/console_module.h"
#include "../../include/console_record.h"

// Maximum number of records + value storage for a single response
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
    if (g_currentWebSink == nullptr || g_currentWebSink->recordCount >= CONSOLE_RESPONSE_RECORDS_MAX) return;
    ConsoleRecord* rec = &g_currentWebSink->records[g_currentWebSink->recordCount++];
    rec->type = "begin";
    rec->requestId = requestId;
    rec->operation = arenaStoreString(operationType);
}

static void webOnRecordField_impl(uint32_t requestId, const char* name, const char* value) {
    if (g_currentWebSink == nullptr || g_currentWebSink->recordCount >= CONSOLE_RESPONSE_RECORDS_MAX) return;
    ConsoleRecord* rec = &g_currentWebSink->records[g_currentWebSink->recordCount++];
    rec->type = "field";
    rec->requestId = requestId;
    rec->name = arenaStoreString(name);
    rec->value = arenaStoreString(value);
}

static void webOnRecordItem_impl(uint32_t requestId, const char* value) {
    if (g_currentWebSink == nullptr || g_currentWebSink->recordCount >= CONSOLE_RESPONSE_RECORDS_MAX) return;
    ConsoleRecord* rec = &g_currentWebSink->records[g_currentWebSink->recordCount++];
    rec->type = "item";
    rec->requestId = requestId;
    rec->value = arenaStoreString(value);
}

static void webOnRecordResult_impl(uint32_t requestId, ConsoleStatus status,
                                   ConsoleOutcome outcome, ConsoleReason reason) {
    if (g_currentWebSink == nullptr || g_currentWebSink->recordCount >= CONSOLE_RESPONSE_RECORDS_MAX) return;
    ConsoleRecord* rec = &g_currentWebSink->records[g_currentWebSink->recordCount++];
    rec->type = "result";
    rec->requestId = requestId;
    rec->status = consoleStatusString(status);
    rec->outcome = consoleOutcomeString(outcome);
    if (status == CONSOLE_STATUS_ERR) rec->reason = consoleReasonString(reason);
}

static void webOnRecordEnd_impl(uint32_t requestId, ConsoleStatus status,
                               ConsoleOutcome outcome, ConsoleReason reason) {
    if (g_currentWebSink == nullptr || g_currentWebSink->recordCount >= CONSOLE_RESPONSE_RECORDS_MAX) return;
    ConsoleRecord* rec = &g_currentWebSink->records[g_currentWebSink->recordCount++];
    rec->type = "end";
    rec->requestId = requestId;
    rec->status = consoleStatusString(status);
    rec->outcome = consoleOutcomeString(outcome);
    if (status == CONSOLE_STATUS_ERR) rec->reason = consoleReasonString(reason);
}

void handleConsolePost(WebRequest& req) {
    char command[256] = {};

    // Check for truncation in form parameter (D13: detect oversized command)
    const char* paramValue = req.paramRef("command");
    if (paramValue != nullptr) {
        if (strlen(paramValue) >= sizeof(command)) {
            // Line too long; emit error with proper reason code
            ConsoleWebSink webSink = {};
            g_currentWebSink = &webSink;

            uint32_t reqId = consoleGetNextRequestId();
            ConsoleRecordSink sink = {
                .onRecordBegin = webOnRecordBegin_impl,
                .onRecordField = webOnRecordField_impl,
                .onRecordItem = webOnRecordItem_impl,
                .onRecordResult = webOnRecordResult_impl,
                .onRecordEnd = webOnRecordEnd_impl,
            };

            webOnRecordEnd_impl(reqId, CONSOLE_STATUS_ERR, CONSOLE_OUTCOME_INVALID, CONSOLE_REASON_LINE_TOO_LONG);
            g_currentWebSink = nullptr;

            // Build and send error response
            JsonDocument responseDoc;
            JsonArray recordsArray = responseDoc.createNestedArray("records");

            const ConsoleRecord& rec = webSink.records[0];
            JsonObject recordObj = recordsArray.createNestedObject();
            recordObj["id"] = rec.requestId;
            recordObj["type"] = rec.type;
            recordObj["status"] = rec.status;
            recordObj["outcome"] = rec.outcome;
            if (rec.reason != nullptr) recordObj["reason"] = rec.reason;

            static char responseBody[4096];
            size_t bodySize = serializeJson(responseDoc, responseBody, sizeof(responseBody));
            req.send(200, "application/json", responseBody);
            return;
        }
        snprintf(command, sizeof(command), "%s", paramValue);
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

    // Cast to (unsigned char) to avoid UB on high-bit chars
    size_t start = 0;
    while (command[start] && isspace((unsigned char)command[start])) start++;
    size_t end = strlen(command);
    while (end > start && isspace((unsigned char)command[end - 1])) end--;
    command[end] = '\0';
    if (start > 0) memmove(command, command + start, end - start + 1);

    if (command[0] == '\0') {
        req.send(400, "application/json", "{\"ok\":false,\"error\":\"empty command\"}");
        return;
    }

    ConsoleWebSink webSink = {};
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

    JsonDocument responseDoc;
    JsonArray recordsArray = responseDoc.createNestedArray("records");

    for (size_t i = 0; i < webSink.recordCount; i++) {
        const ConsoleRecord& rec = webSink.records[i];
        JsonObject recordObj = recordsArray.createNestedObject();
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

    static char responseBody[4096];
    memset(responseBody, 0, sizeof(responseBody));
    size_t bodySize = serializeJson(responseDoc, responseBody, sizeof(responseBody));
    if (bodySize == 0 || bodySize >= sizeof(responseBody)) {
        req.send(500, "application/json", "{\"ok\":false,\"error\":\"response too large\"}");
        return;
    }

    req.send(200, "application/json", responseBody);
}
