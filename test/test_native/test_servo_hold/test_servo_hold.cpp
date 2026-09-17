// =============================================================================
// test/test_native/test_servo_hold/test_servo_hold.cpp
//
// The calibration dial's hold and its two firmware bounds (ADR 0064, #364).
//
// Both bounds are required and neither alone is safe, so what these hold is the
// part a page cannot reach: a hold kept alive by arriving commands still ends
// at the ceiling, and a hold nobody refreshes ends within seconds however long
// the ceiling has left. The reason each one gives is asserted too, because it
// is what the surface tells the builder.
// =============================================================================
#include <unity.h>

#include "config.h"       // SERVO_HOLD_EXPIRY_MS, SERVO_HOLD_CEILING_MS
#include "robot_state.h"  // ServoLimpReason -- the word a surface reads
#include "servo_hold.h"

void setUp() {}
void tearDown() {}

// The numbers ADR 0064 settled: a few seconds, and ten minutes.
void test_the_two_bounds_are_the_numbers_the_decision_names() {
    TEST_ASSERT_EQUAL_UINT32(3000, SERVO_HOLD_EXPIRY_MS);
    TEST_ASSERT_EQUAL_UINT32(600000, SERVO_HOLD_CEILING_MS);
}

void test_nothing_is_bounded_until_a_hold_is_taken() {
    ServoHoldState hold = {};
    TEST_ASSERT_FALSE(hold.held);
    TEST_ASSERT_EQUAL_UINT8(
        SERVO_HOLD_BOUND_NONE,
        servoHoldBoundHit(hold, 900000, SERVO_HOLD_EXPIRY_MS, SERVO_HOLD_CEILING_MS));
}

void test_the_first_command_takes_the_hold_and_the_next_only_refreshes_it() {
    ServoHoldState hold = {};
    TEST_ASSERT_TRUE(servoHoldCommand(&hold, 10000));
    TEST_ASSERT_TRUE(hold.held);
    TEST_ASSERT_EQUAL_UINT32(10000, hold.takenMs);

    // Every command after it moves the expiry and leaves the ceiling where it
    // was: that is what stops a page extending the hold indefinitely.
    TEST_ASSERT_FALSE(servoHoldCommand(&hold, 11000));
    TEST_ASSERT_EQUAL_UINT32(10000, hold.takenMs);
    TEST_ASSERT_EQUAL_UINT32(11000, hold.lastCommandMs);
}

void test_commands_that_stop_arriving_end_the_hold_within_seconds() {
    ServoHoldState hold = {};
    servoHoldCommand(&hold, 10000);
    // One second on: the page's keepalive cadence, well inside the expiry.
    TEST_ASSERT_EQUAL_UINT8(
        SERVO_HOLD_BOUND_NONE,
        servoHoldBoundHit(hold, 11000, SERVO_HOLD_EXPIRY_MS, SERVO_HOLD_CEILING_MS));
    TEST_ASSERT_EQUAL_UINT8(
        SERVO_HOLD_BOUND_NONE,
        servoHoldBoundHit(hold, 12999, SERVO_HOLD_EXPIRY_MS, SERVO_HOLD_CEILING_MS));
    TEST_ASSERT_EQUAL_UINT8(
        SERVO_HOLD_BOUND_EXPIRY,
        servoHoldBoundHit(hold, 13000, SERVO_HOLD_EXPIRY_MS, SERVO_HOLD_CEILING_MS));
}

// The case the short expiry alone cannot catch: a page left open on an
// abandoned bench keeps sending, and the hold must still end.
void test_a_hold_kept_alive_by_commands_still_ends_at_the_ceiling() {
    ServoHoldState hold = {};
    servoHoldCommand(&hold, 1000);
    uint32_t now = 1000;
    for (int beat = 0; beat < 599; ++beat) {
        now += 1000;
        servoHoldCommand(&hold, now);  // one command a second, exactly what the dial sends
        TEST_ASSERT_EQUAL_UINT8(
            SERVO_HOLD_BOUND_NONE,
            servoHoldBoundHit(hold, now, SERVO_HOLD_EXPIRY_MS, SERVO_HOLD_CEILING_MS));
    }
    now += 1000;  // ten minutes since the hold was taken
    servoHoldCommand(&hold, now);
    TEST_ASSERT_EQUAL_UINT8(
        SERVO_HOLD_BOUND_CEILING,
        servoHoldBoundHit(hold, now, SERVO_HOLD_EXPIRY_MS, SERVO_HOLD_CEILING_MS));
}

// Past the ceiling with the commands stopped as well: the surface should say
// ten minutes is the most a dial holds, not that the link dropped.
void test_the_ceiling_is_the_reason_given_when_both_have_fired() {
    ServoHoldState hold = {};
    servoHoldCommand(&hold, 1000);
    TEST_ASSERT_EQUAL_UINT8(
        SERVO_HOLD_BOUND_CEILING,
        servoHoldBoundHit(hold, 1000 + SERVO_HOLD_CEILING_MS + 60000, SERVO_HOLD_EXPIRY_MS,
                          SERVO_HOLD_CEILING_MS));
}

