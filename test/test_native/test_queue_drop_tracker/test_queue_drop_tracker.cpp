// =============================================================================
// test/test_native/test_queue_drop_tracker/test_queue_drop_tracker.cpp
//
// Native tests for queueDropShouldLog() — the pure rate-limit decision.
//
// Covers the two key rules:
//   1. First drop always logs (everLogged starts false).
//   2. Second drop within 5s window does not log (rate limiting).
// =============================================================================

#include <unity.h>
#include "../../../include/queue_drop_tracker.h"

void setUp(void) {
    // Fixture: clean state for each test.
}

void tearDown(void) {
}

// Test 1: First drop always logs, even at uptime 0.
void test_first_drop_always_logs_at_boot(void) {
    QueueDropRateState state = {.lastLogMs = 0, .everLogged = false};
    uint32_t nowMs = 0;  // At boot

    bool shouldLog = queueDropShouldLog(state, nowMs);

    TEST_ASSERT_TRUE(shouldLog);
    TEST_ASSERT_TRUE(state.everLogged);
    TEST_ASSERT_EQUAL_UINT32(0, state.lastLogMs);
}

// Test 2: First drop always logs, even at 3s uptime (before 5s rate limit).
void test_first_drop_always_logs_before_5s_window(void) {
    QueueDropRateState state = {.lastLogMs = 0, .everLogged = false};
    uint32_t nowMs = 3000;  // 3 seconds uptime

    bool shouldLog = queueDropShouldLog(state, nowMs);

    TEST_ASSERT_TRUE(shouldLog);
    TEST_ASSERT_TRUE(state.everLogged);
    TEST_ASSERT_EQUAL_UINT32(3000, state.lastLogMs);
}

// Test 3: Second drop within 5s window does NOT log.
void test_second_drop_within_5s_does_not_log(void) {
    QueueDropRateState state = {.lastLogMs = 1000, .everLogged = true};
    uint32_t nowMs = 4000;  // 3s after first drop

    bool shouldLog = queueDropShouldLog(state, nowMs);

    TEST_ASSERT_FALSE(shouldLog);
    TEST_ASSERT_EQUAL_UINT32(1000, state.lastLogMs);  // Unchanged
}

// Test 4: Drop at exactly 5s boundary (5000ms elapsed) logs again.
void test_drop_at_5s_boundary_logs(void) {
    QueueDropRateState state = {.lastLogMs = 1000, .everLogged = true};
    uint32_t nowMs = 6001;  // 5001ms after first drop

    bool shouldLog = queueDropShouldLog(state, nowMs);

    TEST_ASSERT_TRUE(shouldLog);
    TEST_ASSERT_EQUAL_UINT32(6001, state.lastLogMs);
}

// Test 5: Drop at 5s boundary minus 1ms (4999ms elapsed) does NOT log.
void test_drop_just_before_5s_boundary_does_not_log(void) {
    QueueDropRateState state = {.lastLogMs = 1000, .everLogged = true};
    uint32_t nowMs = 5999;  // 4999ms after first drop

    bool shouldLog = queueDropShouldLog(state, nowMs);

    TEST_ASSERT_FALSE(shouldLog);
    TEST_ASSERT_EQUAL_UINT32(1000, state.lastLogMs);  // Unchanged
}

// Test 6: Multiple rate-limit windows work correctly.
void test_multiple_rate_limit_windows(void) {
    QueueDropRateState state = {.lastLogMs = 0, .everLogged = false};

    // First drop at t=0.
    bool shouldLog1 = queueDropShouldLog(state, 0);
    TEST_ASSERT_TRUE(shouldLog1);
    TEST_ASSERT_EQUAL_UINT32(0, state.lastLogMs);

    // Second drop at t=3s (within window).
    bool shouldLog2 = queueDropShouldLog(state, 3000);
    TEST_ASSERT_FALSE(shouldLog2);

    // Third drop at t=5001ms (outside window).
    bool shouldLog3 = queueDropShouldLog(state, 5001);
    TEST_ASSERT_TRUE(shouldLog3);
    TEST_ASSERT_EQUAL_UINT32(5001, state.lastLogMs);

    // Fourth drop at t=8000ms (within new window).
    bool shouldLog4 = queueDropShouldLog(state, 8000);
    TEST_ASSERT_FALSE(shouldLog4);

    // Fifth drop at t=10002ms (outside new window).
    bool shouldLog5 = queueDropShouldLog(state, 10002);
    TEST_ASSERT_TRUE(shouldLog5);
    TEST_ASSERT_EQUAL_UINT32(10002, state.lastLogMs);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_first_drop_always_logs_at_boot);
    RUN_TEST(test_first_drop_always_logs_before_5s_window);
    RUN_TEST(test_second_drop_within_5s_does_not_log);
    RUN_TEST(test_drop_at_5s_boundary_logs);
    RUN_TEST(test_drop_just_before_5s_boundary_does_not_log);
    RUN_TEST(test_multiple_rate_limit_windows);
    return UNITY_END();
}
