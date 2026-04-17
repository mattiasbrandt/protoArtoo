// =============================================================================
// src/tasks/rc_input.cpp
//
// RcInputTask — handles all RC input modes: standard_pwm, single_sbus, dual_sbus.
// Renamed from sbus_input.cpp; the task was never SBUS-specific.
//
// Receiver #1 (PIN_SBUS1_RX = GPIO 15): Drive — CH1=speed, CH2=steer
// Receiver #2 (PIN_SBUS2_RX = GPIO 13): Dome spin
//
// SbusDecoder API:
//   .begin(pin)   — initialize RMT channel on pin; returns false if no channel free
//   .read()       — returns true when a new 25-byte frame is decoded
//   .data()       — returns SbusData{ch[16], failsafe, lost_frame}
//   ch[]          — 0-indexed, range SBUS_MIN(172)..SBUS_MAX(1811), center ~992
//
// Safety layers implemented here:
//   Layer 1: SBUS receiver hardware failsafe flag (data.failsafe)
//   Layer 2: SBUS software watchdog (SBUS_TIMEOUT_MS = 200 ms)
//
// Runtime drive output uses cfg_speedLimitMax and speed presets; no RC speed-limit axis.
// =============================================================================

#include <Arduino.h>
#include <esp_task_wdt.h>
#include <esp_system.h>

#include "../../include/config.h"
#include "../../include/audio_task.h"
#include "../../include/system_sounds.h"
#include "../../include/ledc_pwm.h"
#include "../../include/logging.h"
#include "../../include/dome_rx_parser.h"
#include "../../include/marcduino_helpers.h"
#include "../../include/dome_link.h"
#include "../../include/web_server.h"
#include "../../include/drive_speed_preset.h"
#include "../../include/rc_pwm_helpers.h"
#include "../../include/robot_state.h"
#include "../../include/sbus_decoder.h"
#include "../../include/sbus_math.h"

static const char* TAG = "RCInputTask";

// SBUS receiver objects — RMT-based, no hardware UART consumed.
// GPIO 15 (PIN_SBUS1_RX) and GPIO 13 (PIN_SBUS2_RX) are the SBUS receiver pins.
// SBUS1 and SBUS2 each occupy one RMT channel (3 memory blocks each).
// UART1 is now exclusively owned by DriveTask; UART2 by DomeLinkTask.
static SbusDecoder sbus_drive;
static SbusDecoder sbus_dome;
static const uint8_t kRcPwmPins[6] = {PIN_RC_CH1, PIN_RC_CH2, PIN_RC_CH3,
                                      PIN_RC_CH4, PIN_RC_CH5, PIN_RC_CH6};

struct RcRuntimeConfig {
    bool enableRc[6];
    bool enableDome;
    bool enableArm1;
    bool enableArm2;
    bool enableSound;
    int16_t maxOut;
    RcBindingConfig driveSpeed;
    RcBindingConfig driveSteer;
    RcBindingConfig domeSpeed;
    RcBindingConfig arm1;
    RcBindingConfig arm2;
    RcBindingConfig sound;
};

static bool bindingSourceActive(const RcBindingConfig& binding, RcInputMode mode, bool enableRcCh1,
                                bool enableRcCh2) {
    switch (binding.source) {
        case RC_BINDING_PWM:
            return mode == RC_INPUT_STANDARD_PWM && binding.channel >= 1 && binding.channel <= 6;
        case RC_BINDING_SBUS1:
            if (mode == RC_INPUT_SINGLE_SBUS) return true;
            return mode == RC_INPUT_DUAL_SBUS && enableRcCh1;
        case RC_BINDING_SBUS2:
            return mode == RC_INPUT_DUAL_SBUS && enableRcCh2;
        case RC_BINDING_NONE:
        default:
            return false;
    }
}

static void loadRcRuntimeConfig(RcRuntimeConfig* cfg, RcInputMode mode) {
    if (cfg == nullptr) {
        return;
    }

    taskENTER_CRITICAL(&robotStateMux);
    cfg->enableRc[0] = robotState.cfg_enable_rc_ch1;
    cfg->enableRc[1] = robotState.cfg_enable_rc_ch2;
    cfg->enableRc[2] = robotState.cfg_enable_rc_ch3;
    cfg->enableRc[3] = robotState.cfg_enable_rc_ch4;
    cfg->enableRc[4] = robotState.cfg_enable_rc_ch5;
    cfg->enableRc[5] = robotState.cfg_enable_rc_ch6;
    cfg->enableDome = robotState.cfg_enable_dome;
    cfg->enableArm1 = robotState.cfg_enable_arm1;
    cfg->enableArm2 = robotState.cfg_enable_arm2;
    cfg->enableSound = robotState.cfg_enable_s2_sound;
    cfg->maxOut = robotState.cfg_speedLimitMax;

    if (mode == RC_INPUT_STANDARD_PWM) {
        cfg->driveSpeed = robotState.cfg_rc_pwm_drive_speed;
        cfg->driveSteer = robotState.cfg_rc_pwm_drive_steer;
        cfg->domeSpeed = robotState.cfg_rc_pwm_dome_speed;
        cfg->arm1 = robotState.cfg_rc_pwm_arm1;
        cfg->arm2 = robotState.cfg_rc_pwm_arm2;
        cfg->sound = robotState.cfg_rc_pwm_sound;
    } else {
        cfg->driveSpeed = robotState.cfg_rc_sbus_drive_speed;
        cfg->driveSteer = robotState.cfg_rc_sbus_drive_steer;
        cfg->domeSpeed = robotState.cfg_rc_sbus_dome_speed;
        cfg->arm1 = robotState.cfg_rc_sbus_arm1;
        cfg->arm2 = robotState.cfg_rc_sbus_arm2;
        cfg->sound = robotState.cfg_rc_sbus_sound;
    }
    taskEXIT_CRITICAL(&robotStateMux);
}

static bool readPwmBindingRaw(const RcBindingConfig& binding, const uint32_t pulses[6], int* raw) {
    if (raw == nullptr || binding.source != RC_BINDING_PWM || binding.channel < 1 ||
        binding.channel > 6) {
        return false;
    }

    uint32_t pulse = pulses[binding.channel - 1];
    if (!rcPwmPulseIsValid(pulse)) {
        return false;
    }

    *raw = (int)pulse;
    return true;
}

static bool readSbusAnalog(const SbusData& data, const RcBindingConfig& binding, int* raw) {
    if (raw == nullptr || !rcBindingSupportsAnalog(binding)) {
        return false;
    }

    *raw = data.ch[binding.channel - 1];
    return true;
}

static bool readSbusDigital(const SbusData& data, const RcBindingConfig& binding, bool* pressed) {
    if (pressed == nullptr || !rcBindingIsDigital(binding)) {
        return false;
    }

    if (binding.channel == 17) {
        *pressed = data.ch17;
        return true;
    }
    if (binding.channel == 18) {
        *pressed = data.ch18;
        return true;
    }
    return false;
}

