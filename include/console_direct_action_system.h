// =============================================================================
// include/console_direct_action_system.h
//
// Controller Console direct-action executors - system domain (#226 criterion
// 4's Commanded Modes: stationary, sleep/wake, web-control, active mood; plus
// system.action.set-identity's ADR 0034 Commit Step). Split out of
// src/console/console_module.cpp by #257 so this domain's rows can be
// extended (sound/dome/servo/drive/system/aux-rc sweeps still open on the
// epic) without colliding on that one file.
//
// HEADER-ONLY DELIBERATELY, same reasoning include/console_config_fields.h
// already gives for this exact constraint: [env:native]'s build_src_filter in
// platformio.ini is an explicit allowlist of src/*.cpp translation units and
// is fenced on #257, so a new src/console/*.cpp here would have no way to
// reach the native test binary (undefined reference at link time) even
// though the main artoo_esp32 env has no such filter and would compile it
// fine - the native env is the one that cannot be reached. A plain function
// or data definition in a header risks ODR violations if the header is ever
// included from more than one translation unit; that risk does not apply
// here because this header has exactly one includer, so its executors and
// table below are `static`, matching the linkage they had before the move
// (kComponentToggleFields/consoleFindComponentToggleField use `inline`
// instead because THAT header is genuinely shared by two .cpp files -
// src/console/console_module.cpp and src/config_store.cpp - a fact this
// header does not share).
//
// NOT a standalone compilation unit: this file must be #include'd from
// src/console/console_module.cpp only, at the point the Commanded Mode
// direct-action executors used to live, so that its bodies can see that
// file's own `static` helpers - consoleEmitArgFailure(), consoleCommandSourceFor(),
// consoleMoodIdValid() - by ordinary same-translation-unit visibility. It is
// not included, and must not be included, from anywhere else.
// =============================================================================
#pragma once

#include <stdlib.h>
#include <string.h>

#include <Arduino.h>  // millis() - disable-web-control's zero-frame release below

#include "console_direct_action_types.h"  // ConsoleDirectActionExecutorFn/Entry
#include "console_module.h"               // ConsoleCommandSource, ConsoleRecordSink
#include "console_args.h"                 // ConsoleArgs, consoleArgsFind()
#include "console_catalog.h"              // ConsoleCatalogEntry, consoleCatalogFindByName()
#include "robot_state.h"                  // robotState, robotStateMux, CommandSource, saveConfigToNvs()
#include "commanded_modes.h"              // commandedSetStationary/Sleep/WebControl()
#include "drive_arbiter.h"                // driveArbiterSubmit(), DriveSource
#include "web_server.h"                   // requestStatusBroadcastNow()
#include "mood.h"                         // applyMood()
#include "config_cache.h"                 // ConfigSnapshot, configCacheRead()
#include "api_helpers.h"                  // normalizeDroidName(), parseBoolValue()
#include "api_identity.h"                 // identitySetCommitApplied(), IdentitySetCommitOutcome
#include "config.h"                       // DROID_NAME_MAX_LEN
#include "api_profiler.h"                 // profilerTraceStart()/profilerTraceStop() and
                                          // ProfilerTraceOutcome - the Tier 3 leak-trace cores
                                          // POST /api/profiler/trace/start|stop also call (#224)

// system.action.set-mode: mode=stationary|driving, the same two values
// POST /api/mode accepts (src/web/api_drive.cpp's handleModePost) - commit
// step reproduced here: setter, persist, broadcast, in that order.
static void consoleExecuteDirectSetMode(uint32_t requestId, const char* operationName,
                                        const ConsoleArgs& args, ConsoleCommandSource source,
                                        const ConsoleRecordSink* sink) {
    const char* badKey = nullptr;
    for (size_t i = 0; i < args.count; ++i) {
        if (strcmp(args.items[i].key, "mode") != 0) {
            badKey = args.items[i].key;
            break;
        }
    }
    if (badKey != nullptr) {
        consoleEmitArgFailure(requestId, operationName, badKey, CONSOLE_REASON_UNKNOWN_ARGUMENT, sink);
        return;
    }
    const char* mode = consoleArgsFind(args, "mode");
    if (mode == nullptr) {
        consoleEmitArgFailure(requestId, operationName, "mode", CONSOLE_REASON_MISSING_ARGUMENT, sink);
        return;
    }

    bool stationary;
    if (strcmp(mode, "stationary") == 0) {
        stationary = true;
    } else if (strcmp(mode, "driving") == 0) {
        stationary = false;
    } else {
        consoleEmitArgFailure(requestId, operationName, "mode", CONSOLE_REASON_OUT_OF_RANGE, sink);
        return;
    }

    commandedSetStationary(stationary, consoleCommandSourceFor(source));
    // saveConfigToNvs() persists the whole cache (commandedSetStationary()
    // already synced robotState.stationary into it) - the same call
    // handleModePost makes, its result unchecked there; the Console checks
    // it so a failed write is an explicit error (criterion 3) rather than a
    // silently discarded one.
    const bool persisted = saveConfigToNvs();
    requestStatusBroadcastNow();

    if (sink->onRecordResult) {
        sink->onRecordResult(requestId, persisted ? CONSOLE_STATUS_OK : CONSOLE_STATUS_ERR,
                            persisted ? CONSOLE_OUTCOME_APPLIED : CONSOLE_OUTCOME_INTERNAL_ERROR,
                            CONSOLE_REASON_NONE);
    }
}

