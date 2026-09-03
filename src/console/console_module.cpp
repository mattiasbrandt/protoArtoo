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

// Unguarded (unlike the FreeRTOS/heap-caps group below, which has no native
// stub): millis() needs a declaration on both Arduino and native builds, and
// test/stubs/include/Arduino.h supplies one for native - same reasoning
// src/web/api_drive.cpp's own unconditional Arduino.h include already
// documents for its driveArbiterSubmit()/millis() calls, reused verbatim
// here for the Commanded Mode direct executors below (#226).
#include <Arduino.h>

#ifdef ARDUINO
#include <freertos/FreeRTOS.h>
#include <esp_heap_caps.h>
#endif

#include "logging.h"
#include "robot_state.h"
#include "failsafe_gate.h"
#include "web_server.h"       // getLogBufferCount(), copyLogLineAt() - the log-ring seam
                              // consoleExecuteSystemStatusLogs() below streams from (#239)
#include "log_buffer.h"       // LOG_LINE_MAX - sizes that executor's per-line scratch buffer
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
#include "api_config.h"        // configCommitApplied() - the ADR 0034 Commit Step beside
                               // configApply(), shared verbatim with handleConfigPost (#226)
#include "api_config_apply.h"  // configApply(), ConfigApplyResult
#include "api_wifi_apply.h"    // wifiApply(), wifiCommitApplied() - the POST /api/wifi
                               // Apply Core and its ADR 0034 Commit Step, shared verbatim
                               // with handleWifiPost (#227; the Commit Step was extracted
                               // for exactly this second caller in that ticket's phase 1)
#include "api_helpers.h"       // parseBoolValue() - reused verbatim for rc.action.toggle-debug's
                               // enabled= argument, matching src/web/api_rc.cpp's own JSON body parse
#include "commanded_modes.h"   // commandedSetStationary/Sleep/WebControl/RcDebug() - Commanded
                               // Mode setters (#226 criterion 4: Commanded Modes go only
                               // through these, never the queued RC-dispatch core below)
#include "config_cache.h"      // configCacheRead/Apply, configCacheReadActiveComponentToggle
#include "console_config_fields.h"  // kComponentToggleFields[] - Component Toggle name<->field
                                     // table (#226; see its own header for why this is
                                     // hand-written C++ rather than registry-generated)
#include "drive_arbiter.h"     // driveArbiterSubmit(), DriveSource - the zero-frame release
                               // disable-web-control already submits (src/web/api_drive.cpp),
                               // and (#222) drive.action.move's own submission
#include "drive_speed_preset.h"  // SpeedPresetId, applySpeedPresetPersisted() - #222's
                                  // drive.action.speed-preset-{slow,normal,turbo} executors
                                  // reuse this verbatim, the same function
                                  // src/web/api_drive.cpp's handleSpeedPresetPost() calls
#include "mood.h"              // applyMood() - system.config.mood's and system.action.set-mood's
                               // real executor (registry drift note: the registry's own
                               // `executor:` field for system.config.mood says configApply,
                               // which is wrong - see the status comment; not fixed here, that
                               // edit reaches the fenced data/console_help.txt)
#include "api_servo.h"         // parseArmId(), servoSubmitCommand() - the ADR 0034 Commit Step
                               // beside handleServoPost() (#221 remainder), reused verbatim by
                               // servo.action.open/close/set-position below
#include "ledc_pwm.h"          // SERVO_PULSE_MIN_US/MAX_US - the same pulse-width bounds
                               // handleServoPost() enforces for its own position_us range
#include "api_identity.h"      // identitySetCommitApplied() - the ADR 0034 Commit Step beside
                               // handleIdentityPost() (#221 remainder), reused verbatim by
                               // system.action.set-identity below
#include "aux_led.h"           // AuxLedEffect, parseAuxLedEffect(), auxLedQueueSetColor/SetEffect()
                               // - reused verbatim by aux.action.led-color/-effect below (#221
                               // remainder); no extraction needed, both are already complete
                               // single-call cores with no persist step
#include "seq_store_index.h"   // SeqIndexEntry, seqStoreIndexCount/At() - dome.api.list-sequences
                               // below (#221 remainder), the same index handleSeqListGet() reads
#include "sequence_dispatcher.h"  // SequenceEntry, sequenceCatalogCount/At(), sequenceCatalogFind()
                                   // - dome.api.list-builtin-sequences below and list-sequences'
                                   // own "retrained" check, both reused verbatim from
                                   // handleSeqBuiltinsGet()/handleSeqListGet() (src/web/api_seq.cpp)
#include "seq_json.h"          // seqToggleGroupToString() - reused verbatim by both list executors
#include "sequence_run_evidence.h"  // SeqRunEvidence, seqEvidenceSnapshot(), seqRunOutcomeName() -
                                     // dome.api.get-sequence-last-run below, the same snapshot
                                     // GET /api/seq/last-run serializes (src/seq_last_run_json.cpp)
#include "api_profiler.h"           // ProfilerReading, profilerRead(), profilerRequestTrace*() -
                                     // the shared read GET /api/profiler renders as JSON and
                                     // system.api.get-profiler renders as records (#224). Included
                                     // unconditionally: its reading types are plain data and exist
                                     // on every build, only the functions that fill them are behind
                                     // PA_HEAP_PROFILE.
#include "console_profiler_view.h"  // consoleEmitProfilerReading() - that rendering

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
            // A write-excluded param displaces required/optional rather than
            // adding a fourth token: the field is documented so an operator
            // can see it exists, and "write-excluded" is the whole answer to
            // whether it can be supplied (docs/console-protocol.md s.4.1).
            // Where to set it instead is the entry's own description.
            const char* disposition = p->write_excluded ? "write-excluded"
                                                        : (p->required ? "required" : "optional");
            int n = snprintf(paramsBuf + used, remaining, "%s%s:%s:%s", (used > 0) ? "," : "",
                            p->name, p->type, disposition);
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

