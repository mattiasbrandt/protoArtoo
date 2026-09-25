// =============================================================================
// src/tasks/safety.cpp
//
// SafetyMonitorTask  --  secondary audit task for protoArtoo.
// Runs at 10 Hz on Core 0 (low priority, non-blocking).
//
// Responsibilities:
//   - Log failsafe trigger count increases
//   - Verify dome connection state transitions (connected <-> lost)
//   - Warn if free heap drops below 20 KB and monitor heap fragmentation
//   - Carry out an operator-requested restart (requestSystemRestart())
//
// SAFETY: This task does NOT directly control motors or actuators.
//         It is an observer only. All motor control is in DriveTask. The one
//         thing it does is the restart an operator asked for, which it owns
//         because the Arduino loopTask, which used to, exits after setup().
// =============================================================================

#include <Arduino.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>

#include "api_profiler.h"
#include "failed_alloc_tracker.h"
#include "heap_health.h"
#include "logging.h"
#include "robot_state.h"
#include "safety.h"
#include "web_server.h"  // requestSystemRestart()

static const char* TAG = "SafetyMonitor";

// Track previous values to detect transitions
static uint32_t lastFailsafeCount = 0;
static bool lastDomeConnected = false;
static bool lastSbusLost = true;
static bool lastLowHeap = false;
static bool lastFragmented = false;
static uint8_t fragmentedSampleCount = 0;

constexpr size_t HEAP_FRAGMENT_LARGEST_BLOCK_WARN_BYTES = 10240;
constexpr uint8_t HEAP_FRAGMENT_WARN_SAMPLE_COUNT = 30;  // 3 s at 10 Hz

// An operator-requested restart: armed by requestSystemRestart(), carried out
// by restartIfRequested() on this task's 10 Hz tick once the delay has run.
// Nothing else arms it - a network fault never restarts the controller
// (ADR 0032). It lived on the Arduino loopTask's loop() until #428 let that
// task exit after setup() to give its stack back to the heap; this task was
// already polling at the same 100 ms.
static volatile bool restartRequested = false;
static volatile uint32_t restartAtMs = 0;
static portMUX_TYPE restartMux = portMUX_INITIALIZER_UNLOCKED;

void requestSystemRestart(uint32_t delayMs) {
    taskENTER_CRITICAL(&restartMux);
    restartRequested = true;
    restartAtMs = millis() + delayMs;
    taskEXIT_CRITICAL(&restartMux);
}

static void restartIfRequested() {
    bool shouldRestart = false;

    taskENTER_CRITICAL(&restartMux);
    if (restartRequested && (int32_t)(millis() - restartAtMs) >= 0) {
        shouldRestart = true;
    }
    taskEXIT_CRITICAL(&restartMux);

    if (!shouldRestart) {
        return;
    }

    PA_LOG_INFO(TAG, "restarting controller");
    // No Serial.flush() here: this task does not own the wire (ADR 0039). The
    // line above is in the ring, the Console task drains it within its 10 ms
    // poll, and the delay(100) below is well past the ~4 ms a line of this
    // length takes at 115200 8N1 - so the restart notice still reaches the
    // operator, and on the CDC it reaches them at all (flush() there discarded
    // the ring rather than draining it).
    //
    // Deinit TWDT before restart  --  prevents esp_restart() from being
    // misclassified as ESP_RST_TASK_WDT and triggering a boot-time estop.
    // ESP-IDF refuses the deinit while any task is still subscribed
    // (task_wdt.c: "Tasks/users still subscribed"), and DriveTask, ServoTask
    // and SeqDisp always are, so the watchdog stays on; they go on feeding it
    // through the delay below, and esp_restart() resets with ESP_RST_SW
    // either way. The refusal is logged rather than dropped.
    const esp_err_t twdtDeinit = esp_task_wdt_deinit();
    if (twdtDeinit != ESP_OK) {
        PA_LOG_WARN(TAG, "task watchdog still running at restart: %s",
                    esp_err_to_name(twdtDeinit));
    }
    delay(100);
    ESP.restart();
}

