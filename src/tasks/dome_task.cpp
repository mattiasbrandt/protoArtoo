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
    uint16_t neutralUs, minPulseUs, maxPulseUs;
    uint8_t speedLimitPct;

    taskENTER_CRITICAL(&robotStateMux);
    neutralUs = robotState.cfg_dome_neutral_us;
    minPulseUs = robotState.cfg_dome_min_pulse_us;
    maxPulseUs = robotState.cfg_dome_max_pulse_us;
    speedLimitPct = robotState.cfg_dome_speed_limit_pct;
    taskEXIT_CRITICAL(&robotStateMux);

    uint16_t pulseUs = domeSpeedToPulseUs(speed, neutralUs, minPulseUs, maxPulseUs, speedLimitPct);

    ledcPwmSetPulseWidth(LEDC_CH_DOME, pulseUs);

    taskENTER_CRITICAL(&robotStateMux);
    robotState.domeTargetSpeed = speed;
    taskEXIT_CRITICAL(&robotStateMux);

    PA_LOG_DEBUG(TAG, "Dome speed %d%% -> %d us (neutral=%d min=%d max=%d lim=%d%%)",
                 (int)(speed * 100.0f), (int)pulseUs, (int)neutralUs,
                 (int)minPulseUs, (int)maxPulseUs, (int)speedLimitPct);
}

// -----------------------------------------------------------------------------
// setDomeNeutral()
// Output the configured neutral pulse — safe idle output.
// Used on disable, estop, timeout, and startup.
// -----------------------------------------------------------------------------
static void setDomeNeutral() {
    uint16_t neutralUs;
    taskENTER_CRITICAL(&robotStateMux);
    neutralUs = robotState.cfg_dome_neutral_us;
    taskEXIT_CRITICAL(&robotStateMux);

    ledcPwmSetPulseWidth(LEDC_CH_DOME, neutralUs);

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
    taskENTER_CRITICAL(&robotStateMux);
    bool enabled = robotState.cfg_enable_dome;
    uint16_t neutralUs = robotState.cfg_dome_neutral_us;
    taskEXIT_CRITICAL(&robotStateMux);

    if (!enabled) {
        PA_LOG_INFO(TAG, "dome disabled (en_dome=false) — skipping ESC arming");
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
    taskENTER_CRITICAL(&robotStateMux);
    bool domeEnabledAtBoot = robotState.cfg_enable_dome;
    taskEXIT_CRITICAL(&robotStateMux);
    if (domeEnabledAtBoot) {
        setDomeNeutral();
    }

    bool hwmLogged = false;
    bool sleepHolding = false;

    while (true) {
        if (!hwmLogged) {
            PA_LOG_INFO(TAG, "stack HWM: %u words free",
                        (unsigned)uxTaskGetStackHighWaterMark(NULL));
            hwmLogged = true;
        }

        // Read safety state under mutex
        taskENTER_CRITICAL(&robotStateMux);
        bool estop = robotState.estop;
        bool enabled = robotState.cfg_enable_dome;
        bool sleepMode = robotState.sleepMode;
        taskEXIT_CRITICAL(&robotStateMux);

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

        // Process any pending commands (non-blocking), skip if estop
        while (!estop && xQueueReceive(domeCmdQueue, &cmd, 0) == pdTRUE) {
            currentSpeed = cmd.speed;
            lastCommandMs = millis();
            hasCommand = true;

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

        // Feed watchdog
        esp_task_wdt_reset();

        // 50Hz update rate (standard RC servo/ESC frequency)
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
