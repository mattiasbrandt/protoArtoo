// =============================================================================
// test/test_native/test_servo_travel/test_servo_travel.cpp
//
// The travel a Part runs through when a builder presses on a body view
// (ADR 0063, #352). What these hold is what makes the motion honest: it goes to
// the two ends the builder RECORDED and nowhere else, it comes back to where it
// started, it keeps the Endpoint Pair's direction rather than sorting it, and a
// pair with no distance between its ends is refused rather than run as a travel
// of nothing.
// =============================================================================
#include <unity.h>

#include "servo_travel.h"

void setUp() {}
void tearDown() {}

void test_the_legs_are_open_across_to_close_and_back() {
    ServoTravelPlan plan = {};
    TEST_ASSERT_TRUE(servoTravelPlan(1500, 2000, 1000, &plan));
    TEST_ASSERT_EQUAL_UINT16(1500, plan.homeUs);
    TEST_ASSERT_EQUAL_UINT16(2000, plan.openUs);
    TEST_ASSERT_EQUAL_UINT16(1000, plan.closeUs);
    // Out, across, and back: the legs in the order ServoTask drives them.
    TEST_ASSERT_EQUAL_UINT16(2000, servoTravelLegTarget(plan, 1));
    TEST_ASSERT_EQUAL_UINT16(1000, servoTravelLegTarget(plan, 2));
    TEST_ASSERT_EQUAL_UINT16(1500, servoTravelLegTarget(plan, 3));
    TEST_ASSERT_EQUAL_UINT16(3, SERVO_TRAVEL_LEG_COUNT);
}

// A reversed linkage is simply open < close (ADR 0041). Nothing here sorts the
// pair, so the first leg is still the end the builder called open and the
// travel reads the same way round on a reversed Output as on any other.
void test_a_reversed_pair_still_opens_first() {
    ServoTravelPlan plan = {};
    TEST_ASSERT_TRUE(servoTravelPlan(1400, 1150, 1850, &plan));
    TEST_ASSERT_EQUAL_UINT16(1150, plan.openUs);
    TEST_ASSERT_EQUAL_UINT16(1850, plan.closeUs);
    TEST_ASSERT_EQUAL_UINT16(1150, servoTravelLegTarget(plan, 1));
    TEST_ASSERT_EQUAL_UINT16(1850, servoTravelLegTarget(plan, 2));
    TEST_ASSERT_EQUAL_UINT16(1400, servoTravelLegTarget(plan, 3));
}

// Home is the width on the pin, not the centre and not either end: the press
// puts the part back exactly where the builder found it.
void test_the_travel_returns_to_where_it_started_not_to_a_middle() {
    ServoTravelPlan plan = {};
    TEST_ASSERT_TRUE(servoTravelPlan(1730, 2000, 1000, &plan));
    TEST_ASSERT_EQUAL_UINT16(1730, plan.homeUs);
    TEST_ASSERT_EQUAL_UINT16(1730, servoTravelLegTarget(plan, 3));
}

// Two ends at the same width is no travel at all - there is nothing for a
// builder to watch, so it is refused rather than run as three legs to one spot.
void test_a_pair_with_no_distance_is_refused() {
    ServoTravelPlan plan = {7, 7, 7};
    TEST_ASSERT_FALSE(servoTravelPlan(1500, 1600, 1600, &plan));
    TEST_ASSERT_EQUAL_UINT16(7, plan.homeUs);
    TEST_ASSERT_EQUAL_UINT16(7, plan.openUs);
    TEST_ASSERT_EQUAL_UINT16(7, plan.closeUs);
    TEST_ASSERT_FALSE(servoTravelPlan(1500, 2000, 1000, nullptr));
}

// Nothing is clamped here and nothing is narrowed: the ends were clamped onto
// the row when they were recorded (ADR 0041) and each leg goes through
// resolveArmPulse() on the way to the pin, so a second rule here would be the
// same rule in two places. An MG90S pair outside the cautious band travels.
void test_the_recorded_ends_are_used_as_recorded() {
    ServoTravelPlan plan = {};
    TEST_ASSERT_TRUE(servoTravelPlan(1500, 2400, 600, &plan));
    TEST_ASSERT_EQUAL_UINT16(2400, servoTravelLegTarget(plan, 1));
    TEST_ASSERT_EQUAL_UINT16(600, servoTravelLegTarget(plan, 2));
}

void test_a_leg_past_the_end_goes_home_never_out() {
    ServoTravelPlan plan = {};
    TEST_ASSERT_TRUE(servoTravelPlan(1500, 2000, 1000, &plan));
    TEST_ASSERT_EQUAL_UINT16(1500, servoTravelLegTarget(plan, 0));
    TEST_ASSERT_EQUAL_UINT16(1500, servoTravelLegTarget(plan, 4));
}

int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_the_legs_are_open_across_to_close_and_back);
    RUN_TEST(test_a_reversed_pair_still_opens_first);
    RUN_TEST(test_the_travel_returns_to_where_it_started_not_to_a_middle);
    RUN_TEST(test_a_pair_with_no_distance_is_refused);
    RUN_TEST(test_the_recorded_ends_are_used_as_recorded);
    RUN_TEST(test_a_leg_past_the_end_goes_home_never_out);
    return UNITY_END();
}
