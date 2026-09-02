// =============================================================================
// include/console_direct_action_dome.h
//
// Controller Console direct-action executors - dome domain (#259): the
// dome.action.* rows the firmware already implements but that neither
// ACTION_REGISTRY[] fallback path (consoleExecuteCommand()'s CONSOLE_OP_ACTION
// case, src/console/console_module.cpp) nor #221's Marcduino-payload carve-out
// (consoleExecuteAction()) can reach. #257 split the executor table into five
// per-domain headers; dome was not among them because no dome.action.* row
// was wired at that point - this file and its cascade entry are what closes
// that gap.
//
// Six of dome's 21 dome.action.* rows are wired below. The rest already
// dispatch through the existing ACTION_REGISTRY[] fallback with no direct
// executor needed (verified with a temporary diagnostic sweep before writing
// this file, not assumed):
//   - dome.action.set-speed: analog (robotActionIsAnalog(DOME_ACTION_SPEED)),
//     so consoleExecuteCommand()'s guard already answers
//     CONSOLE_REASON_NOT_EXECUTABLE - the same "RC-only axis, no direct
//     executor" precedent drive.action.speed/steer set (include/
//     console_direct_action_drive.h never wires them either).
//   - dome.action.marcduino-sequence / dome.action.marcduino-command: the two
//     payload-needing targets consoleExecuteAction() already validates and
//     dispatches inline (see that function's own header comment,
//     src/console/console_module.cpp) - #221 landed these, not this ticket.
//   - the 11 dome.action.droid-sequence-* rows (SE01-SE16, minus SE10-14
//     which are moods, not droid sequences): none of them is analog or
//     payload-needing (robotActionNeedsPayload() only names the two Marcduino
//     targets and dome.action.dome-sequence), so evaluateActionTestGuard()
//     already allows them through dispatchRcTriggerActionTest() - the same
//     guard+dispatch core POST /api/actions/test uses.
//
// One row, dome.action.save-sequence, stays CONSOLE_REASON_EXECUTOR_NOT_READY
// on purpose: its REST body (POST /api/seq, a full Learned Sequence JSON v1
// document up to SEQ_FILE_MAX_BYTES with a steps array) is exactly the
// "document/bulk transfer" #206 names out of scope for this epic - the
// Console's one-line key=value argument grammar (docs/console-protocol.md
// s.1.2) has no shape for an arbitrarily large JSON body, and inventing one
// is a new Console Record/argument shape the coordinator pin requires asking
// about first, not building. Reported on the ticket, not silently left
// unexplained.
//
// dome.action.sequence-stop's dangling-Learned-Sequence-binding report (the
// analogous field on dome.action.delete-sequence's REST sibling,
// handleSeqDelete(), src/web/api_seq.cpp) is likewise not reproduced here:
// docs/console-protocol.md s.3.1 defines `result` as "the whole answer to an
// action ... in one record" and reserves `begin`/`item`/`end` for queries -
// turning an action into a multi-item response would be inventing a new use
// of an existing shape the protocol document does not sanction, not reusing
// one. dome.action.delete-sequence answers ok/applied or err/invalid only;
// an operator who needs the dangling-binding detail still has the REST route.
//
// HEADER-ONLY DELIBERATELY - see include/console_direct_action_system.h's
// header comment for the full reasoning (native's fenced build_src_filter
// allowlist has no room for a new src/console/*.cpp here) and for why
// `static`, not `inline`, is the right linkage given this header has exactly
// one includer.
//
// NOT a standalone compilation unit: #include'd from src/console/
// console_module.cpp only, at the point these executors would have lived had
// #257 found any dome.action.* row wired, so their bodies can see that
// file's own `static` consoleEmitArgFailure() and consoleCommandSourceFor()
// by ordinary same-translation-unit visibility. Not included, and must not
// be included, from anywhere else.
// =============================================================================
#pragma once

#include <Arduino.h>  // millis()
#include <string.h>

#include "console_direct_action_types.h"  // ConsoleDirectActionExecutorFn/Entry
#include "console_module.h"               // ConsoleCommandSource, ConsoleRecordSink
#include "console_args.h"                 // ConsoleArgs, consoleArgsFind(), schema validation
#include "console_catalog.h"              // ConsoleCatalogEntry, consoleCatalogFindByName()
#include "robot_state.h"                  // robotState, robotStateMux, DomeCommand, domeCmdQueue
#include "config_cache.h"                 // ConfigSnapshot, configCacheRead()
#include "dome_link.h"                    // domeQueueTx(), DomeTxCmd sizing (dome_link.h)
#include "sequence_dispatcher.h"          // sequenceStart()
#include "api_drive.h"                    // executeManualCommand()
#include "seq_store.h"                    // seqStoreDelete()
#include "seq_store_index.h"              // seqStoreIndexFind()

