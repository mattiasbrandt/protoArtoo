// =============================================================================
// src/tasks/dome_task.cpp
//
// DomeTask — LEDC PWM control for dome rotation ESC (ISDT ESC70).
//
// ESC PWM semantics (standard RC PWM, 50 Hz):
//   1000µs = full reverse / max brake
//   1500µs = neutral / stop  ← safe idle output; emitted on disable, estop, timeout
//   2000µs = full forward
//
// All pulse limits (neutral, min, max) and speed limit percentage are read from
// persisted config (cfg_dome_neutral_us, cfg_dome_min_pulse_us,
// cfg_dome_max_pulse_us, cfg_dome_speed_limit_pct) so individual ESC calibration
// can be trimmed via the Setup page without a firmware rebuild.
//
// ESC configuration (running mode, throttle calibration, PWM frequency, voltage
// cutoff) is handled exclusively via the ISD Go APP over Bluetooth — out of scope
// for this firmware. Throttle calibration via the ISD Go APP is a hardware
// bring-up prerequisite before the dome motor will respond correctly to our PWM.
//
// Feature toggle: cfg_enable_dome must be true for the task to process commands
// or actuate the ESC. When disabled, the task holds neutral output and discards
// all queued commands.
// =============================================================================

#include "dome_task.h"

#include <esp_task_wdt.h>

