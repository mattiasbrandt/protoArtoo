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
//
// SAFETY: This task does NOT directly control motors or actuators.
//         It is an observer only. All motor control is in DriveTask.
// =============================================================================

#include <Arduino.h>
#include <esp_heap_caps.h>

#include "api_profiler.h"
#include "logging.h"
#include "robot_state.h"

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
    profilerInit();

    while (true) {
        if (!hwmLogged) {
            PA_LOG_DEBUG(TAG, "stack HWM: %u words free",
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

        // Heap health: warn on low free heap, high fragmentation, and log periodic metrics
        uint32_t freeHeap = ESP.getFreeHeap();
        size_t largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
        float fragRatio = (freeHeap > 0) ? (1.0f - (float)largestBlock / (float)freeHeap) : 0.0f;

        bool nowLowHeap = (freeHeap < 20480);
        if (nowLowHeap && !lastLowHeap) {
            PA_LOG_WARN(TAG, "low heap entered: %lu bytes free, largest block: %u bytes",
                        (unsigned long)freeHeap, (unsigned)largestBlock);
        } else if (!nowLowHeap && lastLowHeap) {
            PA_LOG_INFO(TAG, "low heap recovered: %lu bytes free", (unsigned long)freeHeap);
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
            PA_LOG_DEBUG(TAG, "heap: free=%lu min=%lu largest=%u frag=%.2f",
                         (unsigned long)freeHeap, (unsigned long)ESP.getMinFreeHeap(),
                         (unsigned)largestBlock, (double)fragRatio);
        }

        profilerPeriodicCollect();

        vTaskDelay(pdMS_TO_TICKS(100));  // 10 Hz
    }
}