// dome.action.send-command: command=<marcduino line or keyword>, the same
// single form field POST /api/manual-command reads (handleManualCommandPost(),
// src/web/api_system.cpp) before handing it to executeManualCommand()
// (src/web/api_drive.cpp) - the SAME dispatch core, reused verbatim rather
// than reimplemented, so every prefix branch it owns ($/audio, :#/body,
// */@/%/&!/dome-forward, and the keyword commands) stays in that one place.
// The rate limit handleManualCommandPost() applies (10/s) is an HTTP-abuse
// guard, not one of this ticket's five safety guards (estop, stationary/
// sleep, component availability, queue limits, validation) and no other
// direct executor in this module reproduces a REST endpoint's rate limit
// either - not reproduced here. The sleep-mode prefix block IS one of the
// five (blocked-by-state) and is reproduced verbatim below.
static void consoleExecuteDomeSendCommand(uint32_t requestId, const char* operationName,
                                          const ConsoleArgs& args, ConsoleCommandSource source,
                                          const ConsoleRecordSink* sink) {
    (void)source;
    const char* badKey = nullptr;
    for (size_t i = 0; i < args.count; ++i) {
        if (strcmp(args.items[i].key, "command") != 0) {
            badKey = args.items[i].key;
            break;
        }
    }
    if (badKey != nullptr) {
        consoleEmitArgFailure(requestId, operationName, badKey, CONSOLE_REASON_UNKNOWN_ARGUMENT, sink);
        return;
    }
    const char* command = consoleArgsFind(args, "command");
    if (command == nullptr || command[0] == '\0') {
        consoleEmitArgFailure(requestId, operationName, "command", CONSOLE_REASON_MISSING_ARGUMENT,
                              sink);
        return;
    }

    // Sleep-mode prefix block, read verbatim from handleManualCommandPost():
    // only the prefixes that reach AudioTask/body Marcduino/dome-forward/mood
    // are held; the keyword commands (estop, reboot, ...) are not.
    taskENTER_CRITICAL(&robotStateMux);
    bool sleepMode = robotState.sleepMode;
    taskEXIT_CRITICAL(&robotStateMux);
    if (sleepMode) {
        const char prefix = command[0];
        bool blockedBySleep = prefix == '$' || prefix == ':' || prefix == '#' ||
                              prefix == '*' || prefix == '@' || prefix == '%' ||
                              prefix == '&' || prefix == '!';
        if (blockedBySleep) {
            if (sink->onRecordResult) {
                sink->onRecordResult(requestId, CONSOLE_STATUS_ERR, CONSOLE_OUTCOME_BLOCKED,
                                    CONSOLE_REASON_BLOCKED_BY_STATE);
            }
            return;
        }
    }

    if (!executeManualCommand(command)) {
        // Matches handleManualCommandPost()'s own single failure shape
        // (400 "unsupported command") - executeManualCommand() returns one
        // bool for every branch it owns, so an audio-queue-full $ command
        // and a genuinely unrecognized keyword answer the same way on both
        // the REST route and here; that conflation is pre-existing in the
        // reused core, not introduced by this executor.
        consoleEmitArgFailure(requestId, operationName, "command", CONSOLE_REASON_OUT_OF_RANGE, sink);
        return;
    }

    if (sink->onRecordResult) {
        sink->onRecordResult(requestId, CONSOLE_STATUS_OK, CONSOLE_OUTCOME_APPLIED,
                            CONSOLE_REASON_NONE);
    }
}