static void storePwmDiagnostics(const uint32_t pulses[6], const bool enabled[6]) {
    bool anyValid = false;
    uint32_t now = millis();

    taskENTER_CRITICAL(&robotStateMux);
    for (int i = 0; i < 6; ++i) {
        uint32_t pulse = pulses[i];
        bool valid = enabled[i] && rcPwmPulseIsValid(pulse);
        robotState.rcPwmPulseUs[i] = pulse > 0xFFFFu ? 0xFFFFu : (uint16_t)pulse;
        robotState.rcPwmPulseValid[i] = valid;
        anyValid = anyValid || valid;
    }
    if (anyValid) {
        robotState.lastPwmMs = now;
    }
    taskEXIT_CRITICAL(&robotStateMux);
}

static bool queueDomeCommand(float speed, CommandSource source) {
    DomeCommand domeCmd = {};
    domeCmd.speed = speed;
    domeCmd.source = source;
    domeCmd.timestampMs = millis();
    if (xQueueSend(domeCmdQueue, &domeCmd, 0) != pdTRUE) {
        taskENTER_CRITICAL(&robotStateMux);
        robotState.queueOverflowCount++;
        taskEXIT_CRITICAL(&robotStateMux);
        return false;
    }
    return true;
}

static bool queueServoCommand(uint8_t armId, ServoCommandType type, uint16_t positionUs,
                              CommandSource source) {
    ServoCommand cmd = {};
    cmd.armId = armId;
    cmd.type = type;
    cmd.positionUs = positionUs;
    cmd.source = source;
    cmd.timestampMs = millis();
    if (xQueueSend(servoCmdQueue, &cmd, 0) != pdTRUE) {
        taskENTER_CRITICAL(&robotStateMux);
        robotState.queueOverflowCount++;
        taskEXIT_CRITICAL(&robotStateMux);
        return false;
    }
    return true;
}

static void dispatchSwitchAction(const RcBindingConfig& binding, int raw, uint8_t armId,
                                 RcSwitchState* lastState) {
    if (lastState == nullptr) {
        return;
    }

    RcSwitchState state = rcAnalogToSwitchState(raw, binding);
    if (state == RC_SWITCH_INVALID || state == *lastState) {
        return;
    }

    if (state == RC_SWITCH_HIGH) {
        queueServoCommand(armId, SERVO_CMD_OPEN, 0, SRC_SBUS);
    } else if (state == RC_SWITCH_LOW) {
        queueServoCommand(armId, SERVO_CMD_CLOSE, 0, SRC_SBUS);
    } else {
        queueServoCommand(armId, SERVO_CMD_POSITION, SERVO_PULSE_NEUTRAL_US, SRC_SBUS);
    }
    *lastState = state;
}

static void dispatchDigitalAction(bool pressed, uint8_t armId, bool* lastPressed) {
    if (lastPressed == nullptr || pressed == *lastPressed) {
        return;
    }

    if (pressed) {
        queueServoCommand(armId, SERVO_CMD_OPEN, 0, SRC_SBUS);
    } else {
        queueServoCommand(armId, SERVO_CMD_CLOSE, 0, SRC_SBUS);
    }
    *lastPressed = pressed;
}

static void handleSoundTrigger(bool pressed, bool* lastPressed) {
    if (lastPressed == nullptr) {
        return;
    }
    if (pressed && !*lastPressed) {
        parseMarcduinoCommand("$87");
    }
    *lastPressed = pressed;
}

// Tier 2 Trigger Binding Runtime State
struct TriggerRuntimeState {
    bool lastPressed;
    RcSwitchState lastSwitchState;
};

static TriggerRuntimeState g_triggerStates[11] = {};  // One per Tier 2 binding slot

static bool setStationaryMode(bool stationary) {
    bool queueDriveOn = false;
    bool wasStationary = false;
    taskENTER_CRITICAL(&robotStateMux);
    wasStationary = robotState.stationary;
    robotState.stationary = stationary;
    if (wasStationary && !stationary) {
        queueDriveOn = true;
    }
    taskEXIT_CRITICAL(&robotStateMux);
    if (!queueDriveOn) {
        return false;
    }
    return audioQueuePlaySlot(AUDIO_SLOT_SYS_DRIVE_ON, SRC_INTERNAL);
}


static bool queueRandomTrackForAction(RobotActionId target) {
    const char* categoryLabel = randomSoundCategoryLabel(target);
    uint16_t lo = 0;
    uint16_t hi = 0;
    taskENTER_CRITICAL(&robotStateMux);
    switch (target) {
        case SOUND_ACTION_RANDOM_GENERAL:
            lo = robotState.cfg_snd_cat_gen_lo;
            hi = robotState.cfg_snd_cat_gen_hi;
            break;
        case SOUND_ACTION_RANDOM_CHATTY:
            lo = robotState.cfg_snd_cat_chat_lo;
            hi = robotState.cfg_snd_cat_chat_hi;
            break;
        case SOUND_ACTION_RANDOM_HAPPY:
            lo = robotState.cfg_snd_cat_hap_lo;
            hi = robotState.cfg_snd_cat_hap_hi;
            break;
        case SOUND_ACTION_RANDOM_PROCESSING:
            lo = robotState.cfg_snd_cat_proc_lo;
            hi = robotState.cfg_snd_cat_proc_hi;
            break;
        case SOUND_ACTION_RANDOM_SAD:
            lo = robotState.cfg_snd_cat_sad_lo;
            hi = robotState.cfg_snd_cat_sad_hi;
            break;
        case SOUND_ACTION_RANDOM_SENTIMENTAL:
            lo = robotState.cfg_snd_cat_sent_lo;
            hi = robotState.cfg_snd_cat_sent_hi;
            break;
        case SOUND_ACTION_RANDOM_HUMMING:
            lo = robotState.cfg_snd_cat_hum_lo;
            hi = robotState.cfg_snd_cat_hum_hi;
            break;
        case SOUND_ACTION_RANDOM_SCREAM:
            lo = robotState.cfg_snd_cat_scrm_lo;
            hi = robotState.cfg_snd_cat_scrm_hi;
            break;
        case SOUND_ACTION_RANDOM_SURPRISED:
            lo = robotState.cfg_snd_cat_ooh_lo;
            hi = robotState.cfg_snd_cat_ooh_hi;
            break;
        case SOUND_ACTION_RANDOM_ALERT:
            lo = robotState.cfg_snd_cat_alrm_lo;
            hi = robotState.cfg_snd_cat_alrm_hi;
            break;
        case SOUND_ACTION_RANDOM_SNARKY:
            lo = robotState.cfg_snd_cat_snarky_lo;
            hi = robotState.cfg_snd_cat_snarky_hi;
            break;
        case SOUND_ACTION_RANDOM_WHISTLE:
            lo = robotState.cfg_snd_cat_whis_lo;
            hi = robotState.cfg_snd_cat_whis_hi;
            break;
        default:
            break;
    }
    taskEXIT_CRITICAL(&robotStateMux);

    if (categoryLabel == nullptr) {
        PA_LOG_WARN(TAG, "random sound trigger ignored: unknown category action=%u",
                    (unsigned)target);
        return false;
    }

    uint16_t track = 0;
    if (!selectRandomTrackInRange(lo, hi, esp_random(), &track)) {
        PA_LOG_WARN(TAG, "random sound trigger ignored: category=%s range=%u..%u inactive",
                    categoryLabel, (unsigned)lo, (unsigned)hi);
        return false;
    }
    if (!audioQueuePlayTrack(track, SRC_SBUS)) {
        PA_LOG_WARN(TAG, "random sound trigger dropped: category=%s track=%u queue full",
                    categoryLabel, (unsigned)track);
        return false;
    }
    PA_LOG_INFO(TAG, "random sound trigger: category=%s track=%u",
                categoryLabel, (unsigned)track);
    return true;
}

