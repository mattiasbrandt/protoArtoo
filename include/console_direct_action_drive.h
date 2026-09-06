// =============================================================================
// include/console_direct_action_drive.h
//
// Controller Console direct-action executors - drive domain (#222): motion
// (drive.action.move) and the three speed-preset actions. Split out of
// src/console/console_module.cpp by #257 so this domain's rows can be
// extended without colliding with the other domains' files.
//
// HEADER-ONLY DELIBERATELY - see include/console_direct_action_system.h's
// header comment for the full reasoning (native's fenced build_src_filter
// allowlist has no room for a new src/console/*.cpp here) and for why
// `static`, not `inline`, is the right linkage given this header has exactly
// one includer.
//
// NOT a standalone compilation unit: #include'd from src/console/
// console_module.cpp only, at the point these executors used to live, so
// their bodies can see that file's own `static` consoleEmitArgFailure() by
// ordinary same-translation-unit visibility. Not included, and must not be
// included, from anywhere else.
// =============================================================================
#pragma once

#include <Arduino.h>  // millis(), constrain()

#include "console_direct_action_types.h"  // ConsoleDirectActionExecutorFn/Entry
#include "console_module.h"               // ConsoleCommandSource, ConsoleRecordSink
#include "console_args.h"                 // ConsoleArgs, consoleArgsFind(), schema validation
#include "console_catalog.h"              // ConsoleCatalogEntry, consoleCatalogFindByName()
#include "robot_state.h"                  // robotState, robotStateMux
#include "config_cache.h"                 // ConfigSnapshot, configCacheRead()
#include "drive_arbiter.h"                // driveArbiterSubmit(), DriveSource
#include "drive_speed_preset.h"           // SpeedPresetId, applySpeedPresetPersisted()
#include "api_helpers.h"                  // parseDriveValue()

// drive.action.move: speed= steer=, the same two arguments POST /api/drive
// reads (handleDrivePost, src/web/api_drive.cpp). Schema validation reuses
// consoleValidateArgsAgainstSchema() against this operation's own catalog
// params (int16, -1000..1000, both required - #221's shared argument
// contract, the same helper consoleExecuteAction() (src/console/
// console_module.cpp) already uses for ACTION_REGISTRY[]-resolved targets).
// Consent and clamp are reproduced verbatim from handleDrivePost() below, not
// reinvented: blocked = estop || stationary || (!sbusHealthy &&
// !webControlEnabled). Submits through driveArbiterSubmit() with
// DriveSource::WEB_API - the arbiter has no console-specific source, and
// #226 already established reusing WEB_API for a non-RC console-adjacent
// zero-frame release (disable-web-control, include/
// console_direct_action_system.h); the effect here is identical regardless
// of which non-RC caller asked for it, so this is that same reuse, not a new
// DriveSource variant.
static void consoleExecuteDirectDriveMove(uint32_t requestId, const char* operationName,
                                          const ConsoleArgs& args, ConsoleCommandSource source,
                                          const ConsoleRecordSink* sink) {
    (void)source;  // motion consent below reads RobotState, not which adapter asked

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

    // parseDriveValue() (src/web/api_helpers.cpp) is the same int16 parser
    // handleDrivePost() calls - the schema check above already confirmed
    // both values are present and within the catalog's declared -1000..1000
    // range as numeric text, so this can only fail on a disagreement between
    // that range and int16's own bounds, which never occurs for -1000..1000;
    // defensive, answered the same as any other out-of-range argument.
    int16_t speed = 0;
    int16_t steer = 0;
    if (!parseDriveValue(consoleArgsFind(args, "speed"), &speed) ||
        !parseDriveValue(consoleArgsFind(args, "steer"), &steer)) {
        consoleEmitArgFailure(requestId, operationName, "speed", CONSOLE_REASON_OUT_OF_RANGE, sink);
        return;
    }

    // Consent + clamp, read verbatim from handleDrivePost() (src/web/
    // api_drive.cpp): config snapshot read before the critical section,
    // exactly matching that handler's own ordering.
    ConfigSnapshot cfg = {};
    configCacheRead(&cfg);
    taskENTER_CRITICAL(&robotStateMux);
    const bool sbusHealthy = !robotState.sbusSignalLost && !robotState.sbusHwFailsafe;
    const bool blocked = robotState.estop || robotState.stationary ||
                         (!sbusHealthy && !robotState.webControlEnabled);
    taskEXIT_CRITICAL(&robotStateMux);
    const int16_t maxOut = cfg.drive.speedLimitMax;

    if (blocked) {
        if (sink->onRecordResult) {
            sink->onRecordResult(requestId, CONSOLE_STATUS_ERR, CONSOLE_OUTCOME_BLOCKED,
                                CONSOLE_REASON_BLOCKED_BY_STATE);
        }
        return;
    }

    // Widened to int on all three arguments, matching handleDrivePost()'s own
    // comment: Arduino's constrain() is a macro on the device but a
    // same-type template on the host, and the clamp has to be identical
    // arithmetic in both builds for a host test to mean anything about the
    // device.
    const int16_t clampedSpeed = (int16_t)constrain((int)speed, (int)-maxOut, (int)maxOut);
    const int16_t clampedSteer = (int16_t)constrain((int)steer, (int)-maxOut, (int)maxOut);
    driveArbiterSubmit(DriveSource::WEB_API, clampedSpeed, clampedSteer, millis());

    if (sink->onRecordResult) {
        sink->onRecordResult(requestId, CONSOLE_STATUS_OK, CONSOLE_OUTCOME_APPLIED,
                            CONSOLE_REASON_NONE);
    }
}

