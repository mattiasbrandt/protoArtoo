// =============================================================================
// src/rc_dispatcher_helpers.cpp
//
// Implementation of RC dispatch helpers  --  queue commands to subsystems,
// encapsulating all subsystem-specific knowledge.
//
// =============================================================================

#include "rc_dispatcher_helpers.h"

#include <Arduino.h>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "audio_dollar_parser.h"  // audioDollarBankForm()
#include "audio_task.h"
#include "commanded_modes.h"
#include "config.h"
#include "config_cache.h"
#include "dome_link.h"
#include "dome_rx_parser.h"
#include "drive_arbiter.h"
#include "drive_speed_preset.h"
#include "failsafe_gate.h"
#include "logging.h"
#include "marcduino_helpers.h"
#include "marcduino_router.h"  // routeMarcduinoLine()
#include "queue_drop_tracker.h"
#include "rc_action_dispatcher.h"
#include "rc_input_processor.h"
#include "rc_mapping.h"
#include "rc_pwm_helpers.h"
#include "sequence_dispatcher.h"
#include "web_server.h"

static const char* TAG = "RCDispatch";

// =============================================================================
// Drive Dispatch
// =============================================================================

void rcDispatchDrive(int16_t driveSpeed, int16_t driveSteer, bool shouldStop) {
    driveArbiterSubmit(DriveSource::RC, driveSpeed, driveSteer, millis());
}

// =============================================================================
// Dome Dispatch
// =============================================================================

void rcDispatchDome(int domeRawFiltered, const RcMappingConfig& mapping, bool domeFiltered) {
    if (!configCacheReadActiveDomeEnabled()) {
        return;
    }
    if (domeFiltered) {
        float calibrated = applyRcAnalogCalibration(domeRawFiltered, mapping.domeSpeed, nullptr);
        DomeCommand domeCmd = {};
        domeCmd.speed = calibrated;
        domeCmd.source = SRC_SBUS;
        domeCmd.timestampMs = millis();
        if (xQueueSend(domeCmdQueue, &domeCmd, 0) != pdTRUE) {
            logQueueDrop(QUEUE_DOME_CMD, "dome command");
        }
    }
}

// =============================================================================
// Audio Trigger Dispatch (Backbone)
// =============================================================================

void rcDispatchAudioTrigger(const char* audioTrigger) {
    if (audioTrigger != nullptr) {
        if (!parseMarcduinoCommand(audioTrigger)) {
            PA_LOG_DEBUG(TAG, "audio trigger command not recognized: %s", audioTrigger);
        }
    }
}

// =============================================================================
// Servo Command Queue Helpers
// =============================================================================

// A numbered body routine, :SE30..:SE36, is a Factory Sequence built from Body
// Steps (ADR 0049), so it starts through the Sequence Coordinator like any other
// DM:* name -- which is also what lets a Retrained Sequence of that name shadow
// it. False when the id is not a body routine or the request queue is full.
static bool startBodyRoutine(uint8_t sequenceId, CommandSource src) {
    const char* name = sequenceBodyRoutineName(sequenceId);
    return name != nullptr && sequenceStart(name, src);
}

// `boardIndex` is the RC action's arm/aux index, which counts the board's own
// Outputs in their table's order; it goes out as that Output's address.
//
// noinline, deliberately: rcDispatchSingleAction() is on RCInputTask's measured
// chain (ADR 0040), and inlined there the address lookup grew its frame by 16 B
// (#444). Out of line its own route - one queue send and the drop's log line -
// is far shallower than the Marcduino route that sets the chain.
static __attribute__((noinline)) bool queueServoCommand(uint8_t boardIndex, ServoCommandType type,
                                                        uint16_t positionUs, CommandSource src) {
    ServoCommand cmd = {};
    cmd.output = boardOutputAddress(boardIndex);
    cmd.type = type;
    cmd.positionUs = positionUs;
    cmd.source = src;
    if (xQueueSend(servoCmdQueue, &cmd, 0) != pdTRUE) {
        logQueueDrop(QUEUE_SERVO_CMD, "servo command");
        return false;
    }
    return true;
}

// =============================================================================
// Single Action Dispatch (for processTriggerAction and trigger loop)
// =============================================================================

