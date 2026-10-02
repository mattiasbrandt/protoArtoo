// =============================================================================
// src/tasks/dome_task.cpp
//
// DomeTask  --  LEDC PWM control for dome rotation ESC (ISDT ESC70).
//
// ESC PWM semantics (standard RC PWM, 50 Hz):
//   1000us = full reverse / max brake
//   1500us = neutral / stop  <- safe idle output; emitted on disable, estop, timeout
//   2000us = full forward
//
// All pulse limits (neutral, min, max) and speed limit percentage are read from
// persisted config (cfg_dome_neutral_us, cfg_dome_min_pulse_us,
// cfg_dome_max_pulse_us, cfg_dome_speed_limit_pct) so individual ESC calibration
// can be trimmed via the Setup page without a firmware rebuild.
//
// ESC configuration (running mode, throttle calibration, PWM frequency, voltage
// cutoff) is handled exclusively via the ISD Go APP over Bluetooth  --  out of scope
// for this firmware. Throttle calibration via the ISD Go APP is a hardware
// bring-up prerequisite before the dome motor will respond correctly to our PWM.
//
// Feature toggle: cfg_enable_dome_esc (staged at reboot per ADR 0027) gates whether
// the task is spawned at all. When disabled at boot, DomeTask does not run.
//
// It also holds the Dome Bearing, the angle the dome believes it points at,
// integrated from the pulses written here (the section below setDomeSpeed()'s
// constants, and include/dome_bearing.h).
// =============================================================================

#include "dome_task.h"

#include <esp_task_wdt.h>

#include "config.h"
#include "config_cache.h"
#include "dome_bearing.h"
#include "dome_math.h"
#include "drive_motion.h"  // driveMotionIsDriving() - Resting Behaviour waits while driving
#include "ledc_pwm.h"
#include "logging.h"
#include "robot_state.h"

static const char* TAG = "DOME";

// -----------------------------------------------------------------------------
// Constants
// -----------------------------------------------------------------------------
#define DOME_COMMAND_TIMEOUT_MS 500  // Zero speed if no command for 500ms
#define ESC_ARMING_DURATION_MS 2000  // Time to hold neutral for arming

// -----------------------------------------------------------------------------
// The Dome Bearing (include/dome_bearing.h, ADR 0051 as amended 2026-09-30,
// #445)
//
// Every pulse the dome gets is written by this task - RC, web, sequences, the
// dome link, the Console and random movement all end in setDomeSpeed() or
// setDomeNeutral() - so this is the one place the belief can be integrated, and
// it is integrated from the pulse ACTUALLY written, after the speed limit has
// scaled it, never from a speed somebody asked for. Each write first moves the
// belief by what the pulse being replaced did over the measured time it was on
// the wire, and the loop moves it once a tick as well: a tick is vTaskDelay(20)
// after the loop's work, not a fixed period, so the elapsed time is always
// measured, never assumed.
//
// Only an estop, Sleep Mode and a boot forget it. A commanded stop does not:
// the coast after it is part of what "believed" admits (the 2026-09-30
// amendment). A boot forgets because nothing here survives one, and the
// RobotState mirror starts zeroed, which reads as unknown.
//
// The state is file-static rather than on domeTask()'s frame, which sits on
// every one of this task's measured chains (tools/task_stack_recipes.json), and
// the helpers below are leaves that never log, so none of them lengthens the log
// route that is this task's deepest. They run on this task only.
// -----------------------------------------------------------------------------
static struct {
    uint16_t pulseUs;  // the pulse on the wire since atMs
    uint32_t atMs;     // when the belief was last moved
    bool     believed;
    float    deg;      // meaningful only while believed
    // The last tick an estop or Sleep Mode held, 0 for none since boot. A
    // Front is here pressed at or before it is stale: the dome may have
    // coasted since (bearingDeclareFront()).
    uint32_t forgotAtMs;
} s_bearing = {0, 0, false, 0.0f, 0};

static DomeTurnCalibration domeCalibrationOf(const DomeConfig& cfg) {
    return {cfg.dome_neutral_us,   cfg.dome_min_pulse_us,  cfg.dome_max_pulse_us,
            cfg.dome_full_turn_ms, cfg.dome_full_turn_pct, cfg.dome_positive_turn};
}

// The RobotState mirror, which every surface reads through domeBearingRead().
static void bearingPublish() {
    taskENTER_CRITICAL(&robotStateMux);
    robotState.domeBearingBelieved = s_bearing.believed;
    robotState.domeBearingDeg = s_bearing.deg;
    taskEXIT_CRITICAL(&robotStateMux);
}

