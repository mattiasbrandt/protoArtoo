// =============================================================================
// test/test_native/test_web_response_deadline/test_web_response_deadline.cpp
//
// Native unit tests for the response-phase deadline decision core
// (include/web_response_deadline.h, ADR 0020/0024).
//
// Covers: the clock starting at the first write rather than at admission, the
// breach latch, the exemptions (wrong socket, event stream, disabled), what
// disarm reports into the published maximum, and millisecond rollover.
// =============================================================================
#include <unity.h>

#include "web_response_deadline.h"

namespace {

constexpr int kSocket = 7;
constexpr uint32_t kDeadlineMs = 4000;

WebResponseDeadline d;

// One write attempt on the armed socket.
WebResponseDeadlineVerdict check(uint32_t nowMs) {
    return webResponseDeadlineCheck(&d, kSocket, nowMs, kDeadlineMs);
}

}  // namespace

void setUp() {
    webResponseDeadlineInit(&d);
}
void tearDown() {
}

// -----------------------------------------------------------------------------
// Idle and arming
// -----------------------------------------------------------------------------

void test_idle_deadline_never_breaches() {
    TEST_ASSERT_EQUAL(WebResponseDeadlineVerdict::kProceed, check(0));
    TEST_ASSERT_EQUAL(WebResponseDeadlineVerdict::kProceed, check(1000000));
}

void test_negative_socket_leaves_deadline_idle() {
    webResponseDeadlineArm(&d, -1);
    TEST_ASSERT_EQUAL(WebResponseDeadlineVerdict::kProceed, check(0));
    TEST_ASSERT_EQUAL(WebResponseDeadlineVerdict::kProceed, check(kDeadlineMs * 10));
}

// -----------------------------------------------------------------------------
// The clock starts at the first write, not at admission
// -----------------------------------------------------------------------------

void test_first_write_starts_the_clock_and_always_proceeds() {
    webResponseDeadlineArm(&d, kSocket);
    // Arrives long after admission -- a slow body build, or an upload that
    // spent its time receiving. The response itself has not been slow yet.
    TEST_ASSERT_EQUAL(WebResponseDeadlineVerdict::kProceed, check(60000));
    // And is still inside its deadline one millisecond later.
    TEST_ASSERT_EQUAL(WebResponseDeadlineVerdict::kProceed, check(60001));
}

void test_write_at_the_deadline_breaches() {
    webResponseDeadlineArm(&d, kSocket);
    TEST_ASSERT_EQUAL(WebResponseDeadlineVerdict::kProceed, check(1000));
    TEST_ASSERT_EQUAL(WebResponseDeadlineVerdict::kProceed, check(1000 + kDeadlineMs - 1));
    TEST_ASSERT_EQUAL(WebResponseDeadlineVerdict::kBreach, check(1000 + kDeadlineMs));
}

// -----------------------------------------------------------------------------
// The latch
// -----------------------------------------------------------------------------

void test_breach_latches_for_the_rest_of_the_phase() {
    webResponseDeadlineArm(&d, kSocket);
    check(0);
    TEST_ASSERT_EQUAL(WebResponseDeadlineVerdict::kBreach, check(kDeadlineMs));
    // A vendor send path that tries once more must not be let through just
    // because the clock has not moved.
    TEST_ASSERT_EQUAL(WebResponseDeadlineVerdict::kBreach, check(kDeadlineMs));
    TEST_ASSERT_EQUAL(WebResponseDeadlineVerdict::kBreach, check(kDeadlineMs + 1));
}

void test_rearming_clears_a_latched_breach() {
    webResponseDeadlineArm(&d, kSocket);
    check(0);
    TEST_ASSERT_EQUAL(WebResponseDeadlineVerdict::kBreach, check(kDeadlineMs));

    // The next request on the same socket is innocent. Carrying the latch would
    // fail its very first write, which on a keep-alive connection is exactly
    // the request after the one that stalled.
    webResponseDeadlineArm(&d, kSocket);
    TEST_ASSERT_EQUAL(WebResponseDeadlineVerdict::kProceed, check(kDeadlineMs));
    TEST_ASSERT_EQUAL(WebResponseDeadlineVerdict::kProceed, check(kDeadlineMs + 1));
}

// -----------------------------------------------------------------------------
// Exemptions
// -----------------------------------------------------------------------------

void test_other_sockets_are_untouched() {
    webResponseDeadlineArm(&d, kSocket);
    webResponseDeadlineCheck(&d, kSocket, 0, kDeadlineMs);

    // An event-stream broadcast on another connection, long past the armed
    // request's deadline.
    const int other = kSocket + 1;
    TEST_ASSERT_EQUAL(WebResponseDeadlineVerdict::kProceed,
                      webResponseDeadlineCheck(&d, other, kDeadlineMs * 5, kDeadlineMs));
}