RcDispatchOutcome rcDispatchSingleAction(const RcActionResult& res, CommandSource src) {
    bool queueFull = false;

    if (res.audioTrack != 0) {
        if (!audioQueuePlayTrack(res.audioTrack, src)) {
            PA_LOG_WARN(TAG, "audio track dropped: track=%u queue full", (unsigned)res.audioTrack);
            queueFull = true;
        }
    }

    if (res.audioDollarCmd[0] != '\0') {
        if (!audioQueueDollar(res.audioDollarCmd, src)) {
            PA_LOG_WARN(TAG, "droid sequence audio dropped: %s", res.audioDollarCmd);
            queueFull = true;
        }
    }

    if (res.audioStep != 0) {
        if (!audioQueueStepSound(res.audioStep, src)) {
            PA_LOG_WARN(TAG, "%s sound dropped: queue full", res.audioStep > 0 ? "next" : "previous");
            queueFull = true;
        }
    }

    if (res.servoIndex >= 0) {
        if (res.servoIsSequence) {
            if (!startBodyRoutine(res.servoSequenceId, src)) {
                PA_LOG_WARN(TAG, "droid sequence body routine not started: :SE%02u",
                            (unsigned)res.servoSequenceId);
                queueFull = true;
            }
        } else {
            ServoCommandType cmd = res.servoOpen ? SERVO_CMD_OPEN : SERVO_CMD_CLOSE;
            if (!queueServoCommand((uint8_t)res.servoIndex, cmd, 0, src)) {
                queueFull = true;  // queueServoCommand() already logs the drop
            }
        }
    }

    if (res.domeTxCmd[0] != '\0') {
        if (strncmp(res.domeTxCmd, "DM:", 3) == 0) {
            if (!sequenceStart(res.domeTxCmd, src)) {
                PA_LOG_WARN(TAG, "sequence start failed: %s", res.domeTxCmd);
                queueFull = true;
            }
        } else if (domeConnected()) {
            if (!domeQueueTx(res.domeTxCmd)) {
                PA_LOG_WARN(TAG, "dome tx queue full: %s", res.domeTxCmd);
                queueFull = true;
            }
        } else {
            // Dome transport not connected: pre-#220 this was a silent drop
            // with no log line at all (reachable by DROID_SEQ_* actions, the
            // droid sequence's dome-forward portion). #220 needs a truthful
            // outcome for the test/Console caller, so this now logs and
            // counts the same as a queue-full - live RC behavior is
            // otherwise unchanged (no side effect either way).
            PA_LOG_WARN(TAG, "dome tx dropped: dome not connected: %s", res.domeTxCmd);
            queueFull = true;
        }
    }

    // A Marcduino binding's line goes where Command Ownership sends it: the
    // body runs what it owns and the rest is forwarded to the dome (ADR 0055,
    // include/marcduino_router.h). A '$' line - the one other prefix
    // rcPayloadValidForMarcduinoCommand() admits - is audio, queued as such.
    // The binding carries this line and nothing else, so its outcome is the
    // answer; the live RC loop still discards it, and the Console action
    // executor reports it (#221).
    if (res.marcduinoCmd[0] != '\0') {
        uint16_t bankSound = 0;
        if (res.marcduinoCmd[0] == ':' || res.marcduinoCmd[0] == '#') {
            const RcDispatchOutcome routed = rcDispatchOutcomeForMarcduinoRoute(
                routeMarcduinoLine(res.marcduinoCmd));
            if (routed != RcDispatchOutcome::kQueued) {
                // A refused queue earlier in this result still reads as one.
                return queueFull ? RcDispatchOutcome::kQueueFull : routed;
            }
        } else if (audioDollarBankForm(res.marcduinoCmd, nullptr, &bankSound) && bankSound == 0) {
            // "$800": the bank form naming sound 00, which no bank has.
            // Whether a bank 8 is fitted is AudioTask's to answer - the bank
            // table is not read from RCInputTask.
            return queueFull ? RcDispatchOutcome::kQueueFull : RcDispatchOutcome::kNotExecutable;
        } else if (!audioQueueDollar(res.marcduinoCmd, src)) {
            PA_LOG_WARN(TAG, "marcduino audio dropped: %s", res.marcduinoCmd);
            queueFull = true;
        }
    }

    return queueFull ? RcDispatchOutcome::kQueueFull : RcDispatchOutcome::kQueued;
}

// =============================================================================
// Tier 2 Trigger Result Dispatch
// =============================================================================

void rcDispatchTriggerResults(const RcProcessorOutput& output,
                              const RcTriggerBinding* triggers) {
    for (size_t i = 0; i < RC_TRIGGER_MAX; ++i) {
        const RcActionResult& res = output.triggerResults[i];

        // Log audio dispatch for trigger bindings (trigger context is available)
        if (res.audioTrack != 0) {
            const RcTriggerBinding& b = triggers[i];
            const char* catLabel = randomSoundCategoryLabel(b.target);
            PA_LOG_INFO(TAG, "[RC] sound %s CH%u %s -> track %u",
                        rcBindingSourceToLabel(b.source), (unsigned)b.channel,
                        catLabel ? catLabel : robotActionIdToString(b.target),
                        (unsigned)res.audioTrack);
        }

        if (res.audioDollarCmd[0] != '\0') {
            const RcTriggerBinding& b = triggers[i];
            PA_LOG_INFO(TAG, "[RC] sound %s CH%u %s -> seq %s",
                        rcBindingSourceToLabel(b.source), (unsigned)b.channel,
                        robotActionIdToString(b.target), res.audioDollarCmd);
        }

        if (res.audioStep != 0) {
            const RcTriggerBinding& b = triggers[i];
            PA_LOG_INFO(TAG, "[RC] sound %s CH%u %s -> %s", rcBindingSourceToLabel(b.source),
                        (unsigned)b.channel, robotActionIdToString(b.target),
                        res.audioStep > 0 ? "next" : "previous");
        }

        // Dispatch audio, servo, dome, marcduino commands. This loop is the
        // live SBUS Tier-2 trigger path; always attributed to SRC_SBUS
        // (unchanged from before #220's src parameter). The return value is
        // not consumed here - the RC path had no outcome-reporting consumer
        // before this ticket and still does not.
        rcDispatchSingleAction(res, SRC_SBUS);

        // Handle system modes (estop, sleep, stationary, speed preset)
        if (res.triggerEstop) {
            failsafeTrigger(FailsafeLayer::ESTOP);
        }

        if (res.setSleep) {
            commandedSetSleep(res.newSleepMode, SRC_SBUS);
            requestStatusBroadcastNow();
        }

        if (res.setStationary) {
            commandedSetStationary(res.newStationaryMode, SRC_SBUS);
        }

        if (res.setSpeedPreset) {
            applySpeedPresetRuntime(res.newSpeedPreset);
        }
    }
}
