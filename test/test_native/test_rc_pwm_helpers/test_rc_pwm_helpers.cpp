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

void test_rc_pwm_drive_maps_to_max() {
    TEST_ASSERT_EQUAL_INT16(600, rcPwmPulseToDrive(2000, 600));
}

void test_rc_pwm_drive_maps_to_min() {
    TEST_ASSERT_EQUAL_INT16(-600, rcPwmPulseToDrive(1000, 600));
}

void test_rc_pwm_switch_low() {
    TEST_ASSERT_EQUAL_UINT8(RC_PWM_SWITCH_LOW, rcPwmPulseToSwitchState(1100));
}

void test_rc_pwm_switch_mid() {
    TEST_ASSERT_EQUAL_UINT8(RC_PWM_SWITCH_MID, rcPwmPulseToSwitchState(1500));
}

void test_rc_pwm_switch_high() {
    TEST_ASSERT_EQUAL_UINT8(RC_PWM_SWITCH_HIGH, rcPwmPulseToSwitchState(1900));
}

void test_rc_pwm_switch_invalid() {
    TEST_ASSERT_EQUAL_UINT8(RC_PWM_SWITCH_INVALID, rcPwmPulseToSwitchState(0));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_rc_pwm_valid_minimum);
    RUN_TEST(test_rc_pwm_invalid_below_range);
    RUN_TEST(test_rc_pwm_normalized_center);
    RUN_TEST(test_rc_pwm_normalized_min);
    RUN_TEST(test_rc_pwm_normalized_max);
    RUN_TEST(test_rc_pwm_drive_maps_to_max);
    RUN_TEST(test_rc_pwm_drive_maps_to_min);
    RUN_TEST(test_rc_pwm_switch_low);
    RUN_TEST(test_rc_pwm_switch_mid);
    RUN_TEST(test_rc_pwm_switch_high);
    RUN_TEST(test_rc_pwm_switch_invalid);
    return UNITY_END();
}
