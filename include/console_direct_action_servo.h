// =============================================================================
// include/console_direct_action_servo.h
//
// Controller Console direct-action executors - servo domain: open, close and
// set-position (#221 remainder). Split out of src/console/console_module.cpp
// by #257 so this domain's rows can be extended without colliding with the
// other domains' files.
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

#include <stdlib.h>

#include "console_direct_action_types.h"  // ConsoleDirectActionExecutorFn/Entry
#include "console_module.h"               // ConsoleCommandSource, ConsoleRecordSink
#include "console_args.h"                 // ConsoleArgs, consoleArgsFind(), schema validation
#include "console_catalog.h"              // ConsoleCatalogEntry, consoleCatalogFindByName()
#include "api_servo.h"                    // parseArmId(), servoSubmitCommand(), ServoSubmitOutcome
#include "ledc_pwm.h"                     // SERVO_PULSE_MIN_US/MAX_US

// servo.action.open/close/set-position: target=<arm1|arm2|aux1|aux2|aux3
// [|both]>, set-position also carries position_us=<500..2500>.
// parseArmId() and servoSubmitCommand() (include/api_servo.h) are the SAME
// target<->id mapping and the SAME queue submission handleServoPost() uses,
// reused verbatim - the ADR 0034 Commit Step beside that handler. Deliberately
// NOT wired here: servo.action.stop, which the registry declares with zero
// params even though the underlying /api/servo endpoint requires an `arm`
// for every action including "stop", and armId=255 only broadcasts to
// arm1+arm2 (robot_state.h's own field comment) - never aux1..3. Answering
// "stop" with a hardcoded broadcast id would silently leave three of five
// servos moving on an operator's "stop" call; that is a registry/
// implementation decision this ticket does not invent, so
// servo.action.stop stays CONSOLE_REASON_EXECUTOR_NOT_READY (reported on
// the ticket).
static void consoleExecuteServoCommand(uint32_t requestId, const char* operationName,
                                       ServoCommandType type, const ConsoleArgs& args,
                                       ConsoleCommandSource source, const ConsoleRecordSink* sink) {
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

    // Schema already confirmed "target" is one of the catalog's own enum
    // values; parseArmId() can only fail here on a disagreement between
    // that enum and its own accepted set, which never occurs for the
    // lowercase names the registry declares - defensive, the same
    // "reparse after schema" precedent drive.action.move set (include/
    // console_direct_action_drive.h).
    int16_t armId = parseArmId(consoleArgsFind(args, "target"));
    if (armId < 0) {
        consoleEmitArgFailure(requestId, operationName, "target", CONSOLE_REASON_OUT_OF_RANGE, sink);
        return;
    }

    uint16_t positionUs = 0;  // dead for OPEN/CLOSE (src/tasks/servo_task.cpp never reads it)
    if (type == SERVO_CMD_POSITION) {
        char* end = nullptr;
        long parsed = strtol(consoleArgsFind(args, "position_us"), &end, 10);
        if (*end != '\0' || parsed < SERVO_PULSE_MIN_US || parsed > SERVO_PULSE_MAX_US) {
            consoleEmitArgFailure(requestId, operationName, "position_us", CONSOLE_REASON_OUT_OF_RANGE,
                                  sink);
            return;
        }
        positionUs = (uint16_t)parsed;
    }

    ServoSubmitOutcome outcome =
        servoSubmitCommand((uint8_t)armId, type, positionUs, consoleCommandSourceFor(source));
    if (!outcome.ok) {
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

static void consoleExecuteServoOpen(uint32_t requestId, const char* operationName,
                                    const ConsoleArgs& args, ConsoleCommandSource source,
                                    const ConsoleRecordSink* sink) {
    consoleExecuteServoCommand(requestId, operationName, SERVO_CMD_OPEN, args, source, sink);
}

static void consoleExecuteServoClose(uint32_t requestId, const char* operationName,
                                     const ConsoleArgs& args, ConsoleCommandSource source,
                                     const ConsoleRecordSink* sink) {
    consoleExecuteServoCommand(requestId, operationName, SERVO_CMD_CLOSE, args, source, sink);
}

static void consoleExecuteServoSetPosition(uint32_t requestId, const char* operationName,
                                           const ConsoleArgs& args, ConsoleCommandSource source,
                                           const ConsoleRecordSink* sink) {
    consoleExecuteServoCommand(requestId, operationName, SERVO_CMD_POSITION, args, source, sink);
}

static const ConsoleDirectActionExecutorEntry g_servoDirectActionExecutors[] = {
    {"servo.action.open", consoleExecuteServoOpen},
    {"servo.action.close", consoleExecuteServoClose},
    {"servo.action.set-position", consoleExecuteServoSetPosition},
};
static const size_t kServoDirectActionExecutorCount =
    sizeof(g_servoDirectActionExecutors) / sizeof(g_servoDirectActionExecutors[0]);
