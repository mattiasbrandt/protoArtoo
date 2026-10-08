// =============================================================================
// test/test_native/test_servo_run/test_servo_run.cpp
//
// A Find by Moving run's hold on a free Servo Output (#411; ADR 0050, ADR 0064).
//
// What a run may take is the part a page cannot be trusted with: a light's
// wire, an Output a Part is on and an Output ServoTask already drives are never
// taken, whatever asks. And a run's hold is the dial's, bounded the same way:
// nudges that stop arriving end it within seconds, and nudges that keep
// arriving still end at the ceiling - asserted here through the rule a run
// uses, not by re-reading the dial's own tests.
// =============================================================================
#include <unity.h>

#include "config.h"  // SERVO_HOLD_EXPIRY_MS, SERVO_HOLD_CEILING_MS
#include "servo_nudge.h"  // servoNudgePlan() - what the first width must be plannable by
#include "servo_run.h"

void setUp() {}
void tearDown() {}

namespace {
// A free servo Output on a board whose LEDC came up.
ServoRunTakeInputs freeOutput() {
    ServoRunTakeInputs in = {};
    in.backendReady = true;
    return in;
}

// One nudge arriving on a run's Output, as ServoTask takes it: a nudge on a
// free Output is a press, so it takes the Output or refreshes a hold standing.
ServoHoldOutcome nudge(ServoHoldState* hold, uint32_t nowMs) {
    return servoHoldCommand(hold, nowMs, SERVO_HOLD_ASK_TAKE);
}

ServoHoldBound boundAt(const ServoHoldState& hold, uint32_t nowMs) {
    return servoHoldBoundHit(hold, nowMs, SERVO_HOLD_EXPIRY_MS, SERVO_HOLD_CEILING_MS);
}
}  // namespace

void test_a_run_takes_only_a_free_servo_output() {
    TEST_ASSERT_TRUE(servoRunMayTake(freeOutput()));

    ServoRunTakeInputs driven = freeOutput();
    driven.drivenNow = true;
    TEST_ASSERT_FALSE_MESSAGE(servoRunMayTake(driven), "an Output ServoTask drives is someone's");

    ServoRunTakeInputs parted = freeOutput();
    parted.partCount = 1;
    TEST_ASSERT_FALSE_MESSAGE(servoRunMayTake(parted), "an Output a Part is on moves through its Part");

    ServoRunTakeInputs waiting = freeOutput();
    waiting.wiredAtStart = true;
    TEST_ASSERT_FALSE_MESSAGE(servoRunMayTake(waiting), "an Output wired at start is not free");

    ServoRunTakeInputs litThen = freeOutput();
    litThen.litAtStart = true;
    TEST_ASSERT_FALSE_MESSAGE(servoRunMayTake(litThen), "a strip may still be on the pin");

    ServoRunTakeInputs lightNow = freeOutput();
    lightNow.lightNow = true;
    TEST_ASSERT_FALSE_MESSAGE(servoRunMayTake(lightNow), "a light's wire is never a servo's");

    ServoRunTakeInputs noTimer = freeOutput();
    noTimer.backendReady = false;
    TEST_ASSERT_FALSE_MESSAGE(servoRunMayTake(noTimer), "no channel can be attached");
}

void test_a_runs_output_goes_limp_within_seconds_of_the_last_nudge() {
    ServoHoldState hold = {};
    TEST_ASSERT_EQUAL_UINT8(SERVO_HOLD_TAKEN, nudge(&hold, 1000));
    TEST_ASSERT_EQUAL_UINT8(SERVO_HOLD_BOUND_NONE, boundAt(hold, 1000 + SERVO_HOLD_EXPIRY_MS - 1));
    TEST_ASSERT_EQUAL_UINT8(SERVO_HOLD_BOUND_EXPIRY, boundAt(hold, 1000 + SERVO_HOLD_EXPIRY_MS));
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(SERVO_LIMP_OFF,
                                    servoRunLimpReason(true, boundAt(hold, 1000 + SERVO_HOLD_EXPIRY_MS)),
                                    "a run's Output is free again, not a dial's that stopped asking");
}

void test_nudges_that_keep_arriving_still_end_the_hold_at_the_ceiling() {
    ServoHoldState hold = {};
    TEST_ASSERT_EQUAL_UINT8(SERVO_HOLD_TAKEN, nudge(&hold, 0));
    uint32_t now = 0;
    while (now + SERVO_HOLD_EXPIRY_MS / 2 < SERVO_HOLD_CEILING_MS) {
        now += SERVO_HOLD_EXPIRY_MS / 2;
        TEST_ASSERT_EQUAL_UINT8(SERVO_HOLD_REFRESHED, nudge(&hold, now));
        TEST_ASSERT_EQUAL_UINT8(SERVO_HOLD_BOUND_NONE, boundAt(hold, now));
    }
    TEST_ASSERT_EQUAL_UINT8(SERVO_HOLD_BOUND_CEILING, boundAt(hold, SERVO_HOLD_CEILING_MS));
    TEST_ASSERT_EQUAL_UINT8(SERVO_LIMP_OFF, servoRunLimpReason(true, SERVO_HOLD_BOUND_CEILING));
}

