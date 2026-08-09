// =============================================================================
// src/tasks/rc_input.cpp
//
// RcInputTask  --  handles all RC input modes: standard_pwm, single_sbus, dual_sbus.
// Renamed from sbus_input.cpp; the task was never SBUS-specific.
//
// Receiver #1 (PIN_SBUS1_RX = GPIO 15): Drive  --  CH1=speed, CH2=steer
// Receiver #2 (PIN_SBUS2_RX = GPIO 13): Dome spin
//
// SbusDecoder API:
//   .begin(pin)    --  initialize RMT channel on pin; returns false if no channel free
//   .read()        --  returns true when a new 25-byte frame is decoded
//   .data()        --  returns SbusData{ch[16], failsafe, lost_frame}
//   ch[]           --  0-indexed, range SBUS_MIN(172)..SBUS_MAX(1811), center ~992
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

#include "../../include/commanded_modes.h"
#include "../../include/config.h"
#include "../../include/config_cache.h"
#include "../../include/dome_rx_parser.h"
#include "../../include/drive_arbiter.h"
#include "../../include/drive_speed_preset.h"
#include "../../include/failsafe_gate.h"
#include "../../include/ledc_pwm.h"
#include "../../include/logging.h"
#include "../../include/queue_drop_tracker.h"
#include "../../include/rc_channel_mapper.h"
#include "../../include/rc_dispatcher_helpers.h"
#include "../../include/rc_input_processor.h"
#include "../../include/rc_mapping_cache.h"
#include "../../include/rc_pwm_helpers.h"
#include "../../include/robot_state.h"
#include "../../include/sbus_decoder.h"
#include "../../include/sbus_watchdog.h"
#include "../../include/web_server.h"

static const char* TAG = "RCInputTask";

// SBUS receiver objects  --  RMT-based, no hardware UART consumed.
// GPIO 15 (PIN_SBUS1_RX) and GPIO 13 (PIN_SBUS2_RX) are the SBUS receiver pins.
// SBUS1 and SBUS2 each occupy one RMT channel (3 memory blocks each).
// UART1 is now exclusively owned by DriveTask; UART2 by DomeLinkTask.
static SbusDecoder sbus_drive;
static SbusDecoder sbus_dome;
static const uint8_t kRcPwmPins[6] = {PIN_RC_CH1, PIN_RC_CH2, PIN_RC_CH3,
                                      PIN_RC_CH4, PIN_RC_CH5, PIN_RC_CH6};

static RcInputProcessor s_rcProcessor = {};

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


// Build an RcMappingConfig from the config cache for the given RC input mode.
// prevSoundPressed is left false; callers must set it from their static state.
static RcMappingConfig rcBuildMappingConfig(RcInputMode mode) {
    RcMappingConfig out = {};
    ConfigSnapshot cfg = {};
    configCacheRead(&cfg);
    out.enableRc[0] = cfg.system.enable_rc_ch1;
    out.enableRc[1] = cfg.system.enable_rc_ch2;
    out.enableRc[2] = cfg.system.enable_rc_ch3;
    out.enableRc[3] = cfg.system.enable_rc_ch4;
    out.enableRc[4] = cfg.system.enable_rc_ch5;
    out.enableRc[5] = cfg.system.enable_rc_ch6;
    out.enableDome = cfg.system.enable_dome;
    out.enableArm1 = cfg.system.enable_arm1;
    out.enableArm2 = cfg.system.enable_arm2;
    out.enableSound = cfg.system.enable_s2_sound;
    out.maxOut = cfg.drive.speedLimitMax;
    if (mode == RC_INPUT_STANDARD_PWM) {
        out.driveSpeed = cfg.system.rc_pwm_drive_speed;
        out.driveSteer = cfg.system.rc_pwm_drive_steer;
        out.domeSpeed = cfg.system.rc_pwm_dome_speed;
        out.arm1 = cfg.system.rc_pwm_arm1;
        out.arm2 = cfg.system.rc_pwm_arm2;
        out.sound = cfg.system.rc_pwm_sound;
    } else {
        out.driveSpeed = cfg.system.rc_sbus_drive_speed;
        out.driveSteer = cfg.system.rc_sbus_drive_steer;
        out.domeSpeed = cfg.system.rc_sbus_dome_speed;
        out.arm1 = cfg.system.rc_sbus_arm1;
        out.arm2 = cfg.system.rc_sbus_arm2;
        out.sound = cfg.system.rc_sbus_sound;
    }
    out.prevSoundPressed = false;  // caller sets from static state
    return out;
}

