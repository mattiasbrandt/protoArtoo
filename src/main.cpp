// =============================================================================
// src/main.cpp
//
// protoArtoo  --  ESP32 body controller for MK4 astromech droid.
// Boot: config load, safety defaults, task creation.
// =============================================================================

#include <Arduino.h>
#include <Preferences.h>
#include <cstddef>
#include <esp_task_wdt.h>
#include <freertos/semphr.h>

#include "audio_dollar_parser.h"
#include "audio_task.h"
#include "aux_led.h"
#include "config_store.h"
#include "config_cache.h"
#include "dome_link.h"
#include "dome_task.h"
#include "drive.h"
#include "drive_arbiter.h"
#include "failsafe_boot_sbus.h"
#include "failsafe_boot_twdt.h"
#include "failsafe_gate.h"
#include "ledc_pwm.h"
#include "log_buffer.h"
#include "mood.h"
#include "rc_input.h"
#include "rc_input_step.h"
#include "reset_reason.h"
#include "robot_state.h"
#include "safety.h"
#include "seq_store.h"
#include "sequence_dispatcher.h"
#include "servo_task.h"
#include "web_server.h"

// Global state  --  all tasks share these
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
static StaticSemaphore_t logSerialMutexStorage = {};
static SemaphoreHandle_t logSerialMutex = nullptr;
// Static bootstrap ring: captures the few lines logged before NVS config loads
// (schema migration, mount problems). paLogRingApplyBootDepth() replaces it
// with a heap ring sized to the saved log level and carries these lines over.
static char logBootstrapStorage[LOG_RING_BOOTSTRAP_LINES][LOG_LINE_MAX];
static LogBuffer recentLogBuf = {};
// /api/logs response body, allocated alongside the sized ring (capacity *
// LOG_LINE_MAX + 1). One shared buffer is race-free because web handlers
// serialize on one task; see handleLogsGet().
static char* logsBodyBuf = nullptr;
static size_t logsBodyBufSize = 0;

namespace {

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

void paLogInit() {
    if (logSerialMutex == nullptr) {
        logSerialMutex = xSemaphoreCreateMutexStatic(&logSerialMutexStorage);
    }
    if (recentLogBuf.lines == nullptr) {
        logBufferInit(&recentLogBuf, logBootstrapStorage, LOG_RING_BOOTSTRAP_LINES);
    }
}

// Size the log ring and the /api/logs body from the operator's saved log
// level. Called once from setup() after NVS config loads and before any task
// or the web server starts; bootstrap lines are carried over. On allocation
// failure the bootstrap ring stays in place so logging never loses its store.
static void paLogRingApplyBootDepth() {
    const size_t wanted = logRingLinesForLevel(configCurrentLogLevel());
    char(*storage)[LOG_LINE_MAX] = (char(*)[LOG_LINE_MAX])malloc(wanted * LOG_LINE_MAX);
    char* body = (char*)malloc(wanted * LOG_LINE_MAX + 1);
    if (storage == nullptr || body == nullptr) {
        free(storage);
        free(body);
        PA_LOG_ERROR("log", "ring alloc failed (%u lines) - keeping bootstrap depth",
                     (unsigned)wanted);
        return;
    }

    LogBuffer sized = {};
    logBufferInit(&sized, storage, wanted);

    taskENTER_CRITICAL(&logMux);
    size_t start =
        (recentLogBuf.head + recentLogBuf.capacity - recentLogBuf.count) % recentLogBuf.capacity;
    for (size_t i = 0; i < recentLogBuf.count; ++i) {
        logBufferAppend(&sized, recentLogBuf.lines[(start + i) % recentLogBuf.capacity]);
    }
    sized.totalWritten = recentLogBuf.totalWritten;
    recentLogBuf = sized;
    logsBodyBuf = body;
    logsBodyBufSize = wanted * LOG_LINE_MAX + 1;
    taskEXIT_CRITICAL(&logMux);
}

char* recentLogsBodyBuffer(size_t* size) {
    if (logsBodyBuf == nullptr) {
        *size = 0;
        return nullptr;
    }
    *size = logsBodyBufSize;
    return logsBodyBuf;
}

void paLogLineRaw(const char* line) {
    taskENTER_CRITICAL(&logMux);
    logBufferAppend(&recentLogBuf, line);
    taskEXIT_CRITICAL(&logMux);
}

void paLogLine(const char* line) {
    if (line == nullptr) {
        return;
    }

    size_t lineLen = 0;
    while (line[lineLen] != '\0' && lineLen < PA_LOG_SERIAL_LINE_MAX) {
        ++lineLen;
    }

    SemaphoreHandle_t serialMutex = logSerialMutex;
    if (serialMutex != nullptr) {
        xSemaphoreTake(serialMutex, portMAX_DELAY);
    }
    Serial.write((const uint8_t*)line, lineLen);
    Serial.write('\n');
    if (serialMutex != nullptr) {
        xSemaphoreGive(serialMutex);
    }

    paLogLineRaw(line);
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
    size_t startIdx =
        (recentLogBuf.head + recentLogBuf.capacity - (size_t)count) % recentLogBuf.capacity;
    for (uint32_t i = 0; i < n; ++i) {
        size_t ringIdx =
            (startIdx + (size_t)(from - ringStart) + (size_t)i) % recentLogBuf.capacity;
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
            (recentLogBuf.head + recentLogBuf.capacity - recentLogBuf.count) % recentLogBuf.capacity;
        size_t ringIdx = (startIdx + idx) % recentLogBuf.capacity;
        strncpy(out, recentLogBuf.lines[ringIdx], outSize - 1);
        out[outSize - 1] = '\0';
    }
    taskEXIT_CRITICAL(&logMux);
    return valid;
}
// -----------------------------------------------------------------------------
// loadConfigToState()  --  load NVS config into the runtime config cache
// Called once at boot before tasks start.
// Logs an error if config load fails; safe defaults are applied in all paths.
// -----------------------------------------------------------------------------
void loadConfigToState() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    ConfigSnapshot snap;
    bool configOk = configLoad(prefs, &snap);
    uint8_t lastMood = prefs.getUChar("last_mood", 0);  // read BEFORE prefs.end()
    prefs.end();