// Move the belief by the turn the pulse on the wire made since it was last
// moved. A belief whose calibration has been cleared is dropped: the turn it
// would need to follow cannot be followed, and a number that stops moving while
// the dome does is the one failure ADR 0051 exists to prevent. ("Front is here"
// is refused without a calibration, so this is reached only by clearing one.)
static void __attribute__((noinline)) bearingAdvance(const DomeConfig& cfg, uint32_t nowMs) {
    const uint32_t elapsedMs = nowMs - s_bearing.atMs;
    s_bearing.atMs = nowMs;
    if (!s_bearing.believed) {
        return;
    }
    const DomeTurnCalibration cal = domeCalibrationOf(cfg);
    if (!domeTurnCalibrated(cal)) {
        s_bearing.believed = false;
    } else {
        s_bearing.deg = domeBearingWrap(
            s_bearing.deg + domeBearingRateDegPerMs(s_bearing.pulseUs, cal) * (float)elapsedMs);
    }
    bearingPublish();
}

// Once a tick: the turn so far, and then the estop or Sleep Mode forgetting it.
// Every tick either holds, not only the one that wrote neutral - the estop
// branch below writes neutral only when the dome was turning, and Sleep Mode's
// skips the rest of the loop.
static void __attribute__((noinline)) bearingTick(bool forget) {
    DomeConfig cfg = {};
    configCacheReadDome(&cfg);
    const uint32_t nowMs = millis();
    bearingAdvance(cfg, nowMs);
    if (!forget) {
        return;
    }
    s_bearing.forgotAtMs = nowMs;
    if (s_bearing.believed) {
        s_bearing.believed = false;
        bearingPublish();
    }
}

// "Front is here": the builder turned the dome to front and says so. Refused -
// false - without a calibration, which no belief could be integrated from, and
// when it was pressed at or before the last tick an estop or Sleep Mode held.
// The estop leaves this task's queue undrained, so a press the route accepted
// just before an estop latched waits there, and taken after the clear it would
// call a coasted dome front: the bearing would silently become a number again.
// Every sender stamps timestampMs when it sends; the comparison is wrap-safe.
static bool __attribute__((noinline)) bearingDeclareFront(uint32_t pressedAtMs) {
    if (s_bearing.forgotAtMs != 0 && (int32_t)(pressedAtMs - s_bearing.forgotAtMs) <= 0) {
        return false;
    }
    DomeConfig cfg = {};
    configCacheReadDome(&cfg);
    if (!domeTurnCalibrated(domeCalibrationOf(cfg))) {
        return false;
    }
    s_bearing.atMs = millis();
    s_bearing.believed = true;
    s_bearing.deg = 0.0f;
    bearingPublish();
    return true;
}

// A turn to a Dome Bearing, rewritten in place into the timed turn that makes
// it (domeBearingTurnPlan()): the short way, at the speed the full turn was
// timed at, stopped on time - which the timed-turn path below already does. A
// plan with nothing to turn is a stop. Returns why it cannot be planned, or
// nullptr. The caller asked the same questions before it sent this; they are
// asked again here because the answers may have changed on the way.
static const char* __attribute__((noinline)) bearingPlanTurn(DomeCommand* cmd) {
    DomeConfig cfg = {};
    configCacheReadDome(&cfg);
    const DomeTurnCalibration cal = domeCalibrationOf(cfg);
    if (!domeTurnCalibrated(cal)) {
        return "dome not calibrated";
    }
    if (!s_bearing.believed) {
        return "bearing unknown";
    }
    const DomeTurnPlan plan = domeBearingTurnPlan(s_bearing.deg, (float)cmd->targetTenths / 10.0f,
                                                  cal, cfg.dome_speed_limit_pct);
    cmd->kind = DOME_CMD_SPEED;
    cmd->speed = plan.speed;
    cmd->durationMs = plan.durationMs;
    return nullptr;
}

// The one pulse write: the belief catches up on the pulse it replaces first.
static void writeDomePulse(const DomeConfig& cfg, uint16_t pulseUs) {
    bearingAdvance(cfg, millis());
    ledcPwmSetPulseWidth(LEDC_CH_DOME, pulseUs);
    s_bearing.pulseUs = pulseUs;
}

