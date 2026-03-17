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
    // Register this task with TWDT before any blocking work.
    // If esp_task_wdt_reset() is not reached within WATCHDOG_TIMEOUT_S, chip resets.
    esp_task_wdt_add(NULL);

    hoverSerial.begin(HOVERBOARD_BAUD, SERIAL_8N1, PIN_HOVERBOARD_RX, PIN_HOVERBOARD_TX);
    PA_LOG_INFO(TAG, "started — UART1 %d baud, GPIO TX=%d RX=%d",
                HOVERBOARD_BAUD, PIN_HOVERBOARD_TX, PIN_HOVERBOARD_RX);

    uint8_t frameBuf[8];
    const TickType_t period = pdMS_TO_TICKS(1000 / DRIVE_FREQ_HZ);  // 20 ms at 50 Hz

    while (true) {
        // Feed TWDT — if this line is not reached within WATCHDOG_TIMEOUT_S, chip resets
        esp_task_wdt_reset();

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
        failsafeActive = robotState.estop || robotState.sbusSignalLost || robotState.sbusHwFailsafe;
        maxOut = robotState.cfg_speedLimitMax;

        if (robotState.lastDriveSource == SRC_WEB_API &&
            (uint32_t)(nowMs - robotState.lastDriveCommandMs) > robotState.cfg_webDriveTimeoutMs) {
            robotState.webDriveExpired = true;
            robotState.failsafeSource = FS_WEB_TIMEOUT;
            robotState.driveSpeed = 0;
            robotState.driveSteer = 0;
            speed = 0;
            steer = 0;
            failsafeActive = true;
        }
        taskEXIT_CRITICAL(&robotStateMux);

        // Apply SPEED_LIMIT_MAX cap unconditionally — never exceed hardware limit.
        speed = constrain(speed, (int16_t)(-maxOut), maxOut);
        steer = constrain(steer, (int16_t)(-maxOut), maxOut);

        // Zero output if any failsafe active (estop, SBUS loss, HW failsafe, web timeout)
        if (failsafeActive) {
            speed = 0;
            steer = 0;
        }

        // Send frame — always (zero-frame rule: never go silent, hoverboard must coast, not drift)
        buildHoverboardFrame(frameBuf, steer, speed);
        hoverSerial.write(frameBuf, sizeof(frameBuf));

#ifdef PA_VERBOSE_DRIVE
        Serial.printf("[%s] frame spd:%d str:%d fs:%d\n", TAG, speed, steer, (int)failsafeActive);
#endif

        vTaskDelay(period);
    }
}
