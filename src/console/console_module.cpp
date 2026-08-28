// =============================================================================
// src/console/console_module.cpp
//
// Controller Console module - transport-independent operation processor.
// ADR 0034: one operation core below HTTP handlers, operation catalog from registry.
//
// Integrates the generated console_catalog with file-based help text in LittleFS.
// Help file is opened once in setup() and held for the process lifetime.
// =============================================================================

#include "console_module.h"
#include "console_record.h"
#include "console_catalog.h"

#include <string.h>
#include <ctype.h>

#ifdef ARDUINO
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <esp_heap_caps.h>
#include <FS.h>
#include <LittleFS.h>
#endif

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
// Help File Management (once-open pattern, no per-request allocation)
// =============================================================================

#ifdef ARDUINO
// File handle for help text, opened once and held for lifetime.
// Opened in setup(), never closed. Each help lookup is seek() + read() into stack buffer.
static File g_helpFile;
#endif

static bool g_helpFileReady = false;

// Path to the help text file in LittleFS
static const char* HELP_FILE_PATH = "/console_help.txt";

// Maximum help text size per operation (description + display_name + delimiters)
static const size_t HELP_TEXT_MAX = 512;

// =============================================================================
// Private: Help Text Lookup from File
// =============================================================================

// =============================================================================
// Seam: File access (can be mocked in native tests)
// =============================================================================

// Abstract file access for help text (allows native tests to substitute)
struct HelpFileHandle {
    bool valid;
    // Platform-specific implementation
    #ifdef ARDUINO
    File file;
    #endif
};

// Seek to offset in help file. Returns true on success.
static inline bool helpFileSeek(uint32_t offset) {
    #ifdef ARDUINO
    if (!g_helpFileReady) return false;
    return g_helpFile.seek(offset);
    #else
    // Native test stub
    (void)offset;
    return false;
    #endif
}

// Read bytes from current position. Returns number of bytes read.
static inline size_t helpFileRead(char* buffer, size_t size) {
    #ifdef ARDUINO
    if (!g_helpFileReady || !buffer || size == 0) return 0;
    return g_helpFile.read((uint8_t*)buffer, size);
    #else
    // Native test stub
    (void)buffer;
    (void)size;
    return 0;
    #endif
}

// Extract help text for an operation from the help file using addressed read.
// The catalog entry contains offset and length, so this is a single seek + read.
// Returns true if help text was found and written to out_buffer (null-terminated).
// If false, out_buffer is left empty.
// This function seeks into the already-open help file, so no allocation occurs in the loop.
static bool consoleGetHelpText(const char* operationName, uint16_t help_offset,
                                uint16_t help_length, char* out_buffer, size_t buffer_size) {
    out_buffer[0] = '\0';

    if (!g_helpFileReady || !operationName || help_length == 0) {
        return false;
    }

    // Seek to the help offset for this operation
    if (!helpFileSeek(help_offset)) {
        return false;
    }

    // Read the help text line (it's one line per entry)
    // Help text format: name|display_name|description|executor|params
    size_t lineLen = help_length;
    if (lineLen >= buffer_size) {
        lineLen = buffer_size - 1;
    }

    size_t bytesRead = helpFileRead(out_buffer, lineLen);
    if (bytesRead > 0) {
        out_buffer[bytesRead] = '\0';
        return true;
    }

    return false;
}