static bool queueServoSequence(uint8_t sequenceId, CommandSource source) {
    ServoCommand cmd = {};
    cmd.type = SERVO_CMD_SEQUENCE;
    cmd.sequenceId = sequenceId;
    cmd.source = source;
    cmd.timestampMs = millis();
    if (xQueueSend(servoCmdQueue, &cmd, 0) != pdTRUE) {
        taskENTER_CRITICAL(&robotStateMux);
        robotState.queueOverflowCount++;
        taskEXIT_CRITICAL(&robotStateMux);
        return false;
    }
    return true;
}

static void dispatchFullDroidSequence(int seqId) {
    FullDroidBodyAction bodyAction = marcduino_full_droid_body_actions(seqId);
    if (bodyAction.audioDollarCmd == nullptr && bodyAction.bodySeqId < 0) {
        PA_LOG_WARN(TAG, "droid sequence ignored: no body mapping for SE%02d", seqId);
        return;
    }

    if (bodyAction.audioDollarCmd != nullptr &&
        !audioQueueDollar(bodyAction.audioDollarCmd, SRC_SBUS)) {
        PA_LOG_WARN(TAG, "droid sequence audio dropped: %s", bodyAction.audioDollarCmd);
    }

    int queuedBodySeqId = -1;
    if (bodyAction.bodySeqId >= 30) {
        bool estop = false;
        taskENTER_CRITICAL(&robotStateMux);
        estop = robotState.estop;
        taskEXIT_CRITICAL(&robotStateMux);

        if (estop) {
            PA_LOG_WARN(TAG, "droid sequence servo blocked by estop: SE%02d -> body SE%d",
                        seqId, bodyAction.bodySeqId);
        } else if (!queueServoSequence((uint8_t)bodyAction.bodySeqId, SRC_SBUS)) {
            PA_LOG_WARN(TAG, "droid sequence servo queue full: SE%02d -> body SE%d", seqId,
                        bodyAction.bodySeqId);
        } else {
            queuedBodySeqId = bodyAction.bodySeqId;
        }
    }

    const char* domeStatus = "disconnected";
    if (domeConnected()) {
        char cmd[8];
        snprintf(cmd, sizeof(cmd), ":SE%02d", seqId);
        if (domeQueueTx(cmd)) {
            domeStatus = "forwarded";
        } else {
            domeStatus = "queue_full";
            PA_LOG_WARN(TAG, "droid sequence dome queue full: %s", cmd);
        }
    }

    PA_LOG_INFO(TAG, "droid sequence trigger: SE%02d audio=%s body_seq=%d dome=%s", seqId,
                bodyAction.audioDollarCmd != nullptr ? bodyAction.audioDollarCmd : "none",
                queuedBodySeqId, domeStatus);
}

static void processTriggerAction(RobotActionId target, const char* payload, bool pressed) {
    switch (target) {
        case ROBOT_ACTION_NONE:
            break;
        case SERVO_ACTION_ARM1_TOGGLE:
            if (pressed) {
                queueServoCommand(0, SERVO_CMD_OPEN, 0, SRC_SBUS);
            } else {
                queueServoCommand(0, SERVO_CMD_CLOSE, 0, SRC_SBUS);
            }
            break;
        case SERVO_ACTION_ARM2_TOGGLE:
            if (pressed) {
                queueServoCommand(1, SERVO_CMD_OPEN, 0, SRC_SBUS);
            } else {
                queueServoCommand(1, SERVO_CMD_CLOSE, 0, SRC_SBUS);
            }
            break;
        case SERVO_ACTION_AUX1_TOGGLE:
            if (pressed) {
                queueServoCommand(2, SERVO_CMD_OPEN, 0, SRC_SBUS);
            } else {
                queueServoCommand(2, SERVO_CMD_CLOSE, 0, SRC_SBUS);
            }
            break;
        case SERVO_ACTION_AUX2_TOGGLE:
            if (pressed) {
                queueServoCommand(3, SERVO_CMD_OPEN, 0, SRC_SBUS);
            } else {
                queueServoCommand(3, SERVO_CMD_CLOSE, 0, SRC_SBUS);
            }
            break;
        case SERVO_ACTION_AUX3_TOGGLE:
            if (pressed) {
                queueServoCommand(4, SERVO_CMD_OPEN, 0, SRC_SBUS);
            } else {
                queueServoCommand(4, SERVO_CMD_CLOSE, 0, SRC_SBUS);
            }
            break;
        case DOME_ACTION_MARCDUINO_SEQ:
            if (pressed && rcPayloadValidForBodySequence(payload)) {
                char cmd[20];
                snprintf(cmd, sizeof(cmd), ":SE%s", payload);
                parseMarcduinoCommand(cmd);
            }
            break;
        case DOME_ACTION_MARCDUINO_CMD:
            if (pressed && rcPayloadValidForMarcduinoCommand(payload)) {
                parseMarcduinoCommand(payload);
            }
            break;
        case SOUND_ACTION_RANDOM_GENERAL:
        case SOUND_ACTION_RANDOM_CHATTY:
        case SOUND_ACTION_RANDOM_HAPPY:
        case SOUND_ACTION_RANDOM_PROCESSING:
        case SOUND_ACTION_RANDOM_SAD:
        case SOUND_ACTION_RANDOM_SENTIMENTAL:
        case SOUND_ACTION_RANDOM_HUMMING:
        case SOUND_ACTION_RANDOM_SCREAM:
        case SOUND_ACTION_RANDOM_SURPRISED:
        case SOUND_ACTION_RANDOM_ALERT:
        case SOUND_ACTION_RANDOM_SNARKY:
        case SOUND_ACTION_RANDOM_WHISTLE:
            if (pressed) {
                queueRandomTrackForAction(target);
            }
            break;
        case DROID_SEQ_SCREAM:
        case DROID_SEQ_WAVE:
        case DROID_SEQ_FAST_WAVE:
        case DROID_SEQ_OPEN_WAVE:
        case DROID_SEQ_BEEP_CANTINA:
        case DROID_SEQ_FAINT:
        case DROID_SEQ_CANTINA:
        case DROID_SEQ_LEIA:
        case DROID_SEQ_DISCO:
        case DROID_SEQ_SCREAMS:
        case DROID_SEQ_WIGGLE:
            if (pressed) {
                int seqId = robotActionIdToDroidSeqId(target);
                if (seqId > 0) {
                    dispatchFullDroidSequence(seqId);
                }
            }
            break;
        case SYSTEM_ACTION_ESTOP:
            if (pressed) {
                taskENTER_CRITICAL(&robotStateMux);
                robotState.estop = true;
                robotState.failsafeSource = FS_ESTOP_CMD;
                taskEXIT_CRITICAL(&robotStateMux);
            }
            break;
        case SYSTEM_ACTION_SLEEP_TOGGLE:
            if (pressed) {
                const uint32_t nowMs = millis();
                bool sleepMode = false;
                taskENTER_CRITICAL(&robotStateMux);
                sleepMode = !robotState.sleepMode;
                robotState.sleepMode = sleepMode;
                robotState.sleepSinceMs = sleepMode ? nowMs : 0U;
                taskEXIT_CRITICAL(&robotStateMux);

                requestStatusBroadcastNow();
                if (domeConnected()) {
                    domeQueueTx(sleepMode ? "#PASL" : "#PAWU");
                }
            }
            break;
        case SYSTEM_ACTION_OP_MODE:
            setStationaryMode(pressed);  // HIGH = Stationary, LOW = Driving
            break;
        case DRIVE_ACTION_SPEED_PRESET_CYCLE:
            if (pressed) {
                SpeedPresetId current = SpeedPresetId::Normal;
                taskENTER_CRITICAL(&robotStateMux);
                current = normalizeSpeedPresetId((uint8_t)robotState.cfg_speedPresetActive);
                taskEXIT_CRITICAL(&robotStateMux);
                applySpeedPresetRuntime(nextSpeedPreset(current));
            }
            break;
        case DOME_ACTION_SEQ:
            // Phase 4: Route to DomeLinkTask
            break;
        default:
            break;
    }
}

