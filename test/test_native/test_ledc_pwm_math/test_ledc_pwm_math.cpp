// =============================================================================
// test/test_native/test_ledc_pwm_math/test_ledc_pwm_math.cpp
//
// Native unit tests for LEDC PWM pure-math helpers.
// Tests: pulseUsToDuty() conversion accuracy, clampPulseWidth() per channel.
//
// These functions are inline in ledc_pwm.h and have no ESP32 dependencies,
// so they run on the native host without any hardware or framework stubs.
// =============================================================================
#include <unity.h>

#include "ledc_pwm.h"

void setUp() {
}
void tearDown() {
}

// --- pulseUsToDuty -----------------------------------------------------------

void test_neutral_1500us_maps_to_correct_duty() {
    // 1500 / 20000 * 65535 = 4915.125 → truncates to 4915
    TEST_ASSERT_EQUAL_UINT32(4915, pulseUsToDuty(1500));
}

void test_min_servo_500us_maps_to_correct_duty() {
    // 500 / 20000 * 65535 = 1638.375 → truncates to 1638
    TEST_ASSERT_EQUAL_UINT32(1638, pulseUsToDuty(500));
}

void test_max_servo_2500us_maps_to_correct_duty() {
    // 2500 / 20000 * 65535 = 8191.875 → truncates to 8191
    TEST_ASSERT_EQUAL_UINT32(8191, pulseUsToDuty(2500));
}

void test_esc_min_1000us_maps_to_correct_duty() {
    // 1000 / 20000 * 65535 = 3276.75 → truncates to 3276
    TEST_ASSERT_EQUAL_UINT32(3276, pulseUsToDuty(1000));
}

void test_esc_max_2000us_maps_to_correct_duty() {
    // 2000 / 20000 * 65535 = 6553.5 → truncates to 6553
    TEST_ASSERT_EQUAL_UINT32(6553, pulseUsToDuty(2000));
}

void test_zero_pulse_gives_zero_duty() {
    TEST_ASSERT_EQUAL_UINT32(0, pulseUsToDuty(0));
}

void test_full_period_20000us_gives_max_duty() {
    // 20000 / 20000 * 65535 = 65535
    TEST_ASSERT_EQUAL_UINT32(65535, pulseUsToDuty(20000));
}

void test_duty_monotonically_increases() {
    // Verify ordering: 500 < 1000 < 1500 < 2000 < 2500
    TEST_ASSERT_TRUE(pulseUsToDuty(500) < pulseUsToDuty(1000));
    TEST_ASSERT_TRUE(pulseUsToDuty(1000) < pulseUsToDuty(1500));
    TEST_ASSERT_TRUE(pulseUsToDuty(1500) < pulseUsToDuty(2000));
    TEST_ASSERT_TRUE(pulseUsToDuty(2000) < pulseUsToDuty(2500));
}

void test_precision_adjacent_microseconds_differ() {
    // At 16-bit / 50Hz: 1 LSB = 20000/65535 ≈ 0.305µs.
    // Adjacent integer µs values must produce different duty counts.
    TEST_ASSERT_NOT_EQUAL(pulseUsToDuty(1499), pulseUsToDuty(1500));
    TEST_ASSERT_NOT_EQUAL(pulseUsToDuty(1500), pulseUsToDuty(1501));
}

// --- clampPulseWidth — servo channels (ARM1, ARM2, AUX1-3) ------------------

void test_servo_channel_in_range_passes_through() {
    TEST_ASSERT_EQUAL_UINT16(1500, clampPulseWidth(LEDC_CH_ARM1, 1500));
    TEST_ASSERT_EQUAL_UINT16(1500, clampPulseWidth(LEDC_CH_ARM2, 1500));
    TEST_ASSERT_EQUAL_UINT16(1500, clampPulseWidth(LEDC_CH_AUX1, 1500));
}

void test_servo_channel_below_min_clamps_to_500() {
    TEST_ASSERT_EQUAL_UINT16(500, clampPulseWidth(LEDC_CH_ARM1, 0));
    TEST_ASSERT_EQUAL_UINT16(500, clampPulseWidth(LEDC_CH_ARM1, 499));
}