// Any estop ends the run and leaves every Output it drove limp (ADR 0043):
// the halt releases every arm this rule calls live, so an Output a run holds
// is live though nothing enabled it at start, and one the run has let go is
// not driven by anything.
void test_an_output_a_run_holds_is_one_every_release_reaches() {
    TEST_ASSERT_TRUE_MESSAGE(servoRunOutputLive(false, true), "a run's free Output is let go by the halt");
    TEST_ASSERT_TRUE(servoRunOutputLive(true, false));
    TEST_ASSERT_FALSE_MESSAGE(servoRunOutputLive(false, false), "a free Output no run holds has no pulse to take");
}

// A run holds at most one Output (#411 slice 4): stepping on to the next free
// Output lets go of the one before, in firmware, so a page that dies between
// two nudges can never leave two free servos energized.
void test_taking_the_next_output_lets_go_of_the_one_before() {
    const ServoRunNudgeStep first = servoRunOnNudge(SERVO_RUN_NONE, 2, true);
    TEST_ASSERT_EQUAL_UINT8(SERVO_RUN_TAKE, first.act);
    TEST_ASSERT_EQUAL_UINT8(SERVO_RUN_NONE, first.letGo);

    const ServoRunNudgeStep next = servoRunOnNudge(2, 3, true);
    TEST_ASSERT_EQUAL_UINT8(SERVO_RUN_TAKE, next.act);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(2, next.letGo, "ARM3 is let go before ARM4 is taken");

    const ServoRunNudgeStep again = servoRunOnNudge(3, 3, true);
    TEST_ASSERT_EQUAL_UINT8(SERVO_RUN_KEEP, again.act);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(SERVO_RUN_NONE, again.letGo, "a second nudge on the held Output keeps it");
}

// A held Output stays eligible or is let go: a Part or a Light Type landing on
// it while the run holds it is seen on the next nudge, which lets it go rather
// than refreshing the hold. The frame check asks servoRunMayTake() of the same
// facts, so a Part on the row is enough.
void test_a_part_landing_on_the_held_output_lets_it_go() {
    ServoRunTakeInputs landed = freeOutput();
    landed.partCount = 1;
    const ServoRunNudgeStep step = servoRunOnNudge(2, 2, servoRunMayTake(landed));
    TEST_ASSERT_EQUAL_UINT8(SERVO_RUN_REFUSE, step.act);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(2, step.letGo, "the run lets go of an Output a Part is on");

    ServoRunTakeInputs lit = freeOutput();
    lit.lightNow = true;
    TEST_ASSERT_EQUAL_UINT8(2, servoRunOnNudge(2, 2, servoRunMayTake(lit)).letGo);

    // Refusing an Output the run does not hold lets go of nothing: the one it
    // holds is still free.
    TEST_ASSERT_EQUAL_UINT8(SERVO_RUN_NONE, servoRunOnNudge(3, 2, false).letGo);
}

// The first width stays inside what the nudge plans from: a recorded centre
// outside the band (up to 2500 us on an MG90S) would sit pulsed there with no
// twitch until the expiry, and one at the band's edge would twitch one way.
void test_the_first_width_is_one_a_symmetric_nudge_plans_from() {
    const uint16_t centres[] = {500, 900, 1000, 1050, 1500, 1950, 2000, 2400, 2500};
    for (const uint16_t centreUs : centres) {
        const uint16_t firstUs = servoRunFirstWidthUs(centreUs, SERVO_BAND_STD, SERVO_NUDGE_AMPLITUDE_US);
        ServoNudgePlan plan = {};
        TEST_ASSERT_TRUE_MESSAGE(servoNudgePlan(firstUs, SERVO_BAND_STD, SERVO_NUDGE_AMPLITUDE_US, &plan),
                                 "the nudge plans from the first width");
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(firstUs - SERVO_NUDGE_AMPLITUDE_US, plan.loUs, "symmetric, not shifted");
        TEST_ASSERT_EQUAL_UINT16(firstUs + SERVO_NUDGE_AMPLITUDE_US, plan.hiUs);
    }
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(1500, servoRunFirstWidthUs(1500, SERVO_BAND_STD, SERVO_NUDGE_AMPLITUDE_US),
                                     "a centre the nudge can use is kept");
    TEST_ASSERT_EQUAL_UINT16(1900, servoRunFirstWidthUs(2500, SERVO_BAND_STD, SERVO_NUDGE_AMPLITUDE_US));
}

void test_a_dials_output_still_says_which_bound_let_it_go() {
    TEST_ASSERT_EQUAL_UINT8(SERVO_LIMP_EXPIRED, servoRunLimpReason(false, SERVO_HOLD_BOUND_EXPIRY));
    TEST_ASSERT_EQUAL_UINT8(SERVO_LIMP_CEILING, servoRunLimpReason(false, SERVO_HOLD_BOUND_CEILING));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_a_run_takes_only_a_free_servo_output);
    RUN_TEST(test_a_runs_output_goes_limp_within_seconds_of_the_last_nudge);
    RUN_TEST(test_nudges_that_keep_arriving_still_end_the_hold_at_the_ceiling);
    RUN_TEST(test_an_output_a_run_holds_is_one_every_release_reaches);
    RUN_TEST(test_a_dials_output_still_says_which_bound_let_it_go);
    RUN_TEST(test_taking_the_next_output_lets_go_of_the_one_before);
    RUN_TEST(test_a_part_landing_on_the_held_output_lets_it_go);
    RUN_TEST(test_the_first_width_is_one_a_symmetric_nudge_plans_from);
    return UNITY_END();
}
