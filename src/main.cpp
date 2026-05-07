// =============================================================================
// src/main.cpp
//
// protoArtoo — ESP32 body controller for MK4 astromech droid.
// Boot sequence — Phase 1 stub.
// =============================================================================

#include <Arduino.h>
#include <Preferences.h>
#include <cstddef>
#include <esp_task_wdt.h>

#include "audio_dollar_parser.h"
#include "audio_task.h"
#include "aux_led.h"
#include "config_store.h"
#include "dome_link.h"
#include "dome_task.h"
#include "drive.h"
#include "drive_arbiter.h"
#include "failsafe_gate.h"
#include "ledc_pwm.h"
#include "log_buffer.h"
#include "mood.h"
#include "rc_input.h"
#include "robot_state.h"
#include "safety.h"
#include "servo_task.h"
#include "web_server.h"

// Global state — all tasks share these
RobotState robotState = {};
portMUX_TYPE robotStateMux = portMUX_INITIALIZER_UNLOCKED;
QueueHandle_t servoCmdQueue = nullptr;
QueueHandle_t domeCmdQueue = nullptr;
QueueHandle_t audioCmdQueue = nullptr;
QueueHandle_t domeTxQueue = nullptr;
static volatile bool restartRequested = false;
static volatile uint32_t restartAtMs = 0;
static portMUX_TYPE restartMux = portMUX_INITIALIZER_UNLOCKED;
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
    ConfigSnapshot cfg = {};
    configCacheRead(&cfg);
    PA_LOG_INFO("main", "protoArtoo boot begin");
    PA_LOG_INFO("main", "reset_reason=%s (%d)", resetReasonName(esp_reset_reason()),
                (int)esp_reset_reason());
    PA_LOG_INFO("main",
                "config speed_limit_max=%d sbus_timeout_ms=%lu web_timeout_ms=%lu audio_volume=%u",
                cfg.drive.speedLimitMax, (unsigned long)cfg.drive.sbusTimeoutMs,
                (unsigned long)cfg.drive.webDriveTimeoutMs, cfg.audio.audioVolume);
    PA_LOG_DEBUG("main", "heap_free=%lu", (unsigned long)ESP.getFreeHeap());
}

}  // namespace

void paLogLineRaw(const char* line) {
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

// Copy up to maxLines new log lines written since lastSent into out[][LOG_LINE_MAX].
// Returns new totalWritten. Sets *linesCopied to number of entries filled.
// Lines that have already been overwritten by the ring are silently skipped.
uint32_t copyNewLogLinesSince(uint32_t lastSent, char out[][LOG_LINE_MAX], size_t maxLines,
                              size_t* linesCopied) {
    taskENTER_CRITICAL(&logMux);
    uint32_t total = recentLogBuf.totalWritten;
    uint32_t count = (uint32_t)recentLogBuf.count;
    uint32_t ringStart = (total >= count) ? (total - count) : 0;
    uint32_t from = (lastSent > ringStart) ? lastSent : ringStart;
    uint32_t n = (from < total) ? (total - from) : 0;
    if (n > (uint32_t)maxLines)
        n = (uint32_t)maxLines;
    size_t startIdx = (recentLogBuf.head + LOG_BUFFER_LINES - (size_t)count) % LOG_BUFFER_LINES;
    for (uint32_t i = 0; i < n; ++i) {
        size_t ringIdx = (startIdx + (size_t)(from - ringStart) + (size_t)i) % LOG_BUFFER_LINES;
        strncpy(out[i], recentLogBuf.lines[ringIdx], LOG_LINE_MAX - 1);
        out[i][LOG_LINE_MAX - 1] = '\0';
    }
    *linesCopied = (size_t)n;
    taskEXIT_CRITICAL(&logMux);
    return total;
}

// Return current number of lines in the log ring buffer.
size_t getLogBufferCount() {
    taskENTER_CRITICAL(&logMux);
    size_t count = recentLogBuf.count;
    taskEXIT_CRITICAL(&logMux);
    return count;
}

// Copy the log line at logical index idx (0 = oldest) into out[outSize].
// Returns true if idx is within bounds.
bool copyLogLineAt(size_t idx, char* out, size_t outSize) {
    taskENTER_CRITICAL(&logMux);
    bool valid = idx < recentLogBuf.count;
    if (valid) {
        size_t startIdx =
            (recentLogBuf.head + LOG_BUFFER_LINES - recentLogBuf.count) % LOG_BUFFER_LINES;
        size_t ringIdx = (startIdx + idx) % LOG_BUFFER_LINES;
        strncpy(out, recentLogBuf.lines[ringIdx], outSize - 1);
        out[outSize - 1] = '\0';
    }
    taskEXIT_CRITICAL(&logMux);
    return valid;
}
// -----------------------------------------------------------------------------
// loadConfigToState() — load NVS config into the runtime config cache
// Called once at boot before tasks start.
// -----------------------------------------------------------------------------
void loadConfigToState() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    ConfigSnapshot snap;
    configLoad(prefs, &snap);
    uint8_t lastMood = prefs.getUChar("last_mood", 0);  // read BEFORE prefs.end()
    prefs.end();

    // Apply all config fields to robotState (no mutex needed — called before tasks start)
    // All validation and clamping is now performed within configLoad()
    configCacheApply(snap);

    robotState.activeMood = lastMood;

    // Initialize runtime state from config
    robotState.stationary = snap.system.stationary;
}

