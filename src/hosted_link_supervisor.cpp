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

const char* hostedLinkLivenessSourceName(HostedLinkLivenessSource source) {
    switch (source) {
        case HostedLinkLivenessSource::None:
            return "none";
        case HostedLinkLivenessSource::Heartbeat:
            return "heartbeat";
        case HostedLinkLivenessSource::Probe:
            return "probe";
    }
    return "unknown";
}

// The one Idle->Armed transition. Both a transport failure and a liveness
// miss come through here, so neither can open a second way into a ladder run.
// Armed/Attempting: a run is already in flight and the trigger folds into it.
// Degraded: terminal by design (ADR 0032) -- it stays that way for the rest
// of this boot.
static HostedLinkFailureActions armFromIdle(HostedLinkSupervisorState& state) {
    HostedLinkFailureActions actions;
    if (state.phase == HostedLinkPhase::Idle) {
        state.phase = HostedLinkPhase::Armed;
        state.attemptCount = 0;
        // The watch stops with the run: the ladder re-initialises (and so
        // resets) the C6, which forgets its heartbeat. The device shell
        // restarts the watch after a recovered attempt.
        state.livenessSource = HostedLinkLivenessSource::None;
        state.consecutiveProbeFailures = 0;
        actions.shouldNotifyRecoveryTask = true;
    }
    return actions;
}

HostedLinkFailureActions hostedLinkSupervisorOnTransportFailure(
    HostedLinkSupervisorState& state, uint32_t nowMs) {
    // Counted unconditionally, in every phase -- a failure that folds into
    // an in-flight run or arrives during Degraded is still a real event the
    // status surface must report.
    state.transportFailureEventCount++;
    state.lastFailureAtMs = nowMs;

    return armFromIdle(state);
}

void hostedLinkSupervisorOnTransportUp(HostedLinkSupervisorState& state) {
    state.transportUpEventCount++;
}

void hostedLinkSupervisorStartLivenessWatch(HostedLinkSupervisorState& state,
                                            HostedLinkLivenessSource source, uint32_t nowMs) {
    // Only from Idle: if a transport failure armed a fresh run between an
    // attempt's verdict and this call, that run owns the watch now and
    // restarts it when it recovers (armFromIdle() cleared the source).
    if (state.phase != HostedLinkPhase::Idle) {
        return;
    }
    state.livenessSource = source;
    state.lastLivenessAtMs = nowMs;
    state.consecutiveProbeFailures = 0;
}

void hostedLinkSupervisorOnHeartbeat(HostedLinkSupervisorState& state, uint32_t nowMs,
                                     uint32_t beatNumber) {
    state.heartbeatCount++;
    state.lastHeartbeatNumber = beatNumber;
    if (state.livenessSource == HostedLinkLivenessSource::Heartbeat) {
        state.lastLivenessAtMs = nowMs;
    }
}

void hostedLinkSupervisorRecordProbe(HostedLinkSupervisorState& state, uint32_t nowMs,
                                     bool answered) {
    if (state.livenessSource != HostedLinkLivenessSource::Probe) {
        return;
    }
    if (answered) {
        state.consecutiveProbeFailures = 0;
        state.lastLivenessAtMs = nowMs;
    } else {
        state.consecutiveProbeFailures++;
    }
}

bool hostedLinkSupervisorLivenessDue(const HostedLinkSupervisorState& state, uint32_t nowMs) {
    if (state.phase != HostedLinkPhase::Idle) {
        return false;
    }
    switch (state.livenessSource) {
        case HostedLinkLivenessSource::None:
            return false;
        case HostedLinkLivenessSource::Heartbeat:
            // Unsigned subtraction: correct across the millis() wrap.
            return static_cast<uint32_t>(nowMs - state.lastLivenessAtMs) >=
                   kHostedLinkLivenessIntervalMs * kHostedLinkLivenessMissLimit;
        case HostedLinkLivenessSource::Probe:
            return state.consecutiveProbeFailures >= kHostedLinkLivenessMissLimit;
    }
    return false;
}

HostedLinkFailureActions hostedLinkSupervisorOnLivenessMissed(HostedLinkSupervisorState& state,
                                                              uint32_t nowMs) {
    state.livenessMissCount++;
    state.lastLivenessMissAtMs = nowMs;

    return armFromIdle(state);
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