// Rejects any argument this operation does not declare - both direct
// executors below (sleep/wake, enable/disable-web-control) take none.
static bool consoleRejectAnyArgument(uint32_t requestId, const char* operationName,
                                    const ConsoleArgs& args, const ConsoleRecordSink* sink) {
    if (args.count == 0) {
        return true;
    }
    consoleEmitArgFailure(requestId, operationName, args.items[0].key,
                          CONSOLE_REASON_UNKNOWN_ARGUMENT, sink);
    return false;
}

// system.action.sleep / system.action.wake: no arguments, matching
// POST /api/sleep / /api/wake (src/web/api_system.cpp). Broadcasts only on
// an actual transition, exactly like the REST handler.
static void consoleExecuteDirectSleepWake(uint32_t requestId, const char* operationName, bool sleep,
                                          const ConsoleArgs& args, ConsoleCommandSource source,
                                          const ConsoleRecordSink* sink) {
    if (!consoleRejectAnyArgument(requestId, operationName, args, sink)) {
        return;
    }
    const bool changed = commandedSetSleep(sleep, consoleCommandSourceFor(source));
    if (changed) {
        requestStatusBroadcastNow();
    }
    if (sink->onRecordResult) {
        sink->onRecordResult(requestId, CONSOLE_STATUS_OK, CONSOLE_OUTCOME_APPLIED,
                            CONSOLE_REASON_NONE);
    }
}

static void consoleExecuteDirectSleep(uint32_t requestId, const char* operationName,
                                      const ConsoleArgs& args, ConsoleCommandSource source,
                                      const ConsoleRecordSink* sink) {
    consoleExecuteDirectSleepWake(requestId, operationName, true, args, source, sink);
}

static void consoleExecuteDirectWake(uint32_t requestId, const char* operationName,
                                     const ConsoleArgs& args, ConsoleCommandSource source,
                                     const ConsoleRecordSink* sink) {
    consoleExecuteDirectSleepWake(requestId, operationName, false, args, source, sink);
}

// system.action.enable-web-control / disable-web-control: no arguments,
// matching POST /api/web-control/enable|disable (src/web/api_drive.cpp).
// Disable also zeroes any web-sourced drive frame, the same
// driveArbiterSubmit() call the REST handler makes - DriveSource::WEB_API
// is reused rather than a new console-specific source, since the effect
// (release web-sourced drive control) is identical regardless of which
// non-RC surface asked for it, and inventing a new DriveSource variant for
// one zero-frame submission is out of this ticket's scope.
static void consoleExecuteDirectWebControl(uint32_t requestId, const char* operationName,
                                           bool enable, const ConsoleArgs& args,
                                           ConsoleCommandSource source,
                                           const ConsoleRecordSink* sink) {
    if (!consoleRejectAnyArgument(requestId, operationName, args, sink)) {
        return;
    }
    commandedSetWebControl(enable, consoleCommandSourceFor(source));
    if (!enable) {
        driveArbiterSubmit(DriveSource::WEB_API, 0, 0, millis());
    }
    if (sink->onRecordResult) {
        sink->onRecordResult(requestId, CONSOLE_STATUS_OK, CONSOLE_OUTCOME_APPLIED,
                            CONSOLE_REASON_NONE);
    }
}

static void consoleExecuteDirectEnableWebControl(uint32_t requestId, const char* operationName,
                                                 const ConsoleArgs& args,
                                                 ConsoleCommandSource source,
                                                 const ConsoleRecordSink* sink) {
    consoleExecuteDirectWebControl(requestId, operationName, true, args, source, sink);
}