    if (!configOk) {
        PA_LOG_ERROR("config", "failed to load NVS config (schema or migration error); using safe defaults");
    }

    // Apply all config fields to robotState (no mutex needed  --  called before tasks start)
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
    paLogInit();
    delay(200);

    // Audio module state: 0xFF = "unknown/none" until AudioTask runs its init
    // query. Zero-init would show "USB" (0x00) before any query succeeds.
    robotState.audio_module_device = 0xFF;
    robotState.audio_module_play_state = 0xFF;
    robotState.audio_module_rx_status = AUDIO_RX_UNKNOWN;
    // Load config from NVS  --  may override cfg_logLevel with the user's saved value.
    loadConfigToState();
    paLogRingApplyBootDepth();
    logBootHealth();
    ConfigSnapshot bootCfg = {};
    configCacheRead(&bootCfg);
    const RcInputActiveConfig activeRc = rcInputActiveConfigFromSystem(bootCfg.system);
    configCacheSetActiveRcInput(activeRc);
    configCacheSetActiveDomeEnabled(bootCfg.system.enable_dome_esc);
    configCacheSetActiveAudioEnabled(bootCfg.system.enable_audio);
    RcInputStartupPlan rcPlan = rcInputStepStartupPlan(activeRc);

    // Layer 4: Initialize Task Watchdog Timer
    // IDF 5.x: esp_task_wdt_init() takes a config struct (timeout_ms, idle_core_mask,
    // trigger_panic). IDF 4.x took (timeout_seconds, trigger_panic) directly.
    // idle_core_mask=0: do not subscribe idle tasks; only DriveTask subscribes itself.
    const esp_task_wdt_config_t twdt_config = {
        .timeout_ms    = WATCHDOG_TIMEOUT_S * 1000U,
        .idle_core_mask = 0,
        .trigger_panic  = true,
    };
    esp_err_t twdt_init_result = esp_task_wdt_init(&twdt_config);
    // On ESP32-P4 the system watchdog is pre-initialized, so esp_task_wdt_init() returns
    // ESP_ERR_INVALID_STATE. Reconfigure the existing watchdog with our chosen timeout
    // and idle-core settings. This ensures our safety-critical TWDT configuration applies
    // on all targets. On artoo-esp32 init succeeds and this path is never taken.
    if (twdt_init_result == ESP_ERR_INVALID_STATE) {
        esp_err_t twdt_reconfig_result = esp_task_wdt_reconfigure(&twdt_config);
        if (twdt_reconfig_result != ESP_OK) {
            PA_LOG_ERROR("main", "esp_task_wdt_reconfigure failed: 0x%x", twdt_reconfig_result);
        }
    } else if (twdt_init_result != ESP_OK) {
        PA_LOG_ERROR("main", "esp_task_wdt_init failed: 0x%x", twdt_init_result);
    }