static RcMappingConfig rcGetMappingConfig(RcInputMode mode) {
    static RcMappingCache mappingCache = {};

    bool dirty;
    taskENTER_CRITICAL(&robotStateMux);
    dirty = robotState.rcConfigDirty;
    if (dirty) {
        robotState.rcConfigDirty = false;
    }
    taskEXIT_CRITICAL(&robotStateMux);

    if (dirty) {
        rcMappingCacheInvalidate(&mappingCache);
    }

    RcMappingConfig cached = {};
    if (rcMappingCacheGet(mappingCache, mode, &cached)) {
        return cached;
    }

    cached = rcBuildMappingConfig(mode);
    rcMappingCacheSet(&mappingCache, mode, cached);
    return cached;
}

static void loadTier2TriggerBindings(RcTriggerBinding* bindings, size_t* count) {
    if (bindings == nullptr || count == nullptr) {
        return;
    }

    ConfigSnapshot cfg = {};
    configCacheRead(&cfg);
    static_assert(RC_TRIGGER_MAX >= RC_TRIGGER_SLOT_COUNT,
                  "trigger buffer must hold every config slot");
    *count = rcTriggerSlotsCopy(cfg.system, bindings, RC_TRIGGER_MAX);
}

static void buildRcProcessorConfig(RcInputMode mode, RcProcessorConfig* out) {
    *out = {};
    out->mapping = rcGetMappingConfig(mode);
    loadTier2TriggerBindings(out->triggers, &out->triggerCount);

    static ConfigSnapshot snap = {};
    configCacheRead(&snap);
    out->categories.gen_lo   = snap.audio.snd_cat_gen_lo;
    out->categories.gen_hi   = snap.audio.snd_cat_gen_hi;
    out->categories.chat_lo  = snap.audio.snd_cat_chat_lo;
    out->categories.chat_hi  = snap.audio.snd_cat_chat_hi;
    out->categories.hap_lo   = snap.audio.snd_cat_hap_lo;
    out->categories.hap_hi   = snap.audio.snd_cat_hap_hi;
    out->categories.proc_lo  = snap.audio.snd_cat_proc_lo;
    out->categories.proc_hi  = snap.audio.snd_cat_proc_hi;
    out->categories.sad_lo   = snap.audio.snd_cat_sad_lo;
    out->categories.sad_hi   = snap.audio.snd_cat_sad_hi;
    out->categories.sent_lo  = snap.audio.snd_cat_sent_lo;
    out->categories.sent_hi  = snap.audio.snd_cat_sent_hi;
    out->categories.hum_lo   = snap.audio.snd_cat_hum_lo;
    out->categories.hum_hi   = snap.audio.snd_cat_hum_hi;
    out->categories.scrm_lo  = snap.audio.snd_cat_scrm_lo;
    out->categories.scrm_hi  = snap.audio.snd_cat_scrm_hi;
    out->categories.ooh_lo   = snap.audio.snd_cat_ooh_lo;
    out->categories.ooh_hi   = snap.audio.snd_cat_ooh_hi;
    out->categories.alrm_lo  = snap.audio.snd_cat_alrm_lo;
    out->categories.alrm_hi  = snap.audio.snd_cat_alrm_hi;
    out->categories.snarky_lo = snap.audio.snd_cat_snarky_lo;
    out->categories.snarky_hi = snap.audio.snd_cat_snarky_hi;
    out->categories.whis_lo  = snap.audio.snd_cat_whis_lo;
    out->categories.whis_hi  = snap.audio.snd_cat_whis_hi;

    taskENTER_CRITICAL(&robotStateMux);
    out->estopActive        = robotState.estop;
    out->currentSleepMode   = robotState.sleepMode;
    out->currentSpeedPreset = normalizeSpeedPresetId((uint8_t)snap.drive.speedPresetActive);
    taskEXIT_CRITICAL(&robotStateMux);
}

