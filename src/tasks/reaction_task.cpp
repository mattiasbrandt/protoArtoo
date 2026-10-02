// =============================================================================
// src/tasks/reaction_task.cpp
//
// ReactionTask - fires the droid's Reactions (ADR 0053, #450; CONTEXT.md
// "Reaction"): a trigger binding whose source is a droid condition, which the
// droid fires itself. Core 0, 20 Hz.
//
// The rules are include/reaction_evaluator.h's. This task is the adapter: it
// reads the droid's state from RobotState, asks the evaluator, sends what it
// answers through the action door RC bindings use (dispatchReactionAction(),
// src/tasks/rc_input.cpp), and publishes what each Reaction is doing for the
// RC page.
//
// WHY A TASK OF ITS OWN. A Reaction must fire with no radio fitted, and then
// there is no RC task (src/main.cpp). It must not add work to DriveTask, the
// real-time drive loop on Core 1; this task reads the resolved drive output
// DriveTask already leaves in RobotState, under robotStateMux. The Sequence
// Coordinator's task has no stack to spare for the action door, and the
// SafetyMonitor is an observer that sends nothing. So the door gets a caller
// with a stack walked for it (tools/task_stack_recipes.json).
//
// NOT ON THE TASK WATCHDOG, deliberately. A watchdog reset latches the estop on
// the next boot, which is the right answer for a task the droid cannot be
// trusted without. This one only makes the droid livelier: if it ever
// stalled, Reactions would stop and nothing else would.
// =============================================================================

#include "reaction_task.h"

#include <Arduino.h>
#include <stdlib.h>

#include "config_cache.h"
#include "config_store.h"         // RC_TRIGGER_SLOT_COUNT
#include "drive_backend.h"        // kDriveBackend, driveBackendCurrentReport()
#include "logging.h"
#include "marcduino_helpers.h"
#include "marcduino_ownership.h"  // marcduinoCommandOwner()
#include "rc_input.h"             // dispatchReactionAction()
#include "reaction_evaluator.h"
#include "robot_state.h"
#include "seq_store.h"            // seqStoreMayOpenBodyPart()
#include "sequence_dispatcher.h"  // sequenceBodyRoutineName()

static const char* TAG = "Reaction";

static_assert(REACTION_SLOT_MAX == RC_TRIGGER_SLOT_COUNT,
              "the evaluator keeps one state per trigger slot");
static_assert(REACTION_STATUS_SLOTS == RC_TRIGGER_SLOT_COUNT,
              "RobotState publishes one Reaction status per trigger slot");

// Whether the numbered routine :SE<id> can open a body Part: a body routine
// itself (30-36), or the body half of a full-droid sequence. Asked of the
// sequence that would run, so one retrained to move nothing answers no.
static bool seRoutineOpensBodyPart(int seqId) {
    if (!marcduino_sequence_id_valid(seqId)) {
        seqId = marcduino_full_droid_body_actions(seqId).bodySeqId;
    }
    return seqStoreMayOpenBodyPart(sequenceBodyRoutineName(seqId));
}

// A raw Marcduino line: the ones the body owns and that move a Part away from
// closed. :CLnn is the one panel head that never does.
static bool marcduinoLineOpensBodyPart(const char* line) {
    if (line == nullptr || marcduinoCommandOwner(line) == MarcduinoOwner::Dome) {
        return false;
    }
    if (marcduino_is_panel_command(line)) {
        return strncmp(line, ":CL", 3) != 0;
    }
    if (strncmp(line, ":SE", 3) == 0) {
        return seRoutineOpensBodyPart(atoi(line + 3));
    }
    return false;  // a Mood, a body '#' command: no Part moves
}

// Whether firing this action would open a body Part, whatever path it takes
// there: an Output's toggle, a body routine, a Sequence carrying a Body Step,
// a raw command the body owns. While the droid is driving, such a firing is
// held back whole; lights, sound and the dome are not asked about here and
// still fire (ADR 0053).
static bool reactionOpensBodyPart(RobotActionId target, const char* payload) {
    switch (target) {
        case SERVO_ACTION_ARM1_TOGGLE:
        case SERVO_ACTION_ARM2_TOGGLE:
        case SERVO_ACTION_AUX1_TOGGLE:
        case SERVO_ACTION_AUX2_TOGGLE:
        case SERVO_ACTION_AUX3_TOGGLE:
            return true;
        case DOME_ACTION_MARCDUINO_SEQ:
            return payload != nullptr && seRoutineOpensBodyPart(atoi(payload));
        case DOME_ACTION_MARCDUINO_CMD:
            return marcduinoLineOpensBodyPart(payload);
        case DOME_ACTION_SEQ:
            return seqStoreMayOpenBodyPart(payload);
        default: {
            const int seqId = robotActionIdToDroidSeqId(target);
            return seqId > 0 && seRoutineOpensBodyPart(seqId);
        }
    }
}

