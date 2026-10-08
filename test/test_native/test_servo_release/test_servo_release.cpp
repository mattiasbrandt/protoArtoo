// =============================================================================
// test/test_native/test_servo_release/test_servo_release.cpp
//
// Output Release from arrival (ADR 0043, #443): the rule ServoTask applies once
// a frame, away from the frame.
//
// What these hold is the part that makes a timed release safe to have at all:
// it counts from the arrival that armed it and from nothing else, a later
// arrival starts it again rather than adding to it, a cancelled one never
// fires, "never" and a dial's hold arm nothing, and a millis() wrap changes
// nothing.
// =============================================================================
#include <unity.h>

#include "servo_release.h"

void setUp() {}
void tearDown() {}

void test_nothing_is_owed_until_a_move_arrives() {
    ServoReleaseTimer timer = {};
    TEST_ASSERT_FALSE(servoReleasePending(timer));
    TEST_ASSERT_FALSE(servoReleaseDue(timer, 0));
    TEST_ASSERT_FALSE(servoReleaseDue(timer, 900000));
}

void test_a_release_comes_due_its_time_after_the_arrival() {
    ServoReleaseTimer timer = {};
    TEST_ASSERT_TRUE(servoReleaseArm(&timer, 10000, 2000, /*held=*/false));
    TEST_ASSERT_TRUE(servoReleasePending(timer));
    TEST_ASSERT_FALSE(servoReleaseDue(timer, 11999));
    TEST_ASSERT_TRUE(servoReleaseDue(timer, 12000));
}

// Every existing droid is a row at 0: it must hold where it stops, as today.
void test_a_row_at_never_owes_nothing() {
    ServoReleaseTimer timer = {};
    TEST_ASSERT_FALSE(servoReleaseArm(&timer, 10000, SERVO_RELEASE_MS_NEVER, false));
    TEST_ASSERT_FALSE(servoReleasePending(timer));
    TEST_ASSERT_FALSE(servoReleaseDue(timer, 10000 + SERVO_RELEASE_MS_MAX));
}

// ADR 0064: a move that arrives while the dial holds the Output arms nothing -
// and it also clears what an earlier arrival left pending.
void test_an_arrival_under_a_hold_arms_nothing_and_clears_what_was_pending() {
    ServoReleaseTimer timer = {};
    servoReleaseArm(&timer, 10000, 2000, false);
    TEST_ASSERT_FALSE(servoReleaseArm(&timer, 11000, 2000, /*held=*/true));
    TEST_ASSERT_FALSE(servoReleasePending(timer));
    TEST_ASSERT_FALSE(servoReleaseDue(timer, 60000));
}

// A cancelled release never fires later, on this move or any other.
void test_a_cancelled_release_never_fires() {
    ServoReleaseTimer timer = {};
    servoReleaseArm(&timer, 10000, 2000, false);
    servoReleaseCancel(&timer);
    TEST_ASSERT_FALSE(servoReleaseDue(timer, 12000));
    TEST_ASSERT_FALSE(servoReleaseDue(timer, 900000));
}

// The release belongs to the move that armed it: a second arrival before it
// fired starts the count again from there, rather than letting go on the first
// move's schedule part way through the second one's hold.
void test_a_later_arrival_counts_from_itself() {
    ServoReleaseTimer timer = {};
    servoReleaseArm(&timer, 10000, 2000, false);
    servoReleaseArm(&timer, 11500, 2000, false);
    TEST_ASSERT_FALSE(servoReleaseDue(timer, 12000));
    TEST_ASSERT_FALSE(servoReleaseDue(timer, 13499));
    TEST_ASSERT_TRUE(servoReleaseDue(timer, 13500));
}

void test_a_release_across_a_millis_wrap_is_judged_on_elapsed_time() {
    ServoReleaseTimer timer = {};
    const uint32_t arrived = 0xFFFFFC18u;  // 1000 ms before millis() wraps
    servoReleaseArm(&timer, arrived, 2000, false);
    TEST_ASSERT_FALSE(servoReleaseDue(timer, arrived + 1999u));  // wrapped, 999
    TEST_ASSERT_TRUE(servoReleaseDue(timer, arrived + 2000u));   // wrapped, 1000
}

void test_the_longest_release_is_a_minute() {
    ServoReleaseTimer timer = {};
    TEST_ASSERT_EQUAL_UINT16(60000, SERVO_RELEASE_MS_MAX);
    servoReleaseArm(&timer, 0, SERVO_RELEASE_MS_MAX, false);
    TEST_ASSERT_FALSE(servoReleaseDue(timer, 59999));
    TEST_ASSERT_TRUE(servoReleaseDue(timer, 60000));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_nothing_is_owed_until_a_move_arrives);
    RUN_TEST(test_a_release_comes_due_its_time_after_the_arrival);
    RUN_TEST(test_a_row_at_never_owes_nothing);
    RUN_TEST(test_an_arrival_under_a_hold_arms_nothing_and_clears_what_was_pending);
    RUN_TEST(test_a_cancelled_release_never_fires);
    RUN_TEST(test_a_later_arrival_counts_from_itself);
    RUN_TEST(test_a_release_across_a_millis_wrap_is_judged_on_elapsed_time);
    RUN_TEST(test_the_longest_release_is_a_minute);
    return UNITY_END();
}
