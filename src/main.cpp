// =============================================================================
// src/main.cpp
//
// protoArtoo — ESP32 body controller for MK4 astromech droid.
// Boot sequence — Phase 1 stub.
// =============================================================================

#include <Arduino.h>
#include <esp_task_wdt.h>

#include "drive.h"
#include "robot_state.h"
#include "safety.h"
#include "sbus_input.h"
#include "web_server.h"

// Global state — all tasks share these
RobotState robotState = {};
portMUX_TYPE robotStateMux = portMUX_INITIALIZER_UNLOCKED;
QueueHandle_t driveQueue = nullptr;

// -----------------------------------------------------------------------------
// setDriveCommand() — thread-safe drive command update
// Called from SBUSInputTask, WebAPI handler, or safety zeroing.
// -----------------------------------------------------------------------------
void setDriveCommand(int16_t speed, int16_t steer, CommandSource src) {
    taskENTER_CRITICAL(&robotStateMux);
    robotState.driveSpeed = speed;
    robotState.driveSteer = steer;
    robotState.lastDriveSource = src;
    robotState.lastDriveCommandMs = millis();
    taskEXIT_CRITICAL(&robotStateMux);
}

// -----------------------------------------------------------------------------
// domeConnected() — returns true if dome heartbeat seen within 3 seconds
// -----------------------------------------------------------------------------
bool domeConnected() {
    return robotState.domeLastSeenMs > 0 && (millis() - robotState.domeLastSeenMs) < 3000;
}

// -----------------------------------------------------------------------------
// loadConfigToState() — load NVS config into robotState.cfg_* fields
// Called once at boot before tasks start.
// -----------------------------------------------------------------------------
void loadConfigToState() {
    // Phase 2: load from NVS. For now, set safe defaults.
    robotState.cfg_speedLimitMax = SPEED_LIMIT_MAX;
    robotState.cfg_sbusTimeoutMs = SBUS_TIMEOUT_MS;
    robotState.cfg_webDriveTimeoutMs = WEB_DRIVE_TIMEOUT_MS;
    robotState.cfg_ch8ModeLock = false;
    robotState.cfg_audioVolume = 20;
}

void setup() {
    Serial.begin(115200);
    Serial.println("[main] protoArtoo boot");

    // Safety: boot with drive locked until SBUS confirmed
    robotState.sbusSignalLost = true;
    robotState.estop = false;

    // Load config defaults (NVS in Phase 2)
    loadConfigToState();

    // Create drive command queue (capacity 4, non-blocking sends)
    driveQueue = xQueueCreate(4, sizeof(DriveCommand));

    // Layer 4: Initialize Task Watchdog Timer
    // panic=true means chip resets (not just logs) on timeout — intentional safety
    esp_task_wdt_init(WATCHDOG_TIMEOUT_S, true);

    // Detect TWDT reset from previous boot — set estop so robot does not move
    // until operator explicitly clears via POST /api/estop/clear
    esp_reset_reason_t resetReason = esp_reset_reason();
    if (resetReason == ESP_RST_TASK_WDT) {
        robotState.estop = true;
        robotState.failsafeSource = FS_WATCHDOG_RESET;
        Serial.println("[main] TWDT reset detected — estop set");
    }

    // Launch real-time tasks on Core 1
    // DriveTask: 50 Hz hoverboard frames, feeds TWDT, Layer 3 web timeout
    // SBUSInputTask: 200 Hz SBUS poll, Layer 1+2 failsafe
    xTaskCreatePinnedToCore(driveTask, "DriveTask", 4096, nullptr, 5, nullptr, 1);
    xTaskCreatePinnedToCore(sbusInputTask, "SBUSInputTask", 4096, nullptr, 5, nullptr, 1);

    // SafetyMonitorTask: 10 Hz audit on Core 0 (non-RT, low priority)
    xTaskCreatePinnedToCore(safetyMonitorTask, "SafetyMonitor", 2048, nullptr, 2, nullptr, 0);

    // Start WiFi AP and web server (Phase 1: estop endpoints only)
    webServerInit();

    Serial.println("[main] init complete — DriveTask + SBUSInputTask + SafetyMonitorTask running");
}

void loop() {
    // All logic runs in FreeRTOS tasks — loop() is intentionally empty.
    vTaskDelay(pdMS_TO_TICKS(1000));
}