static ReactionCurrentReport currentReport() {
    switch (driveBackendCurrentReport()) {
        case DriveCurrentReport::Reported:
            return ReactionCurrentReport::Reported;
        case DriveCurrentReport::NotReported:
            return ReactionCurrentReport::NotReported;
        case DriveCurrentReport::Unknown:
            break;
    }
    return ReactionCurrentReport::Unknown;
}

static void readInputs(ReactionInputs* in) {
    in->nowMs = millis();
    in->feedbackSupported = kDriveBackend.reportsFeedback;
    in->currentReport = currentReport();
    taskENTER_CRITICAL(&robotStateMux);
    in->estop = robotState.estop;
    in->sleepMode = robotState.sleepMode;
    in->radioLost = robotState.sbusSignalLost || robotState.sbusHwFailsafe;
    in->failsafeHold = in->radioLost || robotState.webDriveExpired;
    in->driveSpeed = robotState.driveOutputSpeed;
    in->driveSteer = robotState.driveOutputSteer;
    in->feedbackValid = robotState.driveFeedbackValid;
    in->wheelSpeedL = robotState.driveFeedbackSpeedL;
    in->wheelSpeedR = robotState.driveFeedbackSpeedR;
    in->wheelCurrentL = robotState.driveFeedbackCurrentL;
    in->wheelCurrentR = robotState.driveFeedbackCurrentR;
    in->audioPlayState = robotState.audio_module_play_state;
    taskEXIT_CRITICAL(&robotStateMux);
}

static void publishStatus(const ReactionEvaluator& ev) {
    taskENTER_CRITICAL(&robotStateMux);
    for (size_t i = 0; i < REACTION_STATUS_SLOTS; ++i) {
        const ReactionSlotState& slot = ev.slots[i];
        ReactionStatus& status = robotState.reactions[i];
        status.source = (uint8_t)slot.binding.source;
        status.channel = slot.binding.channel;
        status.availability = (uint8_t)slot.availability;
        status.fires = slot.fires;
        status.refusals = slot.refusals;
    }
    taskEXIT_CRITICAL(&robotStateMux);
}

// Out of line so the log's line buffer is in a frame of its own, beside the
// action door on this task's chain rather than under it.
static __attribute__((noinline)) void logFiring(const ReactionFiring& firing, bool carriedOut) {
    PA_LOG_INFO(TAG, "%s %s -> %s%s", rcBindingSourceToLabel(firing.source),
                firing.pressed ? "fired" : "ended", robotActionIdToString(firing.target),
                carriedOut ? "" : " (not carried out)");
}

void reactionTask(void* /*pvParameters*/) {
    // Static, not stack: the evaluator's state and one tick's scratch are a
    // few hundred bytes this task would otherwise carry under the action door.
    static ReactionEvaluator evaluator;
    static RcTriggerBinding bindings[RC_TRIGGER_SLOT_COUNT];
    static ReactionOutput output;

    reactionEvaluatorInit(&evaluator);
    PA_LOG_INFO(TAG, "active");

    while (true) {
        const size_t count = configCacheReadRcTriggerSlots(bindings, RC_TRIGGER_SLOT_COUNT);
        ReactionInputs in = {};
        readInputs(&in);

        reactionEvaluatorTick(
            &evaluator, bindings, count, in,
            [](RobotActionId target, const char* payload) {
                return reactionOpensBodyPart(target, payload);
            },
            &output);

        // Each firing carries its own action: a release may be for a Reaction
        // that has just left its slot. Only a press the door carried out
        // counts as a firing.
        for (uint8_t i = 0; i < output.count; ++i) {
            const ReactionFiring& firing = output.firings[i];
            const bool carriedOut =
                dispatchReactionAction(firing.target, firing.payload, firing.pressed) ==
                RcDispatchOutcome::kQueued;
            if (carriedOut) {
                reactionEvaluatorFired(&evaluator, firing);
            }
            logFiring(firing, carriedOut);
        }

        publishStatus(evaluator);
        vTaskDelay(pdMS_TO_TICKS(REACTION_TICK_MS));
    }
}
