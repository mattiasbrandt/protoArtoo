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
#include "console_args.h"  // ConsoleArgs, consoleSplitCommandLine(), consoleParseArgs(),
                           // consoleValidateArgsAgainstSchema() - the shared argument
                           // contract (#221, ADR 0034, docs/console-protocol.md s.1.2)

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
#include "action_registry.h"  // ACTION_REGISTRY[]: canonical name -> RobotActionId (#220)
#include "api_actions.h"      // evaluateActionTestGuard(), robotActionIsWebTestable() - the
                              // existing guard core (#220), reused verbatim, not duplicated
#include "rc_input.h"          // dispatchRcTriggerActionTest(), RcDispatchOutcome
#include "rc_action_types.h"   // rcPayloadValidForBodySequence(), rcPayloadValidForMarcduinoCommand()
                               // - the existing validators the live RC trigger path already
                               // calls (#221 reuses them verbatim for the raw Marcduino console
                               // operations, rather than inventing a second set of format rules)

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

// Forward declaration: alias-aware catalog lookup (defined in the "Operation
// Lookup and Execution" section below, alongside its other callers) - needed
// here so "help <alias>" resolves the same way "help <canonical-name>" does.
static const ConsoleCatalogEntry* consoleFindByNameOrAlias(const char* name);

