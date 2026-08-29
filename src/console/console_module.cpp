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
#endif

#include "logging.h"
#include "robot_state.h"
#include "failsafe_gate.h"
#include "web_server.h"
#include "web_network_manager.h"
#include "api_status.h"
#include "api_audio.h"
#include "audio_task.h"
#include "rc_diagnostics_snapshot.h"

static const char* TAG = "Console";

// Global request ID counter (one across both adapters)
static volatile uint32_t g_nextRequestId = 1;
static portMUX_TYPE g_requestIdMux = portMUX_INITIALIZER_UNLOCKED;

// =============================================================================
// Help Reader Management (Dependency Injection)
// =============================================================================
// The Console module receives a help reader from the caller (set in setup()),
// allowing LittleFS-backed reads on Arduino and memory-backed reads in tests.
// A NULL reader gracefully degrades to "help unavailable".
// =============================================================================

// The injected help reader (set by consoleModuleSetHelpReader)
static const ConsoleHelpReader* g_helpReader = nullptr;

// Maximum help text size per operation (description + display_name + delimiters)
static const size_t HELP_TEXT_MAX = 512;

// Set the help reader for the Console module.
// Called from setup() after LittleFS is ready on Arduino builds.
// Pass NULL to disable help text.
void consoleModuleSetHelpReader(const ConsoleHelpReader* reader) {
    g_helpReader = reader;
}