void dispatchRcTriggerActionTest(RobotActionId target, const char* payload, bool pressed) {
    processTriggerAction(target, payload, pressed);
}

static void processTier2Trigger(const RcTriggerBinding& binding, int rawValue,
                                TriggerRuntimeState& state) {
    if (binding.target == ROBOT_ACTION_NONE || binding.source == RC_BINDING_NONE) {
        return;
    }

    // Tier 2 only supports button/switch actions - analog targets are backbone-only
    if (!robotActionValidForTier2(binding.target)) {
        return;
    }

    bool pressed = false;
    bool stateChanged = false;

    if (robotActionIsButton(binding.target)) {
        // Button targets use switch state with edge detection
        RcSwitchState switchState = rcTriggerToSwitchState(rawValue, binding);
        if (switchState != RC_SWITCH_INVALID && switchState != state.lastSwitchState) {
            pressed = (switchState == RC_SWITCH_HIGH);
            stateChanged = true;
            state.lastSwitchState = switchState;
        }
    }

    if (stateChanged) {
        processTriggerAction(binding.target, binding.marcduinoPayload, pressed);
        state.lastPressed = pressed;
    }
}

static void loadTier2TriggerBindings(RcTriggerBinding* bindings, size_t* count) {
    if (bindings == nullptr || count == nullptr) {
        return;
    }

    taskENTER_CRITICAL(&robotStateMux);
    bindings[0] = robotState.cfg_rc_arm1;
    bindings[1] = robotState.cfg_rc_arm2;
    bindings[2] = robotState.cfg_rc_aux1;
    bindings[3] = robotState.cfg_rc_aux2;
    bindings[4] = robotState.cfg_rc_aux3;
    bindings[5] = robotState.cfg_rc_sound;
    bindings[6] = robotState.cfg_rc_opmode;
    bindings[7] = robotState.cfg_rc_free0;
    bindings[8] = robotState.cfg_rc_free1;
    bindings[9] = robotState.cfg_rc_free2;
    bindings[10] = robotState.cfg_rc_free3;
    *count = 11;
    taskEXIT_CRITICAL(&robotStateMux);
}

// Helper to convert RcTriggerBinding to RcBindingConfig for existing functions
static RcBindingConfig triggerToBackbone(const RcTriggerBinding& trigger) {
    return makeRcBindingConfig(trigger.source, trigger.channel, trigger.min, trigger.center,
                               trigger.max, trigger.deadband, trigger.reverse);
}

static bool readSbusAnalogTrigger(const SbusData& data, const RcTriggerBinding& binding, int* raw) {
    if (raw == nullptr || binding.source == RC_BINDING_NONE) {
        return false;
    }
    RcBindingConfig backbone = triggerToBackbone(binding);
    return readSbusAnalog(data, backbone, raw);
}

static bool readSbusDigitalTrigger(const SbusData& data, const RcTriggerBinding& binding,
                                   bool* pressed) {
    if (pressed == nullptr || binding.source == RC_BINDING_NONE) {
        return false;
    }
    RcBindingConfig backbone = triggerToBackbone(binding);
    return readSbusDigital(data, backbone, pressed);
}

static void dispatchStandardPwmInputs() {
    uint32_t pulses[6] = {};
    RcRuntimeConfig cfg = {};
    loadRcRuntimeConfig(&cfg, RC_INPUT_STANDARD_PWM);

    // Time-bounded pulse reading: limit total time spent to maintain ~20ms loop cadence
    const uint32_t startMs = millis();
    const uint32_t maxDurationMs = 15;  // Leave 5ms headroom for processing
    for (int i = 0; i < 6; i++) {
        if (!cfg.enableRc[i])
            continue;
        // Check time budget before each pulseIn to prevent exceeding loop cadence
        if (millis() - startMs >= maxDurationMs) {
            break;  // Skip remaining channels to maintain timing
        }
        pinMode(kRcPwmPins[i], INPUT);
        pulses[i] = pulseIn(kRcPwmPins[i], HIGH, 25000);
    }

    // Capture timestamp BEFORE storePwmDiagnostics updates it
    const uint32_t pwmCheckMs = millis();

    storePwmDiagnostics(pulses, cfg.enableRc);

    // PWM failsafe: check if we have recent valid PWM input before processing drive commands
    bool pwmSignalLost = false;
    taskENTER_CRITICAL(&robotStateMux);
    pwmSignalLost =
        pwmSignalLostCheck(robotState.lastPwmMs, pwmCheckMs, robotState.cfg_sbusTimeoutMs);
    taskEXIT_CRITICAL(&robotStateMux);

    if (pwmSignalLost) {
        // Skip drive command processing when PWM signal is lost or stale
        return;
    }

    int rawSpeed = 0;
    int rawSteer = 0;
    bool speedActive = bindingSourceActive(cfg.driveSpeed, RC_INPUT_STANDARD_PWM, cfg.enableRc[0],
                                           cfg.enableRc[1]) &&
                       readPwmBindingRaw(cfg.driveSpeed, pulses, &rawSpeed) &&
                       cfg.enableRc[cfg.driveSpeed.channel - 1];
    bool steerActive = bindingSourceActive(cfg.driveSteer, RC_INPUT_STANDARD_PWM, cfg.enableRc[0],
                                           cfg.enableRc[1]) &&
                       readPwmBindingRaw(cfg.driveSteer, pulses, &rawSteer) &&
                       cfg.enableRc[cfg.driveSteer.channel - 1];

    if (speedActive && steerActive) {
        int16_t maxOut = cfg.maxOut;
        setStationaryMode(false);
        setDriveCommand(
            (int16_t)(applyRcAnalogCalibration(rawSpeed, cfg.driveSpeed, nullptr) * maxOut),
            (int16_t)(applyRcAnalogCalibration(rawSteer, cfg.driveSteer, nullptr) * maxOut),
            SRC_SBUS);
    }

    int rawDome = 0;
    if (cfg.enableDome &&
        bindingSourceActive(cfg.domeSpeed, RC_INPUT_STANDARD_PWM, cfg.enableRc[0],
                            cfg.enableRc[1]) &&
        readPwmBindingRaw(cfg.domeSpeed, pulses, &rawDome) &&
        cfg.enableRc[cfg.domeSpeed.channel - 1]) {
        queueDomeCommand(applyRcAnalogCalibration(rawDome, cfg.domeSpeed, nullptr), SRC_SBUS);
    }

    static RcSwitchState lastArm1State = RC_SWITCH_MID;
    static RcSwitchState lastArm2State = RC_SWITCH_MID;
    static bool lastSoundPressed = false;

    int rawArm1 = 0;
    int rawArm2 = 0;
    int rawSound = 0;
    if (cfg.enableArm1 &&
        bindingSourceActive(cfg.arm1, RC_INPUT_STANDARD_PWM, cfg.enableRc[0], cfg.enableRc[1]) &&
        readPwmBindingRaw(cfg.arm1, pulses, &rawArm1) && cfg.enableRc[cfg.arm1.channel - 1]) {
        dispatchSwitchAction(cfg.arm1, rawArm1, 0, &lastArm1State);
    }
    if (cfg.enableArm2 &&
        bindingSourceActive(cfg.arm2, RC_INPUT_STANDARD_PWM, cfg.enableRc[0], cfg.enableRc[1]) &&
        readPwmBindingRaw(cfg.arm2, pulses, &rawArm2) && cfg.enableRc[cfg.arm2.channel - 1]) {
        dispatchSwitchAction(cfg.arm2, rawArm2, 1, &lastArm2State);
    }
    if (cfg.enableSound &&
        bindingSourceActive(cfg.sound, RC_INPUT_STANDARD_PWM, cfg.enableRc[0], cfg.enableRc[1]) &&
        readPwmBindingRaw(cfg.sound, pulses, &rawSound) && cfg.enableRc[cfg.sound.channel - 1]) {
        handleSoundTrigger(rcAnalogToSwitchState(rawSound, cfg.sound) == RC_SWITCH_HIGH,
                           &lastSoundPressed);
    }
}

