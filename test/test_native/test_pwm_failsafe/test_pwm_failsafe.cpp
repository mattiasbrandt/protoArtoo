// =============================================================================
// test/test_native/test_pwm_failsafe/test_pwm_failsafe.cpp
//
// Native unit tests for PWM failsafe detection logic.
// Tests: pwmSignalLostCheck() for timeout detection and edge cases.
//
// Safety relevance: PWM signal loss must be detected to prevent latched
// drive commands when RC transmitter disconnects or goes out of range.
// =============================================================================
#include <unity.h>

#include "rc_pwm_helpers.h"

void setUp() {
}
void tearDown() {
}

// --- pwmSignalLostCheck() tests ---

void test_pwm_never_received_is_lost() {
    // lastPwmMs = 0 means no valid PWM ever received
    TEST_ASSERT_TRUE(pwmSignalLostCheck(0, 1000, 200));
}

void test_pwm_recent_is_not_lost() {
    // Last PWM 50ms ago, timeout 200ms - should not be lost
    TEST_ASSERT_FALSE(pwmSignalLostCheck(950, 1000, 200));
}

void test_pwm_at_exact_timeout_is_not_lost() {
    // Last PWM 200ms ago, timeout 200ms - not lost yet (> not >=)
    // The check is (current - last) > timeout, so 200 is NOT > 200
    TEST_ASSERT_FALSE(pwmSignalLostCheck(800, 1000, 200));
}

void test_pwm_just_within_timeout_is_not_lost() {
    // Last PWM 199ms ago, timeout 200ms - should not be lost
    TEST_ASSERT_FALSE(pwmSignalLostCheck(801, 1000, 200));
}

void test_pwm_one_ms_over_timeout_is_lost() {
    // Last PWM 201ms ago, timeout 200ms - should be lost
    TEST_ASSERT_TRUE(pwmSignalLostCheck(799, 1000, 200));
}

void test_pwm_millis_overflow_handles_correctly() {
    // Test millis() overflow scenario: lastPwmMs near UINT32_MAX, currentMs wrapped around
    // Unsigned subtraction should still give correct result
    uint32_t lastPwmMs = 0xFFFFFFFF - 50;  // 50ms before overflow
    uint32_t currentMs = 100;              // Wrapped around to 100
    // Time elapsed = 100 - (0xFFFFFFFF - 50) = 150 (with unsigned overflow)
    TEST_ASSERT_FALSE(pwmSignalLostCheck(lastPwmMs, currentMs, 200));
}

void test_pwm_millis_overflow_timeout_exceeded() {
    // Overflow scenario where timeout is exceeded
    uint32_t lastPwmMs = 0xFFFFFFFF - 250;  // 250ms before overflow
    uint32_t currentMs = 100;               // Wrapped around to 100
    // Time elapsed = 350ms > 200ms timeout
    TEST_ASSERT_TRUE(pwmSignalLostCheck(lastPwmMs, currentMs, 200));
}

void test_pwm_zero_timeout_immediate_loss() {
    // With 0 timeout, any non-zero age should trigger loss
    // lastPwmMs == 0 case is handled separately as "never received"
    TEST_ASSERT_TRUE(pwmSignalLostCheck(999, 1000, 0));
}

void test_pwm_very_long_timeout_no_loss() {
    // Very long timeout (2 hours) should not trigger with 1 hour delta
    // lastPwmMs=1 (received), current=3600001, timeout=7200000
    // (3600001 - 1) = 3600000 is NOT > 7200000, so no loss
    TEST_ASSERT_FALSE(pwmSignalLostCheck(1, 3600001, 7200000));
}

void test_pwm_very_old_signal_is_lost() {
    // Signal from 5 minutes ago should be lost with normal 200ms timeout
    TEST_ASSERT_TRUE(pwmSignalLostCheck(0, 300000, 200));
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_pwm_never_received_is_lost);
    RUN_TEST(test_pwm_recent_is_not_lost);
    RUN_TEST(test_pwm_at_exact_timeout_is_not_lost);
    RUN_TEST(test_pwm_just_within_timeout_is_not_lost);
    RUN_TEST(test_pwm_one_ms_over_timeout_is_lost);
    RUN_TEST(test_pwm_millis_overflow_handles_correctly);
    RUN_TEST(test_pwm_millis_overflow_timeout_exceeded);
    RUN_TEST(test_pwm_zero_timeout_immediate_loss);
    RUN_TEST(test_pwm_very_long_timeout_no_loss);
    RUN_TEST(test_pwm_very_old_signal_is_lost);

    return UNITY_END();
}
