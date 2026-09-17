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
    SEQ_DISPATCH_BODY_MOVE,        // resolve the Part against the Servo Output
                                   // rows, then servoCmdQueue
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
    //
    // SEQ_DISPATCH_BODY_MOVE carries no fields either, and for a reason worth
    // stating: which Output drives the Part is the builder's own droid's answer,
    // held in the live Servo Output table, and this core is pure. The adapter
    // walks the rows and hands the one it found to sequenceBodyStepPlan()
    // (include/sequence_body_step.h), which owns that decision.
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
// When a sequence is active, a staged ring-close is pending, or a bulk centre
// is sweeping, the task must run at 10 ms cadence to feed step-driven
// choreography, the ring-close drain and the Cadence Floor. When idle, the task
// blocks on the request queue and only wakes for TWDT reset (3 s timeout) and
// to poll estop/dome-connect edges.
//
// Args:
//   engineActive: true if a sequence is currently running.
//   resyncClosePending: true if a staged ring-close is waiting in resyncCloseIdx.
//   bulkCentreActive: true while a bulk centre sweep has rows left (#365). The
//     idle 250 ms would round the Cadence Floor up to the next wake, so the
//     spacing between two Outputs would be whatever the tick allowed rather
//     than the number the Floor names.
//
// Returns: wait_ms for xQueueReceive timeout (10 ms if any condition is
// true, 250 ms otherwise).
uint32_t sequence_dispatcher_wait_ms(bool engineActive, bool resyncClosePending,
                                     bool bulkCentreActive);