// Check whether the operation was compiled into this image.
//
// available_in_build is the registry `build_flag:` macro's own compile-time
// value (ADR 0029; tools/generate_console_catalog.py emits the macro name
// itself, not a Python constant, so a differently-flagged env flips it with
// no regeneration). Entries with no build_flag carry a literal 1.
//
// Read live from the catalog on every call, exactly like the board check
// above: a Console Record answers for the image running now, never for
// whatever a discovery listing said a moment ago.
static bool consoleIsAvailableInBuild(const char* operationName) {
    const ConsoleCatalogEntry* entry = consoleFindByNameOrAlias(operationName);
    if (!entry) return false;
    return entry->available_in_build;
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
    snprintf(tempBuf, sizeof(tempBuf), "%lu", snap.uptimeMs);
    if (sink->onRecordField) sink->onRecordField(requestId, "uptimeMs", tempBuf);
    // resetReason is resetReasonName()'s static string literal - emitted
    // directly, no tempBuf copy needed (#225).
    if (sink->onRecordField) sink->onRecordField(requestId, "resetReason", snap.resetReason);

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

// system.status.logs (#239): recent log ring history as repeated `item`
// records, not `field` records - the answer is a sequence of log lines, not
// a fixed set of scalar JSON keys, so it does not fit the fields:/JSON-key
// model the other executors above follow (docs/action-registry.yaml carries
// is_query: true with no fields: for this one row - see the comment there).
//
// Deliberately does NOT call recentLogsBodyBuffer()/copyRecentLogs()
// (src/web/api_logs.cpp, GET /api/logs's own path): that buffer is one
// ~6 KB heap allocation sized and synchronised for exactly one caller (the
// web server task) - its own file comment says so. The Console task is a
// second, concurrent caller on a different core, so reusing it would race
// two writers into one unsynchronised buffer. A second dedicated buffer was
// rejected on RAM grounds too (8,580 B free on artoo-esp32 at #239's base
// commit; ~6 KB is over two thirds of that margin for one feature).
//
// Instead this reads the ring directly through getLogBufferCount() and
// copyLogLineAt() (include/web_server.h) - a seam that already existed,
// unused, since #212's ring-sizing work. Each copyLogLineAt() call takes the
// ring's own lock (logMux, src/main.cpp) for exactly one line, then releases
// it; nothing here holds a lock - ring or serial - across the whole listing.
// onRecordItem() (src/tasks/console_task.cpp / src/web/api_console.cpp)
// takes the serial mutex per emitted line for the same reason #219 R1
// established for `operations`: a multi-KB answer must never block Core 1's
// real-time loggers for its own wall-clock duration.
//
// Bounded by construction, not by a paging protocol: LOG_RING_MAX_LINES caps
// the ring at 48 lines (include/log_buffer.h) regardless of log level, so
// this answer's size is a small, fixed ceiling the same way `operations`'
// catalog listing is bounded by its (larger, but still fixed) entry count -
// docs/console-protocol.md s.2.1 the record itself, not a second paging
// mechanism.
//
// Concurrency note: getLogBufferCount() is read once, up front, fixing how
// many lines this answer covers (mirrors fillOperationsResponse() reading
// its command line once for the same reason, src/web/api_console.cpp). If
// the ring is already at full depth and a writer appends between two
// copyLogLineAt() calls, the oldest surviving line shifts by one slot and
// this loop can skip a line it would otherwise have reported - it never
// re-reads a line twice and never reads out of bounds or stale bytes, since
// each call is independently locked and bounds-checked against the ring's
// state at that instant. For a recent-history diagnostic query, an
// occasional dropped line under heavy concurrent logging is the accepted
// trade against the two alternatives above (a second buffer, or one lock
// held across a multi-KB copy).
static void consoleExecuteSystemStatusLogs(uint32_t requestId, const ConsoleRecordSink* sink) {
    size_t total = getLogBufferCount();
    char lineBuf[LOG_LINE_MAX];
    for (size_t i = 0; i < total; ++i) {
        if (copyLogLineAt(i, lineBuf, sizeof(lineBuf)) && sink->onRecordItem) {
            sink->onRecordItem(requestId, lineBuf);
        }
    }

    if (sink->onRecordEnd) {
        sink->onRecordEnd(requestId, CONSOLE_STATUS_OK, CONSOLE_OUTCOME_COMPLETED,
                         CONSOLE_REASON_NONE);
    }
}

// dome.api.get-sequence-last-run (#221 remainder): a plain snapshot copy
// under lock (seqEvidenceSnapshot(), src/sequence_run_evidence.cpp), not a
// paginated read - #223's field-based query shape fits directly. Field names
// and their presence conditions are read straight off populateSeqLastRunJson()
// (src/seq_last_run_json.cpp) for the eight scalar keys the registry's
// fields: list claims (see that entry's own comment for why the nested
// tx/cleanup/warnings/fxScopes/ring-mask keys are deliberately not
// reproduced here): "reason" only when non-empty, "endMs" only when
// non-zero, and every field but "valid" itself only when a run has actually
// been recorded (have == true) - the same "nothing to report yet" shape the
// REST JSON's own early return uses.
static void consoleExecuteDomeApiGetSequenceLastRun(uint32_t requestId, const ConsoleRecordSink* sink) {
    SeqRunEvidence ev = {};
    bool have = seqEvidenceSnapshot(ev);

    if (sink->onRecordField) {
        sink->onRecordField(requestId, "valid", have ? "true" : "false");
    }
    if (have) {
        char tempBuf[16] = {};
        if (sink->onRecordField) {
            sink->onRecordField(requestId, "name", ev.name);
        }
        // ev.source is a raw CommandSource byte (see SeqRunEvidence's own
        // field comment) - populateSeqLastRunJson() emits it as the same
        // unlabelled number, not a translated name, so this matches rather
        // than inventing a friendlier representation the REST answer lacks.
        snprintf(tempBuf, sizeof(tempBuf), "%u", (unsigned)ev.source);
        if (sink->onRecordField) {
            sink->onRecordField(requestId, "source", tempBuf);
            sink->onRecordField(requestId, "outcome", seqRunOutcomeName(ev.outcome));
            sink->onRecordField(requestId, "running",
                               (ev.outcome == SEQ_RUN_RUNNING) ? "true" : "false");
        }
        if (ev.reason[0] != '\0' && sink->onRecordField) {
            sink->onRecordField(requestId, "reason", ev.reason);
        }
        snprintf(tempBuf, sizeof(tempBuf), "%lu", (unsigned long)ev.startMs);
        if (sink->onRecordField) sink->onRecordField(requestId, "startMs", tempBuf);
        if (ev.endMs != 0) {
            snprintf(tempBuf, sizeof(tempBuf), "%lu", (unsigned long)ev.endMs);
            if (sink->onRecordField) sink->onRecordField(requestId, "endMs", tempBuf);
        }
    }

    if (sink->onRecordEnd) {
        sink->onRecordEnd(requestId, CONSOLE_STATUS_OK, CONSOLE_OUTCOME_COMPLETED,
                         CONSOLE_REASON_NONE);
    }
}

// dome.api.list-sequences / dome.api.list-builtin-sequences (#221 remainder):
// item-indexed queries, the same no-fields:/is_query:-true shape #239
// established for system.status.logs above - the answer is a sequence of
// item records (one per stored/catalog sequence), not scalar JSON keys.
// Each item line mirrors the matching REST row's own fields
// (handleSeqListGet()/handleSeqBuiltinsGet(), src/web/api_seq.cpp),
// colon-separated per-field rather than "=" - the same
// consoleFormatRcSourceSummary() convention above, so a value can never be
// mistaken for a second key=value pair on the wire. Both stores are
// small and fully in-memory (SEQ_STORE_MAX = 16, the Factory catalog is a
// flash-resident const table), so - like system.status.logs' bounded ring -
// no separate paging protocol is needed.
static void consoleExecuteDomeApiListSequences(uint32_t requestId, const ConsoleRecordSink* sink) {
    for (uint8_t i = 0; i < seqStoreIndexCount(); ++i) {
        const SeqIndexEntry* e = seqStoreIndexAt(i);
        if (e == nullptr) continue;
        if (sink->onRecordItem) {
            char itemBuf[160];
            // "retrained" mirrors handleSeqListGet()'s own check: a Learned
            // Sequence that shadows a Factory one by name.
            snprintf(itemBuf, sizeof(itemBuf),
                     "%s toggleGroup:%s suppressMs:%lu source:%s modified:%s valid:%s retrained:%s",
                     e->name, seqToggleGroupToString(e->toggleGroup), (unsigned long)e->suppressMs,
                     e->source, e->modified ? "true" : "false", e->valid ? "true" : "false",
                     (sequenceCatalogFind(e->name) != nullptr) ? "true" : "false");
            sink->onRecordItem(requestId, itemBuf);
        }
    }
    if (sink->onRecordEnd) {
        sink->onRecordEnd(requestId, CONSOLE_STATUS_OK, CONSOLE_OUTCOME_COMPLETED,
                         CONSOLE_REASON_NONE);
    }
}

static void consoleExecuteDomeApiListBuiltinSequences(uint32_t requestId,
                                                       const ConsoleRecordSink* sink) {
    for (uint8_t i = 0; i < sequenceCatalogCount(); ++i) {
        const SequenceEntry* e = sequenceCatalogAt(i);
        if (e == nullptr) continue;
        if (sink->onRecordItem) {
            // 320, not 160 like list-sequences above: purpose is free
            // operator-facing prose (up to 159 bytes on the longest Factory
            // entry today, src/tasks/sequence_catalog.cpp's DM:RESET), not a
            // handful of short fixed fields - sized with real margin above
            // the measured longest entry rather than snprintf's silent
            // truncation being the only thing standing between this and a
            // clipped purpose string.
            char itemBuf[320];
            snprintf(itemBuf, sizeof(itemBuf), "%s toggleGroup:%s suppressMs:%lu stepCount:%u purpose:%s",
                     e->name, seqToggleGroupToString(e->toggleGroup), (unsigned long)e->suppressMs,
                     (unsigned)e->stepCount, (e->purpose != nullptr) ? e->purpose : "");
            sink->onRecordItem(requestId, itemBuf);
        }
    }
    if (sink->onRecordEnd) {
        sink->onRecordEnd(requestId, CONSOLE_STATUS_OK, CONSOLE_OUTCOME_COMPLETED,
                         CONSOLE_REASON_NONE);
    }
}

#if PA_HEAP_PROFILE
// system.api.get-profiler (#224). Registered only on a profiler build, and
// only reachable there: consoleExecuteCommand()'s build guard answers
// not-in-this-build for this row on every other image before dispatch is
// consulted at all, so a row registered unconditionally would be dead weight
// in the flash table of every non-profiler build.
//
// profilerRead() (include/api_profiler.h) is the same one-shot read
// buildProfilerJson() renders; the Console does not reach into the profiler's
// muxed state itself, and does not emit JSON. The rendering lives in
// include/console_profiler_view.h so that the half of this executor a host can
// exercise - field names, item shapes, the measurement disclosure - is
// compiled into the native test binary even though the measurement itself is
// ESP32-only.
//
// The reading is a 512-byte stack local (measured for a 32-bit target, not
// estimated), not a static: consoleExecuteCommand() is entered from both the
// Console task and the web request handler, and nothing serialises them yet
// (#229 owns that), so a shared static buffer would let one adapter's
// in-flight snapshot corrupt the other's. The Console task has 5120 B of
// stack (src/main.cpp), and the request-lifecycle ring is streamed one
// 36-byte entry at a time rather than copied, for the same budget.
static void consoleExecuteSystemApiGetProfiler(uint32_t requestId, const ConsoleRecordSink* sink) {
    ProfilerReading reading = {};
    profilerRead(&reading);
    consoleEmitProfilerReading(requestId, reading, profilerRequestTraceCount(),
                               profilerRequestTraceAt, sink);
}
#endif  // PA_HEAP_PROFILE

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
    {"system.status.logs", consoleExecuteSystemStatusLogs},
    {"dome.api.get-sequence-last-run", consoleExecuteDomeApiGetSequenceLastRun},
    {"dome.api.list-sequences", consoleExecuteDomeApiListSequences},
    {"dome.api.list-builtin-sequences", consoleExecuteDomeApiListBuiltinSequences},
#if PA_HEAP_PROFILE
    {"system.api.get-profiler", consoleExecuteSystemApiGetProfiler},
#endif
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

// Maps evaluateActionTestGuard()'s result onto a Console outcome/reason and
// answers a single result record - the shape every guard-refused action
// answers with, shared by consoleExecuteAction() below (the ACTION_REGISTRY[]
// canonical-name path) and consoleExecuteDirectTestBindable() (the by-token
// path, #221 remainder) so the two mappings cannot drift apart. Only called
// once `guard != ACTION_TEST_ALLOWED` is already known.
static void consoleAnswerActionGuardResult(uint32_t requestId, ActionTestGuardResult guard,
                                           RobotActionId target, const ConsoleRecordSink* sink) {
    ConsoleOutcome outcome = CONSOLE_OUTCOME_BLOCKED;
    ConsoleReason reason = CONSOLE_REASON_BLOCKED_BY_STATE;
    if (guard == ACTION_TEST_ACTION_NOT_TESTABLE) {
        // This guard result conflates two different reasons nothing runs
        // (matching the REST route's coarser HTTP shape); the Console can be
        // more precise since #220 already has robotActionIsAnalog() in
        // scope. Motion/analog targets are permanently outside this
        // single-shot mechanism (#222 wires them through the drive/
        // dome-speed backbone instead, never through
        // dispatchRcTriggerActionTest()).
        outcome = CONSOLE_OUTCOME_UNAVAILABLE;
        reason = robotActionIsAnalog(target) ? CONSOLE_REASON_NOT_EXECUTABLE
                                             : CONSOLE_REASON_EXECUTOR_NOT_READY;
    }
    if (sink->onRecordResult) {
        sink->onRecordResult(requestId, CONSOLE_STATUS_ERR, outcome, reason);
    }
}

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
        consoleAnswerActionGuardResult(requestId, guard, target, sink);
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
// Private: config operation dispatch - Component Toggles (#226, ADR 0027/0033)
//
// No registry-driven schema here, unlike the status/action executors above:
// docs/action-registry.yaml's `params:` key (and any `executor:` fix) would
// change tools/generate_console_catalog.py's output, which also regenerates
// data/console_help.txt - unconditionally fenced on this ticket. So this
// dispatch validates against a hand-written table instead of
// consoleValidateArgsAgainstSchema()'s registry-sourced ConsoleParamDescriptor
// array, the same "hardcode the one schema this operation needs" precedent
// consoleExecuteAction() already set for the two Marcduino targets above -
// not a second tokenizer (criterion 2 is still satisfied: consoleParseArgs()/
// ConsoleArgs are reused verbatim, only the schema check is local).
// =============================================================================

// ConfigApplyResult is ~2.5 KB (include/api_config_apply.h) - too large for
// the Console task's 5120 B stack (src/main.cpp), smaller than the 8 KB web
// server task the header's own warning was written for (pin fact 3). The web
// handler (api_config.cpp) already keeps its own static instance rather than
// a stack local; sharing that ONE instance across the web server task and
// this module's two adapter tasks would recreate exactly the shared-single-
// caller-buffer hazard #239 rejected for the log ring. Unlike #239's case,
// there is no "read the underlying source directly" alternative here:
// configApply()'s only contract is to fill a ConfigApplyResult out-parameter,
// so a second static instance - scoped to this module, never shared with
// api_config.cpp's - is the justified choice (pin fact 4).
//
// That still leaves TWO concurrent writers of this ONE instance: the serial
// Console task (src/tasks/console_task.cpp, "Console", priority 2, pinned to
// core 0 - src/main.cpp) and the browser Console's psychic server task
// (src/web/api_console.cpp), which is explicitly pinned to core 0 too
// (s_server.config.core_id = 0, src/web/web_request_psychic.cpp). A
// preemption between the write (configApply()) and either read (the error
// check, or configCommitApplied()'s replay of result.applied/result.actions)
// lets one adapter's write corrupt the other's in-flight read - #206's own
// binding text is explicit that this must not happen ("Serialized at the
// module seam - browser and serial cannot race configuration persistence or
// shared result state"). s_configWriteMutex below serializes every caller of
// consoleWriteScalarConfigField() (Component Toggles and the plain scalar
// fields both go through it) across the entire configApply() -> error check
// -> configCommitApplied() window, so only one adapter's request ever has
// this instance in flight at a time.
static ConfigApplyResult s_consoleConfigApplyResult;

// A blocking FreeRTOS mutex, not a portMUX critical section: the held window
// can perform an NVS write (configCommitApplied()'s Preferences call, several
// ms of flash I/O), and holding interrupts disabled for that long - what a
// portMUX spinlock does - is unacceptable even confined to Core 0. Blocking
// one non-realtime Console-adapter task while the OTHER adapter's NVS write
// finishes is fine: it can never block Core 1 (DriveTask, RCInputTask, ...),
// which never calls into this module. Static storage (no heap allocation),
// matching src/main.cpp's own logSerialMutexStorage/logSerialMutex precedent
// for exactly this "module-scoped mutex, created once at boot" shape.
// Created from consoleModuleInit(), which setup() calls before either
// adapter's task/server exists (src/main.cpp), so by the time any command
// can reach consoleWriteScalarConfigField() the mutex already exists; the
// null check at each use is the same defensive fallback
// src/seq_store.cpp's lock() takes for the pre-init boot path.
static StaticSemaphore_t s_configWriteMutexStorage;
static SemaphoreHandle_t s_configWriteMutex = nullptr;

// A take that cannot get the mutex within this bound answers "temporarily
// unavailable" instead of proceeding - the failure mode this exists to
// prevent is silent corruption, not merely delay, so a bounded wait that can
// report busy is correct where an unbounded one would just hide the
// contention behind a longer stall.
static const TickType_t kConfigWriteMutexTimeoutTicks = pdMS_TO_TICKS(1000);

// RAII scope guard: xSemaphoreGive() runs on every exit path exactly once,
// including every early return in consoleWriteScalarConfigField() below - a
// lock leaked on one path would permanently deadlock every future config
// write on both adapters, a worse defect than the race this section closes.
class ConfigWriteMutexGuard {
public:
    explicit ConfigWriteMutexGuard(SemaphoreHandle_t mutex) : mutex_(mutex), held_(false) {
        if (mutex_ == nullptr) {
            held_ = true;  // pre-init fallback: single-threaded boot path, proceed unlocked
            return;
        }
        held_ = (xSemaphoreTake(mutex_, kConfigWriteMutexTimeoutTicks) == pdTRUE);
    }
    ~ConfigWriteMutexGuard() {
        if (held_ && mutex_ != nullptr) {
            xSemaphoreGive(mutex_);
        }
    }
    bool acquired() const { return held_; }

    ConfigWriteMutexGuard(const ConfigWriteMutexGuard&) = delete;
    ConfigWriteMutexGuard& operator=(const ConfigWriteMutexGuard&) = delete;

private:
    SemaphoreHandle_t mutex_;
    bool held_;
};

// Bridges a Console write onto the exact param name api_config_apply.cpp's
// `boolFields[]` table already reads for this field. The wire grammar allows
// both the generic `value=` shorthand (docs/console-protocol.md s.1: "system.
// config.log-level value=debug") and the underlying named key (criterion 3:
// "value= (or the named keys)"), so this ctx answers a lookup for `fieldKey`
// with whichever the operator supplied, `value=` taking precedence when both
// are present (arbitrary but documented here: the two are never both
// meaningful at once for a single-field toggle).
struct ScalarConfigArg {
    const ConsoleArgs* args;
    const char* fieldKey;
};

static const char* consoleScalarConfigParamGet(void* ctx, const char* name) {
    const ScalarConfigArg* adapter = static_cast<const ScalarConfigArg*>(ctx);
    if (strcmp(name, adapter->fieldKey) == 0) {
        const char* viaValue = consoleArgsFind(*adapter->args, "value");
        if (viaValue != nullptr) {
            return viaValue;
        }
        return consoleArgsFind(*adapter->args, adapter->fieldKey);
    }
    return consoleArgsFind(*adapter->args, name);
}

// A single-field config write accepts exactly two argument keys - "value"
// and the field's own named key - so this is a hand-written check rather
// than consoleValidateArgsAgainstSchema() (whose ConsoleParamDescriptor
// source is registry-driven and unreachable here, see the section header
// comment above). Any other supplied key is unknown, matching criterion 2's
// "unknown key -> invalid with the key named" for every other operation
// type in this module.
static bool consoleScalarConfigArgsValid(const ConsoleArgs& args, const char* fieldKey,
                                         const char** badKeyOut) {
    for (size_t i = 0; i < args.count; ++i) {
        if (strcmp(args.items[i].key, "value") != 0 &&
            strcmp(args.items[i].key, fieldKey) != 0) {
            *badKeyOut = args.items[i].key;
            return false;
        }
    }
    return true;
}

// Shared write path for a single-field scalar config write through
// configApply() + configCommitApplied() - the tokenize/validate/apply/
// commit sequence every scalar config write shares regardless of which
// outcome a clean write reports (Component Toggles below always answer
// staged-until-reboot per ADR 0027; the four non-toggle scalar fields this
// ticket also wires - drive.config.speed-limit, aux.config.led-pin/
// led-count, rc.config.mode - answer applied, matching how their owning
// task already reads config_cache live, the same as every other non-toggle
// configApply field REST already exposes).
static void consoleWriteScalarConfigField(uint32_t requestId, const char* operationName,
                                          const char* fieldKey, char* rawArgs,
                                          ConsoleCommandSource source, ConsoleOutcome successOutcome,
                                          const ConsoleRecordSink* sink) {
    ConsoleArgs parsedArgs = {};
    ConsoleArgParseStatus parseStatus = consoleParseArgs(rawArgs, &parsedArgs);
    if (parseStatus != CONSOLE_ARGS_PARSE_OK) {
        consoleEmitArgParseError(requestId, parseStatus, sink);
        return;
    }
    if (parsedArgs.count == 0) {
        // rawArgs was non-empty whitespace with no key=value pairs at all -
        // malformed, not a silent read (the caller only reaches here once
        // rawArgs held a non-whitespace byte).
        consoleEmitArgParseError(requestId, CONSOLE_ARGS_PARSE_MALFORMED, sink);
        return;
    }

    const char* badKey = nullptr;
    if (!consoleScalarConfigArgsValid(parsedArgs, fieldKey, &badKey)) {
        consoleEmitArgFailure(requestId, operationName, badKey, CONSOLE_REASON_UNKNOWN_ARGUMENT, sink);
        return;
    }

    ConfigSnapshot working = {};
    configCacheRead(&working);
    const bool domeEnabledBefore = working.system.enable_dome_esc;

    ScalarConfigArg adapter{&parsedArgs, fieldKey};
    ConfigParamSource params;
    params.ctx = &adapter;
    params.get = consoleScalarConfigParamGet;

    // Serialized against the other Console adapter (s_configWriteMutex's own
    // declaration comment above has the full reasoning): a scoped guard so
    // the lock releases as soon as this module's last read of
    // s_consoleConfigApplyResult is done, in configCommitApplied(), rather
    // than being held any longer than the shared static needs protecting.
    bool applyHadError = false;
    ConfigCommitOutcome commit = {};
    {
        ConfigWriteMutexGuard guard(s_configWriteMutex);
        if (!guard.acquired()) {
            // The other adapter is mid-write - report busy rather than
            // proceeding unserialized into the shared static.
            if (sink->onRecordResult) {
                sink->onRecordResult(requestId, CONSOLE_STATUS_ERR, CONSOLE_OUTCOME_UNAVAILABLE,
                                    CONSOLE_REASON_TEMPORARILY_UNAVAILABLE);
            }
            return;
        }

        configApply(params, &working, domeEnabledBefore, &s_consoleConfigApplyResult);
        applyHadError = s_consoleConfigApplyResult.error.hasError;
        if (!applyHadError) {
            CommandSource src = (source == CONSOLE_SOURCE_SERIAL) ? SRC_SERIAL_CONSOLE : SRC_WEB_CONSOLE;
            commit = configCommitApplied(&working, s_consoleConfigApplyResult, src);
        }
    }  // guard released here, after the last read of s_consoleConfigApplyResult

    if (applyHadError) {
        // configApply()'s only failure for a single supplied field is that
        // field's own type/range/enum check - OUT_OF_RANGE matches
        // consoleValidateArgsAgainstSchema()'s classification for the same
        // shape of failure on the registry-driven path.
        consoleEmitArgFailure(requestId, operationName, fieldKey, CONSOLE_REASON_OUT_OF_RANGE, sink);
        return;
    }

    if (!commit.persisted) {
        // "a failed NVS write is an explicit error" (criterion 3) - status=err
        // with the module's existing catch-all outcome; no dedicated
        // persistence-failure reason exists in the hand-maintained
        // ConsoleReason set (include/console_module.h), and none is added
        // here for these call sites alone.
        if (sink->onRecordResult) {
            sink->onRecordResult(requestId, CONSOLE_STATUS_ERR, CONSOLE_OUTCOME_INTERNAL_ERROR,
                                CONSOLE_REASON_NONE);
        }
        return;
    }

    if (sink->onRecordResult) {
        sink->onRecordResult(requestId, CONSOLE_STATUS_OK, successOutcome, CONSOLE_REASON_NONE);
    }
}

// system.config.enable_* (Component Toggles, ADR 0027/0033): a read with no
// arguments renders "saved" (the config cache - what the next boot applies)
// and "active" (what actually booted, include/config_cache.h's Active
// Component Toggle snapshot) side by side, criterion 4's "read shows saved
// vs active." A write always answers staged-until-reboot on success -
// ADR 0027: "toggle changes are staged at reboot", unconditionally, not
// contingent on whether the new value happens to differ from the currently
// active one (that comparison is exactly what the read side already
// answers). `entry` and `field` are both non-null on every call (the caller
// resolves `field` before dispatching here).
static void consoleExecuteComponentToggle(uint32_t requestId, const ConsoleCatalogEntry* entry,
                                          const ComponentToggleField* field,
                                          ConsoleCommandSource source, char* rawArgs,
                                          const ConsoleRecordSink* sink) {
    const bool isWrite = (rawArgs != nullptr && rawArgs[0] != '\0');

    if (!isWrite) {
        ConfigSnapshot snap = {};
        configCacheRead(&snap);
        const bool saved = snap.system.*(field->field);
        const size_t bitIndex = (size_t)(field - kComponentToggleFields);
        const bool active = configCacheReadActiveComponentToggle(bitIndex);

        if (sink->onRecordBegin) {
            sink->onRecordBegin(requestId, entry->name);
        }
        if (sink->onRecordField) {
            sink->onRecordField(requestId, "saved", saved ? "true" : "false");
            sink->onRecordField(requestId, "active", active ? "true" : "false");
        }
        if (sink->onRecordEnd) {
            sink->onRecordEnd(requestId, CONSOLE_STATUS_OK, CONSOLE_OUTCOME_COMPLETED,
                             CONSOLE_REASON_NONE);
        }
        return;
    }

    // Every Component Toggle write that clears validation always answers
    // staged-until-reboot (ADR 0027) - never contingent on whether the new
    // value differs from the currently active one, which is exactly what
    // the read side above already answers.
    consoleWriteScalarConfigField(requestId, entry->name, field->paramKey, rawArgs, source,
                                  CONSOLE_OUTCOME_STAGED_UNTIL_REBOOT, sink);
}

// =============================================================================
// Private: the remaining scalar config.type rows configApply() already
// handles live (not staged - unlike the Component Toggles above, these
// fields are read from config_cache every control-loop iteration by their
// owning task, the same "applied" semantics REST already gives them).
// Confirmed one at a time by reading src/web/api_config_apply.cpp, not
// assumed from the registry: sound.config.volume and sound.config.mood-
// interval-* claim executor: configApply too but that function has no
// "volume"/"sndIntQuiet"-shaped param at all (volume is set inline in
// handleAudioPost's action=volume branch; the mood-interval fields have no
// write path anywhere in this codebase today) - both are registry/
// implementation gaps flagged in the status comment, not wired here since
// there is no real Apply Core behind either to reuse.
// =============================================================================

// drive.config.speed-limit: value=<0..600> (speedLimitMax).
static void consoleExecuteDriveSpeedLimit(uint32_t requestId, const ConsoleCatalogEntry* entry,
                                          char* rawArgs, ConsoleCommandSource source,
                                          const ConsoleRecordSink* sink) {
    const bool isWrite = (rawArgs != nullptr && rawArgs[0] != '\0');
    if (!isWrite) {
        ConfigSnapshot snap = {};
        configCacheRead(&snap);
        char buf[12] = {};
        snprintf(buf, sizeof(buf), "%d", (int)snap.drive.speedLimitMax);
        if (sink->onRecordBegin) {
            sink->onRecordBegin(requestId, entry->name);
        }
        if (sink->onRecordField) {
            sink->onRecordField(requestId, "value", buf);
        }
        if (sink->onRecordEnd) {
            sink->onRecordEnd(requestId, CONSOLE_STATUS_OK, CONSOLE_OUTCOME_COMPLETED,
                             CONSOLE_REASON_NONE);
        }
        return;
    }
    consoleWriteScalarConfigField(requestId, entry->name, "speedLimitMax", rawArgs, source,
                                  CONSOLE_OUTCOME_APPLIED, sink);
}

// aux.config.led-pin: value=<0..AUX_LED_PIN_MAX> (aux_led_pin).
static void consoleExecuteAuxLedPin(uint32_t requestId, const ConsoleCatalogEntry* entry,
                                   char* rawArgs, ConsoleCommandSource source,
                                   const ConsoleRecordSink* sink) {
    const bool isWrite = (rawArgs != nullptr && rawArgs[0] != '\0');
    if (!isWrite) {
        ConfigSnapshot snap = {};
        configCacheRead(&snap);
        char buf[8] = {};
        snprintf(buf, sizeof(buf), "%u", (unsigned)snap.servo.aux_led_pin);
        if (sink->onRecordBegin) {
            sink->onRecordBegin(requestId, entry->name);
        }
        if (sink->onRecordField) {
            sink->onRecordField(requestId, "value", buf);
        }
        if (sink->onRecordEnd) {
            sink->onRecordEnd(requestId, CONSOLE_STATUS_OK, CONSOLE_OUTCOME_COMPLETED,
                             CONSOLE_REASON_NONE);
        }
        return;
    }
    consoleWriteScalarConfigField(requestId, entry->name, "aux_led_pin", rawArgs, source,
                                  CONSOLE_OUTCOME_APPLIED, sink);
}

// aux.config.led-count: value=<AUX_LED_COUNT_DEFAULT..AUX_LED_COUNT_MAX>
// (aux_led_count).
static void consoleExecuteAuxLedCount(uint32_t requestId, const ConsoleCatalogEntry* entry,
                                     char* rawArgs, ConsoleCommandSource source,
                                     const ConsoleRecordSink* sink) {
    const bool isWrite = (rawArgs != nullptr && rawArgs[0] != '\0');
    if (!isWrite) {
        ConfigSnapshot snap = {};
        configCacheRead(&snap);
        char buf[8] = {};
        snprintf(buf, sizeof(buf), "%u", (unsigned)snap.servo.aux_led_count);
        if (sink->onRecordBegin) {
            sink->onRecordBegin(requestId, entry->name);
        }
        if (sink->onRecordField) {
            sink->onRecordField(requestId, "value", buf);
        }
        if (sink->onRecordEnd) {
            sink->onRecordEnd(requestId, CONSOLE_STATUS_OK, CONSOLE_OUTCOME_COMPLETED,
                             CONSOLE_REASON_NONE);
        }
        return;
    }
    consoleWriteScalarConfigField(requestId, entry->name, "aux_led_count", rawArgs, source,
                                  CONSOLE_OUTCOME_APPLIED, sink);
}

// rc.config.mode: value=standard_pwm|single_sbus|dual_sbus (rcInputMode).
// Registry drift note: docs/action-registry.yaml lists this row's executor
// as rcMapApply, which is wrong - rcMapApply() handles the RC BINDING
// table, not the input-mode enum; configApply()'s own "rcInputMode" param
// (src/web/api_config_apply.cpp) is the real one. Not fixed in the registry
// here since that edit reaches fenced data/console_help.txt (status comment).
static void consoleExecuteRcMode(uint32_t requestId, const ConsoleCatalogEntry* entry, char* rawArgs,
                                 ConsoleCommandSource source, const ConsoleRecordSink* sink) {
    const bool isWrite = (rawArgs != nullptr && rawArgs[0] != '\0');
    if (!isWrite) {
        ConfigSnapshot snap = {};
        configCacheRead(&snap);
        const char* mode = "dual_sbus";
        switch (snap.system.rc_input_mode) {
            case RC_INPUT_STANDARD_PWM:
                mode = "standard_pwm";
                break;
            case RC_INPUT_SINGLE_SBUS:
                mode = "single_sbus";
                break;
            case RC_INPUT_DUAL_SBUS:
            default:
                mode = "dual_sbus";
                break;
        }
        if (sink->onRecordBegin) {
            sink->onRecordBegin(requestId, entry->name);
        }
        if (sink->onRecordField) {
            sink->onRecordField(requestId, "value", mode);
        }
        if (sink->onRecordEnd) {
            sink->onRecordEnd(requestId, CONSOLE_STATUS_OK, CONSOLE_OUTCOME_COMPLETED,
                             CONSOLE_REASON_NONE);
        }
        return;
    }
    consoleWriteScalarConfigField(requestId, entry->name, "rcInputMode", rawArgs, source,
                                  CONSOLE_OUTCOME_APPLIED, sink);
}

typedef void (*ConsoleScalarConfigExecutorFn)(uint32_t requestId, const ConsoleCatalogEntry* entry,
                                              char* rawArgs, ConsoleCommandSource source,
                                              const ConsoleRecordSink* sink);

struct ConsoleScalarConfigExecutorEntry {
    const char* operationName;
    ConsoleScalarConfigExecutorFn executor;
};

static const ConsoleScalarConfigExecutorEntry g_scalarConfigExecutors[] = {
    {"drive.config.speed-limit", consoleExecuteDriveSpeedLimit},
    {"aux.config.led-pin", consoleExecuteAuxLedPin},
    {"aux.config.led-count", consoleExecuteAuxLedCount},
    {"rc.config.mode", consoleExecuteRcMode},
};
static const size_t kScalarConfigExecutorCount =
    sizeof(g_scalarConfigExecutors) / sizeof(g_scalarConfigExecutors[0]);

static ConsoleScalarConfigExecutorFn consoleFindScalarConfigExecutor(const char* canonicalName) {
    for (size_t i = 0; i < kScalarConfigExecutorCount; ++i) {
        if (strcmp(g_scalarConfigExecutors[i].operationName, canonicalName) == 0) {
            return g_scalarConfigExecutors[i].executor;
        }
    }
    return nullptr;
}

// system.config.mood (#226): the config-typed view of the same active-mood
// mechanism system.action.set-mood exposes as an action - both are backed by
// applyMood()/commandedSetActiveMood() (src/tasks/mood.cpp), one of the five
// Commanded Modes criterion 4 names. Read renders the live robotState value
// with no arguments; write takes value=<10|11|13|14>, the same enum
// system.action.set-mood already validates, and applies immediately -
// active mood is not staged (ADR 0027 only covers the 15 Component
// Toggles above; this is a live, non-hardware runtime flag).
static bool consoleMoodIdValid(uint8_t moodId) {
    return moodId == 10 || moodId == 11 || moodId == 13 || moodId == 14;
}

static void consoleExecuteMoodConfig(uint32_t requestId, const ConsoleCatalogEntry* entry,
                                     char* rawArgs, const ConsoleRecordSink* sink) {
    const bool isWrite = (rawArgs != nullptr && rawArgs[0] != '\0');

    if (!isWrite) {
        uint8_t moodId = 0;
        taskENTER_CRITICAL(&robotStateMux);
        moodId = robotState.activeMood;
        taskEXIT_CRITICAL(&robotStateMux);

        char buf[8] = {};
        snprintf(buf, sizeof(buf), "%u", (unsigned)moodId);
        if (sink->onRecordBegin) {
            sink->onRecordBegin(requestId, entry->name);
        }
        if (sink->onRecordField) {
            sink->onRecordField(requestId, "value", buf);
        }
        if (sink->onRecordEnd) {
            sink->onRecordEnd(requestId, CONSOLE_STATUS_OK, CONSOLE_OUTCOME_COMPLETED,
                             CONSOLE_REASON_NONE);
        }
        return;
    }

    ConsoleArgs parsedArgs = {};
    ConsoleArgParseStatus parseStatus = consoleParseArgs(rawArgs, &parsedArgs);
    if (parseStatus != CONSOLE_ARGS_PARSE_OK) {
        consoleEmitArgParseError(requestId, parseStatus, sink);
        return;
    }
    for (size_t i = 0; i < parsedArgs.count; ++i) {
        if (strcmp(parsedArgs.items[i].key, "value") != 0) {
            consoleEmitArgFailure(requestId, entry->name, parsedArgs.items[i].key,
                                  CONSOLE_REASON_UNKNOWN_ARGUMENT, sink);
            return;
        }
    }
    const char* raw = consoleArgsFind(parsedArgs, "value");
    if (raw == nullptr) {
        consoleEmitArgParseError(requestId, CONSOLE_ARGS_PARSE_MALFORMED, sink);
        return;
    }

    char* end = nullptr;
    long parsed = strtol(raw, &end, 10);
    if (end == raw || *end != '\0' || parsed < 0 || parsed > 255 ||
        !consoleMoodIdValid((uint8_t)parsed)) {
        consoleEmitArgFailure(requestId, entry->name, "value", CONSOLE_REASON_OUT_OF_RANGE, sink);
        return;
    }

    applyMood((uint8_t)parsed);

    if (sink->onRecordResult) {
        sink->onRecordResult(requestId, CONSOLE_STATUS_OK, CONSOLE_OUTCOME_APPLIED,
                            CONSOLE_REASON_NONE);
    }
}

// =============================================================================
// Private: wifi.config.settings - the Console's one GROUPED config write
// (#227). Unlike every scalar row above, this operation's fields are only
// meaningful as a set: a client posture needs a station SSID, an AP posture
// needs an AP SSID, and "half applied" is not a state Device WiFi Settings
// has. So it cannot go through consoleWriteScalarConfigField(); it validates
// the whole set in one call to the POST /api/wifi Apply Core (wifiApply(),
// ADR 0011) and then persists and stages through that core's Commit Step
// (wifiCommitApplied(), ADR 0034, extracted ahead of this path by #227's
// first phase). Neither the WiFi rules nor the post-apply side effects are
// re-implemented here - this section only translates Console arguments into
// the core's ConfigParamSource and the core's verdict into a Console Record.
// =============================================================================

// The two vocabularies this operation has to reconcile, and no third one.
// LEFT is the Console argument key: kebab-case, like every token the protocol
// defines (ADR 0034), and exactly what docs/action-registry.yaml declares as
// this row's params: - the spelling docs/console-protocol.md s.1 already
// shows (`wifi.config.settings mode=client sta-ssid="Workshop WiFi"`). RIGHT
// is the parameter name src/web/api_wifi_apply.cpp's own configParamGet()
// lookups use, copied verbatim: those are POST /api/wifi's body keys and are
// deliberately NOT renamed, because HTTP depends on them.
//
// The password fields are absent on purpose. They are real parameters of the
// Apply Core and real rows in the registry (marked write_excluded there), but
// they have no Console key at all: consoleWifiSettingsParamGet() below
// answers NULL for them unconditionally, and wifiApply() reads NULL as "not
// supplied - keep the stored value" (include/api_wifi_apply.h), so this
// adapter cannot set a password even if one somehow reached it.
struct WifiSettingsField {
    const char* consoleKey;
    const char* applyKey;
};
static const WifiSettingsField kWifiSettingsFields[] = {
    {"mode", "wifiMode"},
    {"sta-ssid", "staSsid"},
    {"ap-ssid", "apSsid"},
};
static const size_t kWifiSettingsFieldCount =
    sizeof(kWifiSettingsFields) / sizeof(kWifiSettingsFields[0]);

// The two human-text fields. Separated from the table above because they are
// the only values that carry operator prose (an SSID), which is what makes
// them the only ones needing the UTF-8 and quoting handling below.
static const char* const kWifiSsidConsoleKeys[] = {"sta-ssid", "ap-ssid"};
static const size_t kWifiSsidConsoleKeyCount =
    sizeof(kWifiSsidConsoleKeys) / sizeof(kWifiSsidConsoleKeys[0]);

struct WifiSettingsArgs {
    const ConsoleArgs* args;
};

static const char* consoleWifiSettingsParamGet(void* ctx, const char* name) {
    const WifiSettingsArgs* adapter = static_cast<const WifiSettingsArgs*>(ctx);
    for (size_t i = 0; i < kWifiSettingsFieldCount; ++i) {
        if (strcmp(kWifiSettingsFields[i].applyKey, name) == 0) {
            return consoleArgsFind(*adapter->args, kWifiSettingsFields[i].consoleKey);
        }
    }
    // Anything else the Apply Core asks for is a password field. NULL here is
    // not "the operator happened not to supply it" - it is the write
    // exclusion itself, expressed in the core's own omission-preserving
    // contract rather than as a special case inside the core.
    return nullptr;
}

// Case-insensitive "does `s` start with `lowerWord`". Written out rather than
// calling strncasecmp(), which is POSIX <strings.h> and not part of the
// freestanding C++ surface this module otherwise sticks to.
static bool consoleStartsWithIgnoringCase(const char* s, const char* lowerWord) {
    for (size_t i = 0; lowerWord[i] != '\0'; ++i) {
        if (s[i] == '\0') return false;
        if (tolower((unsigned char)s[i]) != lowerWord[i]) return false;
    }
    return true;
}

// Whether an argument key names a secret. Matched as a case-insensitive
// substring rather than against a fixed list of the four spellings the two
// vocabularies above can produce (sta-password, ap-password, staPassword,
// apPassword), because the rule has to hold for "any other secret-valued key"
// (#227) and not just today's four: a substring test can only ever refuse
// MORE keys than the list would, never fewer, and no settable field is ever
// going to be named something-password.
static bool consoleArgKeyNamesASecret(const char* key) {
    if (key == nullptr) return false;
    for (const char* p = key; *p != '\0'; ++p) {
        if (consoleStartsWithIgnoringCase(p, "password")) {
            return true;
        }
    }
    return false;
}

// The mode tokens POST /api/wifi's response body has always used, which are
// also the two `values:` docs/action-registry.yaml declares for this row's
// `mode` param - so a mode this read prints is a mode the write side accepts
// straight back (pinned by a round-trip native test, not by this comment).
// Not shared with src/web/api_config.cpp's wifiModeToString(): that one sits
// in an anonymous namespace, has internal linkage, and its file is fenced on
// this ticket, so there is nothing to call.
static const char* consoleWifiModeToken(WifiMode mode) {
    return (mode == WifiMode::STANDALONE_AP) ? "standalone_ap" : "client";
}

// wifiApply() reports a field failure as a human sentence, not a field id -
// that is its contract and POST /api/wifi renders the sentence verbatim, so
// it is not changed here. Every sentence it can produce opens with the API
// key it is about (src/web/api_wifi_apply.cpp: the three param helpers'
// snprintf, the wifiMode parse, and the two mode-vs-SSID rules), so the
// failing field is recovered by matching that opening word back through the
// SAME table this adapter used to feed the core. The alternative - deciding
// which rule fired by re-testing the WiFi rules here - would be a second copy
// of exactly the logic this ticket routes through the core. Returns the
// Console key, or NULL for a message that names no field.
static const char* consoleWifiFailingConsoleKey(const char* message) {
    if (message == nullptr) return nullptr;
    for (size_t i = 0; i < kWifiSettingsFieldCount; ++i) {
        const char* applyKey = kWifiSettingsFields[i].applyKey;
        size_t len = strlen(applyKey);
        if (strncmp(message, applyKey, len) == 0 &&
            (message[len] == ' ' || message[len] == '\0')) {
            return kWifiSettingsFields[i].consoleKey;
        }
    }
    return nullptr;
}

// The read side. Field names are POST /api/wifi's own JSON keys verbatim
// (docs/console-protocol.md s.3.5), and the values come from WifiConfigView -
// the password-safe projection that carries "is a password set" flags instead
// of the passwords themselves (include/config_store.h) - so this read cannot
// echo a secret even by accident.
static void consoleEmitWifiSettings(uint32_t requestId, const ConsoleCatalogEntry* entry,
                                    const ConsoleRecordSink* sink) {
    WifiConfig saved = {};
    configCacheReadWifi(&saved);
    WifiConfig active = {};
    configCacheReadActiveWifi(&active);
    WifiConfigView view = wifiConfigToView(saved);

    if (sink->onRecordBegin) {
        sink->onRecordBegin(requestId, entry->name);
    }
    if (sink->onRecordField) {
        // An SSID is the first field value in this module that can legally
        // contain a space, an '=' or a quote, and docs/console-protocol.md
        // s.3.5 asks for exactly those to come back double-quoted with the
        // input escaping - which is what makes a read pasteable straight back
        // into a write. Neither adapter's record emitter quotes anything
        // today (a pre-existing gap noted at consoleEmitHelpForOperation()
        // above, and both adapters are fenced on this ticket), so the quoting
        // happens here, at the one site that knows the value is human text,
        // using the module's existing consoleQuoteValue(). Buffer: 32 SSID
        // bytes worst-case doubled by escaping, plus both quotes and a NUL.
        char ssidQuoteBuf[WIFI_SSID_MAX_LEN * 2 + 3] = {};

        sink->onRecordField(requestId, "provisioned", view.provisioned ? "true" : "false");
        sink->onRecordField(requestId, "mode", consoleWifiModeToken(view.mode));
        sink->onRecordField(requestId, "staSsid",
                            consoleQuoteValue(view.sta_ssid, ssidQuoteBuf, sizeof(ssidQuoteBuf)));
        sink->onRecordField(requestId, "staPasswordSet",
                            view.sta_password_set ? "true" : "false");
        sink->onRecordField(requestId, "apSsid",
                            consoleQuoteValue(view.ap_ssid, ssidQuoteBuf, sizeof(ssidQuoteBuf)));
        sink->onRecordField(requestId, "apPasswordSet", view.ap_password_set ? "true" : "false");
        // Staged Network Switch (ADR 0015): "saved" is what the next boot
        // will use, "active" is what this boot actually came up on, and the
        // difference is the restart the operator still owes - the same
        // comparison POST /api/wifi's own response reports.
        sink->onRecordField(requestId, "pendingApply",
                            wifiConfigsDiffer(saved, active) ? "true" : "false");
        sink->onRecordField(requestId, "networkRecovery",
                            configCacheReadActiveWifiRecovery() ? "true" : "false");
    }
    if (sink->onRecordEnd) {
        sink->onRecordEnd(requestId, CONSOLE_STATUS_OK, CONSOLE_OUTCOME_COMPLETED,
                          CONSOLE_REASON_NONE);
    }
}

static void consoleExecuteWifiSettings(uint32_t requestId, const ConsoleCatalogEntry* entry,
                                       char* rawArgs, const ConsoleRecordSink* sink) {
    const bool isWrite = (rawArgs != nullptr && rawArgs[0] != '\0');
    if (!isWrite) {
        consoleEmitWifiSettings(requestId, entry, sink);
        return;
    }

    ConsoleArgs parsedArgs = {};
    ConsoleArgParseStatus parseStatus = consoleParseArgs(rawArgs, &parsedArgs);
    if (parseStatus != CONSOLE_ARGS_PARSE_OK) {
        consoleEmitArgParseError(requestId, parseStatus, sink);
        return;
    }
    if (parsedArgs.count == 0) {
        // rawArgs held non-whitespace but tokenized to nothing - malformed,
        // not a silent read (same rule as consoleWriteScalarConfigField()).
        consoleEmitArgParseError(requestId, CONSOLE_ARGS_PARSE_MALFORMED, sink);
        return;
    }

    // Secrets are refused FIRST - before the schema check, before the UTF-8
    // check, and before wifiApply() is called at all. The ordering is the
    // point: a password argument must never reach the Apply Core, and it must
    // never be the thing some later check quotes back in an error. Only the
    // KEY is named in the answer; the value is never read, copied or emitted
    // by any path from here.
    for (size_t i = 0; i < parsedArgs.count; ++i) {
        if (consoleArgKeyNamesASecret(parsedArgs.items[i].key)) {
            consoleEmitArgFailure(requestId, entry->name, parsedArgs.items[i].key,
                                  CONSOLE_REASON_SECRET_NOT_SETTABLE, sink);
            return;
        }
    }

    // Unknown keys and the mode enum come from the registry's own params:
    // table via the shared schema validator (include/console_args.h) - the
    // same one every registry action is checked against, not a hand-written
    // per-field check like the single-field scalar path needs.
    char badKey[40] = {};
    ConsoleArgSchemaStatus schema =
        consoleValidateArgsAgainstSchema(entry->params, parsedArgs, badKey, sizeof(badKey));
    if (schema != CONSOLE_ARG_SCHEMA_OK) {
        ConsoleReason reason = CONSOLE_REASON_OUT_OF_RANGE;
        if (schema == CONSOLE_ARG_SCHEMA_UNKNOWN_KEY) {
            reason = CONSOLE_REASON_UNKNOWN_ARGUMENT;
        } else if (schema == CONSOLE_ARG_SCHEMA_MISSING_REQUIRED) {
            reason = CONSOLE_REASON_MISSING_ARGUMENT;
        }
        consoleEmitArgFailure(requestId, entry->name, badKey, reason, sink);
        return;
    }

    // UTF-8 validity is a Console-protocol rule with no Apply Core
    // counterpart (POST /api/wifi carries bytes the browser already settled),
    // so it belongs here. consoleParseArgs() already validates a QUOTED
    // value's byte structure, but an SSID typed without quotes never passes
    // through that check at all, and docs/console-protocol.md s.1.3 wants the
    // rejection explicit either way. consoleUtf8Valid() is the same validator
    // the quoted path uses - reused, not rewritten.
    for (size_t i = 0; i < kWifiSsidConsoleKeyCount; ++i) {
        const char* value = consoleArgsFind(parsedArgs, kWifiSsidConsoleKeys[i]);
        if (value != nullptr && !consoleUtf8Valid(value)) {
            consoleEmitArgFailure(requestId, entry->name, kWifiSsidConsoleKeys[i],
                                  CONSOLE_REASON_MALFORMED_ARGUMENT, sink);
            return;
        }
    }

    WifiSettingsArgs adapter{&parsedArgs};
    ConfigParamSource params;
    params.ctx = &adapter;
    params.get = consoleWifiSettingsParamGet;

    WifiConfig working = {};
    WifiApplyResult applyResult;
    WifiCommitOutcome commit = {};
    {
        // Serialized against the other Console adapter and against every
        // scalar config write, for the reason s_configWriteMutex's own
        // declaration gives above: wifiCommitApplied() read-modify-writes the
        // shared config-cache snapshot and then writes NVS, so an interleaved
        // config write on the other adapter would lose one of the two
        // updates. The read of the current settings is inside the guard too -
        // reading them outside it would reopen the same window one statement
        // earlier.
        ConfigWriteMutexGuard guard(s_configWriteMutex);
        if (!guard.acquired()) {
            if (sink->onRecordResult) {
                sink->onRecordResult(requestId, CONSOLE_STATUS_ERR, CONSOLE_OUTCOME_UNAVAILABLE,
                                     CONSOLE_REASON_TEMPORARILY_UNAVAILABLE);
            }
            return;
        }

        configCacheReadWifi(&working);
        wifiApply(params, &working, &applyResult);
        if (applyResult.ok) {
            commit = wifiCommitApplied(&working);
        }
    }  // guard released here

    if (!applyResult.ok) {
        const char* failing = consoleWifiFailingConsoleKey(applyResult.errorMessage);
        // A field the operator did not type is missing; a field they did type
        // failed on its value. That split is what makes `mode=client` with no
        // SSID anywhere read as missing-argument sta-ssid - the grouped rule
        // this operation exists for - while an over-long or empty SSID they
        // actually supplied reads as out-of-range on the same field.
        ConsoleReason reason =
            (failing != nullptr && consoleArgsFind(parsedArgs, failing) == nullptr)
                ? CONSOLE_REASON_MISSING_ARGUMENT
                : CONSOLE_REASON_OUT_OF_RANGE;
        consoleEmitArgFailure(requestId, entry->name, failing, reason, sink);
        return;
    }

    if (!commit.persisted) {
        // A failed NVS write is an explicit error, the same shape the scalar
        // config writes above use: no dedicated persistence-failure token
        // exists in the hand-maintained ConsoleReason set
        // (include/console_module.h) and none is added for this one site.
        if (sink->onRecordResult) {
            sink->onRecordResult(requestId, CONSOLE_STATUS_ERR, CONSOLE_OUTCOME_INTERNAL_ERROR,
                                 CONSOLE_REASON_NONE);
        }
        return;
    }

    // ADR 0015: settings are saved, never hot-applied, so the honest answer
    // is whichever of the module's two existing write outcomes matches what
    // the Commit Step just computed - staged-until-reboot while the saved
    // settings differ from the posture actually in force, applied once they
    // agree and there is no restart left to owe. No third outcome is invented
    // for this operation.
    if (sink->onRecordResult) {
        sink->onRecordResult(requestId, CONSOLE_STATUS_OK,
                             commit.pendingApply ? CONSOLE_OUTCOME_STAGED_UNTIL_REBOOT
                                                 : CONSOLE_OUTCOME_APPLIED,
                             CONSOLE_REASON_NONE);
    }
}

// =============================================================================
// Private: direct action executors - Commanded Modes (#226 criterion 4) and
// drive motion (#222). One dispatch mechanism, many callers: a plain
// synchronous C function taking validated Console arguments, checked before
// ACTION_REGISTRY[]/RobotActionId resolution in the CONSOLE_OP_ACTION case
// below - never the queued RC dispatch core the OTHER action executors use.
//
// #226's originating set: "Commanded Modes ... go only through
// commanded_modes setters" (criterion 4) - stationary (system.action.set-
// mode), sleep/wake, non-RC control (enable/disable-web-control) and
// rc-debug all call their commanded_modes.h setter directly, reproducing
// exactly the side-effect sequence their REST handler already runs
// (src/web/api_drive.cpp, src/web/api_system.cpp, src/web/api_rc.cpp) rather
// than a second implementation of it - those three files are outside #226's
// edit list, so this reads their logic and calls the same shared functions,
// it does not import a Commit Step from them.
//
// #222 extends the SAME mechanism (not a second pattern - the pinned
// coordinator comment names this reuse explicitly) with drive.action.move
// and the three drive.action.speed-preset-* entries: driveArbiterSubmit()
// and applySpeedPresetPersisted() are the "shared setter" equivalent for
// motion, reproducing src/web/api_drive.cpp's handleDrivePost() /
// handleSpeedPresetPost() consent and clamp logic verbatim, the same
// "read their logic, call their shared functions" rule #226 already
// established for this section.
//
// An operation resolved here is never also looked up in ACTION_REGISTRY[]
// even when it carries a cpp_enum/rc_token for RC-binding purposes
// (system.action.set-mode does, for a momentary RC switch; so does
// drive.action.speed-preset-cycle, whose separate RC-trigger path through
// dispatchRcTriggerActionTest() is unrelated to and unmodified by this
// section) - that binding is unrelated to what the Console runs for the
// same canonical name.
//
// #257: the table these rows lived in (g_directActionExecutors[]) and every
// row's executor body used to live here, in one file five subsequent action-
// wiring tickets (#221 remainder, the sound/dome/servo/drive/system/aux-rc
// sweeps) would all have had to extend in series. Split into one header per
// domain instead - include/console_direct_action_{system,drive,sound,
// aux_rc,servo}.h, each holding that domain's executor bodies and its own
// slice of the table - so those tickets can dispatch file-disjoint in
// parallel. Each header is HEADER-ONLY, #include'd from here and nowhere
// else, and explains why in its own top comment (native's build_src_filter
// is a fenced, explicit allowlist with no room for a new src/console/*.cpp).
// This file keeps the typedef/struct shape (include/
// console_direct_action_types.h), the small cross-cutting helpers every
// domain's bodies call (consoleEmitArgFailure(), consoleCommandSourceFor()
// below, consoleMapDispatchOutcome(), consoleAnswerActionGuardResult(),
// consoleMoodIdValid() - all still defined above, unmoved), and the
// resolution step below that a domain header's own table cannot provide on
// its own: consoleFindDirectActionExecutor() tries each domain's table in
// turn, so no row gains, loses or changes an executor, an outcome or a
// reason versus the single combined table this replaces.
// =============================================================================

#include "console_direct_action_types.h"

static CommandSource consoleCommandSourceFor(ConsoleCommandSource source) {
    return (source == CONSOLE_SOURCE_SERIAL) ? SRC_SERIAL_CONSOLE : SRC_WEB_CONSOLE;
}

#include "console_direct_action_system.h"
#include "console_direct_action_drive.h"
#include "console_direct_action_sound.h"
#include "console_direct_action_aux_rc.h"
#include "console_direct_action_servo.h"
#include "console_direct_action_dome.h"

// Linear scan of one domain's table - the single scan implementation every
// domain lookup below shares, factored out rather than repeated five times.
static ConsoleDirectActionExecutorFn consoleFindInDirectActionTable(
    const ConsoleDirectActionExecutorEntry* table, size_t count, const char* canonicalName) {
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(table[i].operationName, canonicalName) == 0) {
            return table[i].executor;
        }
    }
    return nullptr;
}

