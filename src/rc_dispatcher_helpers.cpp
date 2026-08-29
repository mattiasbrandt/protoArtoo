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

static bool queueServoSequence(uint8_t sequenceId, CommandSource src) {
    ServoCommand cmd = {};
    cmd.type = SERVO_CMD_SEQUENCE;
    cmd.sequenceId = sequenceId;
    cmd.source = src;
    cmd.timestampMs = millis();
    if (xQueueSend(servoCmdQueue, &cmd, 0) != pdTRUE) {
        logQueueDrop(QUEUE_SERVO_CMD, "servo sequence command");
        return false;
    }
    return true;
}

static bool queueServoCommand(uint8_t armId, ServoCommandType type, uint16_t positionUs,
                              CommandSource src) {
    ServoCommand cmd = {};
    cmd.armId = armId;
    cmd.type = type;
    cmd.positionUs = positionUs;
    cmd.source = src;
    cmd.timestampMs = millis();
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

    if (res.servoIndex >= 0) {
        if (res.servoIsSequence) {
            if (!queueServoSequence(res.servoSequenceId, src)) {
                PA_LOG_WARN(TAG, "droid sequence servo queue full: seq=%u",
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

    if (res.marcduinoCmd[0] != '\0') {
        if (!parseMarcduinoCommand(res.marcduinoCmd)) {
            PA_LOG_DEBUG(TAG, "marcduino command not recognized: %s", res.marcduinoCmd);
            // Not a queue-full - the payload itself failed validation. This
            // branch is unreachable for #220's in-scope action set
            // (DOME_ACTION_MARCDUINO_SEQ/CMD require an argument, fenced to
            // #221/#226); folded into queueFull here only so the live RC
            // path (which can reach it) still reports a non-silent outcome
            // rather than a new, #220-unused outcome value.
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