// Extract help text for an operation using the injected reader.
// The catalog entry contains offset and length, so this is a single seek + read.
// Returns true if help text was found and written to out_buffer (null-terminated).
// If false, out_buffer is left empty. No allocation occurs in this path.
static bool consoleGetHelpText(const char* operationName, uint16_t help_offset,
                                uint16_t help_length, char* out_buffer, size_t buffer_size) {
    out_buffer[0] = '\0';

    // Graceful degradation: if reader is NULL or no help text for this entry
    if (g_helpReader == nullptr || operationName == nullptr || help_length == 0) {
        return false;
    }

    // Seek to the help offset for this operation
    if (!g_helpReader->seek(g_helpReader->ctx, help_offset)) {
        return false;
    }

    // Read the help text line (it's one line per entry)
    // Help text format: name|display_name|description|executor|params
    size_t lineLen = help_length;
    if (lineLen >= buffer_size) {
        lineLen = buffer_size - 1;
    }

    size_t bytesRead = g_helpReader->read(g_helpReader->ctx, out_buffer, lineLen);
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

    // Schema and availability fields come from the IN-IMAGE catalog table, not
    // the FS-resident help file, so they render whether or not the help file
    // is readable (#219 D3: help_file_status below covers only the prose -
    // display_name/description/executor - not this). Field names match the
    // catalog struct / docs/action-registry.yaml keys verbatim (snake_case) on
    // purpose, distinct from the camelCase used by status-type queries whose
    // field list mirrors a REST JSON schema (docs/console-protocol.md s.3.5) -
    // these fields have no REST counterpart to mirror.
    if (sink->onRecordField) {
        sink->onRecordField(requestId, "available_on_board",
                           entry->available_on_board ? "true" : "false");
        sink->onRecordField(requestId, "available_in_build",
                           entry->available_in_build ? "true" : "false");
        sink->onRecordField(requestId, "requires_web_control",
                           entry->requires_web_control ? "true" : "false");
        sink->onRecordField(requestId, "executor_ready",
                           entry->executor_ready ? "true" : "false");
    }

    // Aliases: comma-joined into one field value. Neither adapter's record
    // emitter quotes values today (docs/console-protocol.md s.7 asks for it,
    // but consoleQuoteValue() is unused - a pre-existing gap out of scope
    // here), so this stays comma-joined rather than space-separated to keep
    // the value one whitespace-free token regardless.
    if (entry->aliases != nullptr) {
        char aliasesBuf[128] = {};
        size_t used = 0;
        for (const char* const* a = entry->aliases; *a != nullptr; ++a) {
            size_t remaining = sizeof(aliasesBuf) - used;
            if (remaining <= 1) break;
            int n = snprintf(aliasesBuf + used, remaining, "%s%s", (used > 0) ? "," : "", *a);
            if (n < 0) break;
            used += ((size_t)n < remaining) ? (size_t)n : (remaining - 1);
        }
        if (sink->onRecordField) {
            sink->onRecordField(requestId, "aliases", aliasesBuf);
        }
    }

    // Params: name:type:required|optional per parameter, comma-joined - same
    // one-token-per-value constraint as aliases above. Bounds/ranges are not
    // in this table (they are part of the FS-resident help text, not the
    // in-image catalog - see #233's rejected "bounds as strings" arm).
    if (entry->params != nullptr) {
        char paramsBuf[256] = {};
        size_t used = 0;
        for (const ConsoleParamDescriptor* p = entry->params; p->name != nullptr; ++p) {
            size_t remaining = sizeof(paramsBuf) - used;
            if (remaining <= 1) break;
            int n = snprintf(paramsBuf + used, remaining, "%s%s:%s:%s", (used > 0) ? "," : "",
                            p->name, p->type, p->required ? "required" : "optional");
            if (n < 0) break;
            used += ((size_t)n < remaining) ? (size_t)n : (remaining - 1);
        }
        if (sink->onRecordField) {
            sink->onRecordField(requestId, "params", paramsBuf);
        }
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
    } else {
        // Help text not available - determine reason and emit explicit degradation status
        // (ADR 0034: never degrade silently; missing, stale or unreadable help file is always reported)
        if (g_helpReader == nullptr) {
            // Reader not available
            if (sink->onRecordField) {
                sink->onRecordField(requestId, "help_file_status", "unavailable");
            }
        } else {
            // Reader exists but seek failed or read returned 0 bytes (unreadable file)
            if (sink->onRecordField) {
                sink->onRecordField(requestId, "help_file_status", "unreadable");
            }
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
// (ADR 0029: catalog entries carry build_flags and board_capability; runtime checks them)
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
// Private: status query executors (#223, ADR 0034)
//
// Each executor calls the same capture*Snapshot() "Zone Snapshot" function the
// REST handler for that query calls (src/web/api_status*.cpp, src/web/api_audio.cpp,
// src/web/rc_diagnostics_snapshot.cpp), then renders the snapshot's fields as
// Console field records - never through the JSON builder. Field names are the
// API's JSON keys verbatim (docs/console-protocol.md s.3.5); the registry's
// fields: list, the JSON builder's keys and these names are checked against
// each other by test/test_native/test_console_module.
//
// Note: onRecordBegin is already called by consoleExecuteCommand before an
// executor runs, so each executor only emits fields (and its own onRecordEnd).
// =============================================================================

static void consoleExecuteSystemStatusHealth(uint32_t requestId, const ConsoleRecordSink* sink) {
    HealthSnapshot snap = {};
    captureHealthSnapshot(&snap);

    char tempBuf[32] = {};

    if (sink->onRecordField) {
        sink->onRecordField(requestId, "estop", snap.estop ? "true" : "false");
        sink->onRecordField(requestId, "sbusSignalLost", snap.sbusSignalLost ? "true" : "false");
        sink->onRecordField(requestId, "sbusHwFailsafe", snap.sbusHwFailsafe ? "true" : "false");
        sink->onRecordField(requestId, "webControlEnabled",
                           snap.webControlEnabled ? "true" : "false");
        sink->onRecordField(requestId, "wifiConnected", snap.wifiConnected ? "true" : "false");
        sink->onRecordField(requestId, "wifiClientConnected",
                           snap.wifiClientConnected ? "true" : "false");
        sink->onRecordField(requestId, "littleFsReady", snap.littleFsReady ? "true" : "false");
    }

    snprintf(tempBuf, sizeof(tempBuf), "%lu", snap.heapFree);
    if (sink->onRecordField) sink->onRecordField(requestId, "heapFree", tempBuf);
    snprintf(tempBuf, sizeof(tempBuf), "%lu", snap.heapMin);
    if (sink->onRecordField) sink->onRecordField(requestId, "heapMin", tempBuf);
    snprintf(tempBuf, sizeof(tempBuf), "%lu", snap.heapLargestBlock);
    if (sink->onRecordField) sink->onRecordField(requestId, "heapLargestBlock", tempBuf);
    snprintf(tempBuf, sizeof(tempBuf), "%ld", snap.wifiRssi);
    if (sink->onRecordField) sink->onRecordField(requestId, "wifiRssi", tempBuf);

    if (sink->onRecordEnd) {
        sink->onRecordEnd(requestId, CONSOLE_STATUS_OK, CONSOLE_OUTCOME_COMPLETED,
                         CONSOLE_REASON_NONE);
    }
}

static void consoleExecuteSystemStatusWifi(uint32_t requestId, const ConsoleRecordSink* sink) {
    WifiStatusSnapshot snap = {};
    captureWifiStatusSnapshot(&snap);

    if (sink->onRecordField) {
        sink->onRecordField(requestId, "apSsid", snap.apSsid);
        sink->onRecordField(requestId, "apIp", snap.apIp);
        sink->onRecordField(requestId, "staEnabled", snap.staEnabled ? "true" : "false");
        sink->onRecordField(requestId, "staConnected", snap.staConnected ? "true" : "false");
        sink->onRecordField(requestId, "staIp", snap.staIp);
        sink->onRecordField(requestId, "staSsid", snap.staSsid);
    }

    char tempBuf[16] = {};
    snprintf(tempBuf, sizeof(tempBuf), "%ld", snap.wifiRssi);
    if (sink->onRecordField) sink->onRecordField(requestId, "wifiRssi", tempBuf);
    if (sink->onRecordField) {
        sink->onRecordField(requestId, "networkRecovery", snap.networkRecovery ? "true" : "false");
    }

    if (sink->onRecordEnd) {
        sink->onRecordEnd(requestId, CONSOLE_STATUS_OK, CONSOLE_OUTCOME_COMPLETED,
                         CONSOLE_REASON_NONE);
    }
}

static void consoleExecuteDomeStatusCurrent(uint32_t requestId, const ConsoleRecordSink* sink) {
    DomeStatusSnapshot snap = {};
    captureDomeStatusSnapshot(&snap);

    // %.3f matches buildStatusJson()'s own "domeTargetSpeed" formatting
    // (src/web/web_server.cpp) so the value reads identically on both adapters.
    char tempBuf[24] = {};
    snprintf(tempBuf, sizeof(tempBuf), "%.3f", (double)snap.domeTargetSpeed);
    if (sink->onRecordField) {
        sink->onRecordField(requestId, "domeTargetSpeed", tempBuf);
        sink->onRecordField(requestId, "domeEnabled", snap.domeEnabled ? "true" : "false");
    }

    if (sink->onRecordEnd) {
        sink->onRecordEnd(requestId, CONSOLE_STATUS_OK, CONSOLE_OUTCOME_COMPLETED,
                         CONSOLE_REASON_NONE);
    }
}

static void consoleExecuteSoundStatusCurrent(uint32_t requestId, const ConsoleRecordSink* sink) {
    AudioStatusSnapshot snap = {};
    captureAudioStatusSnapshot(&snap);

    char tempBuf[16] = {};

    if (sink->onRecordField) {
        sink->onRecordField(requestId, "driver", audioGetDriverName());
    }
    snprintf(tempBuf, sizeof(tempBuf), "%u", (unsigned)audioGetCapabilities());
    if (sink->onRecordField) sink->onRecordField(requestId, "capabilities", tempBuf);
    if (sink->onRecordField) {
        sink->onRecordField(requestId, "link_ok", snap.linkOk ? "true" : "false");
        sink->onRecordField(requestId, "active", snap.active ? "true" : "false");
        sink->onRecordField(requestId, "play_state", audioPlayStateLabel(snap.playState));
        sink->onRecordField(requestId, "device", audioDeviceLabel(snap.device));
    }
    snprintf(tempBuf, sizeof(tempBuf), "%u", (unsigned)snap.totalTracks);
    if (sink->onRecordField) sink->onRecordField(requestId, "total_tracks", tempBuf);
    snprintf(tempBuf, sizeof(tempBuf), "%u", (unsigned)snap.currentTrack);
    if (sink->onRecordField) sink->onRecordField(requestId, "current_track", tempBuf);
    if (sink->onRecordField) {
        sink->onRecordField(requestId, "rx_status", audioRxStatusToken(snap.rxStatus));
        sink->onRecordField(requestId, "rx_detail", audioRxStatusDetail(snap.rxStatus));
    }

    if (sink->onRecordEnd) {
        sink->onRecordEnd(requestId, CONSOLE_STATUS_OK, CONSOLE_OUTCOME_COMPLETED,
                         CONSOLE_REASON_NONE);
    }
}

static void consoleExecuteDomeStatusSerialLink(uint32_t requestId, const ConsoleRecordSink* sink) {
    DomeSerialLinkSnapshot snap = {};
    captureDomeSerialLinkSnapshot(&snap);

    if (sink->onRecordField) {
        sink->onRecordField(requestId, "active", snap.active ? "true" : "false");
    }
    char tempBuf[16] = {};
    snprintf(tempBuf, sizeof(tempBuf), "%lu", snap.heartbeatRx);
    if (sink->onRecordField) sink->onRecordField(requestId, "heartbeatRx", tempBuf);
    snprintf(tempBuf, sizeof(tempBuf), "%lu", snap.heartbeatTx);
    if (sink->onRecordField) sink->onRecordField(requestId, "heartbeatTx", tempBuf);

    if (sink->onRecordEnd) {
        sink->onRecordEnd(requestId, CONSOLE_STATUS_OK, CONSOLE_OUTCOME_COMPLETED,
                         CONSOLE_REASON_NONE);
    }
}

// Renders one RcDiagnosticsSourceSnapshot as a single whitespace-free token.
// Colon-separated (not "="): the record's own wire format is space-separated
// key=value tokens and no adapter quotes values yet (docs/console-protocol.md
// s.3.5 asks for it; consoleQuoteValue() is unused, a pre-existing gap out of
// scope here - same reasoning as the alias/param comma-joining above). An
// internal "=" would still parse under a first-"="-split reader, but would
// violate the documented "quote a value containing =" rule for no benefit, so
// this avoids "=" entirely instead.
static void consoleFormatRcSourceSummary(const RcDiagnosticsSourceSnapshot& source, char* out,
                                        size_t outSize) {
    snprintf(out, outSize, "enabled:%s,linked:%s,ageMs:%lu,lostFrames:%lu,failsafe:%s",
             source.enabled ? "true" : "false", source.linked ? "true" : "false",
             (unsigned long)source.ageMs, (unsigned long)source.lostFrames,
             source.failsafe ? "true" : "false");
}

static void consoleExecuteRcStatusSnapshot(uint32_t requestId, const ConsoleRecordSink* sink) {
    RcDiagnosticsSnapshot snap = {};
    captureRcDiagnosticsSnapshot(&snap);

    if (sink->onRecordField) {
        sink->onRecordField(requestId, "mode", snap.mode != nullptr ? snap.mode : "");
    }

    // sources[0]/[1] are always "sbus1"/"sbus2" (fixed order,
    // src/web/rc_diagnostics_snapshot.cpp); "sbus1"/"sbus2" are real JSON keys
    // at sources.sbus1/sources.sbus2 in populateRcDiagnosticsJson()'s output,
    // collapsed to one summary token per source rather than expanded field by
    // field - /api/rc's full shape (sources/channels/digital/mappingProfile/raw)
    // is deeply nested and not scalar-field-shaped.
    char tempBuf[80] = {};
    if (snap.sourceCount > 0 && sink->onRecordField) {
        consoleFormatRcSourceSummary(snap.sources[0], tempBuf, sizeof(tempBuf));
        sink->onRecordField(requestId, "sbus1", tempBuf);
    }
    if (snap.sourceCount > 1 && sink->onRecordField) {
        consoleFormatRcSourceSummary(snap.sources[1], tempBuf, sizeof(tempBuf));
        sink->onRecordField(requestId, "sbus2", tempBuf);
    }

    if (sink->onRecordEnd) {
        sink->onRecordEnd(requestId, CONSOLE_STATUS_OK, CONSOLE_OUTCOME_COMPLETED,
                         CONSOLE_REASON_NONE);
    }
}

// =============================================================================
// Status executor dispatch table (#223)
//
// One dispatch point that resolves a catalog entry to an executor - not a
// chain of strcmp prefixes bolted beside the meta-command checks. This is the
// seam #220/#224/#225/#226 (actions, profiling, survival, config) extend:
// adding an executor is adding a row here, not editing consoleExecuteCommand's
// control flow.
// =============================================================================

typedef void (*ConsoleStatusExecutorFn)(uint32_t requestId, const ConsoleRecordSink* sink);

struct ConsoleStatusExecutorEntry {
    const char* operationName;
    ConsoleStatusExecutorFn executor;
};

static const ConsoleStatusExecutorEntry g_statusExecutors[] = {
    {"system.status.health", consoleExecuteSystemStatusHealth},
    {"system.status.wifi", consoleExecuteSystemStatusWifi},
    {"dome.status.current", consoleExecuteDomeStatusCurrent},
    {"sound.status.current", consoleExecuteSoundStatusCurrent},
    {"dome.status.serial-link", consoleExecuteDomeStatusSerialLink},
    {"rc.status.snapshot", consoleExecuteRcStatusSnapshot},
};
static const size_t kStatusExecutorCount =
    sizeof(g_statusExecutors) / sizeof(g_statusExecutors[0]);

static ConsoleStatusExecutorFn consoleFindStatusExecutor(const char* operationName) {
    if (operationName == nullptr) {
        return nullptr;
    }
    for (size_t i = 0; i < kStatusExecutorCount; ++i) {
        if (strcmp(g_statusExecutors[i].operationName, operationName) == 0) {
            return g_statusExecutors[i].executor;
        }
    }
    return nullptr;
}

// =============================================================================
// Public API Implementation
// =============================================================================

void consoleModuleInit(void) {
    // Console module initialization (before LittleFS and web server).
    // The help reader will be set separately via consoleModuleSetHelpReader()
    // after LittleFS is ready (see ADR 0034).

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
            // detach_key names how to leave the console (#219 D4). Serial only:
            // the field used to hand the browser adapter a value it must not
            // claim ("close browser tab (web)") - the criterion is explicit
            // that the web adapter has no detach convention of its own. Present
            // exactly when there is one, matching the reason= field's convention.
            if (request->source == CONSOLE_SOURCE_SERIAL && sink->onRecordField) {
                sink->onRecordField(request->requestId, "detach_key", CONSOLE_DETACH_KEY_SERIAL);
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
    if (opName != nullptr && (strcmp(opName, "operations") == 0 || strncmp(opName, "operations ", 11) == 0)) {
        // List all operations in the catalog, optionally filtered by type
        const char* filterType = nullptr;

        // Parse type filter if present (e.g., "operations type=action")
        if (strlen(opName) > 10 && opName[10] == ' ') {
            const char* args = opName + 11;
            // Skip leading whitespace
            while (*args == ' ') {
                ++args;
            }
            // Check for "type=" prefix
            if (strncmp(args, "type=", 5) == 0) {
                filterType = args + 5;
            }
        }

        // A type= filter must name one of the catalog's four types. Without this
        // check an unrecognized value (typo, or a value from a future registry
        // type) fell through to a filter that matches nothing and answered
        // status=ok outcome=completed with zero items - indistinguishable on the
        // wire from "no operations of this type exist", which hides the typo from
        // the operator instead of naming it (#219 D1).
        if (filterType != nullptr &&
            strcmp(filterType, CONSOLE_CATALOG_TYPE_ACTION) != 0 &&
            strcmp(filterType, CONSOLE_CATALOG_TYPE_STATUS) != 0 &&
            strcmp(filterType, CONSOLE_CATALOG_TYPE_CONFIG) != 0 &&
            strcmp(filterType, CONSOLE_CATALOG_TYPE_EVENT) != 0) {
            if (sink->onRecordResult) {
                sink->onRecordResult(request->requestId, CONSOLE_STATUS_ERR,
                                    CONSOLE_OUTCOME_INVALID, CONSOLE_REASON_OUT_OF_RANGE);
            }
            return;
        }

        size_t catalogCount = 0;
        const ConsoleCatalogEntry* entries = consoleCatalogGetEntries(&catalogCount);

        if (sink->onRecordBegin) {
            sink->onRecordBegin(request->requestId, "operations");
        }

        for (size_t i = 0; i < catalogCount; ++i) {
            const ConsoleCatalogEntry* entry = &entries[i];

            // Skip if type filter is specified and does not match
            if (filterType != nullptr && strcmp(entry->type, filterType) != 0) {
                continue;
            }

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
        case CONSOLE_OP_STATUS: {
            ConsoleStatusExecutorFn executor = consoleFindStatusExecutor(request->operationName);
            if (executor != nullptr) {
                // A real query: begin + fields (emitted by the executor) + end.
                if (sink->onRecordBegin) {
                    sink->onRecordBegin(request->requestId, request->operationName);
                }
                executor(request->requestId, sink);  // executor emits fields + calls onRecordEnd
                break;
            }

            // No dispatch table row. The catalog's is_query flag (generated
            // from the registry's fields:/is_query: false, #212) distinguishes
            // two different reasons nothing runs, so the operator sees which
            // one applies instead of one generic answer for both:
            const ConsoleCatalogEntry* entry = consoleCatalogFindByName(request->operationName);
            bool isQuery = (entry != nullptr) && entry->is_query;
            if (sink->onRecordResult) {
                if (isQuery) {
                    // fields: is present, so this should be queryable, but no
                    // dispatch table row exists yet - genuinely not wired.
                    sink->onRecordResult(request->requestId, CONSOLE_STATUS_ERR,
                                        CONSOLE_OUTCOME_UNAVAILABLE,
                                        CONSOLE_REASON_EXECUTOR_NOT_READY);
                } else {
                    // is_query: false (#212): this row only describes a field
                    // inside another query's response, never an independently
                    // executable command - not "not ready", never will be.
                    sink->onRecordResult(request->requestId, CONSOLE_STATUS_ERR,
                                        CONSOLE_OUTCOME_UNAVAILABLE, CONSOLE_REASON_NOT_EXECUTABLE);
                }
            }
            break;
        }

        case CONSOLE_OP_ACTION:
        case CONSOLE_OP_CONFIG:
            // T2+: not yet implemented - return single result record (guard path)
            if (sink->onRecordResult) {
                sink->onRecordResult(request->requestId, CONSOLE_STATUS_ERR,
                                    CONSOLE_OUTCOME_UNAVAILABLE, CONSOLE_REASON_EXECUTOR_NOT_READY);
            }
            break;

        case CONSOLE_OP_EVENT:
            // Events are never executable - they are signals, not commands.
            // Return distinct reason so operator does not retry.
            if (sink->onRecordResult) {
                sink->onRecordResult(request->requestId, CONSOLE_STATUS_ERR,
                                    CONSOLE_OUTCOME_UNAVAILABLE, CONSOLE_REASON_NOT_EXECUTABLE);
            }
            break;
    }
}
