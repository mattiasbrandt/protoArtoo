// =============================================================================
// include/console_direct_action_servo.h
//
// Controller Console direct-action executors - servo domain: open, close,
// set-position and stop (#221 remainder), nudge (#363), hold and release
// (#364), and the bulk centre (#365). Split out of
// src/console/console_module.cpp by #257 so this domain's rows can be extended
// without colliding with the other domains' files.
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

// servo.action.open/close/set-position/stop/nudge: target=<arm1|arm2|aux1|
// aux2|aux3[|both]>, set-position also carries position_us=<500..2500>.
// parseArmId() and servoSubmitCommand() (include/api_servo.h) are the SAME
// target<->id mapping and the SAME queue submission handleServoPost() uses,
// reused verbatim - the ADR 0036 Commit Step beside that handler.
//
// servo.action.nudge (#363, ADR 0050) carries a target and nothing else: the
// registry's enum for it excludes "both", the same way set-position's does,
// because a Find by Moving nudge is one output at a time by definition, and
// no width, because ServoTask computes the bounded pair from the width on the
// pin (include/servo_nudge.h) so that no source can ask for a big one.
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

    // Dead for OPEN/CLOSE/NUDGE/RELEASE, which src/tasks/servo_task.cpp never
    // reads it for. POSITION and HOLD both carry a width and take the same
    // check: a hold is a drive that keeps the pulse on afterwards.
    uint16_t positionUs = 0;
    if (type == SERVO_CMD_POSITION || type == SERVO_CMD_HOLD) {
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

static void consoleExecuteServoNudge(uint32_t requestId, const char* operationName,
                                     const ConsoleArgs& args, ConsoleCommandSource source,
                                     const ConsoleRecordSink* sink) {
    consoleExecuteServoCommand(requestId, operationName, SERVO_CMD_NUDGE, args, source, sink);
}

// servo.action.hold (#364, ADR 0064): the calibration dial's hold, reached from
// the Console as well as from the dial. It carries a position_us like
// set-position and goes through the same width check, because a hold IS a drive
// -- what differs is that the pulse stays on afterwards. One output per hold,
// so the registry's enum for it excludes "both" the way set-position's does.
//
// Holding from the Console is a real thing to want on a FireBeetle 2 with no
// WiFi up, where the Console is the only surface there is; the two firmware
// bounds apply identically, so a Console session that walks away leaves an
// output held for at most ten minutes, and for about three seconds if it stops
// sending.
static void consoleExecuteServoHold(uint32_t requestId, const char* operationName,
                                    const ConsoleArgs& args, ConsoleCommandSource source,
                                    const ConsoleRecordSink* sink) {
    consoleExecuteServoCommand(requestId, operationName, SERVO_CMD_HOLD, args, source, sink);
}

// servo.action.release (#364, ADR 0043): pulses off. No width, because a
// release commands no position at all, so it takes consoleExecuteServoCommand()'s
// no-width path exactly as open, close and nudge do. "both" IS in this row's
// enum, unlike hold's: letting go of two arms is the same act twice rather than
// two outputs being moved together.
static void consoleExecuteServoRelease(uint32_t requestId, const char* operationName,
                                       const ConsoleArgs& args, ConsoleCommandSource source,
                                       const ConsoleRecordSink* sink) {
    consoleExecuteServoCommand(requestId, operationName, SERVO_CMD_RELEASE, args, source, sink);
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

// servo.action.centre-all (#318, #365): no arguments, matching
// POST /api/servo/centre (handleServoCentrePost(), src/web/api_servo.cpp) - a
// transient flag set unconditionally, the same shape dome.action.sequence-stop
// uses. No arm, because the sweep covers every Servo Output and choosing them
// is the Sequence Coordinator's; no estop or sleep gate, because the REST
// source has none either and the Coordinator is the one place that judgement
// lives (it refuses to start under a halt and says so).
//
// APPLIED rather than QUEUED: nothing entered a queue here. The flag is read on
// the Coordinator's next tick, and what it queues after that is one servo
// command per Output, no closer together than the Cadence Floor.
static void consoleExecuteServoCentreAll(uint32_t requestId, const char* operationName,
                                         const ConsoleArgs& args, ConsoleCommandSource source,
                                         const ConsoleRecordSink* sink) {
    if (!consoleRejectAnyArgument(requestId, operationName, args, sink)) {
        return;
    }
    taskENTER_CRITICAL(&robotStateMux);
    robotState.bulkCentreRequest = consoleCommandSourceFor(source);
    taskEXIT_CRITICAL(&robotStateMux);

    if (sink->onRecordResult) {
        sink->onRecordResult(requestId, CONSOLE_STATUS_OK, CONSOLE_OUTCOME_APPLIED,
                            CONSOLE_REASON_NONE);
    }
}

static const ConsoleDirectActionExecutorEntry g_servoDirectActionExecutors[] = {
    {"servo.action.open", consoleExecuteServoOpen},
    {"servo.action.close", consoleExecuteServoClose},
    {"servo.action.set-position", consoleExecuteServoSetPosition},
    {"servo.action.stop", consoleExecuteServoStop},
    {"servo.action.nudge", consoleExecuteServoNudge},
    {"servo.action.hold", consoleExecuteServoHold},
    {"servo.action.release", consoleExecuteServoRelease},
    {"servo.action.centre-all", consoleExecuteServoCentreAll},
};
static const size_t kServoDirectActionExecutorCount =
    sizeof(g_servoDirectActionExecutors) / sizeof(g_servoDirectActionExecutors[0]);