// Emit help for an operation as console records
// Help text comes from the LittleFS file opened in setup().
// If the file is unavailable, degrade gracefully.
static void consoleEmitHelpForOperation(uint32_t requestId, const char* operationName,
                                       const ConsoleRecordSink* sink) {
    const ConsoleCatalogEntry* entry = consoleCatalogFindByName(operationName);
    if (entry == nullptr) {
        // Operation not found in catalog
        if (sink->onRecordResult) {
            sink->onRecordResult(requestId, CONSOLE_STATUS_ERR, CONSOLE_OUTCOME_INVALID,
                                CONSOLE_REASON_UNKNOWN_OPERATION);
        }
        return;
    }

    // Emit help as multi-record response
    if (sink->onRecordBegin) {
        sink->onRecordBegin(requestId, operationName);
    }

    // Type field
    if (sink->onRecordField) {
        sink->onRecordField(requestId, "type", entry->type);
    }

    // Help text from file - addressed by offset+length
    char helpBuf[HELP_TEXT_MAX] = {};
    if (consoleGetHelpText(operationName, entry->help_offset, entry->help_length,
                           helpBuf, sizeof(helpBuf))) {
        // Parse help text format: name|display_name|description|executor|params
        // Extract fields by splitting on pipe delimiter
        const char* pos = helpBuf;
        int field = 0;
        const char* fieldStart = pos;

        while (*pos != '\0' && field < 5) {
            if (*pos == '|') {
                size_t fieldLen = pos - fieldStart;

                if (field == 1 && fieldLen > 0) {
                    // display_name field
                    char displayName[64] = {};
                    size_t cpyLen = (fieldLen < sizeof(displayName) - 1) ? fieldLen : sizeof(displayName) - 1;
                    memcpy(displayName, fieldStart, cpyLen);
                    displayName[cpyLen] = '\0';
                    if (sink->onRecordField) {
                        sink->onRecordField(requestId, "display_name", displayName);
                    }
                } else if (field == 2 && fieldLen > 0) {
                    // description field
                    char description[256] = {};
                    size_t cpyLen = (fieldLen < sizeof(description) - 1) ? fieldLen : sizeof(description) - 1;
                    memcpy(description, fieldStart, cpyLen);
                    description[cpyLen] = '\0';
                    if (sink->onRecordField) {
                        sink->onRecordField(requestId, "description", description);
                    }
                } else if (field == 3 && fieldLen > 0) {
                    // executor field
                    char executor[64] = {};
                    size_t cpyLen = (fieldLen < sizeof(executor) - 1) ? fieldLen : sizeof(executor) - 1;
                    memcpy(executor, fieldStart, cpyLen);
                    executor[cpyLen] = '\0';
                    if (sink->onRecordField) {
                        sink->onRecordField(requestId, "executor", executor);
                    }
                }

                field++;
                fieldStart = pos + 1;
            }
            pos++;
        }
    } else if (!g_helpFileReady) {
        // Help file not available - emit explicit degradation reason
        if (sink->onRecordField) {
            sink->onRecordField(requestId, "help_file_status", "unavailable");
        }
    }

    // End: help is answered in full above, synchronously
    if (sink->onRecordEnd) {
        sink->onRecordEnd(requestId, CONSOLE_STATUS_OK, CONSOLE_OUTCOME_COMPLETED,
                         CONSOLE_REASON_NONE);
    }
}

// =============================================================================
// Private: Operation Lookup and Execution (uses catalog)
// =============================================================================

// Check if operation is recognized in the catalog
static bool consoleIsKnownOperation(const char* operationName) {
    if (operationName == nullptr) return false;
    return consoleCatalogFindByName(operationName) != nullptr;
}

// Check if operation is available on this board
// For now, all catalog entries are available. Future: check board_capability.
static bool consoleIsAvailableOnBoard(const char* operationName) {
    const ConsoleCatalogEntry* entry = consoleCatalogFindByName(operationName);
    if (!entry) return false;
    return entry->available_on_board;
}

