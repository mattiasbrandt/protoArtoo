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
#include <esp_system.h>
#include <esp_task_wdt.h>

#include "../../include/audio_task.h"
#include "../../include/config.h"
#include "../../include/dome_link.h"
#include "../../include/dome_rx_parser.h"
#include "../../include/drive_arbiter.h"
#include "../../include/drive_speed_preset.h"
#include "../../include/failsafe_gate.h"
#include "../../include/ledc_pwm.h"
#include "../../include/logging.h"
#include "../../include/marcduino_helpers.h"
#include "../../include/rc_action_dispatcher.h"
#include "../../include/rc_channel_mapper.h"
#include "../../include/rc_pwm_helpers.h"
#include "../../include/robot_state.h"
#include "../../include/sbus_decoder.h"
#include "../../include/web_server.h"

static const char* TAG = "RCInputTask";

// SBUS receiver objects — RMT-based, no hardware UART consumed.
// GPIO 15 (PIN_SBUS1_RX) and GPIO 13 (PIN_SBUS2_RX) are the SBUS receiver pins.
// SBUS1 and SBUS2 each occupy one RMT channel (3 memory blocks each).
// UART1 is now exclusively owned by DriveTask; UART2 by DomeLinkTask.
static SbusDecoder sbus_drive;
static SbusDecoder sbus_dome;
static const uint8_t kRcPwmPins[6] = {PIN_RC_CH1, PIN_RC_CH2, PIN_RC_CH3,
                                      PIN_RC_CH4, PIN_RC_CH5, PIN_RC_CH6};

