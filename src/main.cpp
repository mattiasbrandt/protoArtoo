// =============================================================================
// src/main.cpp
//
// protoArtoo — ESP32 body controller for MK4 astromech droid.
// Boot sequence — Phase 1 stub.
// =============================================================================

#include <Arduino.h>
#include <Preferences.h>
#include <esp_task_wdt.h>

#include "drive.h"
#include "log_buffer.h"
#include "robot_state.h"
#include "safety.h"
#include "sbus_input.h"
#include "web_server.h"

// Global state — all tasks share these
RobotState robotState = {};
portMUX_TYPE robotStateMux = portMUX_INITIALIZER_UNLOCKED;
static bool restartRequested = false;
static uint32_t restartAtMs = 0;
static portMUX_TYPE logMux = portMUX_INITIALIZER_UNLOCKED;
static LogBuffer recentLogBuf = {};

namespace {

const char* resetReasonName(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_UNKNOWN:
            return "UNKNOWN";
        case ESP_RST_POWERON:
            return "POWERON";
        case ESP_RST_EXT:
            return "EXTERNAL";
        case ESP_RST_SW:
            return "SOFTWARE";
        case ESP_RST_PANIC:
            return "PANIC";
        case ESP_RST_INT_WDT:
            return "INT_WDT";
        case ESP_RST_TASK_WDT:
            return "TASK_WDT";
        case ESP_RST_WDT:
            return "WDT";
        case ESP_RST_DEEPSLEEP:
            return "DEEPSLEEP";
        case ESP_RST_BROWNOUT:
            return "BROWNOUT";
        case ESP_RST_SDIO:
            return "SDIO";
        default:
            return "OTHER";
    }
}

void logBootHealth() {
    PA_LOG_INFO("main", "protoArtoo boot begin");
    PA_LOG_INFO("main", "reset_reason=%s (%d)", resetReasonName(esp_reset_reason()),
                (int)esp_reset_reason());
    PA_LOG_INFO("main",
                "config speed_limit_max=%d sbus_timeout_ms=%lu web_timeout_ms=%lu ch8_mode_lock=%s "
                "audio_volume=%u",
                robotState.cfg_speedLimitMax, (unsigned long)robotState.cfg_sbusTimeoutMs,
                (unsigned long)robotState.cfg_webDriveTimeoutMs,
                robotState.cfg_ch8ModeLock ? "true" : "false", robotState.cfg_audioVolume);
    PA_LOG_DEBUG("main", "heap_free=%lu", (unsigned long)ESP.getFreeHeap());
}

}  // namespace

void paLogLine(const char* tag, const char* message) {
    char line[LOG_LINE_MAX];
    snprintf(line, sizeof(line), "[%s] %s", tag, message);
    Serial.println(line);

    taskENTER_CRITICAL(&logMux);
    logBufferAppend(&recentLogBuf, line);
    taskEXIT_CRITICAL(&logMux);
}

size_t copyRecentLogs(char* buffer, size_t bufferSize) {
    taskENTER_CRITICAL(&logMux);
    size_t used = logBufferCopy(&recentLogBuf, buffer, bufferSize);
    taskEXIT_CRITICAL(&logMux);
    return used;
}

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
bool domeConnected() {
    return robotState.domeLastSeenMs > 0 && (millis() - robotState.domeLastSeenMs) < 5000;
}

// -----------------------------------------------------------------------------
// loadConfigToState() — load NVS config into robotState.cfg_* fields
// Called once at boot before tasks start.
// -----------------------------------------------------------------------------
void loadConfigToState() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    robotState.cfg_speedLimitMax = prefs.getShort("spd_max", SPEED_LIMIT_MAX);
    robotState.cfg_sbusTimeoutMs = prefs.getULong("sbus_tmo", SBUS_TIMEOUT_MS);
    robotState.cfg_webDriveTimeoutMs = prefs.getULong("web_tmo", WEB_DRIVE_TIMEOUT_MS);
    robotState.cfg_ch8ModeLock = prefs.getBool("ch8_lock", false);
    robotState.cfg_audioVolume = (uint8_t)prefs.getUChar("aud_vol", 20);
    prefs.end();

    robotState.cfg_speedLimitMax =
        constrain(robotState.cfg_speedLimitMax, (int16_t)0, (int16_t)SPEED_LIMIT_MAX);
    robotState.cfg_sbusTimeoutMs =
        constrain(robotState.cfg_sbusTimeoutMs, (uint32_t)50, (uint32_t)5000);
    robotState.cfg_webDriveTimeoutMs =
        constrain(robotState.cfg_webDriveTimeoutMs, (uint32_t)100, (uint32_t)5000);
    robotState.cfg_audioVolume = constrain(robotState.cfg_audioVolume, (uint8_t)0, (uint8_t)30);
}

bool saveConfigToNvs() {
    int16_t speedLimitMax;
    uint32_t sbusTimeoutMs;
    uint32_t webDriveTimeoutMs;
    bool ch8ModeLock;
    uint8_t audioVolume;

    taskENTER_CRITICAL(&robotStateMux);
    speedLimitMax = robotState.cfg_speedLimitMax;
    sbusTimeoutMs = robotState.cfg_sbusTimeoutMs;
    webDriveTimeoutMs = robotState.cfg_webDriveTimeoutMs;
    ch8ModeLock = robotState.cfg_ch8ModeLock;
    audioVolume = robotState.cfg_audioVolume;
    taskEXIT_CRITICAL(&robotStateMux);

    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) {
        return false;
    }

    bool ok = true;
    ok = ok && prefs.putShort("spd_max", speedLimitMax) > 0;
    ok = ok && prefs.putULong("sbus_tmo", sbusTimeoutMs) > 0;
    ok = ok && prefs.putULong("web_tmo", webDriveTimeoutMs) > 0;
    ok = ok && prefs.putBool("ch8_lock", ch8ModeLock);
    ok = ok && prefs.putUChar("aud_vol", audioVolume) > 0;
    prefs.end();
    return ok;
}

void requestSystemRestart(uint32_t delayMs) {
    restartRequested = true;
    restartAtMs = millis() + delayMs;
}

void setup() {
    Serial.begin(115200);
    Serial.setDebugOutput(false);
    delay(200);

    // Safety: boot with drive locked until SBUS confirmed
    robotState.sbusSignalLost = true;
    robotState.estop = false;

    // Load config defaults (NVS in Phase 2)
    loadConfigToState();
    logBootHealth();

    // Layer 4: Initialize Task Watchdog Timer
    // panic=true means chip resets (not just logs) on timeout — intentional safety
    esp_task_wdt_init(WATCHDOG_TIMEOUT_S, true);

    // Detect TWDT reset from previous boot — set estop so robot does not move
    // until operator explicitly clears via POST /api/estop/clear
    esp_reset_reason_t resetReason = esp_reset_reason();
    if (resetReason == ESP_RST_TASK_WDT) {
        robotState.estop = true;
        robotState.failsafeSource = FS_WATCHDOG_RESET;
        PA_LOG_ERROR("main", "task watchdog reset detected - estop set");
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

    PA_LOG_INFO("main", "init complete - DriveTask + SBUSInputTask + SafetyMonitorTask running");
    Serial.flush();
}

void loop() {
    if (restartRequested && (int32_t)(millis() - restartAtMs) >= 0) {
        PA_LOG_INFO("main", "restarting controller");
        Serial.flush();
        delay(100);
        ESP.restart();
    }

    vTaskDelay(pdMS_TO_TICKS(100));
}