// Emit help for an operation as console records
// Help text comes from the LittleFS file opened in setup().
// If the file is unavailable, degrade gracefully.
static void consoleEmitHelpForOperation(uint32_t requestId, const char* operationName,
                                       const ConsoleRecordSink* sink) {
    const ConsoleCatalogEntry* entry = consoleFindByNameOrAlias(operationName);
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

// Resolve an operator-typed name to its catalog entry, accepting either the
// canonical dotted name or a registered RC token alias (docs/console-protocol.md
// s.1.1: "existing short RC tokens remain accepted aliases ... an alias
// resolves through the same operation and never creates a second path", #220).
// consoleCatalogFindByName() (tools/generate_console_catalog.py, fenced on
// this ticket) only matches the canonical name; every alias-aware lookup in
// this module goes through this one wrapper instead of duplicating the
// aliases[] scan, so there is exactly one resolution mechanism.
static const ConsoleCatalogEntry* consoleFindByNameOrAlias(const char* name) {
    const ConsoleCatalogEntry* entry = consoleCatalogFindByName(name);
    if (entry != nullptr || name == nullptr) {
        return entry;
    }
    size_t count = 0;
    const ConsoleCatalogEntry* entries = consoleCatalogGetEntries(&count);
    for (size_t i = 0; i < count; ++i) {
        if (entries[i].aliases == nullptr) continue;
        for (const char* const* alias = entries[i].aliases; *alias != nullptr; ++alias) {
            if (strcmp(*alias, name) == 0) {
                return &entries[i];
            }
        }
    }
    return nullptr;
}

// Check if operation is recognized in the catalog
static bool consoleIsKnownOperation(const char* operationName) {
    if (operationName == nullptr) return false;
    return consoleFindByNameOrAlias(operationName) != nullptr;
}

// Check if operation is available on this board
// (ADR 0029: catalog entries carry build_flags and board_capability; runtime checks them)
static bool consoleIsAvailableOnBoard(const char* operationName) {
    const ConsoleCatalogEntry* entry = consoleFindByNameOrAlias(operationName);
    if (!entry) return false;
    return entry->available_on_board;
}

// Get the operation type from its name
static ConsoleOperationType consoleGetOperationType(const char* operationName) {
    const ConsoleCatalogEntry* entry = consoleFindByNameOrAlias(operationName);
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
// Private: non-motion action dispatch (#220/#221, ADR 0034)
//
// Every action entry that is RC-bindable (ACTION_REGISTRY[], canonical names
// verbatim - src/web/action_registry.cpp) and not analog runs from here
// through dispatchRcTriggerActionTest() (include/rc_input.h): the SAME
// dispatch core the REST /api/actions/test route and the live RC trigger
// path share. Motion (analog) actions stay CONSOLE_REASON_NOT_EXECUTABLE
// below, unchanged - #222 (drive safety) wires them through the
// drive/dome-speed backbone instead, never through this path. Of the
// payload-needing targets (robotActionNeedsPayload()), only
// DOME_ACTION_MARCDUINO_SEQ/CMD are validated and dispatched here (#221) -
// see consoleExecuteAction()'s own comment for why dome.action.dome-sequence
// stays CONSOLE_REASON_EXECUTOR_NOT_READY. There is exactly one guard core
// and one dispatch core; this section only maps their results onto Console
// Records.
// =============================================================================

// Multi-record "invalid" response naming the offending argument key - the
// "with the key named" half of docs/console-protocol.md s.1.2 / #221
// criterion 2 the single-record onRecordResult() sink call has no field
// for. Reused by schema failures (unknown/missing/out-of-range) and the
// Marcduino payload's own format check below.
static void consoleEmitArgFailure(uint32_t requestId, const char* operationName,
                                  const char* badKey, ConsoleReason reason,
                                  const ConsoleRecordSink* sink) {
    if (sink->onRecordBegin) {
        sink->onRecordBegin(requestId, operationName);
    }
    if (sink->onRecordField) {
        sink->onRecordField(requestId, "argument", badKey != nullptr ? badKey : "");
    }
    if (sink->onRecordEnd) {
        sink->onRecordEnd(requestId, CONSOLE_STATUS_ERR, CONSOLE_OUTCOME_INVALID, reason);
    }
}

// Argument-parse failures (malformed quoting/escaping/UTF-8, or too many
// key=value pairs) have no single offending key - consoleParseArgs() never
// got far enough to resolve one - so this answers without a field record,
// matching every other guard path's single-result shape (docs/console-
// protocol.md s.3.1).
static void consoleEmitArgParseError(uint32_t requestId, ConsoleArgParseStatus status,
                                     const ConsoleRecordSink* sink) {
    ConsoleReason reason = (status == CONSOLE_ARGS_PARSE_TOO_MANY) ? CONSOLE_REASON_LINE_TOO_LONG
                                                                    : CONSOLE_REASON_MALFORMED_ARGUMENT;
    if (sink->onRecordResult) {
        sink->onRecordResult(requestId, CONSOLE_STATUS_ERR, CONSOLE_OUTCOME_INVALID, reason);
    }
}

// Resolve a catalog operation name to the RobotActionId it dispatches as,
// reusing ACTION_REGISTRY[] (the same canonical-name<->RobotActionId table
// GET /api/actions and the RC mapping UI already use) rather than a second
// name<->id table. Returns false when this operation has no RC-bindable
// target at all (drive/dome-speed motion aside, that covers every config
// and status entry, and every action #221-#227 still own).
static bool consoleFindRobotActionId(const char* canonicalName, RobotActionId* out) {
    for (size_t i = 0; i < ACTION_REGISTRY_SIZE; ++i) {
        if (strcmp(ACTION_REGISTRY[i].name, canonicalName) == 0) {
            *out = ACTION_REGISTRY[i].id;
            return true;
        }
    }
    return false;
}

// Maps the dispatch core's adapter-agnostic outcome onto a Console outcome +
// reason. Only the three outcomes rcDispatchSingleAction()/
// dispatchRcTriggerActionTest() can produce are handled; there is no default
// case so a future RcDispatchOutcome addition fails this switch at compile
// time instead of silently falling through to a wrong reason.
static ConsoleOutcome consoleMapDispatchOutcome(RcDispatchOutcome outcome, ConsoleReason* outReason) {
    switch (outcome) {
        case RcDispatchOutcome::kQueued:
            *outReason = CONSOLE_REASON_NONE;
            return CONSOLE_OUTCOME_QUEUED;
        case RcDispatchOutcome::kQueueFull:
            *outReason = CONSOLE_REASON_QUEUE_FULL;
            return CONSOLE_OUTCOME_QUEUE_FULL;
        case RcDispatchOutcome::kBlockedByState:
            *outReason = CONSOLE_REASON_TEMPORARILY_UNAVAILABLE;
            return CONSOLE_OUTCOME_UNAVAILABLE;
    }
    *outReason = CONSOLE_REASON_NONE;
    return CONSOLE_OUTCOME_INTERNAL_ERROR;
}

// RcTriggerBinding::marcduinoPayload[16] (include/rc_action_types.h) is the
// live RC mapping page's own payload field size - the "existing handler"
// contract a Console-typed value is held to (no widening): a real RC
// binding can never carry more than 15 characters + NUL, so neither can a
// Console argument that reaches the same dispatch core.
static const size_t kMarcduinoPayloadMax = 16;

// Executes one resolved RC-bindable action through the existing guard core
// (evaluateActionTestGuard(), include/api_actions.h - reused verbatim, not
// duplicated) and the existing dispatch core, then answers with a single
// type=result record (guard paths never begin/end a multi-record response,
// matching every other guard path in this module) - except the two
// Marcduino targets' own argument failures, which name the key (see
// consoleEmitArgFailure() above).
static void consoleExecuteAction(uint32_t requestId, const ConsoleCatalogEntry* entry,
                                 RobotActionId target, ConsoleCommandSource source,
                                 const ConsoleArgs& args, const ConsoleRecordSink* sink) {
    bool webControlEnabled = false;
    taskENTER_CRITICAL(&robotStateMux);
    webControlEnabled = robotState.webControlEnabled;
    taskEXIT_CRITICAL(&robotStateMux);

    ActionTestGuardResult guard = evaluateActionTestGuard(target, webControlEnabled);

    // evaluateActionTestGuard() blocks every payload-needing target
    // unconditionally - correct for its only other caller (REST
    // /api/actions/test), which can never supply one. The Console now can,
    // for the two Marcduino targets whose payload is validated below (#221)
    // - every OTHER payload-needing target (dome.action.dome-sequence) is
    // unchanged and still refused here: DM:<NAME> forwarding has no
    // existing pure validator to reuse (its acceptance is decided deep in
    // sequenceStart()/the dome link, not a function this module can call),
    // and "no widening" means not inventing one - that stays
    // CONSOLE_REASON_EXECUTOR_NOT_READY until a future ticket closes it.
    bool isValidatedMarcduinoTarget =
        (target == DOME_ACTION_MARCDUINO_SEQ || target == DOME_ACTION_MARCDUINO_CMD);

    if (guard != ACTION_TEST_ALLOWED &&
        !(guard == ACTION_TEST_ACTION_NOT_TESTABLE && isValidatedMarcduinoTarget)) {
        ConsoleOutcome outcome = CONSOLE_OUTCOME_BLOCKED;
        ConsoleReason reason = CONSOLE_REASON_BLOCKED_BY_STATE;
        if (guard == ACTION_TEST_ACTION_NOT_TESTABLE) {
            // This guard result conflates two different reasons nothing
            // runs (matching the REST route's coarser HTTP shape); the
            // Console can be more precise since #220 already has
            // robotActionIsAnalog() in scope. Motion/analog targets are
            // permanently outside this single-shot mechanism (#222 wires
            // them through the drive/dome-speed backbone instead, never
            // through dispatchRcTriggerActionTest()).
            outcome = CONSOLE_OUTCOME_UNAVAILABLE;
            reason = robotActionIsAnalog(target) ? CONSOLE_REASON_NOT_EXECUTABLE
                                                 : CONSOLE_REASON_EXECUTOR_NOT_READY;
        }
        if (sink->onRecordResult) {
            sink->onRecordResult(requestId, CONSOLE_STATUS_ERR, outcome, reason);
        }
        return;
    }

    // Argument validation happens HERE, after the guard has passed (or been
    // bypassed for the two Marcduino targets) - not earlier in
    // consoleExecuteCommand(), because a target the guard refuses outright
    // (an analog motion target, say) must answer NOT_EXECUTABLE regardless
    // of whether its registry schema has a required argument the operator
    // did not supply; validating first would wrongly preempt that answer
    // with MISSING_ARGUMENT for a target this mechanism was never going to
    // run anyway.
    char payload[kMarcduinoPayloadMax] = {};
    if (isValidatedMarcduinoTarget) {
        // No registry params: schema for these two targets - a raw
        // Marcduino string does not fit a type/range/enum shape, and adding
        // one would touch data/console_help.txt's generated offsets
        // (fenced on this ticket). The key name ("value") and the format
        // rule are hardcoded here instead, reusing the SAME validators the
        // live RC trigger path already calls (include/rc_action_types.h) -
        // "accept exactly what the existing handlers accept ... no
        // widening" (#221 acceptance criterion).
        const char* value = consoleArgsFind(args, "value");
        if (value == nullptr) {
            consoleEmitArgFailure(requestId, entry->name, "value", CONSOLE_REASON_MISSING_ARGUMENT,
                                  sink);
            return;
        }
        if (strlen(value) >= kMarcduinoPayloadMax) {
            consoleEmitArgFailure(requestId, entry->name, "value", CONSOLE_REASON_OUT_OF_RANGE, sink);
            return;
        }
        bool valid = (target == DOME_ACTION_MARCDUINO_SEQ) ? rcPayloadValidForBodySequence(value)
                                                            : rcPayloadValidForMarcduinoCommand(value);
        if (!valid) {
            consoleEmitArgFailure(requestId, entry->name, "value", CONSOLE_REASON_OUT_OF_RANGE, sink);
            return;
        }
        snprintf(payload, sizeof(payload), "%s", value);
    } else {
        // Every other action this mechanism dispatches (guard-allowed,
        // non-analog, non-Marcduino): validate the full argument set
        // against the catalog's schema (criterion 2, #221) - unknown key,
        // missing required key, or type/range/enum failure on a present
        // key, in that order. An empty schema (entry->params == NULL) means
        // zero valid keys, so any supplied argument is unknown - this is
        // the fact-2 fix: a wired action with no declared arguments no
        // longer silently accepts (or, on the pre-#221 serial path,
        // silently ignores) one it was given.
        char badKey[40] = {};
        ConsoleArgSchemaStatus schemaStatus =
            consoleValidateArgsAgainstSchema(entry->params, args, badKey, sizeof(badKey));
        if (schemaStatus != CONSOLE_ARG_SCHEMA_OK) {
            ConsoleReason reason = (schemaStatus == CONSOLE_ARG_SCHEMA_UNKNOWN_KEY)
                                       ? CONSOLE_REASON_UNKNOWN_ARGUMENT
                                   : (schemaStatus == CONSOLE_ARG_SCHEMA_MISSING_REQUIRED)
                                       ? CONSOLE_REASON_MISSING_ARGUMENT
                                       : CONSOLE_REASON_OUT_OF_RANGE;
            consoleEmitArgFailure(requestId, entry->name, badKey, reason, sink);
            return;
        }
    }

    CommandSource src = (source == CONSOLE_SOURCE_SERIAL) ? SRC_SERIAL_CONSOLE : SRC_WEB_CONSOLE;
    RcDispatchOutcome dispatchOutcome = dispatchRcTriggerActionTest(target, payload, true, src);

    ConsoleReason reason = CONSOLE_REASON_NONE;
    ConsoleOutcome outcome = consoleMapDispatchOutcome(dispatchOutcome, &reason);
    ConsoleStatus status = (outcome == CONSOLE_OUTCOME_QUEUED) ? CONSOLE_STATUS_OK : CONSOLE_STATUS_ERR;
    if (sink->onRecordResult) {
        sink->onRecordResult(requestId, status, outcome, reason);
    }
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

    // The shared argument contract's first step (#221, criterion 1): split
    // the combined line both adapters hand over (ConsoleRequest::operationName
    // - a full "operation key=value ..." string, matching what the web
    // adapter has always sent and what the widened consoleBuildCommandLine()
    // now sends for serial too, include/console_cli_line.h) into a bare
    // operation name and a raw, still-untokenized argument remainder. Copied
    // into a local, mutable scratch buffer (sized to match the web adapter's
    // own command buffer, src/web/api_console.cpp) so consoleSplitCommandLine()
    // and consoleParseArgs() (include/console_args.h) can tokenize/unescape in
    // place without requiring either adapter to hand over mutable storage.
    // Local (stack), not static: consoleExecuteCommand() is called from both
    // the serial task and the web request handler, both on Core 0 - a shared
    // static buffer would let one adapter's in-flight command corrupt the
    // other's if they ever overlapped (#229 owns proving/hardening
    // cross-adapter concurrency; this function does not create a new
    // instance of that hazard while #229 is pending).
    char lineBuf[256];
    snprintf(lineBuf, sizeof(lineBuf), "%s", request->operationName != nullptr ? request->operationName : "");
    char* opName = nullptr;
    char* rawArgs = nullptr;
    consoleSplitCommandLine(lineBuf, &opName, &rawArgs);

    // Meta-command: help [operation]
    if (opName != nullptr && strcmp(opName, "help") == 0) {
        // help's argument is a bare operation name, not a key=value pair -
        // meta-commands have their own grammar, distinct from registry
        // operations (docs/console-protocol.md s.2) - so this reads rawArgs
        // directly rather than through consoleParseArgs().
        const char* targetOp = rawArgs;
        while (*targetOp == ' ') {
            ++targetOp;
        }
        if (*targetOp == '\0') {
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
        }
        consoleEmitHelpForOperation(request->requestId, targetOp, sink);
        return;
    }

    // Meta-command: operations [type=<type>]
    if (opName != nullptr && strcmp(opName, "operations") == 0) {
        ConsoleArgs parsedArgs = {};
        ConsoleArgParseStatus parseStatus = consoleParseArgs(rawArgs, &parsedArgs);
        if (parseStatus != CONSOLE_ARGS_PARSE_OK) {
            consoleEmitArgParseError(request->requestId, parseStatus, sink);
            return;
        }

        const char* filterType = consoleArgsFind(parsedArgs, "type");

        // "type" is the only key "operations" recognizes; any other
        // supplied key is unknown rather than silently ignored (matching
        // criterion 2's "unknown key -> invalid with the key named" for
        // registry operations - applied here too so a typo like
        // "operations tyep=action" is reported, not answered as if it were
        // bare "operations").
        for (size_t i = 0; i < parsedArgs.count; ++i) {
            if (strcmp(parsedArgs.items[i].key, "type") != 0) {
                consoleEmitArgFailure(request->requestId, "operations", parsedArgs.items[i].key,
                                      CONSOLE_REASON_UNKNOWN_ARGUMENT, sink);
                return;
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
    if (!consoleIsAvailableOnBoard(opName)) {
        // Unavailable operation: return single result record (guard path)
        if (sink->onRecordResult) {
            sink->onRecordResult(request->requestId, CONSOLE_STATUS_ERR,
                                CONSOLE_OUTCOME_UNAVAILABLE, CONSOLE_REASON_NOT_ON_THIS_BOARD);
        }
        return;
    }

    // Execute the operation based on its type
    ConsoleOperationType opType = consoleGetOperationType(opName);

    switch (opType) {
        case CONSOLE_OP_STATUS: {
            // Status queries take no arguments (docs/console-protocol.md
            // s.1.1) - any supplied key is unknown rather than silently
            // ignored, matching the same rule applied to `operations` above
            // and to registry actions below.
            if (rawArgs != nullptr && rawArgs[0] != '\0') {
                ConsoleArgs parsedArgs = {};
                ConsoleArgParseStatus parseStatus = consoleParseArgs(rawArgs, &parsedArgs);
                if (parseStatus != CONSOLE_ARGS_PARSE_OK) {
                    consoleEmitArgParseError(request->requestId, parseStatus, sink);
                    break;
                }
                if (parsedArgs.count > 0) {
                    consoleEmitArgFailure(request->requestId, opName, parsedArgs.items[0].key,
                                          CONSOLE_REASON_UNKNOWN_ARGUMENT, sink);
                    break;
                }
            }

            ConsoleStatusExecutorFn executor = consoleFindStatusExecutor(opName);
            if (executor != nullptr) {
                // A real query: begin + fields (emitted by the executor) + end.
                if (sink->onRecordBegin) {
                    sink->onRecordBegin(request->requestId, opName);
                }
                executor(request->requestId, sink);  // executor emits fields + calls onRecordEnd
                break;
            }

            // No dispatch table row. The catalog's is_query flag (generated
            // from the registry's fields:/is_query: false, #212) distinguishes
            // two different reasons nothing runs, so the operator sees which
            // one applies instead of one generic answer for both:
            const ConsoleCatalogEntry* entry = consoleCatalogFindByName(opName);
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

        case CONSOLE_OP_ACTION: {
            // Resolve the (possibly aliased) operation name to its
            // RobotActionId via ACTION_REGISTRY[] (#220). Not found here
            // means this action has no RC-bindable target yet - a
            // not-yet-wired action #226/#227 own, or a motion target #222
            // owns - unchanged from before this ticket.
            const ConsoleCatalogEntry* entry = consoleFindByNameOrAlias(opName);
            RobotActionId target = ROBOT_ACTION_NONE;
            if (entry != nullptr && consoleFindRobotActionId(entry->name, &target)) {
                // Tokenize the argument remainder ONCE here (#221 criterion
                // 1: the one fixed-capacity representation every executor
                // reads from). Schema validation happens inside
                // consoleExecuteAction(), AFTER its guard check - not here -
                // so a target the guard refuses outright (an analog motion
                // target, say) still answers on the guard's own terms
                // rather than a schema failure for arguments a dispatch
                // that will never run does not need.
                ConsoleArgs parsedArgs = {};
                ConsoleArgParseStatus parseStatus = consoleParseArgs(rawArgs, &parsedArgs);
                if (parseStatus != CONSOLE_ARGS_PARSE_OK) {
                    consoleEmitArgParseError(request->requestId, parseStatus, sink);
                    break;
                }

                consoleExecuteAction(request->requestId, entry, target, request->source, parsedArgs,
                                     sink);
                break;
            }
            if (sink->onRecordResult) {
                sink->onRecordResult(request->requestId, CONSOLE_STATUS_ERR,
                                    CONSOLE_OUTCOME_UNAVAILABLE, CONSOLE_REASON_EXECUTOR_NOT_READY);
            }
            break;
        }

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