// -----------------------------------------------------------------------------
// safetyMonitorTask()
// Observer-only audit task. Logs state transitions and health warnings.
// Core 0, priority 2, 10 Hz. Stack size is chip-target specific and lives with
// its evidence at SAFETY_MONITOR_STACK_BYTES in include/config.h; it is not
// repeated here, because the figure this line used to name (2048) had been
// stale since the task was created with a larger one.
// Does NOT feed TWDT  --  this is not a real-time task.
// Does NOT set failsafe flags  --  read-only access to RobotState.
// -----------------------------------------------------------------------------
void safetyMonitorTask(void* pvParameters) {
    PA_LOG_INFO(TAG, "active");

    bool hwmLogged = false;
    // Unconditional, and before profilerInit(): the failed-allocation counter
    // is reported by /api/status on every build (ADR 0017's heap rule reads it
    // on a production image), and IDF keeps one hook slot, so this is the only
    // registration in the firmware. Registered here rather than earlier so the
    // count means the same thing it has always meant on a profiler build.
    failedAllocTrackerInit();
    profilerInit();

    while (true) {
        if (!hwmLogged) {
            PA_LOG_DEBUG(TAG, "stack HWM: %u bytes free",
                         (unsigned)uxTaskGetStackHighWaterMark(NULL));
            hwmLogged = true;
        }

        // Read state snapshot under mutex
        FailsafeDiagnostics diag = {};
        uint32_t domeLastMs;
        bool sbusLost;
        taskENTER_CRITICAL(&robotStateMux);
        copyFailsafeDiagnosticsLocked(&diag);
        domeLastMs = robotState.domeLastSeenMs;
        sbusLost = diag.sbusSignalLost;
        taskEXIT_CRITICAL(&robotStateMux);
        // Log new failsafe triggers
        if (diag.failsafeTriggerCount > lastFailsafeCount) {
            PA_LOG_WARN(TAG,
                        "failsafe triggered - count:%lu source:%d estop:%d sbus:%d hw:%d trigger_ms:%lu zero_ms:%lu trigger_to_zero_ms:%lu trigger_src:%d",
                        (unsigned long)diag.failsafeTriggerCount, (int)diag.failsafeSource, (int)diag.estop, (int)diag.sbusSignalLost, (int)diag.sbusHwFailsafe,
                        (unsigned long)diag.failsafeLastTriggerMs, (unsigned long)diag.failsafeLastZeroOutputMs,
                        (unsigned long)diag.failsafeLastTriggerToZeroMs, (int)diag.failsafeLastTriggerSource);
            lastFailsafeCount = diag.failsafeTriggerCount;
        }

        // Log dome connection state transitions
        bool domeNowConnected = (millis() - domeLastMs) < 5000 && domeLastMs > 0;
        if (domeNowConnected != lastDomeConnected) {
            PA_LOG_INFO(TAG, "dome link %s", domeNowConnected ? "CONNECTED" : "LOST");
            profilerModeTransition(domeNowConnected ? "dome_connected" : "dome_lost");
            lastDomeConnected = domeNowConnected;
        }

        // Track RC signal transitions
        if (sbusLost != lastSbusLost) {
            profilerModeTransition(sbusLost ? "rc_lost" : "rc_linked");
            lastSbusLost = sbusLost;
        }

        profilerObserveOptionalSubsystems();

        // Heap health: warn on low free heap, high fragmentation, and log periodic metrics.
        // freeHeap is the Arduino internal figure, kept only for the periodic log below
        // beside data_free so the two can be compared.
        uint32_t freeHeap = ESP.getFreeHeap();
        // Fragmentation pair: both terms come from ONE capability mask, and the mask
        // that matters for safety is the internal 8-bit data heap. INTERNAL alone
        // would count IRAM-only regions malloc cannot return for byte-addressable
        // data (artoo-esp32); 8BIT alone includes PSRAM, which on the ESP32-P4 made
        // largest (~33 MB) dwarf internal free (~114 KB), so frag read -287.92 and
        // the <10 KB WARN below was structurally dead (#245 defect 2).
        uint32_t dataHeapFree =
            (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        uint32_t largestBlock =
            (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        float fragRatio = heapFragRatio(dataHeapFree, largestBlock);

        // Low heap reads the same data heap as the fragmentation pair. ESP.getFreeHeap()
        // is heap_caps_get_free_size(MALLOC_CAP_INTERNAL), which on the classic ESP32
        // (artoo-esp32) includes the IRAM heap - 42,392 B on the image #427 measured -
        // that malloc cannot hand out for byte-addressable data. That alone kept the
        // figure above this 20 KB threshold, so the warning could never fire there.
        bool nowLowHeap = (dataHeapFree < 20480);
        if (nowLowHeap && !lastLowHeap) {
            PA_LOG_WARN(TAG, "low heap entered: %lu bytes free, largest block: %u bytes",
                        (unsigned long)dataHeapFree, (unsigned)largestBlock);
        } else if (!nowLowHeap && lastLowHeap) {
            PA_LOG_INFO(TAG, "low heap recovered: %lu bytes free", (unsigned long)dataHeapFree);
        }
        lastLowHeap = nowLowHeap;

        // Warn only after sustained pressure. WiFi/lwIP/SSE can cause short
        // allocation churn, but a persistent <10 KB largest block is actionable.
        if (largestBlock < HEAP_FRAGMENT_LARGEST_BLOCK_WARN_BYTES) {
            if (fragmentedSampleCount < HEAP_FRAGMENT_WARN_SAMPLE_COUNT) {
                fragmentedSampleCount++;
            }
        } else {
            fragmentedSampleCount = 0;
        }
        bool nowFragmented = (fragmentedSampleCount >= HEAP_FRAGMENT_WARN_SAMPLE_COUNT);
        if (nowFragmented && !lastFragmented) {
            PA_LOG_WARN(TAG, "heap fragmented: largest block %u bytes, frag ratio %.2f",
                        (unsigned)largestBlock, (double)fragRatio);
        } else if (!nowFragmented && lastFragmented) {
            PA_LOG_INFO(TAG, "heap fragmentation cleared: largest block %u bytes",
                        (unsigned)largestBlock);
        }
        lastFragmented = nowFragmented;

        static int periodicCount = 0;
        if (++periodicCount >= 60) {  // ~6 s at 10 Hz
            periodicCount = 0;
            PA_LOG_DEBUG(TAG, "heap: free=%lu min=%lu data_free=%lu largest=%u frag=%.2f",
                         (unsigned long)freeHeap, (unsigned long)ESP.getMinFreeHeap(),
                         (unsigned long)dataHeapFree, (unsigned)largestBlock,
                         (double)fragRatio);
        }

        profilerPeriodicCollect();

        restartIfRequested();

        vTaskDelay(pdMS_TO_TICKS(100));  // 10 Hz
    }
}
