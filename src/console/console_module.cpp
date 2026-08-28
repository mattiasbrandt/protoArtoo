// =============================================================================
// src/console/console_module.cpp
//
// Controller Console module - transport-independent operation processor.
// ADR 0034: one operation core below HTTP handlers.
//
// Handles command parsing and execution for system.status.health (T1 tracer).
// Future tickets (#219-#227) will extend this to other operations.
// =============================================================================

#include "console_module.h"
#include "console_record.h"

#include <Arduino.h>
#include <string.h>
#include <ctype.h>
#include <freertos/FreeRTOS.h>
#include <esp_heap_caps.h>

#include "logging.h"
#include "robot_state.h"
#include "failsafe_gate.h"
#include "web_server.h"
#include "web_network_manager.h"

static const char* TAG = "Console";

// Global request ID counter (one across both adapters)
static volatile uint32_t g_nextRequestId = 1;
static portMUX_TYPE g_requestIdMux = portMUX_INITIALIZER_UNLOCKED;

// =============================================================================
// Private: Operation Lookup and Execution
// =============================================================================

// Check if operation is recognized
static bool consoleIsKnownOperation(const char* operationName) {
    // T1 scope: only system.status.health
    if (operationName == nullptr) return false;
    return strcmp(operationName, "system.status.health") == 0;
}

// Check if operation is available on this board
static bool consoleIsAvailableOnBoard(const char* operationName) {
    // T1 scope: all implemented operations are available
    (void)operationName;
    return true;
}

// Get the operation type from its name
static ConsoleOperationType consoleGetOperationType(const char* operationName) {
    if (strcmp(operationName, "system.status.health") == 0) {
        return CONSOLE_OP_STATUS;
    }
    return CONSOLE_OP_ACTION;  // default (will not be reached in T1)
}

// =============================================================================
// Private: system.status.health Implementation
// =============================================================================

// Execute system.status.health query
// Reads the current system health snapshot and emits records through sink
// ADR 0034: status query flows through the existing snapshot builder, rendering as Console Records
// Note: onRecordBegin is already called by consoleExecuteCommand before this function,
// so the mutex is already held and we emit fields directly.
static void consoleExecuteSystemStatusHealth(uint32_t requestId, const ConsoleRecordSink* sink) {
    // Read values directly from RobotState and other sources
    // This replicates the logic from api_status.cpp's buildHealthJson
    FailsafeDiagnostics diag = {};
    bool webControlEnabled;
    bool wifiConnected;
    bool wifiClientConnected;
    bool fsReady;
    unsigned long heapFree;
    unsigned long heapMin;
    unsigned long heapLargestBlock;
    long wifiRssi;

    taskENTER_CRITICAL(&robotStateMux);
    copyFailsafeDiagnosticsLocked(&diag);
    webControlEnabled = robotState.webControlEnabled;
    taskEXIT_CRITICAL(&robotStateMux);

    WifiConnectivityStatus connectivity = networkManagerQueryConnectivity();
    wifiConnected = connectivity.wifiConnected;
    wifiClientConnected = connectivity.wifiClientConnected;
    wifiRssi = connectivity.wifiRssi;

    fsReady = webLittleFsMounted();
    heapFree = ESP.getFreeHeap();
    heapMin = ESP.getMinFreeHeap();
    heapLargestBlock = (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);

    // Emit fields through sink (Console Record format)
    char tempBuf[64] = {};

    // estop
    snprintf(tempBuf, sizeof(tempBuf), "%s", diag.estop ? "true" : "false");
    if (sink->onRecordField) {
        sink->onRecordField(requestId, "estop", tempBuf);
    }

    // heapFree
    snprintf(tempBuf, sizeof(tempBuf), "%lu", heapFree);
    if (sink->onRecordField) {
        sink->onRecordField(requestId, "heapFree", tempBuf);
    }

    // heapMin
    snprintf(tempBuf, sizeof(tempBuf), "%lu", heapMin);
    if (sink->onRecordField) {
        sink->onRecordField(requestId, "heapMin", tempBuf);
    }

    // heapLargestBlock
    snprintf(tempBuf, sizeof(tempBuf), "%lu", heapLargestBlock);
    if (sink->onRecordField) {
        sink->onRecordField(requestId, "heapLargestBlock", tempBuf);
    }

    // sbusSignalLost
    snprintf(tempBuf, sizeof(tempBuf), "%s", diag.sbusSignalLost ? "true" : "false");
    if (sink->onRecordField) {
        sink->onRecordField(requestId, "sbusSignalLost", tempBuf);
    }

    // sbusHwFailsafe
    snprintf(tempBuf, sizeof(tempBuf), "%s", diag.sbusHwFailsafe ? "true" : "false");
    if (sink->onRecordField) {
        sink->onRecordField(requestId, "sbusHwFailsafe", tempBuf);
    }

    // webControlEnabled
    snprintf(tempBuf, sizeof(tempBuf), "%s", webControlEnabled ? "true" : "false");
    if (sink->onRecordField) {
        sink->onRecordField(requestId, "webControlEnabled", tempBuf);
    }

    // wifiConnected
    snprintf(tempBuf, sizeof(tempBuf), "%s", wifiConnected ? "true" : "false");
    if (sink->onRecordField) {
        sink->onRecordField(requestId, "wifiConnected", tempBuf);
    }

    // wifiClientConnected
    snprintf(tempBuf, sizeof(tempBuf), "%s", wifiClientConnected ? "true" : "false");
    if (sink->onRecordField) {
        sink->onRecordField(requestId, "wifiClientConnected", tempBuf);
    }

    // wifiRssi
    snprintf(tempBuf, sizeof(tempBuf), "%ld", wifiRssi);
    if (sink->onRecordField) {
        sink->onRecordField(requestId, "wifiRssi", tempBuf);
    }

    // fsReady
    snprintf(tempBuf, sizeof(tempBuf), "%s", fsReady ? "true" : "false");
    if (sink->onRecordField) {
        sink->onRecordField(requestId, "fsReady", tempBuf);
    }

    // End multi-record response
    if (sink->onRecordEnd) {
        sink->onRecordEnd(requestId, CONSOLE_STATUS_OK, CONSOLE_OUTCOME_QUEUED,
                         CONSOLE_REASON_NOT_IN_THIS_BUILD);  // Reason 0 (not used for success)
    }
}

