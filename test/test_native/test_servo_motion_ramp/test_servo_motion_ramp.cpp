// =============================================================================
// test/test_native/test_servo_motion_ramp/test_servo_motion_ramp.cpp
//
// A Servo Output's move, planned in time from its own Motion Profile (ADR 0052,
// #354). What these hold is what a builder checks with a stopwatch: a full
// throw takes the output's own time to full throw, a shorter move takes less
// but pays both ramps, and an output nobody has measured jumps rather than
// pretending to know a rate.
//
// And the shape of the move (#414): each ease is pinned against `none` on the
// same move, so what an ease changes is exactly what it is allowed to change.
// =============================================================================
#include <unity.h>

#include "servo_motion_ramp.h"

void setUp() {}
void tearDown() {}

namespace {

// An MG996R-sized pair: 1000 us of throw, 900 ms end to end, 225 ms to speed.
constexpr uint16_t kLo = 1000;
constexpr uint16_t kHi = 2000;
constexpr uint16_t kSpan = kHi - kLo;
constexpr uint16_t kThrowMs = 900;
constexpr uint16_t kAccelMs = 225;

ServoMotionProfile profile(ServoEasing easing = SERVO_EASE_NONE, bool calibrated = true,
                           uint16_t throwMs = kThrowMs, uint16_t accelMs = kAccelMs,
                           uint16_t lo = kLo, uint16_t hi = kHi) {
    ServoMotionProfile p = {};
    p.loUs = lo;
    p.hiUs = hi;
    p.throwMs = throwMs;
    p.accelMs = accelMs;
    p.easing = easing;
    p.calibrated = calibrated;
    return p;
}

ServoMotionRamp plan(uint16_t from, uint16_t to, bool calibrated = true) {
    return servoMotionPlan(from, to, profile(SERVO_EASE_NONE, calibrated), 1000);
}

ServoMotionRamp planEased(uint16_t from, uint16_t to, ServoEasing easing) {
    return servoMotionPlan(from, to, profile(easing), 1000);
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
    const ServoMotionProfile noSpan = profile(SERVO_EASE_NONE, true, kThrowMs, kAccelMs, 1500, 1500);
    TEST_ASSERT_EQUAL_UINT16(0, servoMotionPlan(1000, 2000, noSpan, 0).durationMs);
    TEST_ASSERT_EQUAL_UINT16(0, plan(1500, 1500).durationMs);
}

void test_a_move_inside_one_frame_is_a_snap() {
    // 40 ms to full throw: one microsecond is over long before the next frame.
    const ServoMotionRamp r = servoMotionPlan(1000, 1001, profile(SERVO_EASE_NONE, true, 40, 10), 0);
    TEST_ASSERT_EQUAL_UINT16(0, r.durationMs);
}

void test_a_ramp_longer_than_half_the_throw_still_takes_the_throw() {
    const ServoMotionRamp r = servoMotionPlan(1000, 2000, profile(SERVO_EASE_NONE, true, 400, 400), 0);
    TEST_ASSERT_EQUAL_UINT16(400, r.durationMs);
    TEST_ASSERT_EQUAL_UINT16(200, r.rampMs);
}

void test_a_move_across_the_millis_rollover_ends_on_time() {
    const ServoMotionRamp r = servoMotionPlan(1000, 2000, profile(), 0xFFFFFF00u);
    TEST_ASSERT_FALSE(servoMotionArrived(r, 0xFFFFFF00u + 100));
    TEST_ASSERT_TRUE(servoMotionArrived(r, (uint32_t)(0xFFFFFF00u + kThrowMs)));
    TEST_ASSERT_EQUAL_UINT16(2000, servoMotionPositionAt(r, (uint32_t)(0xFFFFFF00u + kThrowMs)));
}

// -----------------------------------------------------------------------------
// The shape of the move (ADR 0052, #414)
// -----------------------------------------------------------------------------

void test_soft_breathes_in_and_ends_where_and_when_none_does() {
    const ServoMotionRamp none = planEased(1000, 2000, SERVO_EASE_NONE);
    const ServoMotionRamp soft = planEased(1000, 2000, SERVO_EASE_SOFT);
    // Nothing about where or when the move ends changes.
    TEST_ASSERT_EQUAL_UINT16(none.durationMs, soft.durationMs);
    TEST_ASSERT_EQUAL_UINT16(none.rampMs, soft.rampMs);
    TEST_ASSERT_EQUAL_UINT16(none.toUs, soft.toUs);
    TEST_ASSERT_EQUAL_UINT16(none.settleUs, soft.settleUs);
    TEST_ASSERT_FALSE(servoMotionSettles(soft));
    // The acceleration comes in: early in the ramp a soft move has gone less
    // far than a straight ramp would have...
    const uint32_t early = 1000 + kAccelMs / 4;
    TEST_ASSERT_TRUE(servoMotionPositionAt(soft, early) < servoMotionPositionAt(none, early));
    // ...and it has caught up by the end of the ramp, so the cruise and the
    // slowing down are the same move.
    TEST_ASSERT_UINT16_WITHIN(1, servoMotionPositionAt(none, 1000 + kAccelMs),
                              servoMotionPositionAt(soft, 1000 + kAccelMs));
    for (uint32_t t = 1000 + kAccelMs; t <= 1000 + kThrowMs; t += 20) {
        TEST_ASSERT_UINT16_WITHIN(1, servoMotionPositionAt(none, t), servoMotionPositionAt(soft, t));
    }
    // And it never backs up on the way.
    uint16_t last = servoMotionPositionAt(soft, 1000);
    for (uint32_t t = 1000; t <= 1000 + kAccelMs; t += 5) {
        const uint16_t now = servoMotionPositionAt(soft, t);
        TEST_ASSERT_TRUE(now >= last);
        last = now;
    }
}

void test_overshoot_aims_a_twelfth_past_and_settles_back() {
    // 1100 -> 1700 is 600 us, well over an eighth of the 1000 us travel: the
    // aim is 600 / 12 = 50 us past the target.
    const ServoMotionRamp none = planEased(1100, 1700, SERVO_EASE_NONE);
    const ServoMotionRamp over = planEased(1100, 1700, SERVO_EASE_OVERSHOOT);
    TEST_ASSERT_EQUAL_UINT16(1700, none.toUs);
    TEST_ASSERT_FALSE(servoMotionSettles(none));
    TEST_ASSERT_EQUAL_UINT16(1750, over.toUs);
    TEST_ASSERT_EQUAL_UINT16(1700, over.settleUs);
    TEST_ASSERT_TRUE(servoMotionSettles(over));
    TEST_ASSERT_TRUE(over.durationMs > none.durationMs);

    // The way back starts at the aim, ends on the target and settles nowhere
    // else.
    const uint32_t arrived = 1000 + over.durationMs;
    TEST_ASSERT_EQUAL_UINT16(1750, servoMotionPositionAt(over, arrived));
    const ServoMotionRamp back = servoMotionSettleBack(over, profile(SERVO_EASE_OVERSHOOT), arrived);
    TEST_ASSERT_EQUAL_UINT16(1750, back.fromUs);
    TEST_ASSERT_EQUAL_UINT16(1700, back.toUs);
    TEST_ASSERT_FALSE(servoMotionSettles(back));
    TEST_ASSERT_TRUE(back.durationMs > 0);
    TEST_ASSERT_EQUAL_UINT16(1700, servoMotionPositionAt(back, arrived + back.durationMs));

    // The same rule the other way round.
    const ServoMotionRamp down = planEased(1700, 1100, SERVO_EASE_OVERSHOOT);
    TEST_ASSERT_EQUAL_UINT16(1050, down.toUs);
    TEST_ASSERT_EQUAL_UINT16(1100, down.settleUs);
}

void test_overshoot_never_passes_the_recorded_ends() {
    // 1100 -> 1960 would aim 71 us past, at 2031: the recorded high end is
    // 2000, so that is as far as it goes.
    const ServoMotionRamp over = planEased(1100, 1960, SERVO_EASE_OVERSHOOT);
    TEST_ASSERT_EQUAL_UINT16(kHi, over.toUs);
    TEST_ASSERT_EQUAL_UINT16(1960, over.settleUs);
    for (uint32_t t = 1000; t <= 1000u + over.durationMs; t += 5) {
        TEST_ASSERT_TRUE(servoMotionPositionAt(over, t) <= kHi);
    }
    // A move to an end has no room past it, so it does not overshoot at all.
    const ServoMotionRamp toEnd = planEased(1500, kHi, SERVO_EASE_OVERSHOOT);
    TEST_ASSERT_EQUAL_UINT16(kHi, toEnd.toUs);
    TEST_ASSERT_FALSE(servoMotionSettles(toEnd));
    // The fence is the recorded ends, not the widest band a servo takes: a
    // narrower pair clamps the aim to its own end.
    const ServoMotionRamp narrow =
        servoMotionPlan(1300, 1680, profile(SERVO_EASE_OVERSHOOT, true, kThrowMs, kAccelMs, 1200, 1700), 0);
    TEST_ASSERT_EQUAL_UINT16(1700, narrow.toUs);
    TEST_ASSERT_EQUAL_UINT16(1680, narrow.settleUs);
}

void test_overshoot_is_skipped_under_an_eighth_of_travel() {
    // An eighth of 1000 us is 125 us: 120 us reads as a wobble, not as weight,
    // so the move plans exactly as `none` would.
    const ServoMotionRamp none = planEased(1500, 1620, SERVO_EASE_NONE);
    const ServoMotionRamp over = planEased(1500, 1620, SERVO_EASE_OVERSHOOT);
    TEST_ASSERT_EQUAL_UINT16(none.toUs, over.toUs);
    TEST_ASSERT_EQUAL_UINT16(none.settleUs, over.settleUs);
    TEST_ASSERT_EQUAL_UINT16(none.durationMs, over.durationMs);
    TEST_ASSERT_FALSE(servoMotionSettles(over));
}

void test_an_uncalibrated_overshoot_row_plans_exactly_like_none() {
    ServoOutputRow row = {};
    servoOutputRowDefaults(&row, SERVO_DRIVER_LEDC, LEDC_CH_ARM1, SERVO_COMP_MG996R);
    row.easing = SERVO_EASE_OVERSHOOT;
    row.calibrated = false;
    ServoOutputRow plain = row;
    plain.easing = SERVO_EASE_NONE;

    const ServoMotionProfile overProfile = servoMotionProfileOf(row);
    TEST_ASSERT_EQUAL_UINT8(SERVO_EASE_NONE, overProfile.easing);
    const ServoMotionRamp over = servoMotionPlan(1100, 1700, overProfile, 1000);
    const ServoMotionRamp none = servoMotionPlan(1100, 1700, servoMotionProfileOf(plain), 1000);
    TEST_ASSERT_EQUAL_UINT16(none.toUs, over.toUs);
    TEST_ASSERT_EQUAL_UINT16(none.settleUs, over.settleUs);
    TEST_ASSERT_EQUAL_UINT16(none.durationMs, over.durationMs);
    TEST_ASSERT_EQUAL_UINT16(1700, over.toUs);

    // Once somebody measures the ends, the same row overshoots.
    row.calibrated = true;
    TEST_ASSERT_TRUE(servoMotionSettles(servoMotionPlan(1100, 1700, servoMotionProfileOf(row), 1000)));
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
    RUN_TEST(test_soft_breathes_in_and_ends_where_and_when_none_does);
    RUN_TEST(test_overshoot_aims_a_twelfth_past_and_settles_back);
    RUN_TEST(test_overshoot_never_passes_the_recorded_ends);
    RUN_TEST(test_overshoot_is_skipped_under_an_eighth_of_travel);
    RUN_TEST(test_an_uncalibrated_overshoot_row_plans_exactly_like_none);
    return UNITY_END();
}