// -----------------------------------------------------------------------------
// setDomeSpeed()
// Read persisted ESC config, compute pulse width, and output via LEDC.
// -----------------------------------------------------------------------------
static void setDomeSpeed(float speed) {
    DomeConfig cfg = {};
    configCacheReadDome(&cfg);

    uint16_t pulseUs = domeSpeedToPulseUs(speed, cfg.dome_neutral_us, cfg.dome_min_pulse_us,
                                          cfg.dome_max_pulse_us, cfg.dome_speed_limit_pct);

    writeDomePulse(cfg, pulseUs);

    taskENTER_CRITICAL(&robotStateMux);
    robotState.domeTargetSpeed = speed;
    taskEXIT_CRITICAL(&robotStateMux);

    PA_LOG_DEBUG(TAG, "Dome speed %d%% -> %d us (neutral=%d min=%d max=%d lim=%d%%)",
                 (int)(speed * 100.0f), (int)pulseUs, (int)cfg.dome_neutral_us,
                 (int)cfg.dome_min_pulse_us, (int)cfg.dome_max_pulse_us,
                 (int)cfg.dome_speed_limit_pct);
}

// -----------------------------------------------------------------------------
// setDomeNeutral()
// Output the configured neutral pulse  --  safe idle output.
// Used on disable, estop, timeout, and startup.
// -----------------------------------------------------------------------------
static void setDomeNeutral() {
    DomeConfig cfg = {};
    configCacheReadDome(&cfg);

    writeDomePulse(cfg, cfg.dome_neutral_us);

    taskENTER_CRITICAL(&robotStateMux);
    robotState.domeTargetSpeed = 0.0f;
    taskEXIT_CRITICAL(&robotStateMux);
}

// -----------------------------------------------------------------------------
// domeTaskInit()
// Initialize dome ESC with auto-arm sequence.
// Outputs the configured neutral pulse for ESC arming.
// -----------------------------------------------------------------------------
void domeTaskInit() {
    // Feature toggle: skip ESC arming entirely when dome is disabled.
    // No LEDC pulse is written; the channel stays at whatever neutral value
    // ledcPwmInit() set at boot.  domeTask() will also idle (see task body).
    DomeConfig cfg = {};
    configCacheReadDome(&cfg);
    bool enabled = configCacheDomeEnabled();
    uint16_t neutralUs = cfg.dome_neutral_us;

    if (!enabled) {
        PA_LOG_INFO(TAG, "dome disabled");
        return;
    }

    ledcPwmSetPulseWidth(LEDC_CH_DOME, neutralUs);
    PA_LOG_INFO(TAG, "Dome ESC arming (neutral=%d us for %d ms)", (int)neutralUs,
                ESC_ARMING_DURATION_MS);

    delay(ESC_ARMING_DURATION_MS);

    PA_LOG_INFO(TAG, "Dome ESC armed and ready");
}