static void dispatchProcessorOutput(const RcProcessorOutput& output, const RcMappingConfig& mapping,
                                     const RcTriggerBinding* triggers) {
    // Backbone: drive
    if ((output.backbone.driveSpeed != 0 || output.backbone.driveSteer != 0) &&
        !output.stationaryLockedByTrigger) {
        commandedSetStationary(false, SRC_SBUS);
    }
    rcDispatchDrive(output.backbone.driveSpeed, output.backbone.driveSteer, false);

    // Backbone: dome (filtered raw value, re-calibrated)
    rcDispatchDome(output.domeRawFiltered, mapping, output.domeFiltered);

    // Backbone: audio trigger
    rcDispatchAudioTrigger(output.backbone.audioTrigger);

    // Backbone: servo commands (arm1 = index 0, arm2 = index 1)
    auto dispatchServo = [](RcServoCommand cmd, uint8_t armId) {
        if (cmd == RC_SERVO_NO_CHANGE) return;
        ServoCommandType servoType = SERVO_CMD_POSITION;
        uint16_t positionUs = SERVO_PULSE_NEUTRAL_US;
        if (cmd == RC_SERVO_OPEN)    servoType = SERVO_CMD_OPEN;
        else if (cmd == RC_SERVO_CLOSE) servoType = SERVO_CMD_CLOSE;
        ServoCommand servoCmd = {};
        servoCmd.armId = armId;
        servoCmd.type = servoType;
        servoCmd.positionUs = positionUs;
        servoCmd.source = SRC_SBUS;
        servoCmd.timestampMs = millis();
        if (xQueueSend(servoCmdQueue, &servoCmd, 0) != pdTRUE) {
            logQueueDrop(QUEUE_SERVO_CMD, "servo command");
        }
    };
    dispatchServo(output.backbone.arm1Cmd, 0);
    dispatchServo(output.backbone.arm2Cmd, 1);

    // Tier 2 trigger results
    rcDispatchTriggerResults(output, triggers);
}

static constexpr uint32_t kOneShotEdgeDebounceMs = 120;
static constexpr uint8_t kSwitchEdgeConfirmFrames = 2;

