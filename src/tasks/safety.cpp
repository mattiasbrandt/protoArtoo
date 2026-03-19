// =============================================================================
// src/tasks/safety.cpp
//
// SafetyMonitorTask — secondary audit task for protoArtoo.
// Runs at 10 Hz on Core 0 (low priority, non-blocking).
//
// Responsibilities:
//   - Log failsafe trigger count increases
//   - Verify dome connection state transitions (connected ↔ lost)
//   - Warn if free heap drops below 20 KB and monitor heap fragmentation
//
// SAFETY: This task does NOT directly control motors or actuators.
//         It is an observer only. All motor control is in DriveTask.
// =============================================================================

#include <Arduino.h>
#include <esp_heap_caps.h>

#include "logging.h"
#include "robot_state.h"

static const char* TAG = "SafetyMonitor";

// Track previous values to detect transitions
static uint32_t lastFailsafeCount = 0;
static bool lastDomeConnected = false;

// -----------------------------------------------------------------------------
// safetyMonitorTask()
// Observer-only audit task. Logs state transitions and health warnings.
// Core 0, priority 2, 2048-byte stack, 10 Hz.
// Does NOT feed TWDT — this is not a real-time task.
// Does NOT set failsafe flags — read-only access to RobotState.
// -----------------------------------------------------------------------------
void safetyMonitorTask(void* pvParameters) {
    PA_LOG_INFO(TAG, "started — 10 Hz audit on Core 0");

    bool hwmLogged = false;

    while (true) {
        if (!hwmLogged) {
            PA_LOG_INFO(TAG, "stack HWM: %u words free",
                        (unsigned)uxTaskGetStackHighWaterMark(NULL));
            hwmLogged = true;
        }

        // Read state snapshot under mutex
        taskENTER_CRITICAL(&robotStateMux);
        uint32_t fsCount = robotState.failsafeTriggerCount;
        bool estop = robotState.estop;
        bool sbusLost = robotState.sbusSignalLost;
        bool sbusHw = robotState.sbusHwFailsafe;
        uint32_t domeLastMs = robotState.domeLastSeenMs;
        FailsafeSource fsSrc = robotState.failsafeSource;
        taskEXIT_CRITICAL(&robotStateMux);

        // Log new failsafe triggers
        if (fsCount > lastFailsafeCount) {
            PA_LOG_WARN(TAG, "failsafe triggered — count:%lu source:%d estop:%d sbus:%d hw:%d",
                        (unsigned long)fsCount, (int)fsSrc, (int)estop, (int)sbusLost, (int)sbusHw);
            lastFailsafeCount = fsCount;
        }

        // Log dome connection state transitions
        bool domeNowConnected = (millis() - domeLastMs) < 5000 && domeLastMs > 0;
        if (domeNowConnected != lastDomeConnected) {
            PA_LOG_INFO(TAG, "dome link %s", domeNowConnected ? "CONNECTED" : "LOST");
            lastDomeConnected = domeNowConnected;
        }

        // Heap health: warn on low free heap, high fragmentation, and log periodic metrics
        uint32_t freeHeap = ESP.getFreeHeap();
        size_t largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
        float fragRatio = (freeHeap > 0) ? (1.0f - (float)largestBlock / (float)freeHeap) : 0.0f;

        if (freeHeap < 20480) {  // 20 KB threshold
            PA_LOG_WARN(TAG, "low heap: %lu bytes free, largest block: %u bytes",
                        (unsigned long)freeHeap, (unsigned)largestBlock);
        }
        if (largestBlock < 30720) {  // 30 KB fragmentation threshold
            PA_LOG_WARN(TAG, "heap fragmented: largest block %u bytes, frag ratio %.2f",
                        (unsigned)largestBlock, (double)fragRatio);
        }

        static int periodicCount = 0;
        if (++periodicCount >= 60) {  // ~6 s at 10 Hz
            periodicCount = 0;
            PA_LOG_DEBUG(TAG, "heap: free=%lu min=%lu largest=%u frag=%.2f",
                         (unsigned long)freeHeap, (unsigned long)ESP.getMinFreeHeap(),
                         (unsigned)largestBlock, (double)fragRatio);
        }
        vTaskDelay(pdMS_TO_TICKS(100));  // 10 Hz
    }
}
