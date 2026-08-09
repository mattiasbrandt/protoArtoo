// =============================================================================
// src/queue_drop_tracker.cpp
//
// Per-queue rate-limited drop logging. Centralizes diagnostics for queue
// overflow events across all enqueue sites.
//
// Architecture: pure decision logic (queueDropShouldLog) + thin adapter
// (logQueueDrop). The pure function is natively testable; the adapter handles
// I/O and synchronization.
//
// Thread safety: Uses portMUX protection when incrementing robotState counter.
// Real-time safety: No heap allocation, no blocking, O(1) per call.
// =============================================================================

#include "queue_drop_tracker.h"
#include "robot_state.h"
#include "logging.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>

static const char* TAG = "QueueDrop";

// Per-queue rate-limit state (mirrors struct QueueDropRateState in header).
// Tracks last log time and whether this queue has ever logged a drop
// (to ensure first drop is always logged even at boot).
static QueueDropRateState s_dropTracking[QUEUE_DROP_ID_COUNT] = {};

// Thin adapter: call the pure function, emit WARN if true, increment counter.
void logQueueDrop(QueueDropId queueId, const char* description) {
    if (queueId >= QUEUE_DROP_ID_COUNT) {
        return;
    }

    uint32_t nowMs = millis();

    // Pure decision (natively testable).
    if (queueDropShouldLog(s_dropTracking[queueId], nowMs)) {
        PA_LOG_WARN(TAG, "queue full: %s", description);
    }

    // Increment shared counter under portMUX protection.
    taskENTER_CRITICAL(&robotStateMux);
    robotState.queueOverflowCount++;
    taskEXIT_CRITICAL(&robotStateMux);
}
