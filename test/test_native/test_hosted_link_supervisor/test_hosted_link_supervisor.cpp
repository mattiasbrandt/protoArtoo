// =============================================================================
// test/test_native/test_hosted_link_supervisor/test_hosted_link_supervisor.cpp
//
// Native tests for the Hosted Link Supervisor Step Core (#189 slice 2). Covers
// the transitions that carry the safety property, not the plumbing:
//   - a failure arms only from idle
//   - a failure arriving mid-run folds into the run in flight
//   - the ladder stops at its bound
//   - degraded is terminal for the boot and refuses to re-arm
// =============================================================================

#include <unity.h>

#include "hosted_link_supervisor.h"

void setUp() {}
void tearDown() {}

// =============================================================================
// A failure arms only from Idle
// =============================================================================

void test_failure_arms_fresh_ladder_from_idle() {
    HostedLinkSupervisorState s;
    TEST_ASSERT_EQUAL_INT((int)HostedLinkPhase::Idle, (int)s.phase);

    HostedLinkFailureActions a = hostedLinkSupervisorOnTransportFailure(s, 1000);

    TEST_ASSERT_TRUE(a.shouldNotifyRecoveryTask);
    TEST_ASSERT_EQUAL_INT((int)HostedLinkPhase::Armed, (int)s.phase);
    TEST_ASSERT_EQUAL_UINT(1, s.transportFailureEventCount);
    TEST_ASSERT_EQUAL_UINT32(1000, s.lastFailureAtMs);
    TEST_ASSERT_EQUAL_UINT(0, s.attemptCount);
}

void test_failure_does_not_notify_while_armed() {
    HostedLinkSupervisorState s;
    hostedLinkSupervisorOnTransportFailure(s, 1000);  // Idle -> Armed
    TEST_ASSERT_EQUAL_INT((int)HostedLinkPhase::Armed, (int)s.phase);

    HostedLinkFailureActions a = hostedLinkSupervisorOnTransportFailure(s, 1500);

    TEST_ASSERT_FALSE(a.shouldNotifyRecoveryTask);
    TEST_ASSERT_EQUAL_INT((int)HostedLinkPhase::Armed, (int)s.phase);
    // Still counted, even though it did not notify -- the status surface
    // must show every event, not just the ones that armed a run.
    TEST_ASSERT_EQUAL_UINT(2, s.transportFailureEventCount);
}

// =============================================================================
// A failure arriving mid-run folds into the run in flight
// =============================================================================

void test_failure_during_attempting_folds_into_run_in_flight() {
    HostedLinkSupervisorState s;
    hostedLinkSupervisorOnTransportFailure(s, 1000);   // Idle -> Armed
    hostedLinkSupervisorBeginAttemptRun(s);             // Armed -> Attempting
    hostedLinkSupervisorRecordAttempt(s, 2000, false);  // attempt 1/5, still down
    TEST_ASSERT_EQUAL_INT((int)HostedLinkPhase::Attempting, (int)s.phase);
    TEST_ASSERT_EQUAL_UINT(1, s.attemptCount);

    // A second failure event arrives while the first run is still in flight.
    HostedLinkFailureActions a = hostedLinkSupervisorOnTransportFailure(s, 2500);

    TEST_ASSERT_FALSE(a.shouldNotifyRecoveryTask);
    TEST_ASSERT_EQUAL_INT((int)HostedLinkPhase::Attempting, (int)s.phase);
    // Folding in must not reset the attempt count of the run already in
    // flight -- that would restart the ladder's bound instead of continuing
    // it, defeating the "must not retry forever" property.
    TEST_ASSERT_EQUAL_UINT(1, s.attemptCount);
    TEST_ASSERT_EQUAL_UINT(2, s.transportFailureEventCount);
}

// =============================================================================
// The ladder stops at its bound
// =============================================================================

void test_ladder_recovers_before_bound() {
    HostedLinkSupervisorState s;
    hostedLinkSupervisorOnTransportFailure(s, 1000);
    hostedLinkSupervisorBeginAttemptRun(s);

    hostedLinkSupervisorRecordAttempt(s, 2000, false);  // 1/5 down
    HostedLinkAttemptOutcome outcome = hostedLinkSupervisorRecordAttempt(s, 3000, true);  // 2/5 up

    TEST_ASSERT_TRUE(outcome.recovered);
    TEST_ASSERT_FALSE(outcome.exhausted);
    TEST_ASSERT_FALSE(outcome.shouldRetry);
    TEST_ASSERT_EQUAL_INT((int)HostedLinkPhase::Idle, (int)s.phase);
    TEST_ASSERT_EQUAL_UINT(2, s.attemptCount);
    TEST_ASSERT_EQUAL_UINT(2, s.totalAttemptCount);
    TEST_ASSERT_EQUAL_UINT(1, s.recoveredCount);
}

