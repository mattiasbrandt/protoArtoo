// =============================================================================
// src/main.cpp
//
// protoArtoo  --  ESP32 body controller for MK4 astromech droid.
// Boot: config load, safety defaults, task creation.
// =============================================================================

#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <cstddef>
#include <cstring>
#include <esp_log.h>
#include <esp_task_wdt.h>

#include "audio_dollar_parser.h"
#include "audio_task.h"
#include "aux_led.h"
#include "config_store.h"
#include "config_cache.h"
#include "console_module.h"
#include "console_serial_output.h"
#include "console_task.h"
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
// Who owns the serial wire, and how far the owner has drained the ring
// (ADR 0037). Both live under logMux with the ring itself, which is what makes
// the hand-over exact: paLogLine() reads the flag in the same critical section
// it appends in, so a line arriving as ownership changes is either written
// directly (flag still clear, and the cursor is placed past it) or drained
// (flag set, and the cursor was placed before it) - never both, never neither.
//
// There is no serial mutex any more. It existed because any task could write
// the wire if it took the lock; after this ADR the Console task is the only
// writer, so the lock had nothing left to coordinate (#270).
static bool logWireOwnedByConsole = false;
static uint32_t logDrainCursor = 0;
// Static bootstrap ring: captures the few lines logged before NVS config loads
// (schema migration, mount problems). paLogRingApplyBootDepth() replaces it
// with a heap ring sized to the saved log level and carries these lines over.
static char logBootstrapStorage[LOG_RING_BOOTSTRAP_LINES][LOG_LINE_MAX];
static LogBuffer recentLogBuf = {};
// /api/logs response body, allocated alongside the sized ring (capacity *
// LOG_LINE_MAX + 1). One shared buffer, sized and synchronised for exactly
// one caller (handleLogsGet(), src/web/api_logs.cpp) - race-free there
// because its callers serialize on one task. The Console task's
// system.status.logs query answers the same question WITHOUT this buffer:
// it reads the ring directly, one line at a time, through
// getLogBufferCount()/copyLogLineAt() below (#239, src/console/console_module.cpp)
// rather than becoming a second concurrent caller of this allocation. Do not
// add a caller here from a second task.
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

// The ESP-IDF logger's output hook (ADR 0037).
//
// ESP_LOGx does not go through paLogLine(): it formats and hands the result to
// the IDF's own vprintf, which writes stdout - UART0 - with no project lock
// and no knowledge of who owns the wire. src/drivers/sbus_decoder.cpp (8
// sites) and src/drivers/ledc_pwm.cpp (7 sites) log that way, from RCInputTask
// and ServoTask, which is a second writer on a wire that now has an owner.
// Pointing the hook at the Log Ring makes those lines ordinary log lines: they
// land in /api/logs like everything else and the Console task drains them.
//
// The panic handler is deliberately NOT affected and needs no exception here.
// Read, not assumed: panic.c's panic_print_str()/panic_print_char() drive the
// UART and USB-serial-JTAG registers directly, and the core dump's own
// ESP_COREDUMP_LOG* macros expand to esp_rom_printf()
// (espcoredump/include_core_dump/esp_core_dump_types.h). Neither goes through
// esp_log_vprintf, so neither can reach this hook - the panic path keeps its
// own direct route exactly as ADR 0037 requires, and ADR 0031's rule that
// nothing be added to that path is not touched. That matters here beyond
// tidiness: this hook takes the ring's critical section, which a panicking
// core must never be made to wait on.
//
// The formatted line is bounded by PA_LOG_SERIAL_LINE_MAX like every other
// serial-bound line, and its trailing newline is stripped because the ring
// stores lines, not terminators - the drain and /api/logs both add their own.
// This runs in the calling task's context, so it costs that task one
// PA_LOG_SERIAL_LINE_MAX buffer on its stack, the same as any PA_LOG_* call
// already does (include/logging.h's _PA_LOG_FORMAT).
//
// Nothing here may call ESP_LOGx: it is what the logger calls.
static int paLogIdfVprintf(const char* format, va_list args) {
    char buf[PA_LOG_SERIAL_LINE_MAX];
    const int written = vsnprintf(buf, sizeof(buf), format, args);
    if (written <= 0) {
        return written;
    }
    size_t len = ((size_t)written < sizeof(buf)) ? (size_t)written : sizeof(buf) - 1;
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
        buf[--len] = '\0';
    }
    if (len > 0) {
        paLogLine(buf);
    }
    return written;
}

