// =============================================================================
// src/sequence_dispatcher_step.cpp
//
// Sequence Dispatcher Step Core (ADR 0014)  --  pure decision logic for action dispatch.
// =============================================================================

#include "sequence_dispatcher_step.h"

SequenceDispatcherStepActions sequenceDispatcherStep(const SeqAction& act,
                                                     uint32_t nowMs) {
    SequenceDispatcherStepActions actions;

    switch (act.kind) {
        case SEQ_ACT_DOME_CMD:
            // Forward dome text command as-is to dome queue.
            actions.target = SEQ_DISPATCH_DOME_CMD;
            break;

        case SEQ_ACT_DOME_ROTATE: {
            // Convert speed percentage to float, pass duration, set target queue.
            actions.target = SEQ_DISPATCH_DOME_ROTATE;
            actions.domeRotate.speed = (float)act.domeSpeedPct / 100.0f;
            actions.domeRotate.durationMs = act.domeDurationMs;
            break;
        }

        case SEQ_ACT_AUDIO_DOLLAR:
            // Forward audio dollar command to audio queue.
            actions.target = SEQ_DISPATCH_AUDIO_DOLLAR;
            break;

        case SEQ_ACT_AUDIO_CATEGORY:
            // Route to audio category play with fallback slot.
            actions.target = SEQ_DISPATCH_AUDIO_CATEGORY;
            actions.audioCategory.category = act.audioCategory;
            actions.audioCategory.fallbackSlot = act.audioFallbackSlot;
            break;

        case SEQ_ACT_AUDIO_STOP:
            // Forward to audio stop queue.
            actions.target = SEQ_DISPATCH_AUDIO_STOP;
            break;

        default:
            // Unknown action: silent success (fail-safe behavior).
            actions.target = SEQ_DISPATCH_NONE;
            break;
    }

    // Suppress unused-parameter warnings for callers that may not use all fields.
    (void)nowMs;

    return actions;
}

uint32_t sequence_dispatcher_wait_ms(bool engineActive, bool resyncClosePending) {
    // Active choreography and staged ring-close drain require 10 ms cadence for
    // absolute step timing and smooth servo motion. Otherwise the task blocks on
    // the request queue and only wakes to feed the TWDT reset (3 s timeout) and
    // to poll estop/dome-connect edges, whose resync latency tolerance is
    // operator-scale (250 ms idle is acceptable).
    if (engineActive || resyncClosePending) {
        return 10;
    }
    return 250;
}