// drive.action.speed-preset-{slow,normal,turbo}: each carries a single
// `preset` argument the catalog's own enum schema pins to that action's own
// name (e.g. drive.action.speed-preset-slow only accepts preset=slow), so
// the schema check is sufficient validation - the preset itself is chosen by
// which action name was called, not read back out of the argument.
// applySpeedPresetPersisted() (include/drive_speed_preset.h) is the same
// function handleSpeedPresetPost() (src/web/api_drive.cpp) calls, reused
// verbatim. That REST handler applies NO estop/stationary/sbus-health gate -
// it sets the drive speed CAP the arbiter clamps against, not a live drive
// command - so this executor adds none either; a broader gate than
// api_drive.cpp's own is out of #222's scope even where it might look safer.
static void consoleExecuteDirectSpeedPreset(uint32_t requestId, const char* operationName,
                                            SpeedPresetId preset, const ConsoleArgs& args,
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

    if (!applySpeedPresetPersisted(preset)) {
        // Matches handleSpeedPresetPost()'s own failed-persist branch: a
        // failed NVS write is an explicit error, not a silently accepted one.
        if (sink->onRecordResult) {
            sink->onRecordResult(requestId, CONSOLE_STATUS_ERR, CONSOLE_OUTCOME_INTERNAL_ERROR,
                                CONSOLE_REASON_NONE);
        }
        return;
    }

    if (sink->onRecordResult) {
        sink->onRecordResult(requestId, CONSOLE_STATUS_OK, CONSOLE_OUTCOME_APPLIED,
                            CONSOLE_REASON_NONE);
    }
}

static void consoleExecuteDirectSpeedPresetSlow(uint32_t requestId, const char* operationName,
                                                const ConsoleArgs& args, ConsoleCommandSource source,
                                                const ConsoleRecordSink* sink) {
    (void)source;
    consoleExecuteDirectSpeedPreset(requestId, operationName, SpeedPresetId::Slow, args, sink);
}

static void consoleExecuteDirectSpeedPresetNormal(uint32_t requestId, const char* operationName,
                                                  const ConsoleArgs& args, ConsoleCommandSource source,
                                                  const ConsoleRecordSink* sink) {
    (void)source;
    consoleExecuteDirectSpeedPreset(requestId, operationName, SpeedPresetId::Normal, args, sink);
}

static void consoleExecuteDirectSpeedPresetTurbo(uint32_t requestId, const char* operationName,
                                                 const ConsoleArgs& args, ConsoleCommandSource source,
                                                 const ConsoleRecordSink* sink) {
    (void)source;
    consoleExecuteDirectSpeedPreset(requestId, operationName, SpeedPresetId::Turbo, args, sink);
}

static const ConsoleDirectActionExecutorEntry g_driveDirectActionExecutors[] = {
    {"drive.action.move", consoleExecuteDirectDriveMove},
    {"drive.action.speed-preset-slow", consoleExecuteDirectSpeedPresetSlow},
    {"drive.action.speed-preset-normal", consoleExecuteDirectSpeedPresetNormal},
    {"drive.action.speed-preset-turbo", consoleExecuteDirectSpeedPresetTurbo},
};
static const size_t kDriveDirectActionExecutorCount =
    sizeof(g_driveDirectActionExecutors) / sizeof(g_driveDirectActionExecutors[0]);