static void dispatchSbusBindingsForSource(const SbusData& data, RcBindingSource source,
                                          RcInputMode mode, bool enableRcCh1, bool enableRcCh2) {
    RcRuntimeConfig cfg = {};
    loadRcRuntimeConfig(&cfg, mode);
    static RcSwitchState lastArm1Switch = RC_SWITCH_MID;
    static RcSwitchState lastArm2Switch = RC_SWITCH_MID;
    static bool lastArm1Digital = false;
    static bool lastArm2Digital = false;
    static bool lastSoundPressed = false;

    auto sourceActive = [&](const RcBindingConfig& binding) {
        return binding.source == source &&
               bindingSourceActive(binding, mode, enableRcCh1, enableRcCh2);
    };

    int raw = 0;
    bool pressed = false;

    if (cfg.enableDome && sourceActive(cfg.domeSpeed) &&
        readSbusAnalog(data, cfg.domeSpeed, &raw)) {
        queueDomeCommand(applyRcAnalogCalibration(raw, cfg.domeSpeed, nullptr), SRC_SBUS);
    }

    if (cfg.enableArm1 && sourceActive(cfg.arm1)) {
        if (readSbusDigital(data, cfg.arm1, &pressed)) {
            dispatchDigitalAction(pressed, 0, &lastArm1Digital);
        } else if (readSbusAnalog(data, cfg.arm1, &raw)) {
            dispatchSwitchAction(cfg.arm1, raw, 0, &lastArm1Switch);
        }
    }

    if (cfg.enableArm2 && sourceActive(cfg.arm2)) {
        if (readSbusDigital(data, cfg.arm2, &pressed)) {
            dispatchDigitalAction(pressed, 1, &lastArm2Digital);
        } else if (readSbusAnalog(data, cfg.arm2, &raw)) {
            dispatchSwitchAction(cfg.arm2, raw, 1, &lastArm2Switch);
        }
    }

    if (cfg.enableSound && sourceActive(cfg.sound)) {
        if (readSbusDigital(data, cfg.sound, &pressed)) {
            handleSoundTrigger(pressed, &lastSoundPressed);
        } else if (readSbusAnalog(data, cfg.sound, &raw)) {
            handleSoundTrigger(rcAnalogToSwitchState(raw, cfg.sound) == RC_SWITCH_HIGH,
                               &lastSoundPressed);
        }
    }

    // Process Tier 2 Trigger Bindings
    RcTriggerBinding tier2Bindings[11];
    size_t tier2Count = 0;
    loadTier2TriggerBindings(tier2Bindings, &tier2Count);

    for (size_t i = 0; i < tier2Count; ++i) {
        if (tier2Bindings[i].source != source || tier2Bindings[i].target == ROBOT_ACTION_NONE) {
            continue;
        }

        int rawValue = 0;
        RcBindingConfig backbone = triggerToBackbone(tier2Bindings[i]);
        if (rcBindingIsDigital(backbone)) {
            bool digitalPressed = false;
            if (readSbusDigitalTrigger(data, tier2Bindings[i], &digitalPressed)) {
                if (digitalPressed != g_triggerStates[i].lastPressed) {
                    processTriggerAction(tier2Bindings[i].target, tier2Bindings[i].marcduinoPayload,
                                         digitalPressed);
                    g_triggerStates[i].lastPressed = digitalPressed;
                }
            }
        } else if (readSbusAnalogTrigger(data, tier2Bindings[i], &rawValue)) {
            processTier2Trigger(tier2Bindings[i], rawValue, g_triggerStates[i]);
        }
    }
}

static bool is_drive_sbus_mode(RcInputMode mode) {
    return mode == RC_INPUT_SINGLE_SBUS || mode == RC_INPUT_DUAL_SBUS;
}

static bool is_dome_sbus_mode(RcInputMode mode) {
    return mode == RC_INPUT_DUAL_SBUS;
}