// Get the operation type from its name
static ConsoleOperationType consoleGetOperationType(const char* operationName) {
    const ConsoleCatalogEntry* entry = consoleCatalogFindByName(operationName);
    if (!entry) return CONSOLE_OP_ACTION;

    // Map catalog type strings to enum
    if (strcmp(entry->type, CONSOLE_CATALOG_TYPE_STATUS) == 0) {
        return CONSOLE_OP_STATUS;
    }
    if (strcmp(entry->type, CONSOLE_CATALOG_TYPE_CONFIG) == 0) {
        return CONSOLE_OP_CONFIG;
    }
    if (strcmp(entry->type, CONSOLE_CATALOG_TYPE_EVENT) == 0) {
        return CONSOLE_OP_EVENT;
    }
    return CONSOLE_OP_ACTION;
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
#ifdef ARDUINO
    heapFree = ESP.getFreeHeap();
    heapMin = ESP.getMinFreeHeap();
    heapLargestBlock = (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
#else
    // Native tests: use reasonable stub values (not available on standard C heap)
    heapFree = 262144;      // 256 KB stub value for test consistency
    heapMin = 262144;
    heapLargestBlock = 262144;
#endif

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

    // End multi-record response: the snapshot fields are emitted above, so the
    // query is complete and no reason applies.
    if (sink->onRecordEnd) {
        sink->onRecordEnd(requestId, CONSOLE_STATUS_OK, CONSOLE_OUTCOME_COMPLETED,
                         CONSOLE_REASON_NONE);
    }
}

// =============================================================================
// Public API Implementation
// =============================================================================

void consoleModuleInit(void) {
#ifdef ARDUINO
    // Open help file once, hold handle for lifetime (no per-request allocation)
    // This is called from setup() after LittleFS is mounted.
    if (LittleFS.exists(HELP_FILE_PATH)) {
        g_helpFile = LittleFS.open(HELP_FILE_PATH, "r");
        if (g_helpFile) {
            g_helpFileReady = true;
            PA_LOG_DEBUG(TAG, "help file opened, %u bytes", g_helpFile.size());
        } else {
            PA_LOG_WARN(TAG, "failed to open help file at %s", HELP_FILE_PATH);
        }
    } else {
        PA_LOG_WARN(TAG, "help file not found at %s", HELP_FILE_PATH);
    }
#else
    // Native tests: g_helpFileReady stays false, consoleGetHelpText returns false
#endif

    size_t catalogCount = consoleCatalogGetCount();
    PA_LOG_DEBUG(TAG, "console module initialized, %u operations in catalog", catalogCount);
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

    const char* opName = request->operationName;

    // Meta-command: help [operation]
    if (opName != nullptr && strncmp(opName, "help", 4) == 0) {
        // "help" or "help operation_name"
        if (strlen(opName) == 4) {
            // "help" with no arguments - emit general help
            if (sink->onRecordBegin) {
                sink->onRecordBegin(request->requestId, "help");
            }
            if (sink->onRecordField) {
                sink->onRecordField(request->requestId, "detach_key", "Ctrl-C (serial) or close browser tab (web)");
            }
            if (sink->onRecordField) {
                sink->onRecordField(request->requestId, "hint", "Type 'operations' to list all commands");
            }
            if (sink->onRecordEnd) {
                sink->onRecordEnd(request->requestId, CONSOLE_STATUS_OK, CONSOLE_OUTCOME_COMPLETED,
                                 CONSOLE_REASON_NONE);
            }
            return;
        } else if (opName[4] == ' ') {
            // "help operation_name"
            const char* targetOp = opName + 5;
            // Skip leading whitespace
            while (*targetOp == ' ') {
                ++targetOp;
            }
            if (*targetOp != '\0') {
                consoleEmitHelpForOperation(request->requestId, targetOp, sink);
                return;
            }
        }
        // Malformed help command
        if (sink->onRecordResult) {
            sink->onRecordResult(request->requestId, CONSOLE_STATUS_ERR,
                                CONSOLE_OUTCOME_INVALID, CONSOLE_REASON_UNKNOWN_OPERATION);
        }
        return;
    }

    // Meta-command: operations [type=<type>]
    if (opName != nullptr && strcmp(opName, "operations") == 0) {
        // List all operations in the catalog
        size_t catalogCount = 0;
        const ConsoleCatalogEntry* entries = consoleCatalogGetEntries(&catalogCount);

        if (sink->onRecordBegin) {
            sink->onRecordBegin(request->requestId, "operations");
        }

        for (size_t i = 0; i < catalogCount; ++i) {
            const ConsoleCatalogEntry* entry = &entries[i];
            // Emit each operation as an item record
            // Format: name (type, [reason if unavailable])
            if (sink->onRecordItem) {
                char itemBuf[256];
                if (!entry->available_on_board) {
                    snprintf(itemBuf, sizeof(itemBuf), "%s (%s, not-on-this-board)",
                            entry->name, entry->type);
                } else if (!entry->available_in_build) {
                    snprintf(itemBuf, sizeof(itemBuf), "%s (%s, not-in-this-build)",
                            entry->name, entry->type);
                } else if (!entry->executor_ready) {
                    snprintf(itemBuf, sizeof(itemBuf), "%s (%s, executor-not-ready)",
                            entry->name, entry->type);
                } else {
                    snprintf(itemBuf, sizeof(itemBuf), "%s (%s)",
                            entry->name, entry->type);
                }
                sink->onRecordItem(request->requestId, itemBuf);
            }
        }

        if (sink->onRecordEnd) {
            sink->onRecordEnd(request->requestId, CONSOLE_STATUS_OK, CONSOLE_OUTCOME_COMPLETED,
                             CONSOLE_REASON_NONE);
        }
        return;
    }

    // Check if operation is known
    if (!consoleIsKnownOperation(opName)) {
        // Unknown operation: return single result record (guard path)
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
            } else {
                // T2+: status not yet implemented
                if (sink->onRecordEnd) {
                    sink->onRecordEnd(request->requestId, CONSOLE_STATUS_ERR,
                                     CONSOLE_OUTCOME_UNAVAILABLE, CONSOLE_REASON_EXECUTOR_NOT_READY);
                }
            }
            break;

        case CONSOLE_OP_ACTION:
        case CONSOLE_OP_CONFIG:
        case CONSOLE_OP_EVENT:
            // T2+: not yet implemented - return single result record (guard path)
            if (sink->onRecordResult) {
                sink->onRecordResult(request->requestId, CONSOLE_STATUS_ERR,
                                    CONSOLE_OUTCOME_UNAVAILABLE, CONSOLE_REASON_EXECUTOR_NOT_READY);
            }
            break;
    }
}