static void processTriggerAction(RobotActionId target, const char* payload, bool pressed) {
    RcActionPayload ap = {};
    ap.target = target;
    ap.bindingPayload = payload;
    ap.pressed = pressed;
    ap.randomSeed = (uint32_t)esp_random();

    ConfigSnapshot cfg = {};
    configCacheRead(&cfg);
    ap.categories.gen_lo = cfg.audio.snd_cat_gen_lo;
    ap.categories.gen_hi = cfg.audio.snd_cat_gen_hi;
    ap.categories.chat_lo = cfg.audio.snd_cat_chat_lo;
    ap.categories.chat_hi = cfg.audio.snd_cat_chat_hi;
    ap.categories.hap_lo = cfg.audio.snd_cat_hap_lo;
    ap.categories.hap_hi = cfg.audio.snd_cat_hap_hi;
    ap.categories.proc_lo = cfg.audio.snd_cat_proc_lo;
    ap.categories.proc_hi = cfg.audio.snd_cat_proc_hi;
    ap.categories.sad_lo = cfg.audio.snd_cat_sad_lo;
    ap.categories.sad_hi = cfg.audio.snd_cat_sad_hi;
    ap.categories.sent_lo = cfg.audio.snd_cat_sent_lo;
    ap.categories.sent_hi = cfg.audio.snd_cat_sent_hi;
    ap.categories.hum_lo = cfg.audio.snd_cat_hum_lo;
    ap.categories.hum_hi = cfg.audio.snd_cat_hum_hi;
    ap.categories.scrm_lo = cfg.audio.snd_cat_scrm_lo;
    ap.categories.scrm_hi = cfg.audio.snd_cat_scrm_hi;
    ap.categories.ooh_lo = cfg.audio.snd_cat_ooh_lo;
    ap.categories.ooh_hi = cfg.audio.snd_cat_ooh_hi;
    ap.categories.alrm_lo = cfg.audio.snd_cat_alrm_lo;
    ap.categories.alrm_hi = cfg.audio.snd_cat_alrm_hi;
    ap.categories.snarky_lo = cfg.audio.snd_cat_snarky_lo;
    ap.categories.snarky_hi = cfg.audio.snd_cat_snarky_hi;
    ap.categories.whis_lo = cfg.audio.snd_cat_whis_lo;
    ap.categories.whis_hi = cfg.audio.snd_cat_whis_hi;
    taskENTER_CRITICAL(&robotStateMux);
    ap.estopActive = robotState.estop;
    ap.currentSleepMode = robotState.sleepMode;
    ap.currentSpeedPreset = normalizeSpeedPresetId((uint8_t)cfg.drive.speedPresetActive);
    taskEXIT_CRITICAL(&robotStateMux);

    RcActionResult res = rcDispatchAction(ap);

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
        s_rcProcessor.stationaryLocked = res.newStationaryMode;
    }
    if (res.setSpeedPreset) {
        applySpeedPresetRuntime(res.newSpeedPreset);
    }
}

void dispatchRcTriggerActionTest(RobotActionId target, const char* payload, bool pressed) {
    processTriggerAction(target, payload, pressed);
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
    ConfigSnapshot cfgSnap = {};
    configCacheRead(&cfgSnap);
    taskENTER_CRITICAL(&robotStateMux);
    pwmSignalLost = pwmSignalLostCheck(robotState.lastPwmMs, pwmCheckMs, cfgSnap.drive.sbusTimeoutMs);
    taskEXIT_CRITICAL(&robotStateMux);

    if (pwmSignalLost) {
        // Skip drive command processing when PWM signal is lost or stale
        driveArbiterSubmit(DriveSource::RC, 0, 0, pwmCheckMs);
        return;
    }

    // Build channel snapshot from PWM pulses
    RcChannelSnapshot snap = {};
    snap.valid = true;
    snap.mode  = RC_INPUT_STANDARD_PWM;
    for (int i = 0; i < 6; ++i) snap.channels[i] = (int16_t)pulses[i];

    static RcProcessorConfig cfg_proc = {};
    buildRcProcessorConfig(RC_INPUT_STANDARD_PWM, &cfg_proc);
    cfg_proc.mapping.prevSoundPressed = s_rcProcessor.lastSoundPressed;
    // PWM mode has no Tier 2 SBUS triggers  --  clear count so processor skips the loop
    cfg_proc.triggerCount = 0;

    static RcProcessorInput input = {};
    input.channels     = snap;
    input.config       = cfg_proc;
    input.nowMs        = millis();
    input.randomSeed   = (uint32_t)esp_random();
    input.sourceFilter = RC_BINDING_PWM;

    static RcProcessorOutput output = {};
    rcInputProcessorTick(&s_rcProcessor, input, &output);
    dispatchProcessorOutput(output, cfg_proc.mapping, cfg_proc.triggers);
}