#include "config.h"
#include "config_store.h"
#include "dome_math.h"
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
// setDomeSpeed()
// Read persisted ESC config, compute pulse width, and output via LEDC.
// -----------------------------------------------------------------------------
static void setDomeSpeed(float speed) {
    DomeConfig cfg = {};
    configCacheReadDome(&cfg);

    uint16_t pulseUs = domeSpeedToPulseUs(speed, cfg.dome_neutral_us, cfg.dome_min_pulse_us,
                                          cfg.dome_max_pulse_us, cfg.dome_speed_limit_pct);

    ledcPwmSetPulseWidth(LEDC_CH_DOME, pulseUs);

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
// Output the configured neutral pulse — safe idle output.
// Used on disable, estop, timeout, and startup.
// -----------------------------------------------------------------------------
static void setDomeNeutral() {
    DomeConfig cfg = {};
    configCacheReadDome(&cfg);

    ledcPwmSetPulseWidth(LEDC_CH_DOME, cfg.dome_neutral_us);

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
// Main dome task loop.
//
// Feature toggle: when cfg_enable_dome is false, the task holds neutral output
// and discards all queued commands — the ESC is inert.
// -----------------------------------------------------------------------------
void domeTask(void* pvParameters) {
    (void)pvParameters;

    // Register with task watchdog
    esp_task_wdt_add(NULL);

    DomeCommand cmd;
    float currentSpeed = 0.0f;
    uint32_t lastCommandMs = 0;
    bool hasCommand = false;

    // Start at neutral only when dome output is enabled. If disabled, LEDC may
    // be intentionally uninitialized (all outputs disabled configuration).
    bool domeEnabledAtBoot = configCacheDomeEnabled();
    if (domeEnabledAtBoot) {
        setDomeNeutral();
    }

    bool hwmLogged = false;
    bool sleepHolding = false;

    while (true) {
        if (!hwmLogged) {
            PA_LOG_DEBUG(TAG, "stack HWM: %u words free",
                         (unsigned)uxTaskGetStackHighWaterMark(NULL));
            hwmLogged = true;
        }

        // Read safety state under mutex
        taskENTER_CRITICAL(&robotStateMux);
        bool estop = robotState.estop;
        bool sleepMode = robotState.sleepMode;
        taskEXIT_CRITICAL(&robotStateMux);
        bool enabled = configCacheDomeEnabled();

        // Feature toggle: dome disabled — hold neutral, drain queue, do nothing
        if (!enabled) {
            if (currentSpeed != 0.0f) {
                currentSpeed = 0.0f;
                setDomeNeutral();
                PA_LOG_INFO(TAG, "Dome disabled — holding neutral");
            }
            // Drain any queued commands so they don't accumulate
            while (xQueueReceive(domeCmdQueue, &cmd, 0) == pdTRUE) {
                // discard
            }
            hasCommand = false;
            sleepHolding = false;
            esp_task_wdt_reset();
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        if (sleepMode) {
            if (!sleepHolding || currentSpeed != 0.0f) {
                currentSpeed = 0.0f;
                setDomeNeutral();
            }
            if (!sleepHolding) {
                PA_LOG_INFO(TAG, "Sleep mode active — dome neutral");
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
            PA_LOG_INFO(TAG, "Sleep mode cleared — dome command processing resumed");
        }

        // Safety: estop — force neutral while emergency stopped
        if (estop && currentSpeed != 0.0f) {
            currentSpeed = 0.0f;
            setDomeNeutral();
            PA_LOG_WARN(TAG, "Estop active — dome neutral");
        }

        bool manualCommandThisTick = false;

        // Process any pending commands (non-blocking), skip if estop
        while (!estop && xQueueReceive(domeCmdQueue, &cmd, 0) == pdTRUE) {
            currentSpeed = cmd.speed;
            lastCommandMs = millis();
            hasCommand = true;
            manualCommandThisTick = true;

            setDomeSpeed(currentSpeed);
            if (currentSpeed != 0.0f) {
                PA_LOG_INFO(TAG, "[%s] Dome command: speed %d%%",
                            commandSourceToString(cmd.source), (int)(cmd.speed * 100.0f));
            }
        }

        // Check for command timeout (failsafe) — output neutral, never float
        if (hasCommand && (millis() - lastCommandMs) > DOME_COMMAND_TIMEOUT_MS) {
            if (currentSpeed != 0.0f) {
                currentSpeed = 0.0f;
                setDomeNeutral();
                PA_LOG_INFO(TAG, "Command timeout — dome neutral");
            }
            hasCommand = false;
        }

        // Random dome idle rotation state machine
        {
            enum DomeRndState : uint8_t { DOME_RND_PAUSING = 0, DOME_RND_MOVING };
            static DomeRndState rndState    = DOME_RND_PAUSING;
            static uint32_t     rndNextMs   = 0;
            static float        rndSpeed    = 0.0f;
            static bool         rndWasActive = false;

            bool     rndEnabled;
            uint8_t  rndSpeedPct, rndPauseMin, rndPauseMax;
            uint16_t rndMoveMs;
            bool     domeSeqActive;
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
            taskEXIT_CRITICAL(&robotStateMux);

            if (rndEnabled && enabled && !sleepMode && !estop && !domeSeqActive) {
                const uint32_t rndPauseRangeMs =
                    (rndPauseMax > rndPauseMin)
                        ? (uint32_t)(rndPauseMax - rndPauseMin) * 1000UL
                        : 0UL;

                if (!rndWasActive) {
                    // Conditions just became active — set initial pause before first move.
                    rndState    = DOME_RND_PAUSING;
                    rndNextMs   = now + (uint32_t)rndPauseMin * 1000UL +
                                  (rndPauseRangeMs > 0 ? (esp_random() % rndPauseRangeMs) : 0UL);
                    rndWasActive = true;
                } else if (manualCommandThisTick) {
                    rndState  = DOME_RND_PAUSING;
                    rndNextMs = now + (uint32_t)rndPauseMin * 1000UL +
                                (rndPauseRangeMs > 0 ? (esp_random() % rndPauseRangeMs) : 0UL);
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
                        hasCommand = false;
                        rndState   = DOME_RND_PAUSING;
                        rndNextMs  = now + (uint32_t)rndPauseMin * 1000UL +
                                     (rndPauseRangeMs > 0 ? (esp_random() % rndPauseRangeMs) : 0UL);
                    } else {
                        lastCommandMs = now;  // prevent 500 ms manual timeout during random move
                    }
                }
            } else {
                rndWasActive = false;
                if (rndState == DOME_RND_MOVING) {
                    currentSpeed = 0.0f;
                    setDomeNeutral();
                    hasCommand = false;
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
