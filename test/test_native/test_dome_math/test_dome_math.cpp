// =============================================================================
// test/test_native/test_dome_math/test_dome_math.cpp
//
// Native unit tests for dome ESC pulse mapping (domeSpeedToPulseUs).
// Covers: neutral, full forward/reverse, speed limit, asymmetric neutral trim,
// and clamping at boundaries.
// =============================================================================
#include <unity.h>

#include "dome_math.h"

void setUp() {
}
void tearDown() {
}

void test_neutral_speed_gives_neutral_pulse() {
    TEST_ASSERT_EQUAL_UINT16(1500, domeSpeedToPulseUs(0.0f, 1500, 1000, 2000, 100));
}

void test_full_forward_gives_max_pulse() {
    TEST_ASSERT_EQUAL_UINT16(2000, domeSpeedToPulseUs(1.0f, 1500, 1000, 2000, 100));
}

void test_full_reverse_gives_min_pulse() {
    TEST_ASSERT_EQUAL_UINT16(1000, domeSpeedToPulseUs(-1.0f, 1500, 1000, 2000, 100));
}

void test_half_forward_gives_midpoint() {
    TEST_ASSERT_EQUAL_UINT16(1750, domeSpeedToPulseUs(0.5f, 1500, 1000, 2000, 100));
}

void test_half_reverse_gives_midpoint() {
    TEST_ASSERT_EQUAL_UINT16(1250, domeSpeedToPulseUs(-0.5f, 1500, 1000, 2000, 100));
}

void test_speed_limit_50pct_halves_forward_range() {
    uint16_t pulse = domeSpeedToPulseUs(1.0f, 1500, 1000, 2000, 50);
    TEST_ASSERT_EQUAL_UINT16(1750, pulse);
}

void test_speed_limit_50pct_halves_reverse_range() {
    uint16_t pulse = domeSpeedToPulseUs(-1.0f, 1500, 1000, 2000, 50);
    TEST_ASSERT_EQUAL_UINT16(1250, pulse);
}

void test_speed_limit_0pct_gives_neutral() {
    TEST_ASSERT_EQUAL_UINT16(1500, domeSpeedToPulseUs(1.0f, 1500, 1000, 2000, 0));
    TEST_ASSERT_EQUAL_UINT16(1500, domeSpeedToPulseUs(-1.0f, 1500, 1000, 2000, 0));
}

void test_speed_clamped_above_1() {
    TEST_ASSERT_EQUAL_UINT16(2000, domeSpeedToPulseUs(2.0f, 1500, 1000, 2000, 100));
}

void test_speed_clamped_below_minus_1() {
    TEST_ASSERT_EQUAL_UINT16(1000, domeSpeedToPulseUs(-2.0f, 1500, 1000, 2000, 100));
}

void test_asymmetric_neutral_forward_range() {
    uint16_t pulse = domeSpeedToPulseUs(1.0f, 1600, 1000, 2000, 100);
    TEST_ASSERT_EQUAL_UINT16(2000, pulse);
}

void test_asymmetric_neutral_reverse_range() {
    uint16_t pulse = domeSpeedToPulseUs(-1.0f, 1600, 1000, 2000, 100);
    TEST_ASSERT_EQUAL_UINT16(1000, pulse);
}

void test_asymmetric_neutral_half_forward() {
    uint16_t pulse = domeSpeedToPulseUs(0.5f, 1600, 1000, 2000, 100);
    TEST_ASSERT_EQUAL_UINT16(1800, pulse);
}

void test_asymmetric_neutral_half_reverse() {
    uint16_t pulse = domeSpeedToPulseUs(-0.5f, 1600, 1000, 2000, 100);
    TEST_ASSERT_EQUAL_UINT16(1300, pulse);
}

// A set out of order has no range to map into, and clamping into it anyway made
// speed 0 drive the ESC (min 1800, neutral 1500, max 1200 put out 1200 us for
// every speed, #417). Whatever is asked, it answers neutral - inverted, and
// with neutral outside an otherwise ordered pair.
void test_an_out_of_order_set_answers_neutral_for_every_speed() {
    const float speeds[] = {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f};
    for (float speed : speeds) {
        TEST_ASSERT_EQUAL_UINT16(1500, domeSpeedToPulseUs(speed, 1500, 1800, 1200, 100));
        TEST_ASSERT_EQUAL_UINT16(1500, domeSpeedToPulseUs(speed, 1500, 1000, 1400, 100));
        TEST_ASSERT_EQUAL_UINT16(1500, domeSpeedToPulseUs(speed, 1500, 1600, 2000, 100));
    }
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_neutral_speed_gives_neutral_pulse);
    RUN_TEST(test_full_forward_gives_max_pulse);
    RUN_TEST(test_full_reverse_gives_min_pulse);
    RUN_TEST(test_half_forward_gives_midpoint);
    RUN_TEST(test_half_reverse_gives_midpoint);
    RUN_TEST(test_speed_limit_50pct_halves_forward_range);
    RUN_TEST(test_speed_limit_50pct_halves_reverse_range);
    RUN_TEST(test_speed_limit_0pct_gives_neutral);
    RUN_TEST(test_speed_clamped_above_1);
    RUN_TEST(test_speed_clamped_below_minus_1);
    RUN_TEST(test_asymmetric_neutral_forward_range);
    RUN_TEST(test_asymmetric_neutral_reverse_range);
    RUN_TEST(test_asymmetric_neutral_half_forward);
    RUN_TEST(test_asymmetric_neutral_half_reverse);
    RUN_TEST(test_an_out_of_order_set_answers_neutral_for_every_speed);
    return UNITY_END();
}