// =============================================================================
// Public API Implementation
// =============================================================================

void consoleModuleInit(void) {
    PA_LOG_DEBUG(TAG, "console module initialized");
}

uint32_t consoleGetNextRequestId(void) {
    uint32_t id;
    taskENTER_CRITICAL(&g_requestIdMux);
    id = g_nextRequestId;
    g_nextRequestId = g_nextRequestId + 1;
    taskEXIT_CRITICAL(&g_requestIdMux);
    return id;
}

void consoleExecuteCommand(const ConsoleRequest* request, const ConsoleRecordSink* sink) {
    if (request == nullptr || sink == nullptr) {
        return;
    }

    // Check if operation is known
    if (!consoleIsKnownOperation(request->operationName)) {
        // Unknown operation: return single result record (guard path)
        // Per docs/console-protocol.md, a single type=result record, not begin+end
        if (sink->onRecordResult) {
            sink->onRecordResult(request->requestId, CONSOLE_STATUS_ERR, CONSOLE_OUTCOME_INVALID,
                                CONSOLE_REASON_UNKNOWN_OPERATION);
        }
        return;
    }

    // Check if operation is available on this board
    if (!consoleIsAvailableOnBoard(request->operationName)) {
        // Unavailable operation: return single result record (guard path)
        if (sink->onRecordResult) {
            sink->onRecordResult(request->requestId, CONSOLE_STATUS_ERR,
                                CONSOLE_OUTCOME_UNAVAILABLE, CONSOLE_REASON_NOT_ON_THIS_BOARD);
        }
        return;
    }

    // Execute the operation based on its type
    ConsoleOperationType opType = consoleGetOperationType(request->operationName);

    switch (opType) {
        case CONSOLE_OP_STATUS:
            // Status query: emit begin + fields + end
            if (sink->onRecordBegin) {
                sink->onRecordBegin(request->requestId, request->operationName);
            }
            if (strcmp(request->operationName, "system.status.health") == 0) {
                consoleExecuteSystemStatusHealth(request->requestId, sink);
                // consoleExecuteSystemStatusHealth emits onRecordEnd
            }
            break;

        case CONSOLE_OP_ACTION:
        case CONSOLE_OP_CONFIG:
        case CONSOLE_OP_EVENT:
            // T2+ scope: return single result record (guard path)
            if (sink->onRecordResult) {
                sink->onRecordResult(request->requestId, CONSOLE_STATUS_ERR,
                                    CONSOLE_OUTCOME_UNAVAILABLE, CONSOLE_REASON_EXECUTOR_NOT_READY);
            }
            break;
    }
}