static bool bindingSourceActive(const RcBindingConfig& binding, RcInputMode mode, bool enableRcCh1,
                                bool enableRcCh2, bool useCh2) {
    switch (binding.source) {
        case RC_BINDING_PWM:
            return mode == RC_INPUT_STANDARD_PWM && binding.channel >= 1 && binding.channel <= 6;
        case RC_BINDING_SBUS1:
            if (mode == RC_INPUT_SINGLE_SBUS)
                return !useCh2 && enableRcCh1;
            return mode == RC_INPUT_DUAL_SBUS && enableRcCh1;
        case RC_BINDING_SBUS2:
            if (mode == RC_INPUT_SINGLE_SBUS)
                return useCh2 && enableRcCh2;
            return mode == RC_INPUT_DUAL_SBUS && enableRcCh2;
        case RC_BINDING_NONE:
        default:
            return false;
    }
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

// Build an RcMappingConfig from RobotState for the given RC input mode.
// Reads all relevant cfg_* fields under the robotStateMux critical section.
// prevSoundPressed is left false; callers must set it from their static state.
static RcMappingConfig rcBuildMappingConfig(RcInputMode mode) {
    RcMappingConfig out = {};
    taskENTER_CRITICAL(&robotStateMux);
    out.enableRc[0] = robotState.cfg_enable_rc_ch1;
    out.enableRc[1] = robotState.cfg_enable_rc_ch2;
    out.enableRc[2] = robotState.cfg_enable_rc_ch3;
    out.enableRc[3] = robotState.cfg_enable_rc_ch4;
    out.enableRc[4] = robotState.cfg_enable_rc_ch5;
    out.enableRc[5] = robotState.cfg_enable_rc_ch6;
    out.enableDome = robotState.cfg_enable_dome;
    out.enableArm1 = robotState.cfg_enable_arm1;
    out.enableArm2 = robotState.cfg_enable_arm2;
    out.enableSound = robotState.cfg_enable_s2_sound;
    out.maxOut = robotState.cfg_speedLimitMax;
    if (mode == RC_INPUT_STANDARD_PWM) {
        out.driveSpeed = robotState.cfg_rc_pwm_drive_speed;
        out.driveSteer = robotState.cfg_rc_pwm_drive_steer;
        out.domeSpeed = robotState.cfg_rc_pwm_dome_speed;
        out.arm1 = robotState.cfg_rc_pwm_arm1;
        out.arm2 = robotState.cfg_rc_pwm_arm2;
        out.sound = robotState.cfg_rc_pwm_sound;
    } else {
        out.driveSpeed = robotState.cfg_rc_sbus_drive_speed;
        out.driveSteer = robotState.cfg_rc_sbus_drive_steer;
        out.domeSpeed = robotState.cfg_rc_sbus_dome_speed;
        out.arm1 = robotState.cfg_rc_sbus_arm1;
        out.arm2 = robotState.cfg_rc_sbus_arm2;
        out.sound = robotState.cfg_rc_sbus_sound;
    }
    taskEXIT_CRITICAL(&robotStateMux);
    out.prevSoundPressed = false;  // caller sets from static state
    return out;
}

// Cached mapping config — rebuilt from RobotState only when rcConfigDirty is set.
// prevSoundPressed is NOT cached; callers always set it on their local copy.
static RcMappingConfig g_cachedMapCfg = {};
static RcInputMode g_cachedMapMode = static_cast<RcInputMode>(0xFF);  // invalid sentinel

static RcMappingConfig rcGetMappingConfig(RcInputMode mode) {
    bool dirty;
    taskENTER_CRITICAL(&robotStateMux);
    dirty = robotState.rcConfigDirty;
    if (dirty) {
        robotState.rcConfigDirty = false;
    }
    taskEXIT_CRITICAL(&robotStateMux);

    if (dirty || mode != g_cachedMapMode) {
        g_cachedMapCfg = rcBuildMappingConfig(mode);
        g_cachedMapMode = mode;
    }
    return g_cachedMapCfg;
}

// Tier 2 Trigger Binding Runtime State
struct TriggerRuntimeState {
    bool lastPressed;
    RcSwitchState lastSwitchState;
    RcSwitchState pendingSwitchState;
    bool switchStateInit;
    uint8_t pendingCount;
    uint32_t lastEdgeMs;
};

static TriggerRuntimeState g_triggerStates[11] = {};  // One per Tier 2 binding slot
static constexpr uint32_t kOneShotEdgeDebounceMs = 120;
static constexpr uint8_t kSwitchEdgeConfirmFrames = 2;

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


static void processTriggerAction(RobotActionId target, const char* payload, bool pressed) {
    RcActionPayload ap = {};
    ap.target = target;
    ap.bindingPayload = payload;
    ap.pressed = pressed;
    ap.randomSeed = (uint32_t)esp_random();

    taskENTER_CRITICAL(&robotStateMux);
    ap.categories.gen_lo = robotState.cfg_snd_cat_gen_lo;
    ap.categories.gen_hi = robotState.cfg_snd_cat_gen_hi;
    ap.categories.chat_lo = robotState.cfg_snd_cat_chat_lo;
    ap.categories.chat_hi = robotState.cfg_snd_cat_chat_hi;
    ap.categories.hap_lo = robotState.cfg_snd_cat_hap_lo;
    ap.categories.hap_hi = robotState.cfg_snd_cat_hap_hi;
    ap.categories.proc_lo = robotState.cfg_snd_cat_proc_lo;
    ap.categories.proc_hi = robotState.cfg_snd_cat_proc_hi;
    ap.categories.sad_lo = robotState.cfg_snd_cat_sad_lo;
    ap.categories.sad_hi = robotState.cfg_snd_cat_sad_hi;
    ap.categories.sent_lo = robotState.cfg_snd_cat_sent_lo;
    ap.categories.sent_hi = robotState.cfg_snd_cat_sent_hi;
    ap.categories.hum_lo = robotState.cfg_snd_cat_hum_lo;
    ap.categories.hum_hi = robotState.cfg_snd_cat_hum_hi;
    ap.categories.scrm_lo = robotState.cfg_snd_cat_scrm_lo;
    ap.categories.scrm_hi = robotState.cfg_snd_cat_scrm_hi;
    ap.categories.ooh_lo = robotState.cfg_snd_cat_ooh_lo;
    ap.categories.ooh_hi = robotState.cfg_snd_cat_ooh_hi;
    ap.categories.alrm_lo = robotState.cfg_snd_cat_alrm_lo;
    ap.categories.alrm_hi = robotState.cfg_snd_cat_alrm_hi;
    ap.categories.snarky_lo = robotState.cfg_snd_cat_snarky_lo;
    ap.categories.snarky_hi = robotState.cfg_snd_cat_snarky_hi;
    ap.categories.whis_lo = robotState.cfg_snd_cat_whis_lo;
    ap.categories.whis_hi = robotState.cfg_snd_cat_whis_hi;
    ap.estopActive = robotState.estop;
    ap.currentSleepMode = robotState.sleepMode;
    ap.currentSpeedPreset = normalizeSpeedPresetId((uint8_t)robotState.cfg_speedPresetActive);
    taskEXIT_CRITICAL(&robotStateMux);

    RcActionResult res = rcDispatchAction(ap);

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
            if (!queueServoSequence(res.servoSequenceId, SRC_SBUS)) {
                PA_LOG_WARN(TAG, "droid sequence servo queue full: seq=%u",
                            (unsigned)res.servoSequenceId);
            }
        } else {
            ServoCommandType cmd = res.servoOpen ? SERVO_CMD_OPEN : SERVO_CMD_CLOSE;
            queueServoCommand((uint8_t)res.servoIndex, cmd, 0, SRC_SBUS);
        }
    }
    if (res.domeTxCmd[0] != '\0') {
        if (domeConnected()) {
            if (!domeQueueTx(res.domeTxCmd)) {
                PA_LOG_WARN(TAG, "dome tx queue full: %s", res.domeTxCmd);
            }
        }
    }
    if (res.marcduinoCmd[0] != '\0') {
        parseMarcduinoCommand(res.marcduinoCmd);
    }
    if (res.triggerEstop) {
        failsafeTrigger(FailsafeLayer::ESTOP);
    }
    if (res.setSleep) {
        const uint32_t nowMs = millis();
        taskENTER_CRITICAL(&robotStateMux);
        robotState.sleepMode = res.newSleepMode;
        robotState.sleepSinceMs = res.newSleepMode ? nowMs : 0U;
        robotState.domeSleepSyncPending = true;
        robotState.domeSleepSyncSleepMode = res.newSleepMode;
        taskEXIT_CRITICAL(&robotStateMux);
        requestStatusBroadcastNow();
    }
    if (res.setStationary) {
        setStationaryMode(res.newStationaryMode);
    }
    if (res.setSpeedPreset) {
        applySpeedPresetRuntime(res.newSpeedPreset);
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

    if (!robotActionIsButton(binding.target)) {
        return;
    }

    // Button targets use switch state with edge detection.
    RcSwitchState switchState = rcTriggerToSwitchState(rawValue, binding);
    if (switchState == RC_SWITCH_INVALID) {
        return;
    }

    if (!state.switchStateInit) {
        state.lastSwitchState = switchState;
        state.pendingSwitchState = switchState;
        state.switchStateInit = true;
        state.pendingCount = 0;
        return;
    }
    if (switchState == state.lastSwitchState) {
        state.pendingCount = 0;
        state.pendingSwitchState = switchState;
        return;
    }
    if (state.pendingCount == 0 || state.pendingSwitchState != switchState) {
        state.pendingSwitchState = switchState;
        state.pendingCount = 1;
        return;
    }
    state.pendingCount++;
    if (state.pendingCount < kSwitchEdgeConfirmFrames) {
        return;
    }
    state.pendingCount = 0;
    state.lastSwitchState = switchState;

    if (robotActionIsOneShotButton(binding.target)) {
        uint32_t nowMs = millis();
        if ((uint32_t)(nowMs - state.lastEdgeMs) < kOneShotEdgeDebounceMs) {
            return;
        }
        state.lastEdgeMs = nowMs;
        processTriggerAction(binding.target, binding.marcduinoPayload, true);
        state.lastPressed = true;
        return;
    }

    bool pressed = (switchState == RC_SWITCH_HIGH);
    processTriggerAction(binding.target, binding.marcduinoPayload, pressed);
    state.lastPressed = pressed;
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
    RcMappingConfig cfg = rcGetMappingConfig(RC_INPUT_STANDARD_PWM);

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
        driveArbiterSubmit(DriveSource::RC, 0, 0, pwmCheckMs);
        return;
    }

    // Build channel snapshot from PWM pulses for pure mapper
    RcChannelSnapshot snap = {};
    snap.valid = true;
    snap.mode = RC_INPUT_STANDARD_PWM;
    for (int i = 0; i < 6; ++i) {
        snap.channels[i] = (int16_t)pulses[i];  // Store PWM pulse in µs
    }

    static bool lastSoundPressed = false;
    cfg.prevSoundPressed = lastSoundPressed;

    // Map channel snapshot to control intent (pure function)
    RcControlIntent intent = rcMapChannels(snap, cfg);

    // Update sound state for next iteration
    lastSoundPressed = intent.soundPressed;

    // Dispatch backbone controls (drive speed, steer, dome speed)
    if (intent.driveSpeed != 0 || intent.driveSteer != 0) {
        setStationaryMode(false);
    }
    driveArbiterSubmit(DriveSource::RC, intent.driveSpeed, intent.driveSteer, millis());

    if (intent.domeSpeed != 0) {
        float normalizedDomeSpeed = (float)intent.domeSpeed / (float)cfg.maxOut;
        queueDomeCommand(normalizedDomeSpeed, SRC_SBUS);
    }

    // Dispatch audio trigger if fired
    if (intent.audioTrigger != nullptr) {
        parseMarcduinoCommand(intent.audioTrigger);
    }

    // Dispatch servo commands if set
    if (intent.arm1Cmd != RC_SERVO_NO_CHANGE) {
        ServoCommandType servoType = SERVO_CMD_POSITION;
        uint16_t positionUs = SERVO_PULSE_NEUTRAL_US;
        if (intent.arm1Cmd == RC_SERVO_OPEN) {
            servoType = SERVO_CMD_OPEN;
        } else if (intent.arm1Cmd == RC_SERVO_CLOSE) {
            servoType = SERVO_CMD_CLOSE;
        } else if (intent.arm1Cmd == RC_SERVO_NEUTRAL) {
            servoType = SERVO_CMD_POSITION;
            positionUs = SERVO_PULSE_NEUTRAL_US;
        }
        queueServoCommand(0, servoType, positionUs, SRC_SBUS);
    }

    if (intent.arm2Cmd != RC_SERVO_NO_CHANGE) {
        ServoCommandType servoType = SERVO_CMD_POSITION;
        uint16_t positionUs = SERVO_PULSE_NEUTRAL_US;
        if (intent.arm2Cmd == RC_SERVO_OPEN) {
            servoType = SERVO_CMD_OPEN;
        } else if (intent.arm2Cmd == RC_SERVO_CLOSE) {
            servoType = SERVO_CMD_CLOSE;
        } else if (intent.arm2Cmd == RC_SERVO_NEUTRAL) {
            servoType = SERVO_CMD_POSITION;
            positionUs = SERVO_PULSE_NEUTRAL_US;
        }
        queueServoCommand(1, servoType, positionUs, SRC_SBUS);
    }
}

