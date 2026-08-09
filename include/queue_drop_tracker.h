// =============================================================================
// include/queue_drop_tracker.h
//
// Per-queue rate-limited drop logging. Centralizes diagnostics hygiene for
// queue overflow events.
//
// Features:
//   - First drop always logged (even at boot when uptime < 5s).
//   - Rate-limited to one WARN per queue per 5-second window thereafter.
//   - Keyed by queue identity, not call site (multiple enqueues to same queue
//     share the rate-limit counter).
//   - Increments robotState.queueOverflowCount under portMUX protection.
//   - Safe from Core 1 real-time tasks and Core 0 web handlers.
//   - No heap allocation or blocking.
//
// Architecture (ADR 0005/0014 — pure step core + thin adapter):
//   - queueDropShouldLog() is a pure decision function: state + time -> bool.
//     No I/O, no robotState, no portMUX. Natively testable.
//   - logQueueDrop() is the thin adapter: calls the pure function, emits the
//     WARN under portMUX, and increments robotState.queueOverflowCount.
// =============================================================================
#pragma once

#include <stdint.h>

typedef enum {
    QUEUE_SERVO_CMD = 0,
    QUEUE_DOME_CMD = 1,
    QUEUE_AUDIO_CMD = 2,
    QUEUE_DOME_TX = 3,
    QUEUE_SEQUENCE = 4,
    QUEUE_AUX_LED = 5,
    QUEUE_DROP_ID_COUNT = 6
} QueueDropId;

// Per-queue rate-limit state (pure data, no I/O).
struct QueueDropRateState {
    uint32_t lastLogMs;  // Timestamp of last WARN emitted
    bool everLogged;     // True if this queue has ever triggered a WARN
};

// Pure decision function: should a queue drop log now?
// Returns true if this is the first drop (everLogged=false) or if 5s+ has
// elapsed since the last log. Idempotent and natively testable.
// Precondition: nowMs is from millis() (monotonic uptime in ms).
// Implemented inline (no I/O, no dependencies) so it's available in all builds.
inline bool queueDropShouldLog(QueueDropRateState& state, uint32_t nowMs) {
    // First drop is always logged, even at boot (when uptime < 5s).
    if (!state.everLogged) {
        state.lastLogMs = nowMs;
        state.everLogged = true;
        return true;
    }

    // After first log, rate-limit to once per 5s per queue.
    // Prevents log spam from persistent overloads.
    if ((uint32_t)(nowMs - state.lastLogMs) > 5000) {
        state.lastLogMs = nowMs;
        return true;
    }

    return false;
}

// Log a queue drop (full queue, message dropped). Thin adapter that calls
// queueDropShouldLog(), emits a WARN if true, and increments
// robotState.queueOverflowCount under portMUX protection.
// Safe to call from any task/handler context.
void logQueueDrop(QueueDropId queueId, const char* description);
