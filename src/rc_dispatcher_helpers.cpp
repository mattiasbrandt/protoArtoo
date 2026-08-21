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

static bool queueServoSequence(uint8_t sequenceId) {
    ServoCommand cmd = {};
    cmd.type = SERVO_CMD_SEQUENCE;
    cmd.sequenceId = sequenceId;
    cmd.source = SRC_SBUS;
    cmd.timestampMs = millis();
    if (xQueueSend(servoCmdQueue, &cmd, 0) != pdTRUE) {
        logQueueDrop(QUEUE_SERVO_CMD, "servo sequence command");
        return false;
    }
    return true;
}

static bool queueServoCommand(uint8_t armId, ServoCommandType type, uint16_t positionUs) {
    ServoCommand cmd = {};
    cmd.armId = armId;
    cmd.type = type;
    cmd.positionUs = positionUs;
    cmd.source = SRC_SBUS;
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

void rcDispatchSingleAction(const RcActionResult& res) {
    if (res.audioTrack != 0) {
        if (!audioQueuePlayTrack(res.audioTrack, SRC_SBUS)) {
            PA_LOG_WARN(TAG, "audio track dropped: track=%u queue full", (unsigned)res.audioTrack);
        }
    }

    if (res.audioDollarCmd[0] != '\0') {
        if (!audioQueueDollar(res.audioDollarCmd, SRC_SBUS)) {
            PA_LOG_WARN(TAG, "droid sequence audio dropped: %s", res.audioDollarCmd);
        }
    }

    if (res.servoIndex >= 0) {
        if (res.servoIsSequence) {
            if (!queueServoSequence(res.servoSequenceId))
                PA_LOG_WARN(TAG, "droid sequence servo queue full: seq=%u",
                            (unsigned)res.servoSequenceId);
        } else {
            ServoCommandType cmd = res.servoOpen ? SERVO_CMD_OPEN : SERVO_CMD_CLOSE;
            queueServoCommand((uint8_t)res.servoIndex, cmd, 0);
        }
    }

    if (res.domeTxCmd[0] != '\0') {
        if (strncmp(res.domeTxCmd, "DM:", 3) == 0) {
            if (!sequenceStart(res.domeTxCmd, SRC_SBUS))
                PA_LOG_WARN(TAG, "sequence start failed: %s", res.domeTxCmd);
        } else if (domeConnected()) {
            if (!domeQueueTx(res.domeTxCmd))
                PA_LOG_WARN(TAG, "dome tx queue full: %s", res.domeTxCmd);
        }
    }

    if (res.marcduinoCmd[0] != '\0') {
        if (!parseMarcduinoCommand(res.marcduinoCmd)) {
            PA_LOG_DEBUG(TAG, "marcduino command not recognized: %s", res.marcduinoCmd);
        }
    }
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

        // Dispatch audio, servo, dome, marcduino commands
        rcDispatchSingleAction(res);

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
