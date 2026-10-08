// =============================================================================
// test/test_native/test_drive_feedback_stale/test_drive_feedback_stale.cpp
//
// Native unit tests for driveFeedbackIsStale() (include/robot_state.h).
//
// Why this rule matters: the driveFeedback* mirror is a reading, not a
// setting. A backend that goes quiet leaves its last battery, temperature,
// speed and current numbers sitting in RobotState, and /api/status would go on
// publishing them -- a surface cannot tell a stale reading from a live one, so
// DriveTask invalidates the mirror instead. This is the rule it applies, and
// the same honesty the Status Plate's freshness state keeps at the other end
// of the same wire (#346).
// =============================================================================
#include <unity.h>

#include "robot_state.h"

void setUp() {
}
void tearDown() {
}

void test_feedback_never_received_is_stale() {
    // 0 means no frame has ever been stored, which is stale by definition --
    // there is no reading to present, live or otherwise.
    TEST_ASSERT_TRUE(driveFeedbackIsStale(0, 1000, 5000));
}

void test_fresh_feedback_is_not_stale() {
    TEST_ASSERT_FALSE(driveFeedbackIsStale(9000, 10000, 5000));
}

void test_feedback_at_exactly_the_window_is_not_stale() {
    // (10000 - 5000) == 5000 is not > 5000, so the reading still stands.
    TEST_ASSERT_FALSE(driveFeedbackIsStale(5000, 10000, 5000));
}

void test_feedback_one_ms_past_the_window_is_stale() {
    // (10001 - 5000) == 5001 > 5000.
    TEST_ASSERT_TRUE(driveFeedbackIsStale(5000, 10001, 5000));
}

void test_millis_overflow_keeps_a_fresh_reading_fresh() {
    // The clock wrapped between the frame and now: unsigned subtraction gives
    // the real 1000 ms elapsed rather than a huge number, so a reading taken a
    // second ago is not thrown away for an accident of the counter.
    const uint32_t lastFeedbackMs = 0xFFFFFC18u;  // 1000 ms before wrap
    TEST_ASSERT_FALSE(driveFeedbackIsStale(lastFeedbackMs, 0, 5000));
}

void test_millis_overflow_still_detects_a_stale_reading() {
    const uint32_t lastFeedbackMs = 0xFFFFFC18u;  // 1000 ms before wrap
    TEST_ASSERT_TRUE(driveFeedbackIsStale(lastFeedbackMs, 5000, 5000));
}

void test_the_ten_second_reading_drivetask_invalidates() {
    // DriveTask's own window is 5000 ms (kFeedbackStaleMs, src/tasks/drive.cpp).
    // A reading ten seconds old is exactly the case that used to go on being
    // published as live.
    TEST_ASSERT_TRUE(driveFeedbackIsStale(10000, 20000, 5000));
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_feedback_never_received_is_stale);
    RUN_TEST(test_fresh_feedback_is_not_stale);
    RUN_TEST(test_feedback_at_exactly_the_window_is_not_stale);
    RUN_TEST(test_feedback_one_ms_past_the_window_is_stale);
    RUN_TEST(test_millis_overflow_keeps_a_fresh_reading_fresh);
    RUN_TEST(test_millis_overflow_still_detects_a_stale_reading);
    RUN_TEST(test_the_ten_second_reading_drivetask_invalidates);

    return UNITY_END();
}