// dome.action.dome-sequence: value=DM:<NAME>, the same DM: branch
// handleDomeCmdPost() (POST /api/dome/cmd, src/web/api_drive.cpp) forwards to
// sequenceStart() (include/sequence_dispatcher.h) - reused verbatim. This row
// carries no registry params (docs/action-registry.yaml), so the schema is
// hand-checked the same way the two Marcduino targets are in
// consoleExecuteAction() (src/console/console_module.cpp): a single "value"
// key, DM:-prefixed, no length widening. Bounded to DomeTxCmd::buf's own 64
// bytes (include/dome_link.h) rather than handleDomeCmdPost()'s looser
// 127-char REST guard: a value between those two bounds would pass the REST
// endpoint but be silently truncated by domeQueueTx() on the SEQ_FALLBACK/
// SEQ_ALIAS path (src/tasks/dome_link.cpp) - explicit out-of-range here
// instead of a silently truncated forward. handleDomeCmdPost() itself applies
// no estop/sleep gate before this call, and neither does this executor - "no
// widening" cuts both ways, so a guard REST does not have is not added here
// either.
static constexpr size_t kDomeTxCmdMax = 64;  // DomeTxCmd::buf[64], include/dome_link.h

static void consoleExecuteDomeSequence(uint32_t requestId, const char* operationName,
                                       const ConsoleArgs& args, ConsoleCommandSource source,
                                       const ConsoleRecordSink* sink) {
    const char* badKey = nullptr;
    for (size_t i = 0; i < args.count; ++i) {
        if (strcmp(args.items[i].key, "value") != 0) {
            badKey = args.items[i].key;
            break;
        }
    }
    if (badKey != nullptr) {
        consoleEmitArgFailure(requestId, operationName, badKey, CONSOLE_REASON_UNKNOWN_ARGUMENT, sink);
        return;
    }
    const char* value = consoleArgsFind(args, "value");
    if (value == nullptr || value[0] == '\0') {
        consoleEmitArgFailure(requestId, operationName, "value", CONSOLE_REASON_MISSING_ARGUMENT, sink);
        return;
    }
    if (strlen(value) >= kDomeTxCmdMax || strncmp(value, "DM:", 3) != 0) {
        consoleEmitArgFailure(requestId, operationName, "value", CONSOLE_REASON_OUT_OF_RANGE, sink);
        return;
    }

    if (!sequenceStart(value, consoleCommandSourceFor(source))) {
        if (sink->onRecordResult) {
            sink->onRecordResult(requestId, CONSOLE_STATUS_ERR, CONSOLE_OUTCOME_QUEUE_FULL,
                                CONSOLE_REASON_QUEUE_FULL);
        }
        return;
    }
    if (sink->onRecordResult) {
        sink->onRecordResult(requestId, CONSOLE_STATUS_OK, CONSOLE_OUTCOME_QUEUED, CONSOLE_REASON_NONE);
    }
}

// dome.action.sequence-stop: no arguments, matching POST /api/seq/stop
// (handleSeqStopPost(), src/web/api_seq.cpp) - a non-latching transient flag
// set unconditionally, no estop/sleep/component gate in the REST source, so
// none is added here either.
static void consoleExecuteDomeSequenceStop(uint32_t requestId, const char* operationName,
                                           const ConsoleArgs& args, ConsoleCommandSource source,
                                           const ConsoleRecordSink* sink) {
    (void)source;
    if (!consoleRejectAnyArgument(requestId, operationName, args, sink)) {
        return;
    }
    taskENTER_CRITICAL(&robotStateMux);
    robotState.seqStopRequested = true;
    taskEXIT_CRITICAL(&robotStateMux);

    if (sink->onRecordResult) {
        sink->onRecordResult(requestId, CONSOLE_STATUS_OK, CONSOLE_OUTCOME_APPLIED,
                            CONSOLE_REASON_NONE);
    }
}