// Resuming is one press, and it restarts both bounds rather than continuing the
// ones the released hold had run down (ADR 0064).
void test_a_hold_taken_again_restarts_both_bounds() {
    ServoHoldState hold = {};
    servoHoldCommand(&hold, 1000);
    servoHoldEnd(&hold);
    TEST_ASSERT_FALSE(hold.held);
    TEST_ASSERT_EQUAL_UINT8(
        SERVO_HOLD_BOUND_NONE,
        servoHoldBoundHit(hold, 1000 + SERVO_HOLD_CEILING_MS, SERVO_HOLD_EXPIRY_MS,
                          SERVO_HOLD_CEILING_MS));

    TEST_ASSERT_TRUE(servoHoldCommand(&hold, 1000 + SERVO_HOLD_CEILING_MS));
    TEST_ASSERT_EQUAL_UINT32(1000 + SERVO_HOLD_CEILING_MS, hold.takenMs);
    TEST_ASSERT_EQUAL_UINT8(
        SERVO_HOLD_BOUND_NONE,
        servoHoldBoundHit(hold, 1000 + SERVO_HOLD_CEILING_MS + 1000, SERVO_HOLD_EXPIRY_MS,
                          SERVO_HOLD_CEILING_MS));
}

// millis() wraps every 49 days, and a hold taken just before the wrap must not
// read as one taken 49 days ago.
void test_a_hold_across_a_millis_wrap_is_judged_on_elapsed_time() {
    ServoHoldState hold = {};
    // 1,024 ms short of the wrap, so every reading below is on the far side of
    // it and a subtraction that is not unsigned reads as 49 days rather than
    // as two seconds.
    const uint32_t beforeWrap = 0xFFFFFC00u;
    servoHoldCommand(&hold, beforeWrap);

    const uint32_t twoSecondsOn = beforeWrap + 2000u;
    TEST_ASSERT_TRUE(twoSecondsOn < beforeWrap);  // the clock really has wrapped
    TEST_ASSERT_EQUAL_UINT8(
        SERVO_HOLD_BOUND_NONE,
        servoHoldBoundHit(hold, twoSecondsOn, SERVO_HOLD_EXPIRY_MS, SERVO_HOLD_CEILING_MS));

    const uint32_t threeSecondsOn = beforeWrap + SERVO_HOLD_EXPIRY_MS;
    TEST_ASSERT_TRUE(threeSecondsOn < beforeWrap);
    TEST_ASSERT_EQUAL_UINT8(
        SERVO_HOLD_BOUND_EXPIRY,
        servoHoldBoundHit(hold, threeSecondsOn, SERVO_HOLD_EXPIRY_MS, SERVO_HOLD_CEILING_MS));
}

// A surface reads the reason as a word, and the two bounds must not read alike.
void test_every_limp_reason_has_its_own_word() {
    TEST_ASSERT_EQUAL_STRING("off", servoLimpReasonToString(SERVO_LIMP_OFF));
    TEST_ASSERT_EQUAL_STRING("pulses-off", servoLimpReasonToString(SERVO_LIMP_RELEASED));
    TEST_ASSERT_EQUAL_STRING("expiry", servoLimpReasonToString(SERVO_LIMP_EXPIRED));
    TEST_ASSERT_EQUAL_STRING("ceiling", servoLimpReasonToString(SERVO_LIMP_CEILING));
    TEST_ASSERT_EQUAL_STRING("estop", servoLimpReasonToString(SERVO_LIMP_ESTOP));
    TEST_ASSERT_EQUAL_STRING("sleep", servoLimpReasonToString(SERVO_LIMP_SLEEP));
    // A zero-filled mirror is an output nothing has driven, not a released one.
    TEST_ASSERT_EQUAL_UINT8(0, (uint8_t)SERVO_LIMP_OFF);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_the_two_bounds_are_the_numbers_the_decision_names);
    RUN_TEST(test_nothing_is_bounded_until_a_hold_is_taken);
    RUN_TEST(test_the_first_command_takes_the_hold_and_the_next_only_refreshes_it);
    RUN_TEST(test_commands_that_stop_arriving_end_the_hold_within_seconds);
    RUN_TEST(test_a_hold_kept_alive_by_commands_still_ends_at_the_ceiling);
    RUN_TEST(test_the_ceiling_is_the_reason_given_when_both_have_fired);
    RUN_TEST(test_a_hold_taken_again_restarts_both_bounds);
    RUN_TEST(test_a_hold_across_a_millis_wrap_is_judged_on_elapsed_time);
    RUN_TEST(test_every_limp_reason_has_its_own_word);
    return UNITY_END();
}
