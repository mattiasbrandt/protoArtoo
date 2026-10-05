// =============================================================================
// src/hosted_link_supervisor.cpp
//
// Hosted Link Supervisor Step Core  --  phase-model decisions for the bounded
// ESP-Hosted transport-failure recovery ladder as pure functions over
// explicit state. See include/hosted_link_supervisor.h for design rationale.
// =============================================================================

#include "hosted_link_supervisor.h"

const char* hostedLinkPhaseName(HostedLinkPhase phase) {
    switch (phase) {
        case HostedLinkPhase::Idle:
            return "idle";
        case HostedLinkPhase::Armed:
            return "armed";
        case HostedLinkPhase::Attempting:
            return "attempting";
        case HostedLinkPhase::Degraded:
            return "degraded";
    }
    return "unknown";
}

HostedLinkFailureActions hostedLinkSupervisorOnTransportFailure(
    HostedLinkSupervisorState& state, uint32_t nowMs) {
    HostedLinkFailureActions actions;

    // Counted unconditionally, in every phase -- a failure that folds into
    // an in-flight run or arrives during Degraded is still a real event the
    // status surface must report.
    state.transportFailureEventCount++;
    state.lastFailureAtMs = nowMs;

    // Only arm a fresh ladder from Idle. Armed/Attempting: a run is already
    // in flight and this failure folds into it. Degraded: terminal by
    // design (ADR 0032) -- it stays that way for the rest of this boot.
    if (state.phase == HostedLinkPhase::Idle) {
        state.phase = HostedLinkPhase::Armed;
        state.attemptCount = 0;
        actions.shouldNotifyRecoveryTask = true;
    }

    return actions;
}

void hostedLinkSupervisorOnTransportUp(HostedLinkSupervisorState& state) {
    state.transportUpEventCount++;
}

void hostedLinkSupervisorBeginAttemptRun(HostedLinkSupervisorState& state) {
    state.phase = HostedLinkPhase::Attempting;
}

HostedLinkAttemptOutcome hostedLinkSupervisorRecordAttempt(
    HostedLinkSupervisorState& state, uint32_t nowMs, bool transportUp) {
    HostedLinkAttemptOutcome outcome;

    state.attemptCount++;
    state.totalAttemptCount++;
    state.lastAttemptAtMs = nowMs;

    if (transportUp) {
        state.phase = HostedLinkPhase::Idle;
        state.recoveredCount++;
        outcome.recovered = true;
    } else if (state.attemptCount >= kHostedLinkRecoveryMaxAttempts) {
        state.phase = HostedLinkPhase::Degraded;
        state.degradedAtMs = nowMs;
        outcome.exhausted = true;
    } else {
        outcome.shouldRetry = true;
    }

    return outcome;
}
