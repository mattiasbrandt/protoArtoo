// =============================================================================
// src/tasks/drive.cpp
//
// DriveTask  --  feeds the Foot Drive backend one frame every tick at 50 Hz.
// Owns the drive lane this Board Variant declares (UART_PORT_DRIVE on
// PIN_DRIVE_TX / PIN_DRIVE_RX); what is spoken over it is the backend's, not
// this task's -- include/drive_backend.h.
// Runs on Core 1 (real-time).
//
// Safety layers implemented here:
//   Layer 3: Web API drive timeout (DriveArbiter detects; FailsafeGate owns the state)
//   Layer 4: TWDT feed (esp_task_wdt_reset every loop)
//
// SAFETY: SPEED_LIMIT_MAX cap applied unconditionally before every frame.
// SAFETY: Zero frames sent when any failsafe is active (never silent).
// Both are settled above the backend seam and no backend can reach them.
// =============================================================================

#include <Arduino.h>
#include <esp_task_wdt.h>

#include "config.h"
#include "config_cache.h"
#include "drive_arbiter.h"
#include "drive.h"
#include "drive_backend.h"
#include "drive_frame_emit.h"
#include "failsafe_gate.h"
#include "logging.h"
#include "robot_state.h"

static const char* TAG = "DriveTask";

// The drive lane is dedicated to the Foot Drive backend and shared with
// nothing (include/config.h static_asserts that). Which controller index and
// which GPIO pair it is are Board Lane facts declared per Board Variant, not
// one board's traced routing: GPIO 16/17 on artoo-esp32, 20/21 on firebeetle2.
static HardwareSerial driveSerial(UART_PORT_DRIVE);

