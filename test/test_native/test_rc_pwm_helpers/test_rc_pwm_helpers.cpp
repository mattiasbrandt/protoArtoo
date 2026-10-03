#include <unity.h>

#include "rc_pwm_helpers.h"

void test_rc_pwm_valid_minimum() {
    TEST_ASSERT_TRUE(rcPwmPulseIsValid(1000));
}

void test_rc_pwm_invalid_below_range() {
    TEST_ASSERT_FALSE(rcPwmPulseIsValid(800));
}

void test_rc_pwm_normalized_center() {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, rcPwmPulseToNormalized(1500));
}

void test_rc_pwm_normalized_min() {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -1.0f, rcPwmPulseToNormalized(1000));
}

void test_rc_pwm_normalized_max() {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, rcPwmPulseToNormalized(2000));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_rc_pwm_valid_minimum);
    RUN_TEST(test_rc_pwm_invalid_below_range);
    RUN_TEST(test_rc_pwm_normalized_center);
    RUN_TEST(test_rc_pwm_normalized_min);
    RUN_TEST(test_rc_pwm_normalized_max);
    return UNITY_END();
}
