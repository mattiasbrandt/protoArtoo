// =============================================================================
// test/test_native/test_sbus_channel_map/test_sbus_channel_map.cpp
//
// Native unit tests for SBUS channel mapping math.
// Tests: CH8 scale mapping, speed/steer mapping, edge values.
// =============================================================================
#include <unity.h>
#include "sbus_math.h"

void setUp() {}
void tearDown() {}

// --- mapSbusToScale() tests ---

void test_scale_at_minimum() {
    float scale = mapSbusToScale(SBUS_MIN);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, scale);
}

void test_scale_at_maximum() {
    float scale = mapSbusToScale(SBUS_MAX);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, scale);
}

void test_scale_at_center() {
    // Center ~992 should map to ~0.5
    int16_t center = (int16_t)((SBUS_MIN + SBUS_MAX) / 2);
    float scale = mapSbusToScale(center);
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.5f, scale);
}

void test_scale_clamps_below_min() {
    float scale = mapSbusToScale((int16_t)(SBUS_MIN - 100));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, scale);
}

void test_scale_clamps_above_max() {
    float scale = mapSbusToScale((int16_t)(SBUS_MAX + 100));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, scale);
}

// --- mapSbusToSpeed() tests ---

void test_speed_at_minimum() {
    int16_t speed = mapSbusToSpeed(SBUS_MIN);
    TEST_ASSERT_EQUAL_INT16(-1000, speed);
}

void test_speed_at_maximum() {
    int16_t speed = mapSbusToSpeed(SBUS_MAX);
    TEST_ASSERT_EQUAL_INT16(1000, speed);
}

void test_speed_at_center() {
    int16_t center = (int16_t)((SBUS_MIN + SBUS_MAX) / 2);
    int16_t speed = mapSbusToSpeed(center);
    // Center should map near 0 (within ±5 due to integer rounding)
    TEST_ASSERT_INT16_WITHIN(5, 0, speed);
}

void test_speed_clamps_below_min() {
    int16_t speed = mapSbusToSpeed((int16_t)(SBUS_MIN - 100));
    TEST_ASSERT_EQUAL_INT16(-1000, speed);
}

void test_speed_clamps_above_max() {
    int16_t speed = mapSbusToSpeed((int16_t)(SBUS_MAX + 100));
    TEST_ASSERT_EQUAL_INT16(1000, speed);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_scale_at_minimum);
    RUN_TEST(test_scale_at_maximum);
    RUN_TEST(test_scale_at_center);
    RUN_TEST(test_scale_clamps_below_min);
    RUN_TEST(test_scale_clamps_above_max);
    RUN_TEST(test_speed_at_minimum);
    RUN_TEST(test_speed_at_maximum);
    RUN_TEST(test_speed_at_center);
    RUN_TEST(test_speed_clamps_below_min);
    RUN_TEST(test_speed_clamps_above_max);
    return UNITY_END();
}