// -----------------------------------------------------------------------------
// driveTask()
// Sends one backend frame at DRIVE_FREQ_HZ (50 Hz), every tick, unconditionally.
// Registers with TWDT on entry  --  if this loop hangs, chip resets in 3 s.
// Applies SPEED_LIMIT_MAX cap and all active failsafe overrides every frame.
// Thread safety: all RobotState reads/writes use taskENTER/EXIT_CRITICAL.
// -----------------------------------------------------------------------------
void driveTask(void* pvParameters) {
    // Register with TWDT unconditionally  --  this task must feed the watchdog
    // regardless of enable state or the chip will reset after WATCHDOG_TIMEOUT_S.
    // Feed immediately after add: the add-to-first-feed window must stay empty
    // of anything that can stall (#245 defect 1).
    esp_task_wdt_add(NULL);
    esp_task_wdt_reset();

    // Feature toggle: when cfg_enable_drive is false, do not open the drive
    // lane or send any frames. Task idles here feeding TWDT only.
    // Mirrors the DomeLinkTask disabled path.
    {
        ConfigSnapshot cfg = {};
        configCacheRead(&cfg);
        bool enabled = cfg.system.enable_drive;
        if (!enabled) {
            for (;;) {
                esp_task_wdt_reset();
                vTaskDelay(pdMS_TO_TICKS(DRIVE_FRAME_PERIOD_MS));
            }
        }
    }

    driveBackendBegin(driveSerial);
    PA_LOG_INFO(TAG, "started \u2014 %s (%s) on UART%u, %lu baud, GPIO TX=%d RX=%d",
                kDriveBackend.id, kDriveBackend.protocol, (unsigned)UART_PORT_DRIVE,
                (unsigned long)kDriveBackend.baud, PIN_DRIVE_TX, PIN_DRIVE_RX);

    const TickType_t period = pdMS_TO_TICKS(DRIVE_FRAME_PERIOD_MS);  // 20 ms at 50 Hz
    TickType_t lastWakeTime = xTaskGetTickCount();  // Initialize for vTaskDelayUntil
    bool hwmLogged = false;

    bool zeroOutputRecorded = false;
    uint32_t zeroRecordedForTriggerMs = 0;
    while (true) {
        // Feed TWDT  --  if this line is not reached within WATCHDOG_TIMEOUT_S, chip resets
        esp_task_wdt_reset();

        // Log stack high-water mark once, after the first loop (captures init overhead).
        if (!hwmLogged) {
            PA_LOG_DEBUG(TAG, "stack HWM: %u bytes free",
                         (unsigned)uxTaskGetStackHighWaterMark(NULL));
            hwmLogged = true;
        }

        // Resolve drive output from arbiter
        uint32_t nowMs = millis();
        ConfigSnapshot runtimeCfg = {};
        configCacheRead(&runtimeCfg);
        int16_t maxOut = runtimeCfg.drive.speedLimitMax;
        uint32_t webTimeoutMs = runtimeCfg.drive.webDriveTimeoutMs;
        uint32_t rcTimeoutMs = runtimeCfg.drive.sbusTimeoutMs;

        DriveArbiterConfig cfg = {
            .speedLimitMax = maxOut,
            .webDriveTimeoutMs = webTimeoutMs,
            .rcDriveTimeoutMs = rcTimeoutMs,
        };
        DriveOutput driveOut = driveArbiterResolve(cfg, nowMs);

        // Sync resolved web-timeout state into FailsafeGate once per 50 Hz tick.
        failsafeUpdateWebTimeout(driveOut.webTimedOut);

        // Mirror resolved output to robotState for SSE status reporting.
        // Written here (post-resolve) so the values match what is actually sent
        // to the drive backend, not what was last submitted by any one source.
        taskENTER_CRITICAL(&robotStateMux);
        robotState.driveOutputSpeed = driveOut.speed;
        robotState.driveOutputSteer = driveOut.steer;
        robotState.driveOutputSource = (driveOut.activeSource == DriveSource::RC) ? SRC_SBUS : SRC_WEB_API;
        robotState.driveOutputCommandMs = driveOut.activeTimestampMs;
        taskEXIT_CRITICAL(&robotStateMux);

        int16_t speed = driveOut.speed;
        int16_t steer = driveOut.steer;
        bool failsafeActive = driveOut.failsafeActive;

        // Record first zero assertion time once per failsafe episode for timing evidence.
        // (DriveArbiter already zeroes speed/steer when failsafeActive.)
        if (failsafeActive) {
            uint32_t triggerMs;
            taskENTER_CRITICAL(&robotStateMux);
            triggerMs = robotState.failsafeLastTriggerMs;
            taskEXIT_CRITICAL(&robotStateMux);
            if (!zeroOutputRecorded || triggerMs != zeroRecordedForTriggerMs) {
                FailsafeDiagnostics diag = {};
                taskENTER_CRITICAL(&robotStateMux);
                recordFailsafeZeroOutputLocked(nowMs);
                copyFailsafeDiagnosticsLocked(&diag);
                taskEXIT_CRITICAL(&robotStateMux);
                PA_LOG_INFO(TAG,
                            "failsafe zero output asserted - source:%d trigger_to_zero:%lu ms",
                            (int)diag.failsafeLastTriggerSource, (unsigned long)diag.failsafeLastTriggerToZeroMs);
                zeroOutputRecorded = true;
                zeroRecordedForTriggerMs = diag.failsafeLastTriggerMs;
            }
        } else {
            zeroOutputRecorded = false;
            zeroRecordedForTriggerMs = 0;
        }

        // Send frame  --  always (zero-frame rule: never go silent). The
        // backend declares how long its far end tolerates a gap; whether a
        // frame goes out at all is decided here and never down there.
        // Pure step decision: encode frame emission and payload.
        DriveTickInputs tickIn{
            .failsafeActive = failsafeActive,
            .arbiterSpeed = speed,
            .arbiterSteer = steer,
        };
        DriveTickActions tickActions = driveTickDecide(tickIn);
        if (tickActions.shouldEmitFrame) {
            driveBackendSend(driveSerial, tickActions.speed, tickActions.steer);
        }

        // Read drive backend feedback  --  non-blocking, drains available bytes.
        // Feedback is not universal, so this asks the profile rather than the
        // controller: a backend that cannot report is compiled out of the
        // block entirely instead of polling a wire nothing answers on.
        // If no valid reading arrives within kFeedbackStaleMs, mark feedback
        // invalid so the UI does not display stale readings indefinitely.
        static constexpr uint32_t kFeedbackStaleMs = 5000;

        if (kDriveBackend.reportsFeedback) {
            DriveFeedback fb;
            if (driveBackendPollFeedback(driveSerial, &fb)) {
                taskENTER_CRITICAL(&robotStateMux);
                robotState.hb_batteryRaw    = fb.batteryRaw;
                robotState.hb_boardTempRaw  = fb.boardTempRaw;
                robotState.hb_speedR        = fb.speedR;
                robotState.hb_speedL        = fb.speedL;
                robotState.hb_currentL      = fb.currentL;
                robotState.hb_currentR      = fb.currentR;
                robotState.hb_feedbackValid = true;
                robotState.hb_lastFeedbackMs = millis();
                taskEXIT_CRITICAL(&robotStateMux);
            } else {
                // Check for stale data: invalidate if no frame received recently.
                taskENTER_CRITICAL(&robotStateMux);
                bool wasValid  = robotState.hb_feedbackValid;
                uint32_t lastMs = robotState.hb_lastFeedbackMs;
                taskEXIT_CRITICAL(&robotStateMux);
                if (wasValid && (uint32_t)(millis() - lastMs) > kFeedbackStaleMs) {
                    taskENTER_CRITICAL(&robotStateMux);
                    robotState.hb_feedbackValid = false;
                    taskEXIT_CRITICAL(&robotStateMux);
                    PA_LOG_INFO(TAG, "drive backend feedback stale (>%lu ms) - invalidated",
                                (unsigned long)kFeedbackStaleMs);
                }
            }
        }

#ifdef PA_VERBOSE_DRIVE
        PA_LOG_DEBUG(TAG, "frame spd:%d str:%d fs:%d", speed, steer, (int)failsafeActive);
#endif

        vTaskDelayUntil(&lastWakeTime, period);
    }
}