    // Initialize FailsafeGate and DriveArbiter before task creation
    failsafeInit(&robotStateMux);
    driveArbiterInit(&robotStateMux);

    // Safety: boot with drive locked until the identified drive watchdog
    // (SBUS1 or routed SBUS2) sees a frame. This applies the same
    // initialization as the runtime watchdog for the active drive source.
    if (rcPlan.driveWatchdogSource != DriveWatchdogSource::NONE) {
        failsafeTrigger(FailsafeLayer::SBUS_WATCHDOG);
    }

    // Detect watchdog reset from previous boot  --  set estop so robot does not move
    // until operator explicitly clears via POST /api/estop/clear
    esp_reset_reason_t resetReason = esp_reset_reason();
    if (bootWatchdogResetDecision(resetReason)) {
        failsafeTrigger(FailsafeLayer::WATCHDOG_RESET);
        PA_LOG_ERROR("main", "watchdog reset detected - estop set");
    }

    // Create command queues
    servoCmdQueue = xQueueCreate(8, sizeof(ServoCommand));
    domeCmdQueue = xQueueCreate(8, sizeof(DomeCommand));
    audioCmdQueue = xQueueCreate(8, sizeof(AudioCommand));
    domeTxQueue = xQueueCreate(16, sizeof(DomeTxCmd));
    sequenceDispatcherInit();

    // ServoTask owns LEDC hardware init and applies AUX LED channel skip policy.
    servoTaskInit();
    domeTaskInit();
    bool auxLedTaskReady = auxLedTaskInit();
    if (!auxLedTaskReady) {
        PA_LOG_ERROR("main", "aux LED task init failed; AUX LED API will report unavailable");
    }

    // Real-time / core pinning contract: see docs/failsafe.md "Real-Time / Core Pinning Contract".
    // Core 1 real-time (heap-allocation-free): DriveTask, RCInputTask, ServoTask, DomeTask, DomeLinkTask.
    // Core 0 non-RT: AudioTask, AuxLedTask, SafetyMonitorTask, SequenceDispatcherTask, WebEvents, ArduinoOTA.

    // Launch real-time tasks on Core 1
    // DriveTask: 50 Hz hoverboard frames, feeds TWDT, Layer 3 web timeout
    // RcInputTask: ~200 Hz RC poll (all modes), Layer 1+2 failsafe; omitted
    // when no RC input is active for the boot-selected mode and routing.
    // ServoTask: 50 Hz servo PWM updates
    // DomeTask: 50 Hz ESC PWM updates; omitted when dome output is disabled
    // at boot (ADR 0027: not spawning the owning task at all is the preferred form).
    xTaskCreatePinnedToCore(driveTask, "DriveTask", 4096, nullptr, 5, nullptr, 1);
    if (rcPlan.taskEnabled) {
        xTaskCreatePinnedToCore(rcInputTask, "RCInputTask", 7168, nullptr, 5, nullptr, 1);
    }
    xTaskCreatePinnedToCore(
        servoTask, "ServoTask", 4096, nullptr, 4, nullptr,
        1);  // HWM: code fix (ConfigSnapshot->ServoConfig in hot paths) + 3072->4096
    if (bootCfg.system.enable_dome_esc) {
        // Size is chip-target specific; DOME_TASK_STACK_BYTES in include/config.h
        // carries the measured chain and the sizing rule. The note this line used
        // to carry -- "sized from profiler HWM: 108 B free at 2048 B" -- was an
        // artoo-esp32 reading, and on the ESP32-P4 the same source needs 3280 B,
        // which is 208 B more than the 3072 that reading justified (#248).
        xTaskCreatePinnedToCore(domeTask, "DomeTask", DOME_TASK_STACK_BYTES, nullptr, 4,
                                nullptr, 1);
    }

    // AudioTask: Core 0 (non-RT)  --  software bit-bang TX blocks ~6 ms per command;
    // keeping off Core 1 avoids any interaction with DriveTask / ServoTask timing.
    // Omitted when audio output is disabled at boot (ADR 0027: not spawning the owning
    // task at all is the preferred form).
    if (bootCfg.system.enable_audio) {
        xTaskCreatePinnedToCore(audioTask, "AudioTask", 6144, nullptr, 3, nullptr, 0);
    }