void test_ladder_stops_at_max_attempts_and_degrades() {
    HostedLinkSupervisorState s;
    hostedLinkSupervisorOnTransportFailure(s, 1000);
    hostedLinkSupervisorBeginAttemptRun(s);

    HostedLinkAttemptOutcome outcome;
    for (unsigned int i = 1; i <= kHostedLinkRecoveryMaxAttempts; i++) {
        outcome = hostedLinkSupervisorRecordAttempt(s, 1000 * i, /*transportUp=*/false);
        if (i < kHostedLinkRecoveryMaxAttempts) {
            TEST_ASSERT_TRUE(outcome.shouldRetry);
            TEST_ASSERT_FALSE(outcome.exhausted);
            TEST_ASSERT_EQUAL_INT((int)HostedLinkPhase::Attempting, (int)s.phase);
        }
    }

    // The bound-th failed attempt must exhaust the ladder, not retry again.
    TEST_ASSERT_FALSE(outcome.shouldRetry);
    TEST_ASSERT_TRUE(outcome.exhausted);
    TEST_ASSERT_FALSE(outcome.recovered);
    TEST_ASSERT_EQUAL_INT((int)HostedLinkPhase::Degraded, (int)s.phase);
    TEST_ASSERT_EQUAL_UINT(kHostedLinkRecoveryMaxAttempts, s.attemptCount);
    TEST_ASSERT_EQUAL_UINT(kHostedLinkRecoveryMaxAttempts, s.totalAttemptCount);
    TEST_ASSERT_EQUAL_UINT32(1000 * kHostedLinkRecoveryMaxAttempts, s.degradedAtMs);
}

// =============================================================================
// Degraded is terminal for the boot and refuses to re-arm
// =============================================================================

void test_degraded_refuses_to_rearm_on_further_failures() {
    HostedLinkSupervisorState s;
    hostedLinkSupervisorOnTransportFailure(s, 1000);
    hostedLinkSupervisorBeginAttemptRun(s);
    for (unsigned int i = 1; i <= kHostedLinkRecoveryMaxAttempts; i++) {
        hostedLinkSupervisorRecordAttempt(s, 1000 * i, /*transportUp=*/false);
    }
    TEST_ASSERT_EQUAL_INT((int)HostedLinkPhase::Degraded, (int)s.phase);

    // Any number of further transport-failure events must never re-arm the
    // ladder from Degraded -- that phase is terminal for the rest of this
    // boot by design (ADR 0032: no host restart to clear it either).
    for (int i = 0; i < 5; i++) {
        HostedLinkFailureActions a = hostedLinkSupervisorOnTransportFailure(s, 60000 + (uint32_t)i);
        TEST_ASSERT_FALSE(a.shouldNotifyRecoveryTask);
        TEST_ASSERT_EQUAL_INT((int)HostedLinkPhase::Degraded, (int)s.phase);
    }
    // Still counted for observability even though terminal.
    TEST_ASSERT_EQUAL_UINT(6, s.transportFailureEventCount);
}

// =============================================================================
// ESP_HOSTED_EVENT_TRANSPORT_UP is purely observational
// =============================================================================

void test_transport_up_event_is_observational_only() {
    HostedLinkSupervisorState s;
    hostedLinkSupervisorOnTransportUp(s);
    hostedLinkSupervisorOnTransportUp(s);

    TEST_ASSERT_EQUAL_UINT(2, s.transportUpEventCount);
    // Never drives a phase transition on its own.
    TEST_ASSERT_EQUAL_INT((int)HostedLinkPhase::Idle, (int)s.phase);
}

// =============================================================================
// Phase name mapping (used verbatim by /api/status and device logs)
// =============================================================================

void test_phase_name_mapping() {
    TEST_ASSERT_EQUAL_STRING("idle", hostedLinkPhaseName(HostedLinkPhase::Idle));
    TEST_ASSERT_EQUAL_STRING("armed", hostedLinkPhaseName(HostedLinkPhase::Armed));
    TEST_ASSERT_EQUAL_STRING("attempting", hostedLinkPhaseName(HostedLinkPhase::Attempting));
    TEST_ASSERT_EQUAL_STRING("degraded", hostedLinkPhaseName(HostedLinkPhase::Degraded));
}

// =============================================================================
// Test runner
// =============================================================================

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_failure_arms_fresh_ladder_from_idle);
    RUN_TEST(test_failure_does_not_notify_while_armed);
    RUN_TEST(test_failure_during_attempting_folds_into_run_in_flight);
    RUN_TEST(test_ladder_recovers_before_bound);
    RUN_TEST(test_ladder_stops_at_max_attempts_and_degrades);
    RUN_TEST(test_degraded_refuses_to_rearm_on_further_failures);
    RUN_TEST(test_transport_up_event_is_observational_only);
    RUN_TEST(test_phase_name_mapping);

    return UNITY_END();
}