static void consoleExecuteDirectDisableWebControl(uint32_t requestId, const char* operationName,
                                                  const ConsoleArgs& args,
                                                  ConsoleCommandSource source,
                                                  const ConsoleRecordSink* sink) {
    consoleExecuteDirectWebControl(requestId, operationName, false, args, source, sink);
}

// system.action.set-mood: mood=<10|11|13|14> - the same applyMood() core
// system.config.mood already calls (consoleExecuteMoodConfig(),
// src/console/console_module.cpp; both rows model the identical active-mood
// mechanism) but reproducing handleMoodPost()'s OWN gate too (src/web/
// api_audio.cpp): sleep blocks it, unlike system.config.mood's write path,
// which carries no such gate today (a pre-existing discrepancy between the
// two rows, not introduced or fixed here - reported on the ticket, not
// silently closed by loosening this new row to match). requestStatusBroadcastNow()
// is reproduced too, the same broadcast handleMoodPost() makes after applying.
static void consoleExecuteDirectSetMood(uint32_t requestId, const char* operationName,
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

    taskENTER_CRITICAL(&robotStateMux);
    const bool sleeping = robotState.sleepMode;
    taskEXIT_CRITICAL(&robotStateMux);
    if (sleeping) {
        if (sink->onRecordResult) {
            sink->onRecordResult(requestId, CONSOLE_STATUS_ERR, CONSOLE_OUTCOME_BLOCKED,
                                CONSOLE_REASON_TEMPORARILY_UNAVAILABLE);
        }
        return;
    }

    // Schema already confirmed "mood" is one of the catalog's own enum
    // strings ("10"/"11"/"13"/"14"); consoleMoodIdValid() (src/console/
    // console_module.cpp, system.config.mood's own validator) is reused
    // verbatim as defensive depth, the same "reparse after schema" precedent
    // drive.action.move set for its own numeric arguments.
    char* end = nullptr;
    long mood = strtol(consoleArgsFind(args, "mood"), &end, 10);
    if (*end != '\0' || mood < 0 || mood > 255 || !consoleMoodIdValid((uint8_t)mood)) {
        consoleEmitArgFailure(requestId, operationName, "mood", CONSOLE_REASON_OUT_OF_RANGE, sink);
        return;
    }

    applyMood((uint8_t)mood);
    requestStatusBroadcastNow();

    if (sink->onRecordResult) {
        sink->onRecordResult(requestId, CONSOLE_STATUS_OK, CONSOLE_OUTCOME_APPLIED, CONSOLE_REASON_NONE);
    }
}

// system.action.set-identity: droidName=, mdnsUseName=<optional bool,
// default false> - the same two fields handleIdentityPost() reads (src/web/
// api_identity.cpp). normalizeDroidName() and parseBoolValue() (include/
// api_helpers.h) are the SAME pure validators that handler calls, reused
// verbatim; identitySetCommitApplied() is the extracted ADR 0034 Commit Step
// (include/api_identity.h) both now share.
static void consoleExecuteDirectSetIdentity(uint32_t requestId, const char* operationName,
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

    char normalized[DROID_NAME_MAX_LEN + 1] = {};
    if (!normalizeDroidName(consoleArgsFind(args, "droidName"), normalized, sizeof(normalized))) {
        consoleEmitArgFailure(requestId, operationName, "droidName", CONSOLE_REASON_OUT_OF_RANGE, sink);
        return;
    }

    bool mdnsUseName = false;
    const char* mdnsRaw = consoleArgsFind(args, "mdnsUseName");
    if (mdnsRaw != nullptr && !parseBoolValue(mdnsRaw, &mdnsUseName)) {
        consoleEmitArgFailure(requestId, operationName, "mdnsUseName", CONSOLE_REASON_OUT_OF_RANGE, sink);
        return;
    }

    ConfigSnapshot working = {};
    configCacheRead(&working);
    snprintf(working.system.droid_name, sizeof(working.system.droid_name), "%s", normalized);
    working.system.mdns_use_name = mdnsUseName;

    IdentitySetCommitOutcome commit = identitySetCommitApplied(&working);
    if (sink->onRecordResult) {
        sink->onRecordResult(requestId, commit.persisted ? CONSOLE_STATUS_OK : CONSOLE_STATUS_ERR,
                            commit.persisted ? CONSOLE_OUTCOME_APPLIED : CONSOLE_OUTCOME_INTERNAL_ERROR,
                            CONSOLE_REASON_NONE);
    }
}

