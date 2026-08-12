// =============================================================================
// include/sequence_dispatcher_step.h
//
// Sequence Dispatcher Step Core (ADR 0014)  --  pure decision logic for action dispatch.
//
// Extracted from sequenceDispatcher.cpp for native testability. The pure core
// owns the decision: given a SeqAction from the engine, which target (dome,
// audio) and what command should be queued?
//
// The task loop (adapter) owns all side effects: queue calls, FreeRTOS, RobotState,
// and logging. The step-core is Arduino/FreeRTOS-free and compiles in the native
// test environment.
// =============================================================================
#pragma once

#include <stdint.h>

#include "sequence_engine.h"  // SeqAction

// Dispatch targets  --  what queue/function the action should route to
enum SequenceDispatchTarget : uint8_t {
    SEQ_DISPATCH_DOME_CMD,         // domeQueueTx(payload)
    SEQ_DISPATCH_DOME_ROTATE,      // domeRotateQueue: converted to DomeCommand
    SEQ_DISPATCH_AUDIO_DOLLAR,     // audioQueueDollar(payload, SRC_SEQ)
    SEQ_DISPATCH_AUDIO_CATEGORY,   // audioQueuePlayCategory(...)
    SEQ_DISPATCH_AUDIO_STOP,       // audioQueueTrackStop(SRC_SEQ)
    SEQ_DISPATCH_NONE,             // Silent success (unknown action)
};

// Dispatch decision output  --  what the adapter should execute
struct SequenceDispatcherStepActions {
    SequenceDispatchTarget target = SEQ_DISPATCH_NONE;

    // For SEQ_DISPATCH_DOME_ROTATE
    struct {
        float speed = 0.0f;           // from act.domeSpeedPct / 100.0f
        uint32_t durationMs = 0;      // from act.domeDurationMs
    } domeRotate;

    // For SEQ_DISPATCH_AUDIO_CATEGORY
    struct {
        uint8_t category = 0;         // from act.audioCategory
        uint8_t fallbackSlot = 0;     // from act.audioFallbackSlot
    } audioCategory;

    // For all text-payload targets (DOME_CMD, AUDIO_DOLLAR)
    // The adapter will use act.payload directly
};

// Sequence Dispatcher Step Core: pure decision logic.
//
// Given a SeqAction from the engine and the current timestamp, decide which
// target (dome queue, audio queue) and what command format should be used.
// Returns the dispatch decision; the adapter executes it.
//
// No side effects: does not call queues, FreeRTOS, RobotState, or logging.
SequenceDispatcherStepActions sequenceDispatcherStep(const SeqAction& act,
                                                     uint32_t nowMs);

// Idle gating: compute the wait timeout for the task's blocking queue receive.
//
// When a sequence is active or a staged ring-close is pending, the task must
// run at 10 ms cadence to feed step-driven choreography and ring-close drain.
// When idle, the task blocks on the request queue and only wakes for TWDT
// reset (3 s timeout) and to poll estop/dome-connect edges.
//
// Args:
//   engineActive: true if a sequence is currently running.
//   resyncClosePending: true if a staged ring-close is waiting in resyncCloseIdx.
//
// Returns: wait_ms for xQueueReceive timeout (10 ms if either condition is
// true, 250 ms otherwise).
uint32_t sequence_dispatcher_wait_ms(bool engineActive, bool resyncClosePending);
