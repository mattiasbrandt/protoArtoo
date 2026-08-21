// =============================================================================
// test/test_native/test_sbus_watchdog/test_sbus_watchdog.cpp
//
// Native unit tests for SBUS watchdog timeout and transition detection.
//
// Safety relevance: SBUS signal loss must be detected to enter failsafe mode
// and stop the robot when RC transmitter disconnects.
// =============================================================================
#include <unity.h>

#include "sbus_watchdog.h"

void setUp() {
}
void tearDown() {
}

// --- sbusWatchdogTimeoutCheck() tests ---

void test_sbus_never_received_is_timeout() {
    // lastSbusMs = 0 means no valid SBUS ever received
    TEST_ASSERT_TRUE(sbusWatchdogTimeoutCheck(0, 1000, 200));
}

void test_sbus_recent_frame_is_not_timeout() {
    // Last frame 50ms ago, timeout 200ms - should not timeout
    TEST_ASSERT_FALSE(sbusWatchdogTimeoutCheck(950, 1000, 200));
}

void test_sbus_at_exact_timeout_is_not_timeout() {
    // Last frame 200ms ago, timeout 200ms - not timeout yet (> not >=)
    TEST_ASSERT_FALSE(sbusWatchdogTimeoutCheck(800, 1000, 200));
}

void test_sbus_just_within_timeout_is_not_timeout() {
    // Last frame 199ms ago, timeout 200ms - should not timeout
    TEST_ASSERT_FALSE(sbusWatchdogTimeoutCheck(801, 1000, 200));
}

void test_sbus_one_ms_over_timeout_is_timeout() {
    // Last frame 201ms ago, timeout 200ms - should timeout
    TEST_ASSERT_TRUE(sbusWatchdogTimeoutCheck(799, 1000, 200));
}

void test_sbus_millis_overflow_handles_correctly() {
    // Test millis() overflow scenario: lastSbusMs near UINT32_MAX, currentMs wrapped around
    uint32_t lastSbusMs = 0xFFFFFFFF - 50;  // 50ms before overflow
    uint32_t currentMs = 100;               // Wrapped around to 100
    // Time elapsed = 150ms (with unsigned overflow)
    TEST_ASSERT_FALSE(sbusWatchdogTimeoutCheck(lastSbusMs, currentMs, 200));
}

void test_sbus_millis_overflow_timeout_exceeded() {
    // Overflow scenario where timeout is exceeded
    uint32_t lastSbusMs = 0xFFFFFFFF - 250;  // 250ms before overflow
    uint32_t currentMs = 100;                // Wrapped around to 100
    // Time elapsed = 350ms > 200ms timeout
    TEST_ASSERT_TRUE(sbusWatchdogTimeoutCheck(lastSbusMs, currentMs, 200));
}

void test_sbus_zero_timeout_immediate_timeout() {
    // With 0 timeout, any non-zero age should trigger timeout
    TEST_ASSERT_TRUE(sbusWatchdogTimeoutCheck(999, 1000, 0));
}

void test_sbus_standard_200ms_timeout() {
    // Standard SBUS_TIMEOUT_MS = 200ms
    // 199ms should be OK
    TEST_ASSERT_FALSE(sbusWatchdogTimeoutCheck(801, 1000, 200));
    // 201ms should timeout
    TEST_ASSERT_TRUE(sbusWatchdogTimeoutCheck(799, 1000, 200));
}

void test_sbus_long_timeout_for_weak_signal() {
    // Some receivers need longer timeout for weak signal areas
    TEST_ASSERT_FALSE(sbusWatchdogTimeoutCheck(400, 1000, 1000));  // 600ms < 1000ms
    TEST_ASSERT_TRUE(sbusWatchdogTimeoutCheck(0, 1500, 1000));     // 1500ms > 1000ms
}

void test_sbus_watchdog_ok_no_timeout() {
    SbusWatchdog watchdog = {};
    TEST_ASSERT_EQUAL_UINT8((uint8_t)SbusWatchdogTransition::OK,
                            (uint8_t)sbusWatchdogCheck(&watchdog, 900, 1000, 200));
    TEST_ASSERT_FALSE(watchdog.signalLost);
}

void test_sbus_watchdog_just_lost_first_timeout() {
    SbusWatchdog watchdog = {};
    TEST_ASSERT_EQUAL_UINT8((uint8_t)SbusWatchdogTransition::JUST_LOST,
                            (uint8_t)sbusWatchdogCheck(&watchdog, 799, 1000, 200));
    TEST_ASSERT_TRUE(watchdog.signalLost);
}

void test_sbus_watchdog_mid_lost_sustained_timeout() {
    SbusWatchdog watchdog = {};
    TEST_ASSERT_EQUAL_UINT8((uint8_t)SbusWatchdogTransition::JUST_LOST,
                            (uint8_t)sbusWatchdogCheck(&watchdog, 799, 1000, 200));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)SbusWatchdogTransition::LOST,
                            (uint8_t)sbusWatchdogCheck(&watchdog, 800, 1100, 200));
    TEST_ASSERT_TRUE(watchdog.signalLost);
}

void test_sbus_watchdog_just_restored_first_recovery() {
    SbusWatchdog watchdog = {};
    TEST_ASSERT_EQUAL_UINT8((uint8_t)SbusWatchdogTransition::JUST_LOST,
                            (uint8_t)sbusWatchdogCheck(&watchdog, 799, 1000, 200));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)SbusWatchdogTransition::JUST_RESTORED,
                            (uint8_t)sbusWatchdogCheck(&watchdog, 1050, 1100, 200));
    TEST_ASSERT_FALSE(watchdog.signalLost);
}

void test_sbus_watchdog_sustained_restored_is_ok() {
    SbusWatchdog watchdog = {};
    TEST_ASSERT_EQUAL_UINT8((uint8_t)SbusWatchdogTransition::JUST_LOST,
                            (uint8_t)sbusWatchdogCheck(&watchdog, 799, 1000, 200));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)SbusWatchdogTransition::JUST_RESTORED,
                            (uint8_t)sbusWatchdogCheck(&watchdog, 1050, 1100, 200));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)SbusWatchdogTransition::OK,
                            (uint8_t)sbusWatchdogCheck(&watchdog, 1150, 1200, 200));
    TEST_ASSERT_FALSE(watchdog.signalLost);
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_sbus_never_received_is_timeout);
    RUN_TEST(test_sbus_recent_frame_is_not_timeout);
    RUN_TEST(test_sbus_at_exact_timeout_is_not_timeout);
    RUN_TEST(test_sbus_just_within_timeout_is_not_timeout);
    RUN_TEST(test_sbus_one_ms_over_timeout_is_timeout);
    RUN_TEST(test_sbus_millis_overflow_handles_correctly);
    RUN_TEST(test_sbus_millis_overflow_timeout_exceeded);
    RUN_TEST(test_sbus_zero_timeout_immediate_timeout);
    RUN_TEST(test_sbus_standard_200ms_timeout);
    RUN_TEST(test_sbus_long_timeout_for_weak_signal);
    RUN_TEST(test_sbus_watchdog_ok_no_timeout);
    RUN_TEST(test_sbus_watchdog_just_lost_first_timeout);
    RUN_TEST(test_sbus_watchdog_mid_lost_sustained_timeout);
    RUN_TEST(test_sbus_watchdog_just_restored_first_recovery);
    RUN_TEST(test_sbus_watchdog_sustained_restored_is_ok);

    return UNITY_END();
}
