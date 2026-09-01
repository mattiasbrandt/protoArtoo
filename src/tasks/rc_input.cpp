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
#include "../../include/rc_input_step.h"
#include "../../include/rc_mapping_cache.h"
#include "../../include/rc_pwm_helpers.h"
#include "../../include/robot_state.h"
#include "../../include/sbus_decoder.h"
#include "../../include/sbus_watchdog.h"
#include "../../include/web_server.h"

static const char* TAG = "RCInputTask";

// SBUS receiver objects  --  RMT-based, no hardware UART consumed.
// GPIO 15 (PIN_SBUS1_RX) and GPIO 13 (PIN_SBUS2_RX) are the SBUS receiver pins.
// SBUS1 and SBUS2 each occupy one RMT RX channel. The memory blocks per
// channel are derived from the chip's RMT geometry, not fixed at 3 -- see
// include/sbus_rmt_budget.h (#255).
// UART1 is now exclusively owned by DriveTask; UART2 by DomeLinkTask.
static SbusDecoder sbus_drive;
static SbusDecoder sbus_dome;
static const uint8_t kRcPwmPins[6] = {PIN_RC_CH1, PIN_RC_CH2, PIN_RC_CH3,
                                      PIN_RC_CH4, PIN_RC_CH5, PIN_RC_CH6};

static RcInputProcessor s_rcProcessor = {};
static RcInputStepState s_rcStepState = {};

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


// Build an RcMappingConfig from live mapping settings while preserving the
// component toggles published as active at boot.
static RcMappingConfig rcBuildMappingConfig(const RcInputActiveConfig& active) {
    RcMappingConfig out = {};
    ConfigSnapshot cfg = {};
    configCacheRead(&cfg);
    for (size_t i = 0; i < 6; ++i) {
        out.enableRc[i] = active.enableRc[i];
    }
    out.enableDome = active.enableDome;
    out.enableArm1 = active.enableArm1;
    out.enableArm2 = active.enableArm2;
    out.enableSound = active.enableSound;
    out.maxOut = cfg.drive.speedLimitMax;
    if (active.mode == RC_INPUT_STANDARD_PWM) {
        out.driveSpeed = cfg.system.rc_pwm_drive_speed;
        out.driveSteer = cfg.system.rc_pwm_drive_steer;
        out.domeSpeed = cfg.system.rc_pwm_dome_speed;
        out.arm1 = cfg.system.rc_pwm_arm1;
        out.arm2 = cfg.system.rc_pwm_arm2;
        out.sound = cfg.system.rc_pwm_audio;
    } else {
        out.driveSpeed = cfg.system.rc_sbus_drive_speed;
        out.driveSteer = cfg.system.rc_sbus_drive_steer;
        out.domeSpeed = cfg.system.rc_sbus_dome_speed;
        out.arm1 = cfg.system.rc_sbus_arm1;
        out.arm2 = cfg.system.rc_sbus_arm2;
        out.sound = cfg.system.rc_sbus_audio;
    }
    out.prevSoundPressed = false;  // caller sets from static state
    return out;
}