static void dispatchSbusBindingsForSource(const SbusData& data, RcBindingSource source,
                                          RcInputMode mode, bool enableRcCh1, bool enableRcCh2,
                                          bool useCh2) {
    // Build channel snapshot from SBUS frame
    RcChannelSnapshot snap = {};
    snap.valid = true;
    snap.mode  = mode;
    for (int i = 0; i < 16; ++i) snap.channels[i] = data.ch[i];
    snap.channels[16] = data.ch17 ? 1811 : 172;
    snap.channels[17] = data.ch18 ? 1811 : 172;

    static RcProcessorConfig cfg = {};
    buildRcProcessorConfig(mode, &cfg);
    cfg.mapping.prevSoundPressed = s_rcProcessor.lastSoundPressed;

    static RcProcessorInput input = {};
    input.channels     = snap;
    input.config       = cfg;
    input.nowMs        = millis();
    input.randomSeed   = (uint32_t)esp_random();
    input.sourceFilter = source;

    static RcProcessorOutput output = {};
    rcInputProcessorTick(&s_rcProcessor, input, &output);
    dispatchProcessorOutput(output, cfg.mapping, cfg.triggers);
}

static bool is_drive_sbus_mode(RcInputMode mode) {
    return mode == RC_INPUT_SINGLE_SBUS || mode == RC_INPUT_DUAL_SBUS;
}

static bool is_dome_sbus_mode(RcInputMode mode) {
    return mode == RC_INPUT_DUAL_SBUS;
}

static bool driveSbusDecoderEnabledForMode(RcInputMode mode, bool enableRcCh1, bool enableRcCh2,
                                           bool useCh2) {
    if (!is_drive_sbus_mode(mode)) {
        return false;
    }
    if (mode == RC_INPUT_SINGLE_SBUS) {
        return useCh2 ? enableRcCh2 : enableRcCh1;
    }
    return enableRcCh1;
}