void test_servo_channel_above_max_clamps_to_2500() {
    TEST_ASSERT_EQUAL_UINT16(2500, clampPulseWidth(LEDC_CH_ARM1, 2501));
    TEST_ASSERT_EQUAL_UINT16(2500, clampPulseWidth(LEDC_CH_ARM1, 65535));
}

void test_servo_channel_at_boundaries_passes_through() {
    TEST_ASSERT_EQUAL_UINT16(500, clampPulseWidth(LEDC_CH_ARM1, 500));
    TEST_ASSERT_EQUAL_UINT16(2500, clampPulseWidth(LEDC_CH_ARM1, 2500));
}

// --- clampPulseWidth — ESC channel (DOME) ------------------------------------

void test_esc_channel_in_range_passes_through() {
    TEST_ASSERT_EQUAL_UINT16(1500, clampPulseWidth(LEDC_CH_DOME, 1500));
    TEST_ASSERT_EQUAL_UINT16(1200, clampPulseWidth(LEDC_CH_DOME, 1200));
}

void test_esc_channel_below_min_clamps_to_1000() {
    TEST_ASSERT_EQUAL_UINT16(1000, clampPulseWidth(LEDC_CH_DOME, 0));
    TEST_ASSERT_EQUAL_UINT16(1000, clampPulseWidth(LEDC_CH_DOME, 999));
}

void test_esc_channel_above_max_clamps_to_2000() {
    TEST_ASSERT_EQUAL_UINT16(2000, clampPulseWidth(LEDC_CH_DOME, 2001));
    TEST_ASSERT_EQUAL_UINT16(2000, clampPulseWidth(LEDC_CH_DOME, 65535));
}

void test_esc_channel_at_boundaries_passes_through() {
    TEST_ASSERT_EQUAL_UINT16(1000, clampPulseWidth(LEDC_CH_DOME, 1000));
    TEST_ASSERT_EQUAL_UINT16(2000, clampPulseWidth(LEDC_CH_DOME, 2000));
}

void test_esc_rejects_servo_max_2500() {
    // ESC max is 2000 — 2500 must be clamped down
    TEST_ASSERT_EQUAL_UINT16(2000, clampPulseWidth(LEDC_CH_DOME, 2500));
}

void test_esc_rejects_servo_min_500() {
    // ESC min is 1000 — 500 must be clamped up
    TEST_ASSERT_EQUAL_UINT16(1000, clampPulseWidth(LEDC_CH_DOME, 500));
}

// --- clampPulseWidth — invalid channel ---------------------------------------

void test_invalid_channel_returns_neutral() {
    TEST_ASSERT_EQUAL_UINT16(SERVO_PULSE_NEUTRAL_US, clampPulseWidth(LEDC_CH_MAX, 1000));
    TEST_ASSERT_EQUAL_UINT16(SERVO_PULSE_NEUTRAL_US, clampPulseWidth(255, 2500));
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_neutral_1500us_maps_to_correct_duty);
    RUN_TEST(test_min_servo_500us_maps_to_correct_duty);
    RUN_TEST(test_max_servo_2500us_maps_to_correct_duty);
    RUN_TEST(test_esc_min_1000us_maps_to_correct_duty);
    RUN_TEST(test_esc_max_2000us_maps_to_correct_duty);
    RUN_TEST(test_zero_pulse_gives_zero_duty);
    RUN_TEST(test_full_period_20000us_gives_max_duty);
    RUN_TEST(test_duty_monotonically_increases);
    RUN_TEST(test_precision_adjacent_microseconds_differ);

    RUN_TEST(test_servo_channel_in_range_passes_through);
    RUN_TEST(test_servo_channel_below_min_clamps_to_500);
    RUN_TEST(test_servo_channel_above_max_clamps_to_2500);
    RUN_TEST(test_servo_channel_at_boundaries_passes_through);

    RUN_TEST(test_esc_channel_in_range_passes_through);
    RUN_TEST(test_esc_channel_below_min_clamps_to_1000);
    RUN_TEST(test_esc_channel_above_max_clamps_to_2000);
    RUN_TEST(test_esc_channel_at_boundaries_passes_through);
    RUN_TEST(test_esc_rejects_servo_max_2500);
    RUN_TEST(test_esc_rejects_servo_min_500);

    RUN_TEST(test_invalid_channel_returns_neutral);

    return UNITY_END();
}