static RcMappingConfig rcGetMappingConfig(const RcInputActiveConfig& active) {
    static RcMappingCache mappingCache = {};
    const RcInputMode mode = static_cast<RcInputMode>(active.mode);

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

    cached = rcBuildMappingConfig(active);
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

static void buildRcProcessorConfig(const RcInputActiveConfig& active, RcProcessorConfig* out) {
    *out = {};
    out->mapping = rcGetMappingConfig(active);
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

static RcDispatchOutcome processTriggerAction(RobotActionId target, const char* payload,
                                              bool pressed, CommandSource src) {
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

    // The target's own action legitimately produced nothing right now (e.g.
    // an unconfigured sound-category range, rcActionResultHasEffect()) -
    // report that honestly rather than dispatching nothing and calling it
    // "queued" (#220; docs/console-protocol.md s.3.3).
    if (!rcActionResultHasEffect(res)) {
        return RcDispatchOutcome::kBlockedByState;
    }

    // Dispatch audio, servo, dome, marcduino commands, attributed to src.
    RcDispatchOutcome outcome = rcDispatchSingleAction(res, src);

    // Handle system modes (estop, sleep, stationary, speed preset). estop
    // is unreachable here in practice: both existing callers (REST
    // /api/actions/test and the Console action executor, #220) refuse
    // SYSTEM_ACTION_ESTOP through evaluateActionTestGuard() before ever
    // reaching this function.
    if (res.triggerEstop) {
        failsafeTrigger(FailsafeLayer::ESTOP);
    }
    if (res.setSleep) {
        commandedSetSleep(res.newSleepMode, src);
        requestStatusBroadcastNow();
    }
    if (res.setStationary) {
        commandedSetStationary(res.newStationaryMode, src);
        s_rcProcessor.stationaryLocked = res.newStationaryMode;
    }
    if (res.setSpeedPreset) {
        applySpeedPresetRuntime(res.newSpeedPreset);
    }

    return outcome;
}

RcDispatchOutcome dispatchRcTriggerActionTest(RobotActionId target, const char* payload,
                                              bool pressed, CommandSource src) {
    return processTriggerAction(target, payload, pressed, src);
}

static void dispatchStandardPwmInputs(const RcInputActiveConfig& active) {
    uint32_t pulses[6] = {};
    RcMappingConfig cfg = rcGetMappingConfig(active);

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
    buildRcProcessorConfig(active, &cfg_proc);
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
                                          const RcInputActiveConfig& active) {
    // Build channel snapshot from SBUS frame
    RcChannelSnapshot snap = {};
    snap.valid = true;
    snap.mode  = static_cast<RcInputMode>(active.mode);
    for (int i = 0; i < 16; ++i) snap.channels[i] = data.ch[i];
    snap.channels[16] = data.ch17 ? 1811 : 172;
    snap.channels[17] = data.ch18 ? 1811 : 172;

    static RcProcessorConfig cfg = {};
    buildRcProcessorConfig(active, &cfg);
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
    // Feed immediately after add: the add-to-first-feed window must stay empty
    // of anything that can stall (the first-iteration HWM log runs inside it,
    // #245 defect 1).
    esp_task_wdt_add(NULL);
    esp_task_wdt_reset();

    rcInputProcessorInit(&s_rcProcessor);
    rcInputStepInit(&s_rcStepState);

    RcInputActiveConfig active = {};
    configCacheReadActiveRcInput(&active);
    const RcInputMode rcInputMode = static_cast<RcInputMode>(active.mode);
    const bool useCh2 = active.useCh2;
    const RcInputStartupPlan startupPlan = rcInputStepStartupPlan(active);

    bool driveSbusEnabled = false;
    if (startupPlan.driveSbusEnabled) {
        bool useDriveSbus2 = (rcInputMode == RC_INPUT_SINGLE_SBUS) && useCh2;
        int sbusRxPin = useDriveSbus2 ? PIN_SBUS2_RX : PIN_SBUS1_RX;
        if (!sbus_drive.begin(sbusRxPin)) {
            PA_LOG_ERROR(TAG, "RMT init failed for SBUS%d GPIO%d", useDriveSbus2 ? 2 : 1,
                         sbusRxPin);
        } else {
            driveSbusEnabled = true;
        }
    }

    bool domeSbusEnabled = false;
    if (startupPlan.domeSbusEnabled) {
        if (!sbus_dome.begin(PIN_SBUS2_RX)) {
            PA_LOG_ERROR(TAG, "RMT init failed for SBUS2 GPIO%d", PIN_SBUS2_RX);
        } else {
            domeSbusEnabled = true;
        }
    }

    const bool domeOutputActive = configCacheReadActiveDomeEnabled();
    const bool driveWatchdogEnabled = startupPlan.driveWatchdogSource != DriveWatchdogSource::NONE && driveSbusEnabled;

    if (rcInputMode == RC_INPUT_STANDARD_PWM) {
        PA_LOG_INFO(TAG, "started - standard_pwm mode, SBUS decoders inactive");
    } else if (rcInputMode == RC_INPUT_SINGLE_SBUS) {
        if (driveSbusEnabled) {
            int sbusRxPin = useCh2 ? PIN_SBUS2_RX : PIN_SBUS1_RX;
            PA_LOG_INFO(TAG, "started - single_sbus mode, SBUS%d GPIO%d active", useCh2 ? 2 : 1,
                        sbusRxPin);
        } else {
            PA_LOG_INFO(TAG, "started - single_sbus mode, SBUS%d disabled",
                        useCh2 ? 2 : 1);
        }
    } else {
        if (!driveSbusEnabled)
            PA_LOG_INFO(TAG, "started - dual_sbus mode, SBUS2 GPIO%d only (SBUS1 disabled)",
                        PIN_SBUS2_RX);
        else if (!domeSbusEnabled)
            PA_LOG_INFO(TAG, "started - dual_sbus mode, SBUS1 GPIO%d only (SBUS2 disabled)",
                        PIN_SBUS1_RX);
        else
            PA_LOG_INFO(TAG, "started - dual_sbus mode, SBUS1 GPIO%d + SBUS2 GPIO%d active",
                        PIN_SBUS1_RX, PIN_SBUS2_RX);
    }

    bool hwmLogged = false;

    uint32_t lastSbusDiagLogMs = 0;
    constexpr uint32_t kWatchdogDiagIntervalMs = 5000U;
    uint32_t lastSbus1WatchdogDiagMs = 0;
    uint32_t lastSbus2WatchdogDiagMs = 0;
    while (true) {
        if (!hwmLogged) {
            PA_LOG_DEBUG(TAG, "stack HWM: %u bytes free",
                         (unsigned)uxTaskGetStackHighWaterMark(NULL));
            hwmLogged = true;
        }

        // Handle PWM mode early exit (dispatch and delay)
        if (rcInputMode == RC_INPUT_STANDARD_PWM) {
            dispatchStandardPwmInputs(active);
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
                // Routed receiver (single_sbus + useCh2): read the previous hw failsafe state
                // for edge detection. Step function uses this to gate one-shot logs.
                bool routedHwFailsafeWasActive = s_rcStepState.routedHwFailsafeWasActive;

                RcInputStepSbus2FrameInputs frameIn = {
                    .failsafe = data.failsafe,
                    .lostFrame = data.lost_frame,
                    .hwFailsafeWasActive = routedHwFailsafeWasActive,
                };
                RcInputStepSbus2FrameActions frameOut = rcInputStepSbus2RoutedFrame(frameIn);

                // Routed receiver: execute drive-level hardware failsafe actions
                // that mirror SBUS1 behavior (failsafeTrigger, zero submit, clear).
                if (frameOut.triggerSbusHw) {
                    failsafeTrigger(FailsafeLayer::SBUS_HW);
                }
                if (frameOut.submitDriveZeroFrame) {
                    driveArbiterSubmit(DriveSource::RC, 0, 0, millis());
                }
                if (frameOut.logHwFailsafeAsserted) {
                    PA_LOG_WARN(TAG, "SBUS2 (routed) hardware failsafe asserted");
                }
                if (frameOut.clearSbusHw) {
                    failsafeClear(FailsafeLayer::SBUS_HW);
                }
                if (frameOut.logRoutedHwFailsafeClearedOnFallingEdge) {
                    PA_LOG_INFO(TAG, "SBUS2 (routed) hardware failsafe cleared");
                }

                taskENTER_CRITICAL(&robotStateMux);
                for (int i = 0; i < 16; ++i) {
                    robotState.rcSbus2Raw[i] = (uint16_t)data.ch[i];
                }
                robotState.rcSbus2Digital[0] = data.ch17;
                robotState.rcSbus2Digital[1] = data.ch18;
                if (frameOut.setSbus2HwFailsafe) {
                    robotState.sbus2HwFailsafe = true;
                }
                if (frameOut.clearSbus2HwFailsafe) {
                    robotState.sbus2HwFailsafe = false;
                }
                if (frameOut.clearSbus2SignalLost) {
                    robotState.sbus2SignalLost = false;
                }
                if (frameOut.incrementLostFrameCount) {
                    robotState.sbus2LostFrameCount++;
                }
                if (frameOut.updateLastSbus2Ms) {
                    robotState.lastSbus2Ms = millis();
                }
                // Routed receiver: latch the hw-failsafe edge state for next iteration.
                // Latch latches across lost_frame (do not update), only changes on
                // non-lost_frame frames (failsafe flag determines next state).
                if (!data.lost_frame) {
                    s_rcStepState.routedHwFailsafeWasActive = data.failsafe;
                }
                taskEXIT_CRITICAL(&robotStateMux);
                if (frameOut.dispatchBindings) {
                    dispatchSbusBindingsForSource(data, RC_BINDING_SBUS2, active);
                }
            } else {
                taskENTER_CRITICAL(&robotStateMux);
                robotState.lastSbus1Ms = millis();
                for (int i = 0; i < 16; ++i) {
                    robotState.rcSbus1Raw[i] = (uint16_t)data.ch[i];
                }
                robotState.rcSbus1Digital[0] = data.ch17;
                robotState.rcSbus1Digital[1] = data.ch18;
                bool hwFailsafeWasActive = robotState.sbusHwFailsafe;
                taskEXIT_CRITICAL(&robotStateMux);

                RcInputStepSbus1FrameInputs frameIn = {
                    .failsafe = data.failsafe,
                    .lostFrame = data.lost_frame,
                    .hwFailsafeWasActive = hwFailsafeWasActive,
                };
                RcInputStepSbus1FrameActions frameOut = rcInputStepSbus1Frame(frameIn);

                if (frameOut.triggerSbusHw) {
                    failsafeTrigger(FailsafeLayer::SBUS_HW);
                }
                if (frameOut.logHwFailsafeAsserted) {
                    PA_LOG_WARN(TAG, "SBUS1 hardware failsafe asserted");
                }
                if (frameOut.submitDriveZeroFrame) {
                    driveArbiterSubmit(DriveSource::RC, 0, 0, millis());
                }
                if (frameOut.incrementLostFrameCount) {
                    taskENTER_CRITICAL(&robotStateMux);
                    robotState.sbus1LostFrameCount++;
                    uint32_t lostCount = robotState.sbus1LostFrameCount;
                    bool rcDebug = robotState.rcDebugMode;
                    taskEXIT_CRITICAL(&robotStateMux);
                    if (rcDebug && (lostCount % 100 == 0)) {
                        PA_LOG_DEBUG(TAG, "SBUS1 lost_frame count: %lu", (unsigned long)lostCount);
                    }
                }
                if (frameOut.clearSbusHw) {
                    failsafeClear(FailsafeLayer::SBUS_HW);
                }
                if (frameOut.clearSbusWatchdog) {
                    failsafeClear(FailsafeLayer::SBUS_WATCHDOG);
                }
                if (frameOut.dispatchBindings) {
                    dispatchSbusBindingsForSource(data, RC_BINDING_SBUS1, active);
                }
            }
        }

        uint32_t nowMs = millis();
        uint32_t timeoutMs = 0;
        if (driveWatchdogEnabled || domeSbusEnabled) {
            ConfigSnapshot watchdogCfg = {};
            configCacheRead(&watchdogCfg);
            timeoutMs = watchdogCfg.drive.sbusTimeoutMs;
        }

        uint32_t lastSbus1 = 0;
        uint32_t lastSbus2ForDrive = 0;  // Used only if source == SBUS2_ROUTED
        if (driveWatchdogEnabled) {
            taskENTER_CRITICAL(&robotStateMux);
            lastSbus1 = robotState.lastSbus1Ms;
            if (startupPlan.driveWatchdogSource == DriveWatchdogSource::SBUS2_ROUTED) {
                lastSbus2ForDrive = robotState.lastSbus2Ms;
            }
            taskEXIT_CRITICAL(&robotStateMux);

            RcInputStepDriveWatchdogInputs stepDriveIn = {
                .driveDecoderInitialized = true,
                .source = startupPlan.driveWatchdogSource,
                .lastSbus1Ms = lastSbus1,
                .lastSbus2Ms = lastSbus2ForDrive,
                .nowMs = nowMs,
                .timeoutMs = timeoutMs,
            };
            RcInputStepDriveWatchdogActions stepDriveOut =
                rcInputStepDriveWatchdog(&s_rcStepState, stepDriveIn);

            if (stepDriveOut.triggerSbusWatchdog) {
                failsafeTrigger(FailsafeLayer::SBUS_WATCHDOG);
                driveArbiterSubmit(DriveSource::RC, 0, 0, nowMs);
                uint32_t lastMs = (startupPlan.driveWatchdogSource == DriveWatchdogSource::SBUS2_ROUTED)
                                      ? lastSbus2ForDrive
                                      : lastSbus1;
                PA_LOG_WARN(TAG, "drive watchdog fired - no frame for %lu ms (timeout=%lu ms)",
                            (unsigned long)(nowMs - lastMs), (unsigned long)timeoutMs);
                if ((uint32_t)(nowMs - lastSbus1WatchdogDiagMs) >= kWatchdogDiagIntervalMs) {
                    lastSbus1WatchdogDiagMs = nowMs;
                    taskENTER_CRITICAL(&robotStateMux);
                    bool rcDebug = robotState.rcDebugMode;
                    taskEXIT_CRITICAL(&robotStateMux);
                    if (rcDebug) {
                        SbusDecoderDebugStats driveStats = sbus_drive.debugStats();
                        PA_LOG_DEBUG(
                            TAG,
                            "drive watchdog decode stats: rx_done=%lu queued=%lu short=%lu "
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
            if (stepDriveOut.clearSbusWatchdog) {
                failsafeClear(FailsafeLayer::SBUS_WATCHDOG);
            }
            if (stepDriveOut.transition == SbusWatchdogTransition::JUST_RESTORED) {
                const char* sourceStr = (startupPlan.driveWatchdogSource == DriveWatchdogSource::SBUS2_ROUTED)
                                            ? "routed SBUS2"
                                            : "SBUS1";
                PA_LOG_INFO(TAG, "drive signal restored (%s)", sourceStr);
            }
        }

        // --- Dome-spin receiver (SBUS #2) ---
        if (domeSbusEnabled && sbus_dome.read()) {
            SbusData data = sbus_dome.data();

            taskENTER_CRITICAL(&robotStateMux);
            bool wasSbus2HwFailsafe = robotState.sbus2HwFailsafe;
            taskEXIT_CRITICAL(&robotStateMux);

            RcInputStepSbus2FrameInputs frameIn = {
                .failsafe = data.failsafe,
                .lostFrame = data.lost_frame,
                .hwFailsafeWasActive = wasSbus2HwFailsafe,
            };
            RcInputStepSbus2FrameActions frameOut = rcInputStepSbus2Frame(frameIn);

            taskENTER_CRITICAL(&robotStateMux);
            if (frameOut.setSbus2HwFailsafe) {
                robotState.sbus2HwFailsafe = true;
            }
            if (frameOut.clearSbus2HwFailsafe) {
                robotState.sbus2HwFailsafe = false;
            }
            for (int i = 0; i < 16; ++i) {
                robotState.rcSbus2Raw[i] = (uint16_t)data.ch[i];
            }
            robotState.rcSbus2Digital[0] = data.ch17;
            robotState.rcSbus2Digital[1] = data.ch18;
            if (frameOut.incrementLostFrameCount) {
                robotState.sbus2LostFrameCount++;
            }
            if (frameOut.updateLastSbus2Ms) {
                robotState.lastSbus2Ms = millis();
            }
            taskEXIT_CRITICAL(&robotStateMux);

            if (frameOut.logHwFailsafeAsserted) {
                PA_LOG_WARN(TAG, "SBUS2 hardware failsafe asserted");
            }
            if (frameOut.dispatchBindings) {
                dispatchSbusBindingsForSource(data, RC_BINDING_SBUS2, active);
            }
        }

        uint32_t lastSbus2 = 0;
        if (domeSbusEnabled) {
            taskENTER_CRITICAL(&robotStateMux);
            lastSbus2 = robotState.lastSbus2Ms;
            taskEXIT_CRITICAL(&robotStateMux);

            RcInputStepSbus2WatchdogInputs stepSbus2In = {
                .domeSbusInitialized = true,
                .lastSbus2Ms = lastSbus2,
                .nowMs = nowMs,
                .timeoutMs = timeoutMs,
            };
            RcInputStepSbus2WatchdogActions stepSbus2Out =
                rcInputStepSbus2Watchdog(&s_rcStepState, stepSbus2In);

            if (stepSbus2Out.setSbus2SignalLost) {
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
            }
            if (stepSbus2Out.shouldStopDome && domeOutputActive) {
                DomeCommand stopCmd = {};
                stopCmd.speed = 0.0f;
                stopCmd.source = SRC_INTERNAL;
                stopCmd.timestampMs = nowMs;
                xQueueSend(domeCmdQueue, &stopCmd, 0);
            }
            if (stepSbus2Out.clearSbus2SignalLost) {
                taskENTER_CRITICAL(&robotStateMux);
                robotState.sbus2SignalLost = false;
                taskEXIT_CRITICAL(&robotStateMux);
            }
            if (stepSbus2Out.transition == SbusWatchdogTransition::JUST_RESTORED) {
                PA_LOG_INFO(TAG, "SBUS2 signal restored");
            }
        }

        if ((driveSbusEnabled || domeSbusEnabled) &&
            (uint32_t)(nowMs - lastSbusDiagLogMs) >= 2000U) {
            lastSbusDiagLogMs = nowMs;
            bool waitingDrive = driveWatchdogEnabled &&
                                ((startupPlan.driveWatchdogSource == DriveWatchdogSource::SBUS1 && lastSbus1 == 0) ||
                                 (startupPlan.driveWatchdogSource == DriveWatchdogSource::SBUS2_ROUTED && lastSbus2ForDrive == 0));
            bool waitingDome = domeSbusEnabled && (lastSbus2 == 0);
            if (waitingDrive || waitingDome) {
                taskENTER_CRITICAL(&robotStateMux);
                bool rcDebug = robotState.rcDebugMode;
                taskEXIT_CRITICAL(&robotStateMux);
                if (waitingDrive) {
                    const char* sourceStr = (startupPlan.driveWatchdogSource == DriveWatchdogSource::SBUS2_ROUTED)
                                                ? "routed SBUS2"
                                                : "SBUS1";
                    PA_LOG_INFO(TAG, "drive (%s) waiting for first frame", sourceStr);
                }
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