static ConsoleDirectActionExecutorFn consoleFindDirectActionExecutor(const char* canonicalName) {
    ConsoleDirectActionExecutorFn found = consoleFindInDirectActionTable(
        g_systemDirectActionExecutors, kSystemDirectActionExecutorCount, canonicalName);
    if (found != nullptr) return found;
    found = consoleFindInDirectActionTable(g_driveDirectActionExecutors, kDriveDirectActionExecutorCount,
                                           canonicalName);
    if (found != nullptr) return found;
    found = consoleFindInDirectActionTable(g_soundDirectActionExecutors, kSoundDirectActionExecutorCount,
                                           canonicalName);
    if (found != nullptr) return found;
    found = consoleFindInDirectActionTable(g_auxRcDirectActionExecutors, kAuxRcDirectActionExecutorCount,
                                           canonicalName);
    if (found != nullptr) return found;
    found = consoleFindInDirectActionTable(g_servoDirectActionExecutors, kServoDirectActionExecutorCount,
                                           canonicalName);
    if (found != nullptr) return found;
    return consoleFindInDirectActionTable(g_domeDirectActionExecutors, kDomeDirectActionExecutorCount,
                                          canonicalName);
}

// =============================================================================
// Public API Implementation
// =============================================================================

void consoleModuleInit(void) {
    // Console module initialization (before LittleFS and web server).
    // The help reader will be set separately via consoleModuleSetHelpReader()
    // after LittleFS is ready (see ADR 0034).

    // Created once, before either adapter's task/server exists (setup()'s
    // call order, src/main.cpp) - matches src/main.cpp's own paLogInit()
    // guard for logSerialMutex, and src/seq_store.cpp's seqStoreInit() guard
    // for its own module mutex.
    if (s_configWriteMutex == nullptr) {
        s_configWriteMutex = xSemaphoreCreateMutexStatic(&s_configWriteMutexStorage);
    }

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

    // Check if the operation was compiled into this image.
    //
    // Without this guard a build-gated operation fell past the board check -
    // the only availability check there was - and answered with whatever the
    // executor lookup below failed with: executor-not-ready, which means
    // "nobody has wired this yet" and
    // so implies it could start working once someone does. That is the wrong
    // answer twice over: the feature is absent by build configuration, and
    // the `operations` listing a line earlier already said `not-in-this-build`
    // for the same row. Two surfaces, two different answers to one question
    // (routed here from #219).
    //
    // Checked AFTER the board check and in the same order the listing above
    // renders its reason, so a row that is both off-board and out-of-build
    // gets one reason from discovery and execution alike.
    if (!consoleIsAvailableInBuild(opName)) {
        if (sink->onRecordResult) {
            sink->onRecordResult(request->requestId, CONSOLE_STATUS_ERR,
                                CONSOLE_OUTCOME_UNAVAILABLE, CONSOLE_REASON_NOT_IN_THIS_BUILD);
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
            // Resolved once here (by canonical name or alias) and reused by
            // both the Commanded Mode direct-executor check and the
            // RobotActionId fallback below - consoleIsKnownOperation()
            // already guaranteed this resolves (checked earlier in this
            // function), so entry is never null past this point.
            const ConsoleCatalogEntry* entry = consoleFindByNameOrAlias(opName);

            // Commanded Modes first (#226 criterion 4): an operation resolved
            // by consoleFindDirectActionExecutor() (one of the per-domain
            // direct-action tables, #257) is dispatched through its
            // commanded_modes.h setter directly and never reaches
            // ACTION_REGISTRY[]/the queued RC dispatch below, even for
            // system.action.set-mode, which does carry a cpp_enum/rc_token
            // for an unrelated RC-binding purpose - looked up by canonical
            // name so an alias (e.g. its rc_token) resolves the same way.
            ConsoleDirectActionExecutorFn directExecutor =
                (entry != nullptr) ? consoleFindDirectActionExecutor(entry->name) : nullptr;
            if (directExecutor != nullptr) {
                ConsoleArgs parsedArgs = {};
                ConsoleArgParseStatus parseStatus = consoleParseArgs(rawArgs, &parsedArgs);
                if (parseStatus != CONSOLE_ARGS_PARSE_OK) {
                    consoleEmitArgParseError(request->requestId, parseStatus, sink);
                    break;
                }
                directExecutor(request->requestId, entry->name, parsedArgs, request->source, sink);
                break;
            }

            // Resolve the (possibly aliased) operation name to its
            // RobotActionId via ACTION_REGISTRY[] (#220). Not found here
            // means this action has no RC-bindable target yet - a
            // not-yet-wired action #227 owns, or a motion target #222
            // owns - unchanged from before this ticket.
            //
            // Two of the rows that land here on purpose, not by omission:
            // dome.api.get-sequence (seqStoreReadFileSlice()) and
            // dome.api.get-layout (domeLayoutCacheReadChunk()) are byte-slice
            // readers over one stored document (a Learned Sequence JSON v1
            // file; the dome's cached composed-layout JSON) - #206's own
            // "document/bulk transfer ... keeps its dedicated mechanisms"
            // exclusion, the same one dome.action.save-sequence's write side
            // cites (include/console_direct_action_dome.h). They answer
            // EXECUTOR_NOT_READY here like any other unwired action row;
            // the registry entries carry the same reasoning (docs/action-
            // registry.yaml, #221 remainder).
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

        case CONSOLE_OP_CONFIG: {
            // Resolve the (possibly aliased) operation name to a Component
            // Toggle field, mirroring how CONSOLE_OP_ACTION above resolves a
            // RobotActionId - one dispatch point, add a row rather than edit
            // this switch's control flow (#223's own rule, extended here to
            // config rows: see consoleExecuteComponentToggle()'s header
            // comment for why this row is hand-written rather than a
            // catalog-driven table like g_statusExecutors).
            const ConsoleCatalogEntry* entry = consoleFindByNameOrAlias(opName);
            const ComponentToggleField* toggleField =
                (entry != nullptr) ? consoleFindComponentToggleField(entry->name) : nullptr;
            if (toggleField != nullptr) {
                consoleExecuteComponentToggle(request->requestId, entry, toggleField,
                                              request->source, rawArgs, sink);
                break;
            }

            // system.config.mood: the config-typed view of active mood, one
            // of criterion 4's five Commanded Modes (real executor is
            // applyMood(), not the registry's stated configApply - see the
            // include comment at the top of this file).
            if (entry != nullptr && strcmp(entry->name, "system.config.mood") == 0) {
                consoleExecuteMoodConfig(request->requestId, entry, rawArgs, sink);
                break;
            }

            // wifi.config.settings: the one GROUPED config write (#227) -
            // several fields validated as one configuration through the POST
            // /api/wifi Apply Core, which is why it cannot be a row in the
            // single-field scalar table below.
            if (entry != nullptr && strcmp(entry->name, "wifi.config.settings") == 0) {
                consoleExecuteWifiSettings(request->requestId, entry, rawArgs, sink);
                break;
            }

            ConsoleScalarConfigExecutorFn scalarExecutor =
                (entry != nullptr) ? consoleFindScalarConfigExecutor(entry->name) : nullptr;
            if (scalarExecutor != nullptr) {
                scalarExecutor(request->requestId, entry, rawArgs, request->source, sink);
                break;
            }

            // Every other type=config row not yet added as a row above (the
            // remaining scalar entries with no real Apply Core to reuse -
            // sound.config.volume, sound.config.mood-interval-*, see this
            // section's own header comment - or the other grouped writes,
            // audio and rc-map, whose own Apply Cores no ticket has pointed
            // this dispatch at yet) is not wired.
            if (sink->onRecordResult) {
                sink->onRecordResult(request->requestId, CONSOLE_STATUS_ERR,
                                    CONSOLE_OUTCOME_UNAVAILABLE, CONSOLE_REASON_EXECUTOR_NOT_READY);
            }
            break;
        }

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
