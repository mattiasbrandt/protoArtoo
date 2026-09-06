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

// servo.action.open/close/set-position/stop: target=<arm1|arm2|aux1|aux2|
// aux3[|both]>, set-position also carries position_us=<500..2500>.
// parseArmId() and servoSubmitCommand() (include/api_servo.h) are the SAME
// target<->id mapping and the SAME queue submission handleServoPost() uses,
// reused verbatim - the ADR 0036 Commit Step beside that handler.
//
// servo.action.stop (#221 remainder registry fix, docs/action-registry.yaml):
// the row used to declare zero params even though the underlying /api/servo
// endpoint requires an `arm` for every action including "stop" - it now
// declares the same target enum open/close/set-position already do, and
// consoleExecuteServoStop() below resolves it the same way. Two facts this
// wiring does NOT change, because they are firmware behaviour on a path the
// web UI shares (registry/coordinator decision, not this ticket's to make):
// target=both only broadcasts to ARM1+ARM2, never AUX1..3 (ServoCommand::
// armId's own field comment, include/robot_state.h; src/tasks/
// servo_task.cpp:353,367,387); and "stop" does not hold position at all -
// api_servo.cpp's parseAction() maps it to SERVO_CMD_POSITION at
// SERVO_PULSE_NEUTRAL_US (there is no SERVO_CMD_STOP in the enum), so it
// drives the servo to neutral like set-position with a fixed pulse width,
// never a freeze-in-place.
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

// servo.action.stop: target=<arm1|arm2|aux1|aux2|aux3|both> only - no
// position_us (the registry declares none, unlike set-position), because the
// pulse width is not an operator choice here, it is always
// SERVO_PULSE_NEUTRAL_US (see this file's header comment for why). Shares
// consoleExecuteServoCommand()'s schema-check/target-resolution shape rather
// than reusing that function directly: threading a fixed pulse width through
// its SERVO_CMD_POSITION branch would need a position_us key this row's
// schema does not have, so a small dedicated function reads more plainly
// than a "force neutral" parameter on the shared one.
static void consoleExecuteServoStop(uint32_t requestId, const char* operationName,
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

    // Same "reparse after schema" precedent consoleExecuteServoCommand()
    // above documents: parseArmId() can only fail here on a disagreement
    // between the catalog's own enum and its accepted set.
    int16_t armId = parseArmId(consoleArgsFind(args, "target"));
    if (armId < 0) {
        consoleEmitArgFailure(requestId, operationName, "target", CONSOLE_REASON_OUT_OF_RANGE, sink);
        return;
    }

    ServoSubmitOutcome outcome = servoSubmitCommand((uint8_t)armId, SERVO_CMD_POSITION,
                                                     SERVO_PULSE_NEUTRAL_US,
                                                     consoleCommandSourceFor(source));
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

static const ConsoleDirectActionExecutorEntry g_servoDirectActionExecutors[] = {
    {"servo.action.open", consoleExecuteServoOpen},
    {"servo.action.close", consoleExecuteServoClose},
    {"servo.action.set-position", consoleExecuteServoSetPosition},
    {"servo.action.stop", consoleExecuteServoStop},
};
static const size_t kServoDirectActionExecutorCount =
    sizeof(g_servoDirectActionExecutors) / sizeof(g_servoDirectActionExecutors[0]);