// dome.action.move: speed=<-1.0..1.0>, the same single argument POST /api/dome
// (handleDomeSpeedPost(), src/web/api_drive.cpp) reads. Consent is reproduced
// verbatim from that handler: sleeping (isSleepModeActive(), private to that
// file - read directly from RobotState here instead, the drive.action.move
// precedent for a REST-private helper, include/console_direct_action_drive.h)
// blocks it, then the enable_dome_esc Component Toggle gate
// (system.config.enable_dome_esc, the same field aux.action.led-color's own
// CONSOLE_REASON_COMPONENT_DISABLED precedent answers for, include/
// console_direct_action_aux_rc.h), then the queue send. Schema validation
// runs FIRST here rather than between the two REST gates (handleDomeSpeedPost()
// checks sleep before re-validating the parsed value) - every other direct
// executor in this module validates its schema before any state guard, and
// following that one established order here keeps this file's guard shape
// consistent with the rest of the module rather than reproducing one REST
// handler's specific two-step check order; the only case this changes is
// what a SIMULTANEOUSLY-sleeping-and-malformed request answers, and that
// combination has no operator-visible consequence beyond which single
// `reason=` comes back.
static void consoleExecuteDomeMove(uint32_t requestId, const char* operationName,
                                   const ConsoleArgs& args, ConsoleCommandSource source,
                                   const ConsoleRecordSink* sink) {
    const ConsoleCatalogEntry* entry = consoleCatalogFindByName(operationName);
    char badKey[40] = {};
    ConsoleArgSchemaStatus schemaStatus = consoleValidateArgsAgainstSchema(
        entry != nullptr ? entry->params : nullptr, args, badKey, sizeof(badKey));
    if (schemaStatus != CONSOLE_ARG_SCHEMA_OK) {
        ConsoleReason reason = (schemaStatus == CONSOLE_ARG_SCHEMA_UNKNOWN_KEY)
                                   ? CONSOLE_REASON_UNKNOWN_ARGUMENT
                               : (schemaStatus == CONSOLE_ARG_SCHEMA_MISSING_REQUIRED)
                                   ? CONSOLE_REASON_MISSING_ARGUMENT
                                   : CONSOLE_REASON_OUT_OF_RANGE;
        consoleEmitArgFailure(requestId, operationName, badKey, reason, sink);
        return;
    }

    taskENTER_CRITICAL(&robotStateMux);
    bool sleeping = robotState.sleepMode;
    taskEXIT_CRITICAL(&robotStateMux);
    if (sleeping) {
        if (sink->onRecordResult) {
            sink->onRecordResult(requestId, CONSOLE_STATUS_ERR, CONSOLE_OUTCOME_BLOCKED,
                                CONSOLE_REASON_BLOCKED_BY_STATE);
        }
        return;
    }

    ConfigSnapshot cfg = {};
    configCacheRead(&cfg);
    if (!cfg.system.enable_dome_esc) {
        if (sink->onRecordResult) {
            sink->onRecordResult(requestId, CONSOLE_STATUS_ERR, CONSOLE_OUTCOME_UNAVAILABLE,
                                CONSOLE_REASON_COMPONENT_DISABLED);
        }
        return;
    }

    // Schema already confirmed "speed" parses as float in -1.0..1.0 - reparse
    // with the SAME parser the schema check itself used
    // (consoleParamParseNumeric(), include/console_args.h) rather than a
    // second one, the "reparse after schema" precedent drive.action.move and
    // servo's executors both set.
    double parsed = 0.0;
    consoleParamParseNumeric(CONSOLE_PARAM_TYPE_FLOAT, consoleArgsFind(args, "speed"), &parsed);

    DomeCommand cmd = {};
    cmd.speed = constrain((float)parsed, -1.0f, 1.0f);
    cmd.source = consoleCommandSourceFor(source);
    cmd.timestampMs = millis();
    if (xQueueSend(domeCmdQueue, &cmd, 0) != pdTRUE) {
        if (sink->onRecordResult) {
            sink->onRecordResult(requestId, CONSOLE_STATUS_ERR, CONSOLE_OUTCOME_QUEUE_FULL,
                                CONSOLE_REASON_QUEUE_FULL);
        }
        return;
    }
    if (sink->onRecordResult) {
        sink->onRecordResult(requestId, CONSOLE_STATUS_OK, CONSOLE_OUTCOME_QUEUED, CONSOLE_REASON_NONE);
    }
}