static void dispatchSbusBindingsForSource(const SbusData& data, RcBindingSource source,
                                          RcInputMode mode, bool enableRcCh1, bool enableRcCh2,
                                          bool useCh2) {
    RcMappingConfig cfg = rcGetMappingConfig(mode);
    static bool domeRawInit = false;
    static int lastDomeRaw = 0;
    static int pendingDomeRaw = 0;
    static uint8_t pendingDomeCount = 0;

    auto sourceActive = [&](const RcBindingConfig& binding) {
        return binding.source == source &&
               bindingSourceActive(binding, mode, enableRcCh1, enableRcCh2, useCh2);
    };

    int raw = 0;
    bool pressed = false;

    if (cfg.enableDome && sourceActive(cfg.domeSpeed) &&
        readSbusAnalog(data, cfg.domeSpeed, &raw)) {
        const int center = (int)cfg.domeSpeed.center;
        const int kDomeNeutralBand = 140;
        const int kDomeStableBand = 90;

        if (!domeRawInit) {
            domeRawInit = true;
            lastDomeRaw = raw;
            pendingDomeRaw = raw;
        }

        bool wasNearNeutral = abs(lastDomeRaw - center) <= kDomeNeutralBand;
        bool nowNearNeutral = abs(raw - center) <= kDomeNeutralBand;
        bool acceptDomeSample = true;
        if (wasNearNeutral && !nowNearNeutral) {
            if (pendingDomeCount == 0 || abs(raw - pendingDomeRaw) > kDomeStableBand) {
                pendingDomeRaw = raw;
                pendingDomeCount = 1;
                lastDomeRaw = raw;
                acceptDomeSample = false;
            } else {
                pendingDomeCount++;
                if (pendingDomeCount < kSwitchEdgeConfirmFrames) {
                    lastDomeRaw = raw;
                    acceptDomeSample = false;
                }
            }
        }

        if (acceptDomeSample) {
            pendingDomeCount = 0;
            pendingDomeRaw = raw;
            lastDomeRaw = raw;
            queueDomeCommand(applyRcAnalogCalibration(raw, cfg.domeSpeed, nullptr), SRC_SBUS);
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

    bool driveSbusEnabled =
        is_drive_sbus_mode(rcInputMode) &&
        (rcInputMode == RC_INPUT_SINGLE_SBUS ? (useCh2 ? enableRcCh2 : enableRcCh1) : enableRcCh1);
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
            PA_LOG_INFO(TAG, "started — single_sbus mode, SBUS%d GPIO%d active", useCh2 ? 2 : 1,
                        sbusRxPin);
        } else {
            PA_LOG_INFO(TAG, "started — single_sbus mode, SBUS%d disabled (%s=false) — idle",
                        useCh2 ? 2 : 1, useCh2 ? "en_rc_ch2" : "en_rc_ch1");
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
                           ((rcInputMode == RC_INPUT_SINGLE_SBUS && useCh2) || enableRcCh1);
        domeSbusEnabled = is_dome_sbus_mode(rcInputMode) && enableRcCh2;

        // Detect single_sbus receiver selection change BEFORE the reinit guard.
        // If change detection ran after reinit, we could init-then-teardown the decoder
        // in the same iteration when useCh2 changes while the decoder is uninitialized.
        if (rcInputMode == RC_INPUT_SINGLE_SBUS && useCh2 != lastUseCh2 &&
            sbus_drive.isInitialized()) {
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
            failsafeClear(FailsafeLayer::SBUS_WATCHDOG);
            failsafeClear(FailsafeLayer::SBUS_HW);
            // SBUS2 fields are not yet managed by FailsafeGate; clear directly
            taskENTER_CRITICAL(&robotStateMux);
            robotState.sbus2SignalLost = false;
            robotState.sbus2HwFailsafe = false;
            taskEXIT_CRITICAL(&robotStateMux);
            dispatchStandardPwmInputs();
            esp_task_wdt_reset();
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        // --- Drive receiver (SBUS #1, or SBUS2 GPIO when single_sbus+useCh2) ---
        if (driveSbusEnabled && sbus_drive.read()) {
            SbusData data = sbus_drive.data();

            // single_sbus+useCh2=true: decoder reads GPIO13 (dome GPIO).
            // Treat as SBUS2 — store to sbus2 state and dispatch dome/aux bindings only.
            // Drive bindings (SBUS1) never fire; no drive commands from the dome controller.
            bool asSbus2 = (rcInputMode == RC_INPUT_SINGLE_SBUS) && useCh2;

            if (asSbus2) {
                taskENTER_CRITICAL(&robotStateMux);
                robotState.lastSbus1Ms = millis();  // feed sbus1 watchdog
                for (int i = 0; i < 16; ++i) {
                    robotState.rcSbus2Raw[i] = (uint16_t)data.ch[i];
                }
                robotState.rcSbus2Digital[0] = data.ch17;
                robotState.rcSbus2Digital[1] = data.ch18;
                bool suppress = data.failsafe || data.lost_frame;
                if (data.failsafe) {
                    robotState.sbus2HwFailsafe = true;
                } else if (data.lost_frame) {
                    robotState.sbus2LostFrameCount++;
                } else {
                    robotState.sbus2HwFailsafe = false;
                    robotState.sbus2SignalLost = false;
                    robotState.lastSbus2Ms = millis();
                }
                taskEXIT_CRITICAL(&robotStateMux);
                if (!suppress) {
                    dispatchSbusBindingsForSource(data, RC_BINDING_SBUS2, rcInputMode, enableRcCh1,
                                                  enableRcCh2, useCh2);
                }
            } else {
                taskENTER_CRITICAL(&robotStateMux);
                robotState.lastSbus1Ms = millis();
                for (int i = 0; i < 16; ++i) {
                    robotState.rcSbus1Raw[i] = (uint16_t)data.ch[i];
                }
                robotState.rcSbus1Digital[0] = data.ch17;
                robotState.rcSbus1Digital[1] = data.ch18;

                // Layer 1: Hardware failsafe flag from receiver firmware
                if (data.failsafe) {
                    bool hwFailsafeWasActive = robotState.sbusHwFailsafe;
                    taskEXIT_CRITICAL(&robotStateMux);
                    failsafeTrigger(FailsafeLayer::SBUS_HW);
                    if (!hwFailsafeWasActive) {
                        PA_LOG_WARN(TAG, "SBUS1 hardware failsafe asserted");
                    }
                    driveArbiterSubmit(DriveSource::RC, 0, 0, millis());
                } else if (data.lost_frame) {
                    robotState.sbus1LostFrameCount++;
                    uint32_t lostCount = robotState.sbus1LostFrameCount;
                    bool rcDebug = robotState.rcDebugMode;
                    taskEXIT_CRITICAL(&robotStateMux);
                    if (rcDebug && (lostCount % 100 == 0)) {
                        PA_LOG_DEBUG(TAG, "SBUS1 lost_frame count: %lu", (unsigned long)lostCount);
                    }
                } else {
                    taskEXIT_CRITICAL(&robotStateMux);
                    failsafeClear(FailsafeLayer::SBUS_HW);
                    failsafeClear(FailsafeLayer::SBUS_WATCHDOG);
                    taskENTER_CRITICAL(&robotStateMux);
                    taskEXIT_CRITICAL(&robotStateMux);

                    // Build channel snapshot from SBUS data for pure mapper
                    RcChannelSnapshot snap = {};
                    snap.valid = true;
                    snap.mode = rcInputMode;  // SINGLE_SBUS or DUAL_SBUS
                    for (int i = 0; i < 16; ++i) {
                        snap.channels[i] = (int16_t)data.ch[i];
                    }
                    snap.channels[16] = data.ch17 ? 1811 : 172;  // Digital ch17 as SBUS value
                    snap.channels[17] = data.ch18 ? 1811 : 172;  // Digital ch18 as SBUS value

                    RcMappingConfig mapCfg = rcGetMappingConfig(rcInputMode);
                    static bool lastSoundPressed = false;
                    mapCfg.prevSoundPressed = lastSoundPressed;

                    // Map channel snapshot to control intent (pure function)
                    RcControlIntent intent = rcMapChannels(snap, mapCfg);

                    // Update sound state for next iteration
                    lastSoundPressed = intent.soundPressed;

                    // Dispatch backbone controls (drive speed, steer)
                    if (intent.driveSpeed != 0 || intent.driveSteer != 0) {
                        setStationaryMode(false);
                    }
                    driveArbiterSubmit(DriveSource::RC, intent.driveSpeed, intent.driveSteer, millis());

                    // Dispatch audio trigger if fired
                    if (intent.audioTrigger != nullptr) {
                        parseMarcduinoCommand(intent.audioTrigger);
                    }

                    // Dispatch servo commands if set
                    if (intent.arm1Cmd != RC_SERVO_NO_CHANGE) {
                        ServoCommandType servoType = SERVO_CMD_POSITION;
                        uint16_t positionUs = SERVO_PULSE_NEUTRAL_US;
                        if (intent.arm1Cmd == RC_SERVO_OPEN) {
                            servoType = SERVO_CMD_OPEN;
                        } else if (intent.arm1Cmd == RC_SERVO_CLOSE) {
                            servoType = SERVO_CMD_CLOSE;
                        } else if (intent.arm1Cmd == RC_SERVO_NEUTRAL) {
                            servoType = SERVO_CMD_POSITION;
                            positionUs = SERVO_PULSE_NEUTRAL_US;
                        }
                        queueServoCommand(0, servoType, positionUs, SRC_SBUS);
                    }

                    if (intent.arm2Cmd != RC_SERVO_NO_CHANGE) {
                        ServoCommandType servoType = SERVO_CMD_POSITION;
                        uint16_t positionUs = SERVO_PULSE_NEUTRAL_US;
                        if (intent.arm2Cmd == RC_SERVO_OPEN) {
                            servoType = SERVO_CMD_OPEN;
                        } else if (intent.arm2Cmd == RC_SERVO_CLOSE) {
                            servoType = SERVO_CMD_CLOSE;
                        } else if (intent.arm2Cmd == RC_SERVO_NEUTRAL) {
                            servoType = SERVO_CMD_POSITION;
                            positionUs = SERVO_PULSE_NEUTRAL_US;
                        }
                        queueServoCommand(1, servoType, positionUs, SRC_SBUS);
                    }

                    // Dome speed dispatch happens in dispatchSbusBindingsForSource for stabilization
                    dispatchSbusBindingsForSource(data, RC_BINDING_SBUS1, rcInputMode, enableRcCh1,
                                                  enableRcCh2, useCh2);
                }
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
                bool wasSignalLost;
                taskENTER_CRITICAL(&robotStateMux);
                wasSignalLost = robotState.sbusSignalLost;
                taskEXIT_CRITICAL(&robotStateMux);
                if (!wasSignalLost) {
                    failsafeTrigger(FailsafeLayer::SBUS_WATCHDOG);
                    driveArbiterSubmit(DriveSource::RC, 0, 0, nowMs);
                    watchdogFired = true;
                }
                if (watchdogFired) {
                    PA_LOG_WARN(TAG, "SBUS1 watchdog fired - no frame for %lu ms (timeout=%lu ms)",
                                (unsigned long)(nowMs - lastSbus1), (unsigned long)timeoutMs);
                    if ((uint32_t)(nowMs - lastSbus1WatchdogDiagMs) >= kWatchdogDiagIntervalMs) {
                        lastSbus1WatchdogDiagMs = nowMs;
                        taskENTER_CRITICAL(&robotStateMux);
                        bool rcDebug = robotState.rcDebugMode;
                        taskEXIT_CRITICAL(&robotStateMux);
                        if (rcDebug) {
                            SbusDecoderDebugStats driveStats = sbus_drive.debugStats();
                            PA_LOG_DEBUG(
                                TAG,
                                "SBUS1 watchdog decode stats: rx_done=%lu queued=%lu short=%lu "
                                "ok=%lu fail=%lu bitlow=%lu extract=%lu hdr=%lu ftr=%lu "
                                "last_ftr=0x%02x rearm=%lu parity=%lu syms(last=%lu max=%lu)",
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
                }
            } else {
                taskENTER_CRITICAL(&robotStateMux);
                bool signalRestored = robotState.sbusSignalLost;
                taskEXIT_CRITICAL(&robotStateMux);
                if (signalRestored) {
                    failsafeClear(FailsafeLayer::SBUS_WATCHDOG);
                    PA_LOG_INFO(TAG, "SBUS1 signal restored");
                }
                failsafeClear(FailsafeLayer::SBUS_HW);
            }
        } else {
            failsafeClear(FailsafeLayer::SBUS_WATCHDOG);
            failsafeClear(FailsafeLayer::SBUS_HW);
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
                                              enableRcCh2, useCh2);
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
                    recordFailsafeTriggerLocked(FS_SBUS2_TIMEOUT, nowMs);
                    robotState.sbus2SignalLost = true;
                    taskEXIT_CRITICAL(&robotStateMux);
                    PA_LOG_WARN(TAG, "SBUS2 watchdog fired - dome signal lost");
                    if ((uint32_t)(nowMs - lastSbus2WatchdogDiagMs) >= kWatchdogDiagIntervalMs) {
                        lastSbus2WatchdogDiagMs = nowMs;
                        taskENTER_CRITICAL(&robotStateMux);
                        bool rcDebug = robotState.rcDebugMode;
                        taskEXIT_CRITICAL(&robotStateMux);
                        if (rcDebug) {
                            SbusDecoderDebugStats domeStats = sbus_dome.debugStats();
                            PA_LOG_DEBUG(
                                TAG,
                                "SBUS2 watchdog decode stats: rx_done=%lu queued=%lu short=%lu "
                                "ok=%lu fail=%lu bitlow=%lu extract=%lu hdr=%lu ftr=%lu "
                                "last_ftr=0x%02x rearm=%lu parity=%lu syms(last=%lu max=%lu)",
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

                    DomeCommand stopCmd = {};
                    stopCmd.speed = 0.0f;
                    stopCmd.source = SRC_INTERNAL;
                    stopCmd.timestampMs = nowMs;
                    xQueueSend(domeCmdQueue, &stopCmd, 0);
                }
            } else {
                sbus2WatchdogFired = false;
            }
        }

        uint32_t nowMs = millis();
        if ((driveSbusEnabled || domeSbusEnabled) &&
            (uint32_t)(nowMs - lastSbusDiagLogMs) >= 2000U) {
            lastSbusDiagLogMs = nowMs;
            bool waitingDrive = driveSbusEnabled && (lastSbus1 == 0);
            bool waitingDome = domeSbusEnabled && (lastSbus2 == 0);
            if (waitingDrive || waitingDome) {
                taskENTER_CRITICAL(&robotStateMux);
                bool rcDebug = robotState.rcDebugMode;
                taskEXIT_CRITICAL(&robotStateMux);
                if (waitingDrive)
                    PA_LOG_INFO(TAG, "SBUS1 waiting for first frame");
                if (waitingDome)
                    PA_LOG_INFO(TAG, "SBUS2 waiting for first frame");
                if (rcDebug) {
                    SbusDecoderDebugStats driveStats = sbus_drive.debugStats();
                    SbusDecoderDebugStats domeStats = sbus_dome.debugStats();
                    if (waitingDrive) {
                        PA_LOG_DEBUG(TAG,
                                     "SBUS1 decode stats: rx_done=%lu queued=%lu short=%lu ok=%lu "
                                     "fail=%lu bitlow=%lu extract=%lu hdr=%lu ftr=%lu "
                                     "last_ftr=0x%02x rearm=%lu parity=%lu syms(last=%lu max=%lu)",
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
                        PA_LOG_DEBUG(TAG,
                                     "SBUS2 decode stats: rx_done=%lu queued=%lu short=%lu ok=%lu "
                                     "fail=%lu bitlow=%lu extract=%lu hdr=%lu ftr=%lu "
                                     "last_ftr=0x%02x rearm=%lu parity=%lu syms(last=%lu max=%lu)",
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
        }

        // Feed Task Watchdog Timer
        esp_task_wdt_reset();

        // ~200 Hz poll rate — SBUS frames arrive at 100 Hz; poll twice per frame
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