// -----------------------------------------------------------------------------
// domeTask()
// Main dome task loop. Task is only spawned when dome output is enabled at boot
// (staged at reboot per ADR 0027).
// -----------------------------------------------------------------------------
void domeTask(void* pvParameters) {
    (void)pvParameters;

    // Register with task watchdog, and feed immediately: the 3 s window starts
    // at add, and everything between add and the loop's feed (the first-iteration
    // HWM log in particular) runs inside it. A stalled log write in that window
    // is exactly how this task tripped the TWDT on the ESP32-P4 (#245 defect 1).
    esp_task_wdt_add(NULL);
    esp_task_wdt_reset();

    DomeCommand cmd;
    float currentSpeed = 0.0f;
    uint32_t lastCommandMs = 0;
    bool hasCommand = false;

    setDomeNeutral();

    bool hwmLogged = false;
    bool sleepHolding = false;
    uint32_t seqMoveUntilMs = 0;

    while (true) {
        if (!hwmLogged) {
            PA_LOG_DEBUG(TAG, "stack HWM: %u bytes free",
                         (unsigned)uxTaskGetStackHighWaterMark(NULL));
            hwmLogged = true;
        }

        // Read safety state under mutex
        taskENTER_CRITICAL(&robotStateMux);
        bool estop = robotState.estop;
        bool sleepMode = robotState.sleepMode;
        taskEXIT_CRITICAL(&robotStateMux);

        bearingTick(estop || sleepMode);

        if (sleepMode) {
            seqMoveUntilMs = 0;
            if (!sleepHolding || currentSpeed != 0.0f) {
                currentSpeed = 0.0f;
                setDomeNeutral();
            }
            if (!sleepHolding) {
                PA_LOG_INFO(TAG, "Sleep mode active - dome neutral");
                sleepHolding = true;
            }

            while (xQueueReceive(domeCmdQueue, &cmd, 0) == pdTRUE) {
                // discard while sleeping
            }
            hasCommand = false;
            esp_task_wdt_reset();
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (sleepHolding) {
            sleepHolding = false;
            PA_LOG_INFO(TAG, "Sleep mode cleared - dome command processing resumed");
        }

        // Safety: estop  --  force neutral while emergency stopped
        if (estop && currentSpeed != 0.0f) {
            currentSpeed = 0.0f;
            setDomeNeutral();
            seqMoveUntilMs = 0;
            PA_LOG_WARN(TAG, "Estop active - dome neutral");
        }

        bool manualCommandThisTick = false;

        // Process any pending commands (non-blocking), skip if estop
        while (!estop && xQueueReceive(domeCmdQueue, &cmd, 0) == pdTRUE) {
            if (cmd.kind == DOME_CMD_FRONT_IS_HERE) {
                if (bearingDeclareFront(cmd.timestampMs)) {
                    PA_LOG_INFO(TAG, "[%s] front is here", commandSourceToString(cmd.source));
                } else {
                    PA_LOG_INFO(TAG, "[%s] front not taken - pressed before a halt, or not calibrated",
                                commandSourceToString(cmd.source));
                }
                continue;
            }
            if (cmd.kind == DOME_CMD_TURN_TO) {
                const char* notTurned = bearingPlanTurn(&cmd);
                if (notTurned != nullptr) {
                    PA_LOG_INFO(TAG, "[%s] dome not turned - %s", commandSourceToString(cmd.source),
                                notTurned);
                    continue;
                }
            }
            if (cmd.kind != DOME_CMD_SPEED) {
                continue;  // a kind this build does not know moves nothing
            }
            currentSpeed = cmd.speed;
            lastCommandMs = millis();
            hasCommand = true;
            seqMoveUntilMs = (cmd.durationMs > 0) ? (millis() + cmd.durationMs) : 0;
            if (cmd.speed != 0.0f) {
                manualCommandThisTick = true;
            }

            setDomeSpeed(currentSpeed);
            if (currentSpeed != 0.0f) {
                PA_LOG_INFO(TAG, "[%s] Dome command: speed %d%%",
                            commandSourceToString(cmd.source), (int)(cmd.speed * 100.0f));
            }
        }

        // Timed dome-seq rotation: keep lastCommandMs fresh (prevents 500 ms timeout)
        // and auto-stop when duration expires.
        if (seqMoveUntilMs > 0) {
            uint32_t tNow = millis();
            if ((int32_t)(tNow - seqMoveUntilMs) >= 0) {
                currentSpeed  = 0.0f;
                hasCommand    = false;
                seqMoveUntilMs = 0;
                setDomeNeutral();
                PA_LOG_INFO(TAG, "dome seq rot complete");
            } else {
                lastCommandMs = tNow;
            }
        }

        // Check for command timeout (failsafe)  --  output neutral, never float
        if (hasCommand && (millis() - lastCommandMs) > DOME_COMMAND_TIMEOUT_MS) {
            if (currentSpeed != 0.0f) {
                currentSpeed = 0.0f;
                setDomeNeutral();
                PA_LOG_INFO(TAG, "Command timeout - dome neutral");
            }
            hasCommand = false;
        }

        // Random dome idle rotation state machine
        //
        // It is Resting Behaviour, so it waits while the droid is driving.
        //
        // How often it turns follows the Mood (#452): domeRndPauseMsForMood()
        // scales the stored pause window, and Quiet starts no move at all.
        // Quiet goes through the same not-active branch as Sleep and Estop, so
        // a move in progress ends at neutral; leaving Quiet draws a fresh
        // pause, as any return to active does.
        {
            enum DomeRndState : uint8_t { DOME_RND_PAUSING = 0, DOME_RND_MOVING };
            static DomeRndState rndState    = DOME_RND_PAUSING;
            static uint32_t     rndNextMs   = 0;
            static float        rndSpeed    = 0.0f;
            static bool         rndWasActive = false;
            static uint8_t      rndPauseMood = 0;  // Mood the running pause was drawn under
            static DriveMotion  driveMotion  = {};  // this task's reading of "driving"

            bool     rndEnabled;
            uint8_t  rndSpeedPct, rndPauseMin, rndPauseMax;
            uint16_t rndMoveMs;
            bool     domeSeqActive;
            uint8_t  mood;
            DriveMotionReading drive;
            uint32_t now = millis();
            DomeConfig rndCfg = {};
            configCacheReadDome(&rndCfg);
            rndEnabled    = rndCfg.dome_rnd_enable;
            rndSpeedPct   = rndCfg.dome_rnd_speed_pct;
            rndPauseMin   = rndCfg.dome_rnd_pause_min;
            rndPauseMax   = rndCfg.dome_rnd_pause_max;
            rndMoveMs     = rndCfg.dome_rnd_move_ms;
            taskENTER_CRITICAL(&robotStateMux);
            domeSeqActive = robotState.domeSeqActive;
            mood          = robotState.activeMood;
            drive.driveSpeed    = robotState.driveOutputSpeed;
            drive.driveSteer    = robotState.driveOutputSteer;
            drive.feedbackValid = robotState.driveFeedbackValid;
            drive.wheelSpeedL   = robotState.driveFeedbackSpeedL;
            drive.wheelSpeedR   = robotState.driveFeedbackSpeedR;
            taskEXIT_CRITICAL(&robotStateMux);

            // Resting Behaviour is held while the droid is driving (CONTEXT.md,
            // #450) - commanded, still rolling, or just stopped
            // (include/drive_motion.h). Driving goes through the same
            // not-active branch, and the first tick at rest draws a fresh
            // pause, so the dome does not turn on the tick the droid stops.
            const bool driving = driveMotionIsDriving(&driveMotion, drive, now);
            // A timed one-shot turn - Go home, a bearing or timed sequence turn -
            // holds it too, as domeSeqActive holds it for a sequence: Go home
            // from the API or the Console sets no domeSeqActive, and half a turn
            // can outlast a redrawn pause, so a random move would start mid-turn
            // and the turn would land short (#445).
            if (rndEnabled && domeRndMoodStartsMoves(mood) && !sleepMode && !estop &&
                !domeSeqActive && !driving && seqMoveUntilMs == 0) {
                // Every pause below is drawn at the Mood the droid is in now, and
                // records it in rndPauseMood so a later change can be noticed.
                if (!rndWasActive) {
                    // Conditions just became active  --  set initial pause before first move.
                    rndState     = DOME_RND_PAUSING;
                    rndNextMs    = now + domeRndPauseMsForMood(rndPauseMin, rndPauseMax, mood,
                                                               esp_random());
                    rndPauseMood = mood;
                    rndWasActive = true;
                } else if (manualCommandThisTick) {
                    // No setDomeNeutral(): the manual command owns the dome now.
                    rndState     = DOME_RND_PAUSING;
                    rndNextMs    = now + domeRndPauseMsForMood(rndPauseMin, rndPauseMax, mood,
                                                               esp_random());
                    rndPauseMood = mood;
                } else if (rndState == DOME_RND_PAUSING && mood != rndPauseMood) {
                    // The Mood changed mid-pause: the next move comes at the new
                    // Mood's pace, not after the old pause runs out. A change
                    // mid-move needs nothing here - the move keeps its duration
                    // and the pause after it is drawn at the new Mood below.
                    rndNextMs    = now + domeRndPauseMsForMood(rndPauseMin, rndPauseMax, mood,
                                                               esp_random());
                    rndPauseMood = mood;
                } else if (rndState == DOME_RND_PAUSING && (int32_t)(now - rndNextMs) >= 0) {
                    rndSpeed      = ((float)rndSpeedPct / 100.0f) * ((esp_random() & 1) ? 1.0f : -1.0f);
                    currentSpeed  = rndSpeed;
                    lastCommandMs = now;
                    hasCommand    = true;
                    rndState      = DOME_RND_MOVING;
                    rndNextMs     = now + (uint32_t)rndMoveMs;
                    setDomeSpeed(rndSpeed);
                    PA_LOG_INFO(TAG, "dome rnd move: %d%%", (int)(rndSpeed * 100.0f));
                } else if (rndState == DOME_RND_MOVING) {
                    if ((int32_t)(now - rndNextMs) >= 0) {
                        currentSpeed = 0.0f;
                        setDomeNeutral();
                        hasCommand   = false;
                        rndState     = DOME_RND_PAUSING;
                        rndNextMs    = now + domeRndPauseMsForMood(rndPauseMin, rndPauseMax, mood,
                                                                   esp_random());
                        rndPauseMood = mood;
                    } else {
                        lastCommandMs = now;  // prevent 500 ms manual timeout during random move
                    }
                }
            } else {
                rndWasActive = false;
                if (rndState == DOME_RND_MOVING) {
                    // The random turn ends either way. A manual command taken
                    // this tick keeps the speed it set: writing neutral here
                    // would lose it, and a one-shot command for good.
                    if (domeRndStandDownGoesNeutral(manualCommandThisTick)) {
                        currentSpeed = 0.0f;
                        setDomeNeutral();
                        hasCommand = false;
                    }
                    rndNextMs  = now;
                    rndState   = DOME_RND_PAUSING;
                }
            }
        }

        // Feed watchdog
        esp_task_wdt_reset();

        // 50Hz update rate (standard RC servo/ESC frequency)
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
