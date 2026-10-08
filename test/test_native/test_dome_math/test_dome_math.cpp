// =============================================================================
// test/test_native/test_dome_math/test_dome_math.cpp
//
// Native unit tests for dome ESC pulse mapping (domeSpeedToPulseUs) and the
// random-move pause per Mood (domeRndPauseMsForMood).
// Covers: neutral, full forward/reverse, speed limit, asymmetric neutral trim,
// clamping at boundaries; each Mood's pause window, unset Mood 0, the 1 s
// floor, and a window with pauseMax <= pauseMin.
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

// --- Random-move pause per Mood (#452) ---------------------------------------
// randomValue 0 lands on the window's low end, a value one short of the range
// on its high end, so each case pins both ends of the scaled window.

void test_full_awake_and_unset_mood_draw_from_the_stored_window() {
    TEST_ASSERT_EQUAL_UINT32(6000, domeRndPauseMsForMood(6, 12, 11, 0));
    TEST_ASSERT_EQUAL_UINT32(11999, domeRndPauseMsForMood(6, 12, 11, 5999));
    TEST_ASSERT_EQUAL_UINT32(6000, domeRndPauseMsForMood(6, 12, 0, 0));
    TEST_ASSERT_EQUAL_UINT32(11999, domeRndPauseMsForMood(6, 12, 0, 5999));
}

void test_mid_awake_draws_from_one_and_a_half_times_the_window() {
    TEST_ASSERT_EQUAL_UINT32(9000, domeRndPauseMsForMood(6, 12, 13, 0));
    TEST_ASSERT_EQUAL_UINT32(17999, domeRndPauseMsForMood(6, 12, 13, 8999));
    // The widest stored window still fits: 120 s x 1.5.
    TEST_ASSERT_EQUAL_UINT32(179999, domeRndPauseMsForMood(1, 120, 13, 178499));
}

void test_awake_plus_draws_from_half_the_window_in_ms() {
    TEST_ASSERT_EQUAL_UINT32(3000, domeRndPauseMsForMood(6, 12, 14, 0));
    TEST_ASSERT_EQUAL_UINT32(5999, domeRndPauseMsForMood(6, 12, 14, 2999));
    // Halved in ms, not in whole seconds: 7 s is 3500 ms.
    TEST_ASSERT_EQUAL_UINT32(3500, domeRndPauseMsForMood(7, 9, 14, 0));
}

void test_awake_plus_never_pauses_under_one_second() {
    // 1-2 s halves to 0.5-1 s; both ends floor to 1 s.
    TEST_ASSERT_EQUAL_UINT32(1000, domeRndPauseMsForMood(1, 2, 14, 0));
    TEST_ASSERT_EQUAL_UINT32(1000, domeRndPauseMsForMood(1, 2, 14, 12345));
    // 1-4 s: the low end floors, the high end (2 s) does not.
    TEST_ASSERT_EQUAL_UINT32(1000, domeRndPauseMsForMood(1, 4, 14, 0));
    TEST_ASSERT_EQUAL_UINT32(1999, domeRndPauseMsForMood(1, 4, 14, 999));
}

void test_quiet_starts_no_move() {
    TEST_ASSERT_FALSE(domeRndMoodStartsMoves(10));
    TEST_ASSERT_TRUE(domeRndMoodStartsMoves(0));
    TEST_ASSERT_TRUE(domeRndMoodStartsMoves(11));
    TEST_ASSERT_TRUE(domeRndMoodStartsMoves(13));
    TEST_ASSERT_TRUE(domeRndMoodStartsMoves(14));
    TEST_ASSERT_EQUAL_UINT32(0, domeRndPauseMsForMood(6, 12, 10, 1234));
}

void test_a_window_with_max_not_above_min_gives_the_scaled_min() {
    TEST_ASSERT_EQUAL_UINT32(8000, domeRndPauseMsForMood(8, 8, 11, 777));
    TEST_ASSERT_EQUAL_UINT32(8000, domeRndPauseMsForMood(8, 3, 11, 777));
    TEST_ASSERT_EQUAL_UINT32(12000, domeRndPauseMsForMood(8, 3, 13, 777));
    TEST_ASSERT_EQUAL_UINT32(4000, domeRndPauseMsForMood(8, 3, 14, 777));
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
    RUN_TEST(test_full_awake_and_unset_mood_draw_from_the_stored_window);
    RUN_TEST(test_mid_awake_draws_from_one_and_a_half_times_the_window);
    RUN_TEST(test_awake_plus_draws_from_half_the_window_in_ms);
    RUN_TEST(test_awake_plus_never_pauses_under_one_second);
    RUN_TEST(test_quiet_starts_no_move);
    RUN_TEST(test_a_window_with_max_not_above_min_gives_the_scaled_min);
    return UNITY_END();
}