// -----------------------------------------------------------------------------
// rcInputTask()
// Handles all RC input modes: standard_pwm, single_sbus, dual_sbus.
// Polls receivers at ~200 Hz (5 ms delay) to catch every 100 Hz SBUS frame.
// Implements Layer 1 (HW failsafe flag) and Layer 2 (SW watchdog) safety.
// Thread safety: all RobotState writes use taskENTER/EXIT_CRITICAL.
// -----------------------------------------------------------------------------
void rcInputTask(void* pvParameters) {
    // Register with TWDT unconditionally  --  this task feeds the watchdog
    // regardless of which RC mode is active or what channels are enabled.
    esp_task_wdt_add(NULL);

    rcInputProcessorInit(&s_rcProcessor);

    ConfigSnapshot startupCfg = {};
    configCacheRead(&startupCfg);
    RcInputMode rcInputMode = startupCfg.system.rc_input_mode;
    bool enableRcCh1 = startupCfg.system.enable_rc_ch1;
    bool enableRcCh2 = startupCfg.system.enable_rc_ch2;
    bool useCh2 = startupCfg.system.single_sbus_use_ch2;

    bool driveSbusEnabled =
        driveSbusDecoderEnabledForMode(rcInputMode, enableRcCh1, enableRcCh2, useCh2);
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
            PA_LOG_INFO(TAG, "started — single_sbus mode, SBUS%d disabled",
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

    // SBUS watchdog state
    SbusWatchdog sbus1Watchdog = {};
    SbusWatchdog sbus2Watchdog = {};
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
            PA_LOG_DEBUG(TAG, "stack HWM: %u words free",
                         (unsigned)uxTaskGetStackHighWaterMark(NULL));
            hwmLogged = true;
        }

        ConfigSnapshot loopCfg = {};
        configCacheRead(&loopCfg);
        rcInputMode = loopCfg.system.rc_input_mode;
        enableRcCh1 = loopCfg.system.enable_rc_ch1;
        enableRcCh2 = loopCfg.system.enable_rc_ch2;
        useCh2 = loopCfg.system.single_sbus_use_ch2;
        driveSbusEnabled =
            driveSbusDecoderEnabledForMode(rcInputMode, enableRcCh1, enableRcCh2, useCh2);
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
            sbusWatchdogReset(&sbus2Watchdog);
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
            sbusWatchdogReset(&sbus1Watchdog);
            sbusWatchdogReset(&sbus2Watchdog);
            dispatchStandardPwmInputs();
            esp_task_wdt_reset();
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        // --- Drive receiver (SBUS #1, or SBUS2 GPIO when single_sbus+useCh2) ---
        if (driveSbusEnabled && sbus_drive.read()) {
            SbusData data = sbus_drive.data();

            // single_sbus+useCh2=true: decoder reads GPIO13 (dome GPIO).
            // Treat as SBUS2  --  store to sbus2 state and dispatch dome/aux bindings only.
            // Drive bindings (SBUS1) never fire, and SBUS2-only traffic must not feed
            // the drive SBUS watchdog.
            bool asSbus2 = (rcInputMode == RC_INPUT_SINGLE_SBUS) && useCh2;

            if (asSbus2) {
                taskENTER_CRITICAL(&robotStateMux);
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

                    dispatchSbusBindingsForSource(data, RC_BINDING_SBUS1, rcInputMode, enableRcCh1,
                                                  enableRcCh2, useCh2);
                }
            }
        }

        // Layer 2: SBUS software watchdog  --  fires if no valid frame for SBUS_TIMEOUT_MS
        ConfigSnapshot watchdogCfg = {};
        configCacheRead(&watchdogCfg);
        taskENTER_CRITICAL(&robotStateMux);
        uint32_t lastSbus1 = robotState.lastSbus1Ms;
        taskEXIT_CRITICAL(&robotStateMux);
        uint32_t timeoutMs = watchdogCfg.drive.sbusTimeoutMs;

        bool sbus1TrackingActive =
            driveSbusEnabled && !((rcInputMode == RC_INPUT_SINGLE_SBUS) && useCh2);
        if (sbus1TrackingActive) {
            uint32_t nowMs = millis();
            SbusWatchdogTransition transition =
                sbusWatchdogCheck(&sbus1Watchdog, lastSbus1, nowMs, timeoutMs);
            if (transition == SbusWatchdogTransition::JUST_LOST) {
                failsafeTrigger(FailsafeLayer::SBUS_WATCHDOG);
                driveArbiterSubmit(DriveSource::RC, 0, 0, nowMs);
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
            } else if (transition == SbusWatchdogTransition::JUST_RESTORED) {
                failsafeClear(FailsafeLayer::SBUS_WATCHDOG);
                PA_LOG_INFO(TAG, "SBUS1 signal restored");
                failsafeClear(FailsafeLayer::SBUS_HW);
            } else if (transition == SbusWatchdogTransition::OK) {
                failsafeClear(FailsafeLayer::SBUS_HW);
            }
        } else {
            sbusWatchdogReset(&sbus1Watchdog);
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
            }
        }

        // SBUS2 watchdog  --  Layer 2 safety for dome receiver
        taskENTER_CRITICAL(&robotStateMux);
        uint32_t lastSbus2 = robotState.lastSbus2Ms;
        taskEXIT_CRITICAL(&robotStateMux);

        if (domeSbusEnabled) {
            uint32_t nowMs = millis();
            SbusWatchdogTransition transition =
                sbusWatchdogCheck(&sbus2Watchdog, lastSbus2, nowMs, timeoutMs);
            if (transition == SbusWatchdogTransition::JUST_LOST) {
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
            } else if (transition == SbusWatchdogTransition::JUST_RESTORED) {
                taskENTER_CRITICAL(&robotStateMux);
                robotState.sbus2SignalLost = false;
                taskEXIT_CRITICAL(&robotStateMux);
                PA_LOG_INFO(TAG, "SBUS2 signal restored");
            }
        } else {
            sbusWatchdogReset(&sbus2Watchdog);
        }

        uint32_t nowMs = millis();
        if ((driveSbusEnabled || domeSbusEnabled) &&
            (uint32_t)(nowMs - lastSbusDiagLogMs) >= 2000U) {
            lastSbusDiagLogMs = nowMs;
            bool waitingDrive = sbus1TrackingActive && (lastSbus1 == 0);
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

        // ~200 Hz poll rate  --  SBUS frames arrive at 100 Hz; poll twice per frame
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