// PA_HEAP_TRACING alone is the right condition even though api_profiler.h
// declares the trace cores inside its own PA_HEAP_PROFILE block: include/
// config.h:105 already #errors on PA_HEAP_TRACING=1 without PA_HEAP_PROFILE=1,
// so the pair cannot come apart in a compiling image.
#if PA_HEAP_TRACING
// system.action.profiler-trace-start / -stop (#224): the same Tier 3 cores
// POST /api/profiler/trace/start|stop call (profilerTraceStart()/Stop(),
// include/api_profiler.h), which own s_traceRunning so the two adapters cannot
// disagree about whether a trace is already armed. No arguments, matching the
// two REST routes, which read no body.
//
// Registered only when PA_HEAP_TRACING=1, and reachable only there:
// consoleExecuteCommand()'s build guard answers not-in-this-build for both
// rows on every image that does not carry the flag - which today is every
// image, since no environment in platformio.ini sets it (see api_profiler.h).
//
// Outcome mapping. A trace start/stop takes effect immediately and is not
// queued, so success is APPLIED - the same outcome system.action.sleep/wake
// above use for an immediate state change. "Already running" / "not running"
// is a state rule refusing the command, not transient busy-ness, so it is
// BLOCKED / blocked-by-state rather than temporarily-unavailable; the REST
// routes answer 409 for the same condition. A core that reports FAILED could
// not do what it was asked for a reason of its own: internal-error.
static void consoleAnswerTraceOutcome(uint32_t requestId, ProfilerTraceOutcome outcome,
                                      const ConsoleRecordSink* sink) {
    if (sink->onRecordResult == nullptr) {
        return;
    }
    switch (outcome) {
        case PROFILER_TRACE_STARTED:
        case PROFILER_TRACE_STOPPED:
            sink->onRecordResult(requestId, CONSOLE_STATUS_OK, CONSOLE_OUTCOME_APPLIED,
                                 CONSOLE_REASON_NONE);
            return;
        case PROFILER_TRACE_ALREADY_RUNNING:
        case PROFILER_TRACE_NOT_RUNNING:
            sink->onRecordResult(requestId, CONSOLE_STATUS_ERR, CONSOLE_OUTCOME_BLOCKED,
                                 CONSOLE_REASON_BLOCKED_BY_STATE);
            return;
        case PROFILER_TRACE_FAILED:
            sink->onRecordResult(requestId, CONSOLE_STATUS_ERR, CONSOLE_OUTCOME_INTERNAL_ERROR,
                                 CONSOLE_REASON_NONE);
            return;
    }
}

static void consoleExecuteDirectProfilerTraceStart(uint32_t requestId, const char* operationName,
                                                   const ConsoleArgs& args,
                                                   ConsoleCommandSource source,
                                                   const ConsoleRecordSink* sink) {
    (void)source;
    if (!consoleRejectAnyArgument(requestId, operationName, args, sink)) {
        return;
    }
    consoleAnswerTraceOutcome(requestId, profilerTraceStart(), sink);
}

static void consoleExecuteDirectProfilerTraceStop(uint32_t requestId, const char* operationName,
                                                  const ConsoleArgs& args,
                                                  ConsoleCommandSource source,
                                                  const ConsoleRecordSink* sink) {
    (void)source;
    if (!consoleRejectAnyArgument(requestId, operationName, args, sink)) {
        return;
    }
    consoleAnswerTraceOutcome(requestId, profilerTraceStop(), sink);
}
#endif  // PA_HEAP_TRACING

static const ConsoleDirectActionExecutorEntry g_systemDirectActionExecutors[] = {
    {"system.action.set-mode", consoleExecuteDirectSetMode},
    {"system.action.sleep", consoleExecuteDirectSleep},
    {"system.action.wake", consoleExecuteDirectWake},
    {"system.action.enable-web-control", consoleExecuteDirectEnableWebControl},
    {"system.action.disable-web-control", consoleExecuteDirectDisableWebControl},
    {"system.action.set-mood", consoleExecuteDirectSetMood},
    {"system.action.set-identity", consoleExecuteDirectSetIdentity},
#if PA_HEAP_TRACING
    {"system.action.profiler-trace-start", consoleExecuteDirectProfilerTraceStart},
    {"system.action.profiler-trace-stop", consoleExecuteDirectProfilerTraceStop},
#endif
};
static const size_t kSystemDirectActionExecutorCount =
    sizeof(g_systemDirectActionExecutors) / sizeof(g_systemDirectActionExecutors[0]);