bool saveConfigToNvs() {
    ConfigSnapshot snap;
    configCacheRead(&snap);

    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) {
        return false;
    }

    bool ok = configSave(prefs, snap);
    prefs.end();
    return ok;
}

void requestSystemRestart(uint32_t delayMs) {
    taskENTER_CRITICAL(&restartMux);
    restartRequested = true;
    restartAtMs = millis() + delayMs;
    taskEXIT_CRITICAL(&restartMux);
}

void setup() {
    Serial.begin(115200);
    Serial.setDebugOutput(false);
    delay(200);

    // Audio module state: 0xFF = "unknown/none" until AudioTask runs its init
    // query. Zero-init would show "USB" (0x00) before any query succeeds.
    robotState.audio_module_device = 0xFF;
    robotState.audio_module_play_state = 0xFF;
    // Load config from NVS — may override cfg_logLevel with the user's saved value.
    loadConfigToState();
    logBootHealth();

    // Layer 4: Initialize Task Watchdog Timer
    // IDF 5.x: esp_task_wdt_init() takes a config struct (timeout_ms, idle_core_mask,
    // trigger_panic). IDF 4.x took (timeout_seconds, trigger_panic) directly.
    // idle_core_mask=0: do not subscribe idle tasks; only DriveTask subscribes itself.
    const esp_task_wdt_config_t twdt_config = {
        .timeout_ms    = WATCHDOG_TIMEOUT_S * 1000U,
        .idle_core_mask = 0,
        .trigger_panic  = true,
    };
    esp_task_wdt_init(&twdt_config);

    // Initialize FailsafeGate and DriveArbiter before task creation
    failsafeInit(&robotStateMux);
    driveArbiterInit(&robotStateMux);

    // Safety: boot with drive locked until SBUS confirmed
    // Use FailsafeGate's SBUS_WATCHDOG layer; RcInputTask will clear when frames arrive
    failsafeTrigger(FailsafeLayer::SBUS_WATCHDOG);

    // Detect TWDT reset from previous boot — set estop so robot does not move
    // until operator explicitly clears via POST /api/estop/clear
    esp_reset_reason_t resetReason = esp_reset_reason();
    if (resetReason == ESP_RST_TASK_WDT) {
        failsafeTrigger(FailsafeLayer::TWDT_RESET);
        PA_LOG_ERROR("main", "task watchdog reset detected - estop set");
    }

    // Create command queues
    servoCmdQueue = xQueueCreate(8, sizeof(ServoCommand));
    domeCmdQueue = xQueueCreate(8, sizeof(DomeCommand));
    audioCmdQueue = xQueueCreate(8, sizeof(AudioCommand));
    domeTxQueue = xQueueCreate(16, sizeof(DomeTxCmd));

    // ServoTask owns LEDC hardware init and applies AUX LED channel skip policy.
    servoTaskInit();
    domeTaskInit();
    bool auxLedTaskReady = auxLedTaskInit();
    if (!auxLedTaskReady) {
        PA_LOG_WARN("main", "aux LED task init failed; AUX LED API will report unavailable");
    }

    // Launch real-time tasks on Core 1
    // DriveTask: 50 Hz hoverboard frames, feeds TWDT, Layer 3 web timeout
    // RcInputTask: ~200 Hz RC poll (all modes), Layer 1+2 failsafe
    // ServoTask: 50 Hz servo PWM updates
    // DomeTask: 50 Hz ESC PWM updates
    xTaskCreatePinnedToCore(driveTask, "DriveTask", 2560, nullptr, 5, nullptr, 1);
    xTaskCreatePinnedToCore(rcInputTask, "RCInputTask", 6144, nullptr, 5, nullptr, 1);
    xTaskCreatePinnedToCore(
        servoTask, "ServoTask", 3072, nullptr, 4, nullptr,
        1);  // HWM: ~728 B used; was 5120 (oversized for string formatting assumption)
    xTaskCreatePinnedToCore(domeTask, "DomeTask", 3072, nullptr, 4, nullptr,
                            1);  // T24 R1: profiler HWM reached 108 B free at 2048 B.

    // AudioTask: Core 0 (non-RT) — software bit-bang TX blocks ~6 ms per command;
    // keeping off Core 1 avoids any interaction with DriveTask / ServoTask timing.
    xTaskCreatePinnedToCore(audioTask, "AudioTask", 4096, nullptr, 3, nullptr, 0);

    // AuxLedTask: Core 0 (non-RT) - WS2812B effects and API-driven color/effect updates.
    // Runs independently of Core 1 control loops.
    if (auxLedTaskReady) {
        xTaskCreatePinnedToCore(auxLedTask, "AuxLedTask", 3072, nullptr, 2, nullptr, 0);
    }

    // DomeLinkTask: Core 1 — bidirectional Marcduino serial to AstroPixelsPlus.
    // UART2 TX/RX are non-blocking hardware operations; Core 1 at priority 3.
    // 4096: profiler measured 988 B free at 3072 B without WiFi fallback active;
    // HTTPClient call-chain in sendCommandOverWifi needs 3 KB+ of stack headroom.
    xTaskCreatePinnedToCore(domeLinkTask, "DomeLinkTask", 4096, nullptr, 3, nullptr, 1);

    // SafetyMonitorTask: 10 Hz audit on Core 0 (non-RT, low priority).
    // HWM first-iteration: 476 B free — WARN path allocates 128 B format buffer +
    // printf; bumped to 3072 to ensure adequate headroom for all log paths.
    xTaskCreatePinnedToCore(safetyMonitorTask, "SafetyMonitor", 3072, nullptr, 2, nullptr, 0);

    // Restore last mood — audio component only.
    // - Dome link is not yet established at boot, so dome TX is intentionally skipped.
    // - We apply audio directly here rather than via applyMood() to avoid writing
    //   last_mood back to NVS (we just read it; the value has not changed).
    if (robotState.activeMood != 0) {
        const char* bootAudioCmd = moodAudioCommand(robotState.activeMood);
        if (bootAudioCmd) {
            audioQueueDollar(bootAudioCmd, SRC_INTERNAL);
            PA_LOG_INFO("main", "boot mood restore: SE%u -> %s", (unsigned)robotState.activeMood,
                        bootAudioCmd);
        }
    }

    // Start WiFi AP and web server
    webServerInit();

    uint16_t bootTrack = 0;
    ConfigSnapshot bootCfg = {};
    configCacheRead(&bootCfg);
    bootTrack = bootCfg.audio.snd_sys_boot;
    if (bootTrack != 0) {
        if (audioQueuePlaySlot(AUDIO_SLOT_SYS_BOOT, SRC_INTERNAL)) {
            PA_LOG_INFO("main", "system boot sound queued");
        } else {
            PA_LOG_WARN("main", "system boot sound queue full");
        }
    }

    PA_LOG_INFO("main", "init complete");
    Serial.flush();
}

void loop() {
    bool shouldRestart = false;

    taskENTER_CRITICAL(&restartMux);
    if (restartRequested && (int32_t)(millis() - restartAtMs) >= 0) {
        shouldRestart = true;
    }
    taskEXIT_CRITICAL(&restartMux);

    if (shouldRestart) {
        PA_LOG_INFO("main", "restarting controller");
        Serial.flush();
        delay(100);
        ESP.restart();
    }

    vTaskDelay(pdMS_TO_TICKS(100));
}