// -----------------------------------------------------------------------------
// rcInputTask()
// Handles all RC input modes: standard_pwm, single_sbus, dual_sbus.
// Polls receivers at ~200 Hz (5 ms delay) to catch every 100 Hz SBUS frame.
// Implements Layer 1 (HW failsafe flag) and Layer 2 (SW watchdog) safety.
// Thread safety: all RobotState writes use taskENTER/EXIT_CRITICAL.
// -----------------------------------------------------------------------------
void rcInputTask(void* pvParameters) {
    // Register with TWDT unconditionally — this task feeds the watchdog
    // regardless of which RC mode is active or what channels are enabled.
    esp_task_wdt_add(NULL);

    taskENTER_CRITICAL(&robotStateMux);
    RcInputMode rcInputMode = robotState.cfg_rc_input_mode;
    bool enableRcCh1 = robotState.cfg_enable_rc_ch1;
    bool enableRcCh2 = robotState.cfg_enable_rc_ch2;
    bool useCh2 = robotState.cfg_single_sbus_use_ch2;
    taskEXIT_CRITICAL(&robotStateMux);

    bool driveSbusEnabled = is_drive_sbus_mode(rcInputMode) &&
                               (rcInputMode == RC_INPUT_SINGLE_SBUS || enableRcCh1);
    bool domeSbusEnabled = is_dome_sbus_mode(rcInputMode) && enableRcCh2;

    // Do not early-idle when SBUS channels are currently disabled:
    // rcInputMode and channel enables are runtime-configurable via /api/config.
    // This task must keep running so mode/channel changes take effect without reboot.

    if (driveSbusEnabled) {
        bool useDriveSbus2 = (rcInputMode == RC_INPUT_SINGLE_SBUS) && useCh2;
        int sbusRxPin = useDriveSbus2 ? PIN_SBUS2_RX : PIN_SBUS1_RX;
        if (!sbus_drive.begin(sbusRxPin)) {
            PA_LOG_ERROR(TAG, "RMT init failed for SBUS%d GPIO%d", useDriveSbus2 ? 2 : 1,
                         sbusRxPin);
            driveSbusEnabled = false;
        }
    }
    if (domeSbusEnabled) {
        if (!sbus_dome.begin(PIN_SBUS2_RX)) {
            PA_LOG_WARN(TAG, "RMT init failed for SBUS2 GPIO%d", PIN_SBUS2_RX);
            domeSbusEnabled = false;
        }
    }

    if (rcInputMode == RC_INPUT_STANDARD_PWM) {
        PA_LOG_INFO(TAG, "started — standard_pwm mode, SBUS decoders inactive");
    } else if (rcInputMode == RC_INPUT_SINGLE_SBUS) {
        if (driveSbusEnabled) {
            int sbusRxPin = useCh2 ? PIN_SBUS2_RX : PIN_SBUS1_RX;
            PA_LOG_INFO(TAG, "started — single_sbus mode, SBUS%d GPIO%d active",
                        useCh2 ? 2 : 1, sbusRxPin);
        } else {
            PA_LOG_INFO(TAG,
                        "started — single_sbus mode, SBUS%d disabled (en_rc_ch1=false) — idle",
                        useCh2 ? 2 : 1);
        }
    } else {
        if (!driveSbusEnabled)
            PA_LOG_INFO(TAG, "started — dual_sbus mode, SBUS2 GPIO%d only (SBUS1 disabled)",
                        PIN_SBUS2_RX);
        else if (!domeSbusEnabled)
            PA_LOG_INFO(TAG, "started — dual_sbus mode, SBUS1 GPIO%d only (SBUS2 disabled)",
                        PIN_SBUS1_RX);
        else
            PA_LOG_INFO(TAG, "started — dual_sbus mode, SBUS1 GPIO%d + SBUS2 GPIO%d active",
                        PIN_SBUS1_RX, PIN_SBUS2_RX);
    }

    // SBUS2 watchdog state
    bool sbus2WatchdogFired = false;
    bool hwmLogged = false;

    bool driveSbusInitWarned = false;
    bool domeSbusInitWarned = false;
    bool lastUseCh2 = useCh2;
    uint32_t lastSbusDiagLogMs = 0;
    constexpr uint32_t kWatchdogDiagIntervalMs = 5000U;
    uint32_t lastSbus1WatchdogDiagMs = 0;
    uint32_t lastSbus2WatchdogDiagMs = 0;
    while (true) {
        if (!hwmLogged) {
            PA_LOG_INFO(TAG, "stack HWM: %u words free",
                        (unsigned)uxTaskGetStackHighWaterMark(NULL));
            hwmLogged = true;
        }

        taskENTER_CRITICAL(&robotStateMux);
        rcInputMode = robotState.cfg_rc_input_mode;
        enableRcCh1 = robotState.cfg_enable_rc_ch1;
        enableRcCh2 = robotState.cfg_enable_rc_ch2;
        useCh2 = robotState.cfg_single_sbus_use_ch2;
        taskEXIT_CRITICAL(&robotStateMux);
        driveSbusEnabled = is_drive_sbus_mode(rcInputMode) &&
                           (rcInputMode == RC_INPUT_SINGLE_SBUS || enableRcCh1);
        domeSbusEnabled = is_dome_sbus_mode(rcInputMode) && enableRcCh2;

        // Detect single_sbus receiver selection change BEFORE the reinit guard.
        // If change detection ran after reinit, we could init-then-teardown the decoder
        // in the same iteration when useCh2 changes while the decoder is uninitialized.
        if (rcInputMode == RC_INPUT_SINGLE_SBUS && useCh2 != lastUseCh2
                && sbus_drive.isInitialized()) {
            sbus_drive.end();
            PA_LOG_INFO(TAG, "single_sbus receiver changed to SBUS%d \u2014 reinitializing",
                         useCh2 ? 2 : 1);
        }
        // Only track lastUseCh2 while in single_sbus mode. Freezing the baseline across
        // mode transitions prevents a spurious reinit when returning to single_sbus after
        // useCh2 was changed while in dual_sbus or standard_pwm mode.
        if (rcInputMode == RC_INPUT_SINGLE_SBUS) {
            lastUseCh2 = useCh2;
        }

        if (driveSbusEnabled && !sbus_drive.isInitialized()) {
            int sbusRxPin =
                (rcInputMode == RC_INPUT_SINGLE_SBUS && useCh2) ? PIN_SBUS2_RX : PIN_SBUS1_RX;
            bool useDriveSbus2 = (sbusRxPin == PIN_SBUS2_RX);
            if (!sbus_drive.begin(sbusRxPin)) {
                if (!driveSbusInitWarned) {
                    PA_LOG_ERROR(TAG, "RMT init failed for SBUS%d GPIO%d", useDriveSbus2 ? 2 : 1,
                                 sbusRxPin);
                    driveSbusInitWarned = true;
                }
                driveSbusEnabled = false;
            } else {
                driveSbusInitWarned = false;
            }
        }

        if (domeSbusEnabled && !sbus_dome.isInitialized()) {
            if (!sbus_dome.begin(PIN_SBUS2_RX)) {
                if (!domeSbusInitWarned) {
                    PA_LOG_WARN(TAG, "RMT init failed for SBUS2 GPIO%d", PIN_SBUS2_RX);
                    domeSbusInitWarned = true;
                }
                domeSbusEnabled = false;
            } else {
                domeSbusInitWarned = false;
            }
        }

        if (!driveSbusEnabled && sbus_drive.isInitialized()) {
            sbus_drive.end();
            driveSbusInitWarned = false;
            PA_LOG_INFO(TAG, "SBUS1 disabled — RMT channel released");
        }

        if (!domeSbusEnabled && sbus_dome.isInitialized()) {
            sbus_dome.end();
            domeSbusInitWarned = false;
            sbus2WatchdogFired = false;
            PA_LOG_INFO(TAG, "SBUS2 disabled — RMT channel released");
        }

        if (rcInputMode == RC_INPUT_STANDARD_PWM) {
            taskENTER_CRITICAL(&robotStateMux);
            robotState.sbusSignalLost = false;
            robotState.sbusHwFailsafe = false;
            robotState.sbus2SignalLost = false;
            robotState.sbus2HwFailsafe = false;
            taskEXIT_CRITICAL(&robotStateMux);
            dispatchStandardPwmInputs();
            esp_task_wdt_reset();
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        // --- Drive receiver (SBUS #1) ---
        if (driveSbusEnabled && sbus_drive.read()) {
            SbusData data = sbus_drive.data();
            RcRuntimeConfig cfg = {};
            loadRcRuntimeConfig(&cfg, rcInputMode);

            taskENTER_CRITICAL(&robotStateMux);
            robotState.lastSbus1Ms = millis();
            for (int i = 0; i < 16; ++i) {
                robotState.rcSbus1Raw[i] = (uint16_t)data.ch[i];
            }
            robotState.rcSbus1Digital[0] = data.ch17;
            robotState.rcSbus1Digital[1] = data.ch18;

            // Layer 1: Hardware failsafe flag from receiver firmware
            if (data.failsafe) {
                bool hwFailsafeTriggered = !robotState.sbusHwFailsafe;
                robotState.sbusHwFailsafe = true;
                if (hwFailsafeTriggered) {
                    recordFailsafeTriggerLocked(FS_SBUS_HW, robotState.lastSbus1Ms);
                } else {
                    robotState.failsafeSource = FS_SBUS_HW;
                }
                robotState.driveSpeed = 0;
                robotState.driveSteer = 0;
                robotState.lastDriveSource = SRC_SBUS;
                taskEXIT_CRITICAL(&robotStateMux);
                if (hwFailsafeTriggered) {
                    PA_LOG_WARN(TAG, "SBUS1 hardware failsafe asserted");
                }
            } else if (data.lost_frame) {
                // lost_frame: single frame missed — not a failsafe condition.
                // Track count; drive state is unchanged (last good frame holds).
                robotState.sbus1LostFrameCount++;
                uint32_t lostCount = robotState.sbus1LostFrameCount;
                bool rcDebug = robotState.rcDebugMode;
                taskEXIT_CRITICAL(&robotStateMux);
                if (rcDebug && (lostCount % 100 == 0)) {
                    PA_LOG_DEBUG(TAG, "SBUS1 lost_frame count: %lu", (unsigned long)lostCount);
                }
            } else {
                // Signal confirmed — clear watchdog and hardware failsafe flags
                robotState.sbusHwFailsafe = false;
                robotState.sbusSignalLost = false;
                taskEXIT_CRITICAL(&robotStateMux);

                int rawSpeed = 0;
                int rawSteer = 0;
                if (cfg.driveSpeed.source == RC_BINDING_SBUS1 &&
                    cfg.driveSteer.source == RC_BINDING_SBUS1 &&
                    readSbusAnalog(data, cfg.driveSpeed, &rawSpeed) &&
                    readSbusAnalog(data, cfg.driveSteer, &rawSteer)) {
                    int16_t maxOut = cfg.maxOut;
                    setStationaryMode(false);
                    setDriveCommand(constrain((int16_t)(applyRcAnalogCalibration(
                                                            rawSpeed, cfg.driveSpeed, nullptr) *
                                                        maxOut),
                                              (int16_t)(-maxOut), maxOut),
                                    constrain((int16_t)(applyRcAnalogCalibration(
                                                            rawSteer, cfg.driveSteer, nullptr) *
                                                        maxOut),
                                              (int16_t)(-maxOut), maxOut),
                                    SRC_SBUS);
                }

                dispatchSbusBindingsForSource(data, RC_BINDING_SBUS1, rcInputMode, enableRcCh1,
                                              enableRcCh2);
            }
        }

        // Layer 2: SBUS software watchdog — fires if no valid frame for SBUS_TIMEOUT_MS
        taskENTER_CRITICAL(&robotStateMux);
        uint32_t lastSbus1 = robotState.lastSbus1Ms;
        uint32_t timeoutMs = robotState.cfg_sbusTimeoutMs;
        taskEXIT_CRITICAL(&robotStateMux);

        if (driveSbusEnabled) {
            bool watchdogFired = false;
            if ((uint32_t)(millis() - lastSbus1) > timeoutMs) {
                uint32_t nowMs = millis();
                taskENTER_CRITICAL(&robotStateMux);
                if (!robotState.sbusSignalLost) {
                    robotState.sbusSignalLost = true;
                    recordFailsafeTriggerLocked(FS_SBUS_TIMEOUT, nowMs);
                    robotState.driveSpeed = 0;
                    robotState.driveSteer = 0;
                    watchdogFired = true;
                }
                taskEXIT_CRITICAL(&robotStateMux);
                if (watchdogFired) {
                    PA_LOG_WARN(TAG, "SBUS1 watchdog fired - no frame for %lu ms (timeout=%lu ms)",
                                (unsigned long)(nowMs - lastSbus1), (unsigned long)timeoutMs);
                    if ((uint32_t)(nowMs - lastSbus1WatchdogDiagMs) >= kWatchdogDiagIntervalMs) {
                        lastSbus1WatchdogDiagMs = nowMs;
                        SbusDecoderDebugStats driveStats = sbus_drive.debugStats();
                        PA_LOG_WARN(TAG,
                                    "SBUS1 watchdog decode stats: rx_done=%lu queued=%lu short=%lu ok=%lu fail=%lu bitlow=%lu extract=%lu hdr=%lu ftr=%lu last_ftr=0x%02x rearm=%lu parity=%lu syms(last=%lu max=%lu)",
                                    (unsigned long)driveStats.rxDoneCount,
                                    (unsigned long)driveStats.queuedCount,
                                    (unsigned long)driveStats.shortDropCount,
                                    (unsigned long)driveStats.parseOkCount,
                                    (unsigned long)driveStats.parseFailCount,
                                    (unsigned long)driveStats.bitCountLowCount,
                                    (unsigned long)driveStats.extractFailCount,
                                    (unsigned long)driveStats.headerMismatchCount,
                                    (unsigned long)driveStats.footerMismatchCount,
                                    (unsigned int)driveStats.lastRejectedFooter,
                                    (unsigned long)driveStats.rearmFailCount,
                                    (unsigned long)driveStats.parityFailCount,
                                    (unsigned long)driveStats.lastSymbolCount,
                                    (unsigned long)driveStats.maxSymbolCount);
                    }
                }
            } else {
                taskENTER_CRITICAL(&robotStateMux);
                bool signalRestored = robotState.sbusSignalLost;
                if (signalRestored) {
                    robotState.sbusSignalLost = false;
                }
                robotState.sbusHwFailsafe = false;
                taskEXIT_CRITICAL(&robotStateMux);
                if (signalRestored) {
                    PA_LOG_INFO(TAG, "SBUS1 signal restored");
                }
            }
        } else {
            taskENTER_CRITICAL(&robotStateMux);
            robotState.sbusSignalLost = false;
            robotState.sbusHwFailsafe = false;
            taskEXIT_CRITICAL(&robotStateMux);
        }

        // --- Dome-spin receiver (SBUS #2) ---
        if (domeSbusEnabled && sbus_dome.read()) {
            SbusData data = sbus_dome.data();

            taskENTER_CRITICAL(&robotStateMux);
            bool wasSbus2HwFailsafe = robotState.sbus2HwFailsafe;
            robotState.sbus2HwFailsafe = data.failsafe;
            for (int i = 0; i < 16; ++i) {
                robotState.rcSbus2Raw[i] = (uint16_t)data.ch[i];
            }
            robotState.rcSbus2Digital[0] = data.ch17;
            robotState.rcSbus2Digital[1] = data.ch18;
            if (data.lost_frame) {
                robotState.sbus2LostFrameCount++;
            }
            // Suppress dispatch (and watchdog heartbeat) on any receiver-side signal
            // quality event: hardware failsafe OR lost_frame.
            // - failsafe: receiver outputting programmed failsafe positions.
            // - lost_frame: receiver missed a TX packet; outputs hold/failsafe position
            //   with lost_frame=true, failsafe=false. Without this guard, the programmed
            //   hold position (ch1=389 = -89%) would be dispatched to the dome task.
            // Suppressing the watchdog heartbeat on both events means the SBUS2 watchdog
            // fires and stops the dome if either condition persists.
            bool suppress = data.failsafe || data.lost_frame;
            if (!suppress) {
                robotState.lastSbus2Ms = millis();
            }
            taskEXIT_CRITICAL(&robotStateMux);

            if (suppress) {
                if (data.failsafe && !wasSbus2HwFailsafe) {
                    PA_LOG_WARN(TAG, "SBUS2 hardware failsafe asserted");
                }
                // Do not dispatch. Watchdog stops dome if condition persists.
            } else {
                dispatchSbusBindingsForSource(data, RC_BINDING_SBUS2, rcInputMode, enableRcCh1,
                                              enableRcCh2);
                if (sbus2WatchdogFired) {
                    sbus2WatchdogFired = false;
                    PA_LOG_INFO(TAG, "SBUS2 signal restored");
                }
            }
        }

        // SBUS2 watchdog — Layer 2 safety for dome receiver
        taskENTER_CRITICAL(&robotStateMux);
        uint32_t lastSbus2 = robotState.lastSbus2Ms;
        taskEXIT_CRITICAL(&robotStateMux);

        if (domeSbusEnabled) {
            uint32_t nowMs = millis();
            if ((uint32_t)(nowMs - lastSbus2) > timeoutMs) {
                if (!sbus2WatchdogFired) {
                    sbus2WatchdogFired = true;
                    taskENTER_CRITICAL(&robotStateMux);
                    robotState.sbus2SignalLost = true;
                    recordFailsafeTriggerLocked(FS_SBUS2_TIMEOUT, nowMs);
                    taskEXIT_CRITICAL(&robotStateMux);
                    PA_LOG_WARN(TAG, "SBUS2 watchdog fired - dome signal lost");
                    if ((uint32_t)(nowMs - lastSbus2WatchdogDiagMs) >= kWatchdogDiagIntervalMs) {
                        lastSbus2WatchdogDiagMs = nowMs;
                        SbusDecoderDebugStats domeStats = sbus_dome.debugStats();
                        PA_LOG_WARN(TAG,
                                    "SBUS2 watchdog decode stats: rx_done=%lu queued=%lu short=%lu ok=%lu fail=%lu bitlow=%lu extract=%lu hdr=%lu ftr=%lu last_ftr=0x%02x rearm=%lu parity=%lu syms(last=%lu max=%lu)",
                                    (unsigned long)domeStats.rxDoneCount,
                                    (unsigned long)domeStats.queuedCount,
                                    (unsigned long)domeStats.shortDropCount,
                                    (unsigned long)domeStats.parseOkCount,
                                    (unsigned long)domeStats.parseFailCount,
                                    (unsigned long)domeStats.bitCountLowCount,
                                    (unsigned long)domeStats.extractFailCount,
                                    (unsigned long)domeStats.headerMismatchCount,
                                    (unsigned long)domeStats.footerMismatchCount,
                                    (unsigned int)domeStats.lastRejectedFooter,
                                    (unsigned long)domeStats.rearmFailCount,
                                    (unsigned long)domeStats.parityFailCount,
                                    (unsigned long)domeStats.lastSymbolCount,
                                    (unsigned long)domeStats.maxSymbolCount);
                    }

                    DomeCommand stopCmd = {};
                    stopCmd.speed = 0.0f;
                    stopCmd.source = SRC_INTERNAL;
                    stopCmd.timestampMs = nowMs;
                    xQueueSend(domeCmdQueue, &stopCmd, 0);
                }
            } else {
                taskENTER_CRITICAL(&robotStateMux);
                robotState.sbus2SignalLost = false;
                taskEXIT_CRITICAL(&robotStateMux);
            }
        } else {
            taskENTER_CRITICAL(&robotStateMux);
            robotState.sbus2SignalLost = false;
            robotState.sbus2HwFailsafe = false;
            taskEXIT_CRITICAL(&robotStateMux);
        }

        uint32_t nowMs = millis();
        if ((driveSbusEnabled || domeSbusEnabled) &&
            (uint32_t)(nowMs - lastSbusDiagLogMs) >= 2000U) {
            lastSbusDiagLogMs = nowMs;
            bool waitingDrive = driveSbusEnabled && (lastSbus1 == 0);
            bool waitingDome = domeSbusEnabled && (lastSbus2 == 0);
            if (waitingDrive || waitingDome) {
                SbusDecoderDebugStats driveStats = sbus_drive.debugStats();
                SbusDecoderDebugStats domeStats = sbus_dome.debugStats();
                if (waitingDrive) {
                    PA_LOG_WARN(TAG,
                                "SBUS1 waiting first frame: rx_done=%lu queued=%lu short=%lu ok=%lu fail=%lu bitlow=%lu extract=%lu hdr=%lu ftr=%lu last_ftr=0x%02x rearm=%lu parity=%lu syms(last=%lu max=%lu)",
                                (unsigned long)driveStats.rxDoneCount,
                                (unsigned long)driveStats.queuedCount,
                                (unsigned long)driveStats.shortDropCount,
                                (unsigned long)driveStats.parseOkCount,
                                (unsigned long)driveStats.parseFailCount,
                                (unsigned long)driveStats.bitCountLowCount,
                                (unsigned long)driveStats.extractFailCount,
                                (unsigned long)driveStats.headerMismatchCount,
                                (unsigned long)driveStats.footerMismatchCount,
                                (unsigned int)driveStats.lastRejectedFooter,
                                (unsigned long)driveStats.rearmFailCount,
                                (unsigned long)driveStats.parityFailCount,
                                (unsigned long)driveStats.lastSymbolCount,
                                (unsigned long)driveStats.maxSymbolCount);
                }
                if (waitingDome) {
                    PA_LOG_WARN(TAG,
                                "SBUS2 waiting first frame: rx_done=%lu queued=%lu short=%lu ok=%lu fail=%lu bitlow=%lu extract=%lu hdr=%lu ftr=%lu last_ftr=0x%02x rearm=%lu parity=%lu syms(last=%lu max=%lu)",
                                (unsigned long)domeStats.rxDoneCount,
                                (unsigned long)domeStats.queuedCount,
                                (unsigned long)domeStats.shortDropCount,
                                (unsigned long)domeStats.parseOkCount,
                                (unsigned long)domeStats.parseFailCount,
                                (unsigned long)domeStats.bitCountLowCount,
                                (unsigned long)domeStats.extractFailCount,
                                (unsigned long)domeStats.headerMismatchCount,
                                (unsigned long)domeStats.footerMismatchCount,
                                (unsigned int)domeStats.lastRejectedFooter,
                                (unsigned long)domeStats.rearmFailCount,
                                (unsigned long)domeStats.parityFailCount,
                                (unsigned long)domeStats.lastSymbolCount,
                                (unsigned long)domeStats.maxSymbolCount);
                }
            }
        }

        // Feed Task Watchdog Timer
        esp_task_wdt_reset();

        // ~200 Hz poll rate — SBUS frames arrive at 100 Hz; poll twice per frame
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
