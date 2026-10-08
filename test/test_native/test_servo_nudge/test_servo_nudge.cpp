// =============================================================================
// test/test_native/test_servo_nudge/test_servo_nudge.cpp
//
// The pair a Find by Moving nudge visits (ADR 0050, #363). What these hold is
// what keeps the nudge safe on an output nobody has measured: it is small, it
// is symmetric about where the output already is, it stays inside the cautious
// band by moving the whole pair rather than clipping one side, and a start
// outside that band is not nudged at all.
// =============================================================================
#include <unity.h>

#include "servo_nudge.h"

void setUp() {}
void tearDown() {}

void test_the_amplitude_is_a_tenth_of_the_cautious_band() {
    TEST_ASSERT_EQUAL_UINT16(100, SERVO_NUDGE_AMPLITUDE_US);
}

void test_a_pair_about_the_middle_is_symmetric_and_returns_home() {
    ServoNudgePlan plan = {};
    TEST_ASSERT_TRUE(servoNudgePlan(1500, SERVO_BAND_STD, SERVO_NUDGE_AMPLITUDE_US, &plan));
    TEST_ASSERT_EQUAL_UINT16(1500, plan.homeUs);
    TEST_ASSERT_EQUAL_UINT16(1400, plan.loUs);
    TEST_ASSERT_EQUAL_UINT16(1600, plan.hiUs);
    // Out, across, and back: the legs in the order ServoTask drives them.
    TEST_ASSERT_EQUAL_UINT16(1600, servoNudgeLegTarget(plan, 1));
    TEST_ASSERT_EQUAL_UINT16(1400, servoNudgeLegTarget(plan, 2));
    TEST_ASSERT_EQUAL_UINT16(1500, servoNudgeLegTarget(plan, 3));
    TEST_ASSERT_EQUAL_UINT16(3, SERVO_NUDGE_LEG_COUNT);
}

void test_a_pair_near_the_top_is_shifted_down_as_one_never_clipped() {
    ServoNudgePlan plan = {};
    TEST_ASSERT_TRUE(servoNudgePlan(1950, SERVO_BAND_STD, SERVO_NUDGE_AMPLITUDE_US, &plan));
    TEST_ASSERT_EQUAL_UINT16(1950, plan.homeUs);
    TEST_ASSERT_EQUAL_UINT16(2000, plan.hiUs);
    TEST_ASSERT_EQUAL_UINT16(1800, plan.loUs);
    TEST_ASSERT_EQUAL_UINT16(2 * SERVO_NUDGE_AMPLITUDE_US, plan.hiUs - plan.loUs);
}

void test_a_pair_near_the_bottom_is_shifted_up_as_one() {
    ServoNudgePlan plan = {};
    TEST_ASSERT_TRUE(servoNudgePlan(1020, SERVO_BAND_STD, SERVO_NUDGE_AMPLITUDE_US, &plan));
    TEST_ASSERT_EQUAL_UINT16(1000, plan.loUs);
    TEST_ASSERT_EQUAL_UINT16(1200, plan.hiUs);
    TEST_ASSERT_EQUAL_UINT16(1020, plan.homeUs);
}

void test_the_ends_of_the_band_are_still_nudged() {
    ServoNudgePlan plan = {};
    TEST_ASSERT_TRUE(servoNudgePlan(1000, SERVO_BAND_STD, SERVO_NUDGE_AMPLITUDE_US, &plan));
    TEST_ASSERT_EQUAL_UINT16(1000, plan.loUs);
    TEST_ASSERT_EQUAL_UINT16(1200, plan.hiUs);
    TEST_ASSERT_TRUE(servoNudgePlan(2000, SERVO_BAND_STD, SERVO_NUDGE_AMPLITUDE_US, &plan));
    TEST_ASSERT_EQUAL_UINT16(1800, plan.loUs);
    TEST_ASSERT_EQUAL_UINT16(2000, plan.hiUs);
}

// An MG90S output driven to 2400 us by hand: shifting the pair in from there
// would be a 600 us move, which is the jump ADR 0050 refuses, so it is refused.
void test_a_start_outside_the_cautious_band_is_not_nudged() {
    ServoNudgePlan plan = {7, 7, 7};
    TEST_ASSERT_FALSE(servoNudgePlan(2400, SERVO_BAND_STD, SERVO_NUDGE_AMPLITUDE_US, &plan));
    TEST_ASSERT_FALSE(servoNudgePlan(999, SERVO_BAND_STD, SERVO_NUDGE_AMPLITUDE_US, &plan));
    TEST_ASSERT_EQUAL_UINT16(7, plan.homeUs);
    TEST_ASSERT_EQUAL_UINT16(7, plan.loUs);
    TEST_ASSERT_EQUAL_UINT16(7, plan.hiUs);
}

void test_a_band_too_narrow_for_the_pair_is_not_nudged() {
    ServoNudgePlan plan = {};
    const ServoPulseBand narrow = {1500, 1650};
    TEST_ASSERT_FALSE(servoNudgePlan(1580, narrow, SERVO_NUDGE_AMPLITUDE_US, &plan));
    TEST_ASSERT_FALSE(servoNudgePlan(1500, SERVO_BAND_STD, SERVO_NUDGE_AMPLITUDE_US, nullptr));
}

void test_a_leg_past_the_end_goes_home_never_out() {
    ServoNudgePlan plan = {};
    TEST_ASSERT_TRUE(servoNudgePlan(1500, SERVO_BAND_STD, SERVO_NUDGE_AMPLITUDE_US, &plan));
    TEST_ASSERT_EQUAL_UINT16(1500, servoNudgeLegTarget(plan, 0));
    TEST_ASSERT_EQUAL_UINT16(1500, servoNudgeLegTarget(plan, 4));
}

int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_the_amplitude_is_a_tenth_of_the_cautious_band);
    RUN_TEST(test_a_pair_about_the_middle_is_symmetric_and_returns_home);
    RUN_TEST(test_a_pair_near_the_top_is_shifted_down_as_one_never_clipped);
    RUN_TEST(test_a_pair_near_the_bottom_is_shifted_up_as_one);
    RUN_TEST(test_the_ends_of_the_band_are_still_nudged);
    RUN_TEST(test_a_start_outside_the_cautious_band_is_not_nudged);
    RUN_TEST(test_a_band_too_narrow_for_the_pair_is_not_nudged);
    RUN_TEST(test_a_leg_past_the_end_goes_home_never_out);
    return UNITY_END();
}