void paLogInit() {
    if (recentLogBuf.lines == nullptr) {
        logBufferInit(&recentLogBuf, logBootstrapStorage, LOG_RING_BOOTSTRAP_LINES);
    }
    // Install before anything can ESP_LOGx, so the two driver files never get
    // a boot window in which they still write UART0 directly.
    esp_log_set_vprintf(paLogIdfVprintf);
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

void paLogWireBindToConsole() {
    taskENTER_CRITICAL(&logMux);
    logDrainCursor = recentLogBuf.totalWritten;
    logWireOwnedByConsole = true;
    taskEXIT_CRITICAL(&logMux);
}

bool paLogDrainNextLine(char* out, size_t outSize, uint32_t* evicted) {
    taskENTER_CRITICAL(&logMux);
    bool got = logBufferDrainNext(&recentLogBuf, &logDrainCursor, out, outSize, evicted);
    taskEXIT_CRITICAL(&logMux);
    return got;
}

void paLogLine(const char* line) {
    if (line == nullptr) {
        return;
    }

    // ONE write, to the Log Ring, under the ring's own critical section. The
    // ring was already this line's authoritative record (#245); ADR 0037 makes
    // the serial copy something produced FROM it rather than beside it, so a
    // logging task on either core now pays one bounded critical section and a
    // copy, and nothing that depends on the wire.
    //
    // The ownership flag is read here, inside the same section, and not from
    // the sink: reading it outside would leave a window in which a line is
    // appended before the hand-over and its wire decision taken after it (or
    // the reverse), which duplicates or loses exactly one line at boot.
    bool wireOwned;
    taskENTER_CRITICAL(&logMux);
    logBufferAppend(&recentLogBuf, line);
    wireOwned = logWireOwnedByConsole;
    taskEXIT_CRITICAL(&logMux);

    if (!wireOwned) {
        // Before the bind there is no Console task to drain the ring, so the
        // line goes straight out - the boot log, unchanged. One framed write,
        // best-effort, exactly as the pre-bind path has always been (#245's
        // contract, ADR 0036's framing). After the bind this branch is dead by
        // construction and the drain is the only writer
        // (consoleSerialDrainLogs, src/console/console_serial_output.cpp).
        consoleSerialEmitFramedLine(line, strlen(line), /*waitForRoom=*/false);
    }
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
#if ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE
    // BEFORE begin(), deliberately: HWCDC::begin() only creates its receive
    // queue `if (rx_queue == NULL)`, so this is the one call that can size it
    // without vQueueDelete()ing a queue the RX interrupt may already be
    // pushing into. Why the default is too small for a line this Console has
    // to refuse: include/console_task.h's CONSOLE_SERIAL_RX_QUEUE_BYTES (#229).
    //
    // The result is deliberately not checked, and the failure is still safe:
    // on a failed allocation HWCDC::setRxBufferSize() leaves rx_queue NULL and
    // begin() below then creates the driver's own 256-byte default, which is
    // the pre-#229 behaviour rather than a broken one. There is also no logger
    // to report it to here - paLogInit() runs after begin().
    Serial.setRxBufferSize(CONSOLE_SERIAL_RX_QUEUE_BYTES);
#endif
    Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE
    // Serial is the USB-Serial-JTAG CDC (HWCDC). When the host stops draining
    // the port while USB stays enumerated - a detached serial monitor is enough:
    // SOF keepalives continue, so HWCDC's isPlugged() stays true and its
    // `connected` flag never clears - every blocked write waits out HWCDC's
    // bounded-progress cap of 20 x tx_timeout_ms. At the 100 ms default that is
    // ~2 s per write call and ~4 s per paLogLine() (two writes), against a
    // WATCHDOG_TIMEOUT_S of 3 s. That starved DomeTask's TWDT feed from inside
    // one PA_LOG_DEBUG and reset the board (#245 defect 1, coredump-verified);
    // every TWDT-subscribed task that logs is equally exposed. Zero makes the
    // CDC transport strictly best-effort: a full TX ring drops bytes instead of
    // blocking the caller. Nothing is lost from the authoritative record -
    // paLogLine() appends to the log ring (/api/logs) regardless of what the
    // serial write manages to send.
    Serial.setTxTimeoutMs(0);
#endif
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
    consoleModuleInit();
    logBootHealth();
    ConfigSnapshot bootCfg = {};
    configCacheRead(&bootCfg);
    const RcInputActiveConfig activeRc = rcInputActiveConfigFromSystem(bootCfg.system);
    configCacheSetActiveRcInput(activeRc);
    configCacheSetActiveDomeEnabled(bootCfg.system.enable_dome_esc);
    configCacheSetActiveAudioEnabled(bootCfg.system.enable_audio);
    configCacheSetActiveComponentToggles(bootCfg.system);
    RcInputStartupPlan rcPlan = rcInputStepStartupPlan(activeRc);

    // Layer 4: Task Watchdog Timer.
    // IDF 5.x: esp_task_wdt_init() takes a config struct (timeout_ms, idle_core_mask,
    // trigger_panic). Both chip targets ship prebuilt IDF with CONFIG_ESP_TASK_WDT_INIT=y
    // (5 s, panic, idle core 0 watched), so the system TWDT is already running before
    // setup() and esp_task_wdt_init() returns ESP_ERR_INVALID_STATE; the reconfigure
    // path below is the one that actually applies our config. (This comment used to
    // claim init succeeds on artoo-esp32 - its sdkconfig says otherwise; confirmed on
    // P4 hardware by the "TWDT already initialized" boot warning, #245.)
    //
    // idle_core_mask=0: per IDF task_wdt.c, esp_task_wdt_reconfigure() keeps
    // explicitly-added task entries and unsubscribes the previous mask's idle tasks,
    // so after this call the watched set is exactly the tasks that call
    // esp_task_wdt_add(NULL) on themselves: DriveTask, ServoTask, RCInputTask (when
    // spawned), SequenceDispatcherTask, and DomeTask (when spawned).
    const esp_task_wdt_config_t twdt_config = {
        .timeout_ms    = WATCHDOG_TIMEOUT_S * 1000U,
        .idle_core_mask = 0,
        .trigger_panic  = true,
    };
    esp_err_t twdt_init_result = esp_task_wdt_init(&twdt_config);
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
    // Size is chip-target specific; DRIVE_TASK_STACK_BYTES in include/config.h carries
    // the measured worst-case chain for both chips and why ESP32 is raised too even
    // though its own figure reads as 32 B under (that figure is a lower bound).
    xTaskCreatePinnedToCore(driveTask, "DriveTask", DRIVE_TASK_STACK_BYTES, nullptr, 5,
                            nullptr, 1);
    if (rcPlan.taskEnabled) {
        // Size is chip-target specific; RC_INPUT_TASK_STACK_BYTES in include/config.h
        // carries the measured chain. The #248 rule lands on 7168 on both chips
        // (ESP32-P4 5376 * 1.25 = 6720 -> 7168); ESP32 is the existing 7168, not
        // a lowering to its own 5248-chain figure, which is a Xtensa lower bound.
        xTaskCreatePinnedToCore(rcInputTask, "RCInputTask", RC_INPUT_TASK_STACK_BYTES,
                                nullptr, 5, nullptr, 1);
    }
    // Size is chip-target specific; SERVO_TASK_STACK_BYTES in include/config.h
    // carries the measured chain and the sizing rule. What this line used to say
    // -- "HWM: code fix (ConfigSnapshot->ServoConfig in hot paths) + 3072->4096"
    // -- was a high-water mark, the same evidence class that let the Console
    // stand 4 KB under its chain (#226). This is a Core 1 real-time task and it
    // had no static measurement at all until #271 walked it.
    xTaskCreatePinnedToCore(servoTask, "ServoTask", SERVO_TASK_STACK_BYTES, nullptr, 4, nullptr,
                            1);
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
        // Size is chip-target specific; AUDIO_TASK_STACK_BYTES in include/config.h
        // carries the measured chain. The #248 rule lands on 6144 on both chips
        // (ESP32-P4 4848 * 1.25 = 6060 -> 6144).
        xTaskCreatePinnedToCore(audioTask, "AudioTask", AUDIO_TASK_STACK_BYTES, nullptr, 3,
                                nullptr, 0);
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
    // Size is chip-target specific; DOME_LINK_TASK_STACK_BYTES in include/config.h.
    // The old note here -- "4096: profiler measured 988 B free at 3072 B ... HTTPClient
    // call-chain needs 3 KB+" -- was reasoned from a high-water mark, which only ever
    // reports the deepest path that actually ran. The static worst case is 5856 B on
    // ESP32 and 7360 B on ESP32-P4, so 6144 never covered the P4 at all (#250).
    xTaskCreatePinnedToCore(domeLinkTask, "DomeLinkTask", DOME_LINK_TASK_STACK_BYTES,
                            nullptr, 3, nullptr, 1);

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
    // Size is chip-target specific; SEQ_DISPATCHER_TASK_STACK_BYTES in include/config.h
    // carries the measured chain and the sizing rule. The depth is the Learned Sequence
    // load on this task's own stack (seqStorePrepare -> protocolCheck -> pcFailAt ->
    // snprintf float formatting -> first-use heap/log-mutex tail), 240 B past the 4096
    // this used to be (#250). Both chips land on the same 512-byte step.
    xTaskCreatePinnedToCore(sequenceDispatcherTask, "SeqDisp", SEQ_DISPATCHER_TASK_STACK_BYTES,
                            nullptr, 3, nullptr, 0);

    // ConsoleTask: Core 0 (non-RT)  --  serial console adapter using embedded-cli.
    // ADR 0034: persistent Controller Console, no network dependency, no dynamic
    // allocation in its loop. Created on both boards (P4 USB CDC, artoo UART0 bridge).
    //
    // Size is chip-target specific; CONSOLE_TASK_STACK_BYTES in include/config.h
    // carries the measured chain and the sizing rule. What this line used to say --
    // "stack sized from measured high-water mark with margin" -- is why 5120 stood:
    // a high-water mark reports only the paths that have actually run, and no
    // config write could reach this task when it was taken. The first one that did
    // overflowed the stack on the board (#226).
    xTaskCreatePinnedToCore(consoleTask, "Console", CONSOLE_TASK_STACK_BYTES, nullptr, 2, nullptr,
                            0);

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

    // Inject the help reader for the Console module after LittleFS is mounted.
    // The reader provides seek+read access to console_help.txt with no per-request
    // allocation (ADR 0034, address-based read pattern).
    // If the help file is unavailable, pass NULL to degrade gracefully.
#ifdef ARDUINO
    {
        static File g_helpFile;  // Held for process lifetime
        static ConsoleHelpReader g_helpReader = {};

        if (LittleFS.exists("/console_help.txt")) {
            g_helpFile = LittleFS.open("/console_help.txt", "r");
            if (g_helpFile) {
                g_helpReader.seek = [](void* ctx, uint32_t offset) -> bool {
                    File* f = (File*)ctx;
                    return f && f->seek(offset);
                };
                g_helpReader.read = [](void* ctx, char* out_buffer, size_t len) -> size_t {
                    File* f = (File*)ctx;
                    if (!f || !out_buffer || len == 0) return 0;
                    return f->read((uint8_t*)out_buffer, len);
                };
                g_helpReader.ctx = &g_helpFile;
                consoleModuleSetHelpReader(&g_helpReader);
                PA_LOG_DEBUG("main", "console help reader injected");
            } else {
                PA_LOG_WARN("main", "failed to open console help file");
            }
        } else {
            PA_LOG_WARN("main", "console help file not found");
        }
    }
#endif  // ARDUINO

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
    // No Serial.flush() here. The Console task exists by this point (created
    // above), so this line is already the Console task's to put on the wire,
    // and flushing a wire this task does not own is what ADR 0037 removes.
    // It was never doing the job it looked like it was doing either: on a
    // CDC-on-boot build main.cpp's own setTxTimeoutMs(0) makes HWCDC::flush()
    // take its `tries == 0` path, which DISCARDS the TX ring and forces the
    // `connected` latch false - so it dropped whatever of the boot log the
    // host had not yet picked up (ADR 0036's Consequences named these two
    // sites; src/tasks/console_task.cpp:#265 reported them as out of scope
    // then, and this ticket owns src/main.cpp).
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
        // No Serial.flush() here either, for the reason setup()'s tail gives:
        // the Arduino loop task does not own this wire. The line above is in
        // the ring, the Console task drains it within its 10 ms poll, and the
        // delay(100) below is well past the ~4 ms a line of this length takes
        // at 115200 8N1 - so the restart notice still reaches the operator,
        // and on the CDC it now reaches them at all (flush() there discarded
        // the ring rather than draining it).
        // Deinit TWDT before restart  --  prevents esp_restart() from being
        // misclassified as ESP_RST_TASK_WDT and triggering a boot-time estop.
        esp_task_wdt_deinit();
        delay(100);
        ESP.restart();
    }

    vTaskDelay(pdMS_TO_TICKS(100));
}
