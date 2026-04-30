// =============================================================================
// src/tasks/drive.cpp
//
// DriveTask — sends 8-byte Gen2.x frames to the hoverboard at 50 Hz.
// Owns UART1 / Serial1 on GPIO PIN_HOVERBOARD_TX / PIN_HOVERBOARD_RX.
// Runs on Core 1 (real-time).
//
// Safety layers implemented here:
//   Layer 3: Web API drive timeout (WEB_DRIVE_TIMEOUT_MS)
//   Layer 4: TWDT feed (esp_task_wdt_reset every loop)
//
// SAFETY: SPEED_LIMIT_MAX cap applied unconditionally before every frame.
// SAFETY: Zero frames sent when any failsafe is active (never silent).
// =============================================================================

#include <Arduino.h>
#include <esp_task_wdt.h>

#include "config.h"
#include "failsafe_gate.h"
#include "hoverboard_uart.h"
#include "logging.h"
#include "robot_state.h"

static const char* TAG = "DriveTask";

// UART1 (Serial1) is dedicated to the hoverboard on PCB header S1.
// Pins are fixed by the traced board routing: GPIO 16 TX, GPIO 17 RX.
static HardwareSerial hoverSerial(1);

// -----------------------------------------------------------------------------
// driveTask()
// Sends Gen2.x 8-byte frames to the hoverboard at DRIVE_FREQ_HZ (50 Hz).
// Registers with TWDT on entry — if this loop hangs, chip resets in 3 s.
// Applies SPEED_LIMIT_MAX cap and all active failsafe overrides every frame.
// Thread safety: all RobotState reads/writes use taskENTER/EXIT_CRITICAL.
// -----------------------------------------------------------------------------
void driveTask(void* pvParameters) {
    // Register with TWDT unconditionally — this task must feed the watchdog
    // regardless of enable state or the chip will reset after WATCHDOG_TIMEOUT_S.
    esp_task_wdt_add(NULL);

    // Feature toggle: when cfg_enable_s1_hoverboard is false, do not open
    // UART1 or send any frames. Task idles here feeding TWDT only.
    // Mirrors the DomeLinkTask disabled path.
    {
        taskENTER_CRITICAL(&robotStateMux);
        bool enabled = robotState.cfg_enable_s1_hoverboard;
        taskEXIT_CRITICAL(&robotStateMux);
        if (!enabled) {
            for (;;) {
                esp_task_wdt_reset();
                vTaskDelay(pdMS_TO_TICKS(1000 / DRIVE_FREQ_HZ));
            }
        }
    }

    HoverboardFeedbackParser hbParser;
    hoverSerial.begin(HOVERBOARD_BAUD, SERIAL_8N1, PIN_HOVERBOARD_RX, PIN_HOVERBOARD_TX);
    initHoverboardFeedbackParser(&hbParser);  // clear parser state after UART reinit
    PA_LOG_INFO(TAG, "started \u2014 UART1 %lu baud, GPIO TX=%d RX=%d",
                (unsigned long)HOVERBOARD_BAUD, PIN_HOVERBOARD_TX, PIN_HOVERBOARD_RX);

    uint8_t frameBuf[8];
    const TickType_t period = pdMS_TO_TICKS(1000 / DRIVE_FREQ_HZ);  // 20 ms at 50 Hz
    bool hwmLogged = false;

    bool zeroOutputRecorded = false;
    uint32_t zeroRecordedForTriggerMs = 0;
    while (true) {
        // Feed TWDT — if this line is not reached within WATCHDOG_TIMEOUT_S, chip resets
        esp_task_wdt_reset();

        // Log stack high-water mark once, after the first loop (captures init overhead).
        if (!hwmLogged) {
            PA_LOG_INFO(TAG, "stack HWM: %u words free",
                        (unsigned)uxTaskGetStackHighWaterMark(NULL));
            hwmLogged = true;
        }

        // Read current state under mutex
        int16_t speed;
        int16_t steer;
        int16_t maxOut;
        bool failsafeActive;
        uint32_t nowMs;

        nowMs = millis();
        taskENTER_CRITICAL(&robotStateMux);
        speed = robotState.driveSpeed;
        steer = robotState.driveSteer;
        maxOut = robotState.cfg_speedLimitMax;

        // Check web drive timeout and trigger failsafe if needed
        if (robotState.lastDriveSource == SRC_WEB_API &&
            (uint32_t)(nowMs - robotState.lastDriveCommandMs) > robotState.cfg_webDriveTimeoutMs) {
            if (!robotState.webDriveExpired) {
                failsafeTrigger(FailsafeLayer::WEB_TIMEOUT);
            }
            robotState.driveSpeed = 0;
            robotState.driveSteer = 0;
            speed = 0;
            steer = 0;
        }
        taskEXIT_CRITICAL(&robotStateMux);

        // Check if any failsafe is active
        failsafeActive = failsafeIsActive();

        // Apply SPEED_LIMIT_MAX cap unconditionally — never exceed hardware limit.
        speed = constrain(speed, (int16_t)(-maxOut), maxOut);
        steer = constrain(steer, (int16_t)(-maxOut), maxOut);

        // Zero output if any failsafe active (estop, SBUS loss, HW failsafe, web timeout).
        // Record first zero assertion time once per failsafe episode for timing evidence.
        if (failsafeActive) {
            speed = 0;
            steer = 0;
            uint32_t triggerMs;
            taskENTER_CRITICAL(&robotStateMux);
            triggerMs = robotState.failsafeLastTriggerMs;
            taskEXIT_CRITICAL(&robotStateMux);
            if (!zeroOutputRecorded || triggerMs != zeroRecordedForTriggerMs) {
                FailsafeSource triggerSource;
                uint32_t triggerToZeroMs;
                uint32_t recordedTriggerMs;
                taskENTER_CRITICAL(&robotStateMux);
                recordFailsafeZeroOutputLocked(nowMs);
                triggerSource = robotState.failsafeLastTriggerSource;
                triggerToZeroMs = robotState.failsafeLastTriggerToZeroMs;
                recordedTriggerMs = robotState.failsafeLastTriggerMs;
                taskEXIT_CRITICAL(&robotStateMux);
                PA_LOG_INFO(TAG,
                            "failsafe zero output asserted — source:%d trigger_to_zero:%lu ms",
                            (int)triggerSource, (unsigned long)triggerToZeroMs);
                zeroOutputRecorded = true;
                zeroRecordedForTriggerMs = recordedTriggerMs;
            }
        } else {
            zeroOutputRecorded = false;
            zeroRecordedForTriggerMs = 0;
        }

        // Send frame — always (zero-frame rule: never go silent, hoverboard must coast, not drift)
        buildHoverboardFrame(frameBuf, steer, speed);
        hoverSerial.write(frameBuf, sizeof(frameBuf));

        // Read hoverboard controller feedback — non-blocking, drains available bytes.
        // Decodes battery voltage, board temperature, and motor speed from the
        // Gen2.x feedback frame the hoverboard controller sends back at 10–100 Hz.
        // If no valid frame arrives within HB_FEEDBACK_STALE_MS, mark feedback as
        // invalid so the UI does not display stale readings indefinitely.
        static constexpr uint32_t kFeedbackStaleMs = 5000;

        HoverboardFeedback hbFb;
        if (readHoverboardFeedback(hoverSerial, &hbParser, &hbFb)) {
            taskENTER_CRITICAL(&robotStateMux);
            robotState.hb_batteryRaw    = hbFb.batteryRaw;
            robotState.hb_boardTempRaw  = hbFb.boardTempRaw;
            robotState.hb_speedR        = hbFb.speedR;
            robotState.hb_speedL        = hbFb.speedL;
            robotState.hb_currentL      = hbFb.currentL;
            robotState.hb_currentR      = hbFb.currentR;
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
                PA_LOG_INFO(TAG, "hoverboard feedback stale (>%lu ms) — invalidated",
                            (unsigned long)kFeedbackStaleMs);
            }
        }

#ifdef PA_VERBOSE_DRIVE
        Serial.printf("[%s] frame spd:%d str:%d fs:%d\n", TAG, speed, steer, (int)failsafeActive);
#endif

        vTaskDelay(period);
    }
}