// dome.action.delete-sequence: name=<string> - the same lookup-then-delete
// handleSeqDelete() (DELETE /api/seq?name=, src/web/api_seq.cpp) performs,
// reusing seqStoreIndexFind()/seqStoreDelete() (include/seq_store_index.h,
// include/seq_store.h) verbatim. The dangling-RC-binding report that REST
// route also builds is not reproduced - see this file's header comment for
// why (docs/console-protocol.md s.3.1 reserves multi-record answers for
// queries, and an action's whole answer is one `result` record).
static void consoleExecuteDomeDeleteSequence(uint32_t requestId, const char* operationName,
                                             const ConsoleArgs& args, ConsoleCommandSource source,
                                             const ConsoleRecordSink* sink) {
    (void)source;
    const ConsoleCatalogEntry* entry = consoleCatalogFindByName(operationName);
    char badKey[40] = {};
    ConsoleArgSchemaStatus schemaStatus = consoleValidateArgsAgainstSchema(
        entry != nullptr ? entry->params : nullptr, args, badKey, sizeof(badKey));
    if (schemaStatus != CONSOLE_ARG_SCHEMA_OK) {
        ConsoleReason reason = (schemaStatus == CONSOLE_ARG_SCHEMA_UNKNOWN_KEY)
                                   ? CONSOLE_REASON_UNKNOWN_ARGUMENT
                               : (schemaStatus == CONSOLE_ARG_SCHEMA_MISSING_REQUIRED)
                                   ? CONSOLE_REASON_MISSING_ARGUMENT
                                   : CONSOLE_REASON_OUT_OF_RANGE;
        consoleEmitArgFailure(requestId, operationName, badKey, reason, sink);
        return;
    }

    const char* name = consoleArgsFind(args, "name");
    if (seqStoreIndexFind(name) == nullptr) {
        consoleEmitArgFailure(requestId, operationName, "name", CONSOLE_REASON_OUT_OF_RANGE, sink);
        return;
    }
    if (!seqStoreDelete(name)) {
        if (sink->onRecordResult) {
            sink->onRecordResult(requestId, CONSOLE_STATUS_ERR, CONSOLE_OUTCOME_INTERNAL_ERROR,
                                CONSOLE_REASON_NONE);
        }
        return;
    }
    if (sink->onRecordResult) {
        sink->onRecordResult(requestId, CONSOLE_STATUS_OK, CONSOLE_OUTCOME_APPLIED, CONSOLE_REASON_NONE);
    }
}

// dome.action.test-sequence: name=DM:<NAME> - the same DM:-only validation
// and sequenceStart() call handleSeqTestPost() (POST /api/seq/test, src/web/
// api_seq.cpp) makes for its form-field path, reused verbatim (the JSON-body
// fallback that route also accepts is REST-transport-specific - the Console
// has its own key=value argument, so there is no second encoding to accept).
static void consoleExecuteDomeTestSequence(uint32_t requestId, const char* operationName,
                                           const ConsoleArgs& args, ConsoleCommandSource source,
                                           const ConsoleRecordSink* sink) {
    const ConsoleCatalogEntry* entry = consoleCatalogFindByName(operationName);
    char badKey[40] = {};
    ConsoleArgSchemaStatus schemaStatus = consoleValidateArgsAgainstSchema(
        entry != nullptr ? entry->params : nullptr, args, badKey, sizeof(badKey));
    if (schemaStatus != CONSOLE_ARG_SCHEMA_OK) {
        ConsoleReason reason = (schemaStatus == CONSOLE_ARG_SCHEMA_UNKNOWN_KEY)
                                   ? CONSOLE_REASON_UNKNOWN_ARGUMENT
                               : (schemaStatus == CONSOLE_ARG_SCHEMA_MISSING_REQUIRED)
                                   ? CONSOLE_REASON_MISSING_ARGUMENT
                                   : CONSOLE_REASON_OUT_OF_RANGE;
        consoleEmitArgFailure(requestId, operationName, badKey, reason, sink);
        return;
    }

    const char* name = consoleArgsFind(args, "name");
    if (name == nullptr || strncmp(name, "DM:", 3) != 0) {
        consoleEmitArgFailure(requestId, operationName, "name", CONSOLE_REASON_OUT_OF_RANGE, sink);
        return;
    }

    if (!sequenceStart(name, consoleCommandSourceFor(source))) {
        if (sink->onRecordResult) {
            sink->onRecordResult(requestId, CONSOLE_STATUS_ERR, CONSOLE_OUTCOME_QUEUE_FULL,
                                CONSOLE_REASON_QUEUE_FULL);
        }
        return;
    }
    if (sink->onRecordResult) {
        sink->onRecordResult(requestId, CONSOLE_STATUS_OK, CONSOLE_OUTCOME_QUEUED, CONSOLE_REASON_NONE);
    }
}

static const ConsoleDirectActionExecutorEntry g_domeDirectActionExecutors[] = {
    {"dome.action.send-command", consoleExecuteDomeSendCommand},
    {"dome.action.dome-sequence", consoleExecuteDomeSequence},
    {"dome.action.sequence-stop", consoleExecuteDomeSequenceStop},
    {"dome.action.move", consoleExecuteDomeMove},
    {"dome.action.delete-sequence", consoleExecuteDomeDeleteSequence},
    {"dome.action.test-sequence", consoleExecuteDomeTestSequence},
};
static const size_t kDomeDirectActionExecutorCount =
    sizeof(g_domeDirectActionExecutors) / sizeof(g_domeDirectActionExecutors[0]);
