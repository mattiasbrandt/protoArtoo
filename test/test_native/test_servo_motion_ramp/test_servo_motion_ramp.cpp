// =============================================================================
// test/test_native/test_servo_motion_ramp/test_servo_motion_ramp.cpp
//
// A Servo Output's move, planned in time from its own Motion Profile (ADR 0052,
// #354). What these hold is what a builder checks with a stopwatch: a full
// throw takes the output's own time to full throw, a shorter move takes less
// but pays both ramps, and an output nobody has measured jumps rather than
// pretending to know a rate.
// =============================================================================
#include <unity.h>

#include "servo_motion_ramp.h"

void setUp() {}
void tearDown() {}

namespace {

// An MG996R-sized pair: 1000 us of throw, 900 ms end to end, 225 ms to speed.
constexpr uint16_t kSpan = 1000;
constexpr uint16_t kThrowMs = 900;
constexpr uint16_t kAccelMs = 225;

ServoMotionRamp plan(uint16_t from, uint16_t to, bool calibrated = true) {
    return servoMotionPlan(from, to, kSpan, kThrowMs, kAccelMs, calibrated, 1000);
}

}  // namespace

void test_a_full_throw_takes_the_outputs_own_time() {
    const ServoMotionRamp r = plan(1000, 2000);
    TEST_ASSERT_EQUAL_UINT16(kThrowMs, r.durationMs);
    TEST_ASSERT_EQUAL_UINT16(kAccelMs, r.rampMs);
    TEST_ASSERT_EQUAL_UINT16(1000, servoMotionPositionAt(r, 1000));
    TEST_ASSERT_EQUAL_UINT16(2000, servoMotionPositionAt(r, 1000 + kThrowMs));
    TEST_ASSERT_FALSE(servoMotionArrived(r, 1000 + kThrowMs - 1));
    TEST_ASSERT_TRUE(servoMotionArrived(r, 1000 + kThrowMs));
}

void test_a_half_throw_pays_both_ramps() {
    // Cruise is 1000 us / 675 ms; 500 us at cruise is 337.5 ms, plus one ramp's
    // worth for the speeding up and slowing down -- 562.5 ms, not 450.
    const ServoMotionRamp r = plan(1000, 1500);
    TEST_ASSERT_UINT16_WITHIN(1, 563, r.durationMs);
    TEST_ASSERT_EQUAL_UINT16(kAccelMs, r.rampMs);
}

void test_a_short_move_never_reaches_cruise() {
    const ServoMotionRamp r = plan(1000, 1100);
    TEST_ASSERT_TRUE(r.durationMs > 0);
    TEST_ASSERT_TRUE(r.rampMs < kAccelMs);
    TEST_ASSERT_UINT16_WITHIN(1, r.durationMs / 2, r.rampMs);
}

void test_the_move_is_symmetric_and_never_backs_up() {
    const ServoMotionRamp r = plan(1000, 2000);
    TEST_ASSERT_UINT16_WITHIN(1, 1500, servoMotionPositionAt(r, 1000 + kThrowMs / 2));
    uint16_t last = servoMotionPositionAt(r, 1000);
    for (uint32_t t = 1000; t <= 1000 + kThrowMs; t += 20) {
        const uint16_t now = servoMotionPositionAt(r, t);
        TEST_ASSERT_TRUE(now >= last);
        last = now;
    }
    // The ramp is gentle at the start: the first frame covers far less than an
    // even share of the throw would.
    TEST_ASSERT_TRUE(servoMotionPositionAt(r, 1020) - 1000 < kSpan * 20 / kThrowMs);
}

void test_a_reversed_move_mirrors_the_forward_one() {
    const ServoMotionRamp forward = plan(1000, 2000);
    const ServoMotionRamp back = plan(2000, 1000);
    TEST_ASSERT_EQUAL_UINT16(forward.durationMs, back.durationMs);
    for (uint32_t t = 1000; t <= 1000 + kThrowMs; t += 100) {
        TEST_ASSERT_UINT16_WITHIN(1, 3000 - servoMotionPositionAt(forward, t),
                                  servoMotionPositionAt(back, t));
    }
}

void test_an_unmeasured_output_jumps() {
    const ServoMotionRamp r = plan(1000, 2000, false);
    TEST_ASSERT_EQUAL_UINT16(0, r.durationMs);
    TEST_ASSERT_TRUE(servoMotionArrived(r, 1000));
    TEST_ASSERT_EQUAL_UINT16(2000, servoMotionPositionAt(r, 1000));
}

void test_no_span_and_no_distance_are_snaps() {
    TEST_ASSERT_EQUAL_UINT16(0, servoMotionPlan(1000, 2000, 0, kThrowMs, kAccelMs, true, 0).durationMs);
    TEST_ASSERT_EQUAL_UINT16(0, plan(1500, 1500).durationMs);
}

void test_a_move_inside_one_frame_is_a_snap() {
    // 40 ms to full throw: one microsecond is over long before the next frame.
    const ServoMotionRamp r = servoMotionPlan(1000, 1001, kSpan, 40, 10, true, 0);
    TEST_ASSERT_EQUAL_UINT16(0, r.durationMs);
}

void test_a_ramp_longer_than_half_the_throw_still_takes_the_throw() {
    const ServoMotionRamp r = servoMotionPlan(1000, 2000, kSpan, 400, 400, true, 0);
    TEST_ASSERT_EQUAL_UINT16(400, r.durationMs);
    TEST_ASSERT_EQUAL_UINT16(200, r.rampMs);
}

void test_a_move_across_the_millis_rollover_ends_on_time() {
    const ServoMotionRamp r = servoMotionPlan(1000, 2000, kSpan, kThrowMs, kAccelMs, true, 0xFFFFFF00u);
    TEST_ASSERT_FALSE(servoMotionArrived(r, 0xFFFFFF00u + 100));
    TEST_ASSERT_TRUE(servoMotionArrived(r, (uint32_t)(0xFFFFFF00u + kThrowMs)));
    TEST_ASSERT_EQUAL_UINT16(2000, servoMotionPositionAt(r, (uint32_t)(0xFFFFFF00u + kThrowMs)));
}

int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_a_full_throw_takes_the_outputs_own_time);
    RUN_TEST(test_a_half_throw_pays_both_ramps);
    RUN_TEST(test_a_short_move_never_reaches_cruise);
    RUN_TEST(test_the_move_is_symmetric_and_never_backs_up);
    RUN_TEST(test_a_reversed_move_mirrors_the_forward_one);
    RUN_TEST(test_an_unmeasured_output_jumps);
    RUN_TEST(test_no_span_and_no_distance_are_snaps);
    RUN_TEST(test_a_move_inside_one_frame_is_a_snap);
    RUN_TEST(test_a_ramp_longer_than_half_the_throw_still_takes_the_throw);
    RUN_TEST(test_a_move_across_the_millis_rollover_ends_on_time);
    return UNITY_END();
}