    // AuxLedTask: Core 0 (non-RT) - WS2812B effects and API-driven color/effect updates.
    // Runs independently of Core 1 control loops.
    // Size is chip-target specific; AUX_LED_TASK_STACK_BYTES in include/config.h
    // carries the measured chain and the sizing rule.
    if (auxLedTaskReady) {
        xTaskCreatePinnedToCore(auxLedTask, "AuxLedTask", AUX_LED_TASK_STACK_BYTES, nullptr, 2,
                                nullptr, 0);
    }

    // DomeLinkTask: Core 1  --  bidirectional Marcduino serial to AstroPixelsPlus.
    // UART2 TX/RX are non-blocking hardware operations; Core 1 at priority 3.
    // 4096: profiler measured 988 B free at 3072 B without WiFi fallback active;
    // HTTPClient call-chain in sendCommandOverWifi needs 3 KB+ of stack headroom.
    xTaskCreatePinnedToCore(domeLinkTask, "DomeLinkTask", 6144, nullptr, 3, nullptr, 1);

    // SafetyMonitorTask: 10 Hz audit on Core 0 (non-RT, low priority).
    // Size is chip-target specific; SAFETY_MONITOR_STACK_BYTES in include/config.h
    // carries the per-target frame evidence, the margin and how to reproduce it.
    //
    // What this comment used to say was wrong in both halves, and both errors
    // flattered the result. The format buffer is 256 bytes, not 128
    // (PA_LOG_SERIAL_LINE_MAX, include/logging.h) -- and the buffer is not what
    // dominates anyway: it sits inside this task's own frame, which is at most
    // 400 bytes on either chip. The depth is in newlib below snprintf, which
    // nothing had measured. So "bumped to 3072 for adequate headroom" was
    // reasoned from half the buffer and none of the call chain, and on the
    // ESP32-P4 3072 does not in fact cover it (#245).
    xTaskCreatePinnedToCore(safetyMonitorTask, "SafetyMonitor", SAFETY_MONITOR_STACK_BYTES,
                            nullptr, 2, nullptr, 0);

    // Index Learned Sequences from LittleFS before the dispatcher can run one.
    // Mounts LittleFS (idempotent) and scans /seq/. Boot scan reuses the run
    // staging buffers, so it must complete before SeqDisp starts (ADR 0006).
    seqStoreInit();

    // SequenceDispatcherTask: Core 0 (non-RT)  --  body-side DM:* sequence coordinator.
    // 10 ms tick. Dispatches to domeQueueTx / audioQueueDollar / domeCmdQueue.
    // Core 0 keeps the 50 Hz safety loops on Core 1 unburdened (ADR 0004).
    xTaskCreatePinnedToCore(sequenceDispatcherTask, "SeqDisp", 4096, nullptr, 3, nullptr, 0);

    // Restore last mood  --  audio component only.
    // - Dome link is not yet established at boot, so dome TX is intentionally skipped.
    // - We apply audio directly here rather than via applyMood() to avoid writing
    //   last_mood back to NVS (we just read it; the value has not changed).
    if (robotState.activeMood != 0) {
        const char* bootAudioCmd = moodAudioCommand(robotState.activeMood);
        if (bootAudioCmd) {
            audioQueueDollar(bootAudioCmd, SRC_INTERNAL);
            PA_LOG_INFO("main", "boot mood: %s", moodLabel(robotState.activeMood));
        }
    }

    // Start WiFi AP and web server. webServerInit() only mounts LittleFS,
    // registers the WiFi event handler, and decides/executes the WiFi boot
    // posture (ADR 0015) -- it does NOT itself bind port 80. The actual HTTP
    // server only starts once WiFi genuinely comes up, via
    // startHttpServerOnce() inside handleWiFiEvent().
    webServerInit();

    uint16_t bootTrack = 0;
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
        // Deinit TWDT before restart  --  prevents esp_restart() from being
        // misclassified as ESP_RST_TASK_WDT and triggering a boot-time estop.
        esp_task_wdt_deinit();
        delay(100);
        ESP.restart();
    }

    vTaskDelay(pdMS_TO_TICKS(100));
}