void test_exempt_stream_never_breaches() {
    webResponseDeadlineArm(&d, kSocket);
    webResponseDeadlineExempt(&d, kSocket);
    TEST_ASSERT_EQUAL(WebResponseDeadlineVerdict::kProceed, check(0));
    TEST_ASSERT_EQUAL(WebResponseDeadlineVerdict::kProceed, check(kDeadlineMs * 100));
}

void test_exempt_ignores_a_socket_that_is_not_armed() {
    webResponseDeadlineArm(&d, kSocket);
    webResponseDeadlineExempt(&d, kSocket + 1);
    check(0);
    TEST_ASSERT_EQUAL(WebResponseDeadlineVerdict::kBreach, check(kDeadlineMs));
}

void test_zero_deadline_disables_the_guard() {
    webResponseDeadlineArm(&d, kSocket);
    TEST_ASSERT_EQUAL(WebResponseDeadlineVerdict::kProceed,
                      webResponseDeadlineCheck(&d, kSocket, 0, 0));
    TEST_ASSERT_EQUAL(WebResponseDeadlineVerdict::kProceed,
                      webResponseDeadlineCheck(&d, kSocket, 1000000, 0));
}

// -----------------------------------------------------------------------------
// What disarm reports into the published maximum
// -----------------------------------------------------------------------------

void test_disarm_reports_a_completed_response_duration() {
    webResponseDeadlineArm(&d, kSocket);
    check(500);
    check(700);
    TEST_ASSERT_EQUAL_INT32(400, webResponseDeadlineDisarm(&d, 900));
}

void test_disarm_reports_nothing_for_a_response_that_never_wrote() {
    webResponseDeadlineArm(&d, kSocket);
    TEST_ASSERT_EQUAL_INT32(-1, webResponseDeadlineDisarm(&d, 5000));
}

void test_disarm_reports_nothing_when_nothing_was_armed() {
    TEST_ASSERT_EQUAL_INT32(-1, webResponseDeadlineDisarm(&d, 5000));
}

void test_disarm_excludes_a_breached_response_from_the_statistic() {
    webResponseDeadlineArm(&d, kSocket);
    check(0);
    TEST_ASSERT_EQUAL(WebResponseDeadlineVerdict::kBreach, check(kDeadlineMs));
    // A stalled client must not be able to raise the very maximum the deadline
    // margin is measured against.
    TEST_ASSERT_EQUAL_INT32(-1, webResponseDeadlineDisarm(&d, kDeadlineMs + 10));
}

void test_disarm_leaves_the_deadline_idle() {
    webResponseDeadlineArm(&d, kSocket);
    check(0);
    webResponseDeadlineDisarm(&d, 10);
    TEST_ASSERT_EQUAL(WebResponseDeadlineVerdict::kProceed, check(kDeadlineMs * 10));
}

// -----------------------------------------------------------------------------
// Millisecond rollover
// -----------------------------------------------------------------------------

void test_rollover_does_not_breach_a_fresh_response() {
    webResponseDeadlineArm(&d, kSocket);
    // First write just before the 32-bit millisecond counter wraps.
    check(0xFFFFFF00u);
    // Second write 0x200 ms later, on the far side of the wrap.
    TEST_ASSERT_EQUAL(WebResponseDeadlineVerdict::kProceed, check(0x00000100u));
}

void test_rollover_does_not_reprieve_a_stalled_response() {
    webResponseDeadlineArm(&d, kSocket);
    check(0xFFFFFF00u);
    // Past the deadline, measured across the wrap.
    TEST_ASSERT_EQUAL(WebResponseDeadlineVerdict::kBreach, check(0xFFFFFF00u + kDeadlineMs));
}

int main(int, char**) {
    UNITY_BEGIN();

    RUN_TEST(test_idle_deadline_never_breaches);
    RUN_TEST(test_negative_socket_leaves_deadline_idle);

    RUN_TEST(test_first_write_starts_the_clock_and_always_proceeds);
    RUN_TEST(test_write_at_the_deadline_breaches);

    RUN_TEST(test_breach_latches_for_the_rest_of_the_phase);
    RUN_TEST(test_rearming_clears_a_latched_breach);

    RUN_TEST(test_other_sockets_are_untouched);
    RUN_TEST(test_exempt_stream_never_breaches);
    RUN_TEST(test_exempt_ignores_a_socket_that_is_not_armed);
    RUN_TEST(test_zero_deadline_disables_the_guard);

    RUN_TEST(test_disarm_reports_a_completed_response_duration);
    RUN_TEST(test_disarm_reports_nothing_for_a_response_that_never_wrote);
    RUN_TEST(test_disarm_reports_nothing_when_nothing_was_armed);
    RUN_TEST(test_disarm_excludes_a_breached_response_from_the_statistic);
    RUN_TEST(test_disarm_leaves_the_deadline_idle);

    RUN_TEST(test_rollover_does_not_breach_a_fresh_response);
    RUN_TEST(test_rollover_does_not_reprieve_a_stalled_response);

    return UNITY_END();
}
