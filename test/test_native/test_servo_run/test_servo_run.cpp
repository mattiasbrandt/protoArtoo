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
#include "servo_run.h"

void setUp() {}
void tearDown() {}

namespace {
// A free servo Output on a board whose LEDC came up.
ServoRunTakeInputs freeOutput() {
    ServoRunTakeInputs in = {};
    in.ledcReady = true;
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
    noTimer.ledcReady = false;
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
    TEST_ASSERT_TRUE_MESSAGE(servoRunArmLive(false, true), "a run's free Output is let go by the halt");
    TEST_ASSERT_TRUE(servoRunArmLive(true, false));
    TEST_ASSERT_FALSE_MESSAGE(servoRunArmLive(false, false), "a free Output no run holds has no pulse to take");
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
    return UNITY_END();
}
