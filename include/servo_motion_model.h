// =============================================================================
// include/servo_motion_model.h
//
// Auto-generated from docs/servo-motion.yaml by tools/generate_servo_motion.py
// DO NOT EDIT MANUALLY
//
// Source digest: sha256 f58ec1c704908ab8ae93d86faf3f49b6de31a4b2275ade85efb2c266dd8d4e98
//
// How a Servo Output's move is laid out in time from its Motion Profile
// (ADR 0052). ServoTask plans every move with the functions below, and the
// browser runs the same declaration, generated into data/servo_motion.js, to
// time a move before it is sent (#287 specific 11, #439). To change the
// model, edit docs/servo-motion.yaml and run the generator; the prose that
// explains the model as a whole is include/servo_motion_ramp.h's.
// =============================================================================
#pragma once

#include <math.h>
#include <stdint.h>

#include "servo_output_row.h"  // ServoEasing, SERVO_THROW_MS_MIN

// -----------------------------------------------------------------------------
// ServoMotionProfile
// What a move is planned from, read off the Output's row by
// servoMotionProfileOf(). The ends are the recorded Endpoint Pair already
// ordered, so the planner never sorts a directional pair itself (ADR 0041), and
// the ease is the one that actually runs, never the stored one.
//
// Ten bytes, answered by value: ServoTask reads it on a measured Core 1 frame
// (ADR 0040), where a whole ServoOutputRow is the wrong price for five numbers.
// -----------------------------------------------------------------------------
struct ServoMotionProfile {
    uint16_t loUs;       // the lower recorded end
    uint16_t hiUs;       // the higher recorded end
    uint16_t throwMs;    // how long a full throw takes
    uint16_t accelMs;    // how long the move spends getting up to speed
    ServoEasing easing;  // the shape that runs: servoOutputEffectiveEasing()
    bool calibrated;     // somebody measured the ends against the linkage
};

// -----------------------------------------------------------------------------
// ServoMotionRamp
// One planned move: where it starts and ends, when, and how long it spends
// getting up to speed.
// -----------------------------------------------------------------------------
struct ServoMotionRamp {
    uint32_t startMs;
    uint16_t fromUs;
    uint16_t toUs;        // where this plan ends: the target, or an overshoot's aim
    // Where the move comes to rest. Equal to toUs except on the way out of an
    // overshoot, where toUs is the aim and this is the target it settles back
    // to -- which is the number every surface shows as where the move ends.
    uint16_t settleUs;
    uint16_t durationMs;  // 0 = a snap: toUs is where the output is, at once
    uint16_t rampMs;      // time spent getting up to speed, and slowing down
    bool softStart;       // `soft`: the speed rises along a smoothstep
};

// -----------------------------------------------------------------------------
// servoMotionOvershootAim()
// Where an overshoot aims for a move from fromUs to toUs between the recorded
// ends loUs..hiUs, or toUs itself when the move is not one to overshoot.
//
// A twelfth of the distance past the target, on a move longer than an eighth of
// the travel -- a shorter one would read as a wobble rather than as weight --
// and clamped to the recorded ends, which is the whole fence (ADR 0052). A
// target already at or outside an end has no room past it, so it gets no aim
// rather than one pointing back the way the move came, or one clamped onto the
// nearer end past a target the calibration dial left outside the ends (#417).
// -----------------------------------------------------------------------------
inline uint16_t servoMotionOvershootAim(uint16_t fromUs, uint16_t toUs, uint16_t loUs, uint16_t hiUs) {
    if (toUs <= loUs || toUs >= hiUs) {
        return toUs;
    }
    const int32_t delta = (int32_t)toUs - (int32_t)fromUs;
    const int32_t distance = delta < 0 ? -delta : delta;
    if (hiUs <= loUs || distance <= (int32_t)(hiUs - loUs) / 8) {
        return toUs;
    }
    const int32_t over = distance / 12;
    int32_t aim = (int32_t)toUs + (delta > 0 ? over : -over);
    if (aim < (int32_t)loUs) {
        aim = (int32_t)loUs;
    }
    if (aim > (int32_t)hiUs) {
        aim = (int32_t)hiUs;
    }
    if ((delta > 0 && aim <= (int32_t)toUs) || (delta < 0 && aim >= (int32_t)toUs)) {
        return toUs;
    }
    return (uint16_t)aim;
}

// -----------------------------------------------------------------------------
// servoMotionPlan()
// Plan a move from fromUs to toUs by an Output's Motion Profile. The rate comes
// from the span of the profile's recorded ends; the shape from its ease.
//
// The profile stores two times, not a rate: accelerate for the profile's own
// ramp time, cruise at the speed that makes a full throw take exactly
// throw_ms, and slow down the same way. A move shorter than two ramps never
// reaches that speed, so it is a triangle -- the same acceleration, turned
// round half way. A partial move therefore takes less time than a full throw,
// but not proportionally less: the ramps at each end are paid whatever the
// distance.
//
// An overshoot plans the way out, to the aim, with settleUs = toUs; the way back
// is servoMotionSettleBack()'s. Every snap below goes straight to toUs, never
// to an aim: a move with no ramp has nothing to overshoot with.
//
// It snaps -- durationMs 0 -- in four cases, each a statement about what the
// model knows rather than a tuning choice:
//   - the output is uncalibrated. Full throw is only defined once somebody has
//     measured the ends, so there is no rate to derive and the move is a jump
//     rather than a ramp (ADR 0052). Every row adopted from the fixed field sets
//     is uncalibrated, so an existing droid moves exactly as it did until its
//     builder calibrates an output;
//   - the pair has no span, which is the same missing rate by another route;
//   - there is nowhere to go;
//   - the move would finish inside one 50 Hz ServoTask frame, which cannot
//     resolve a ramp shorter than itself (SERVO_THROW_MS_MIN).
// -----------------------------------------------------------------------------
inline ServoMotionRamp servoMotionPlan(uint16_t fromUs, uint16_t toUs, const ServoMotionProfile& profile, uint32_t startMs) {
    ServoMotionRamp plan = {};
    plan.startMs = startMs;
    plan.fromUs = fromUs;
    plan.toUs = toUs;
    plan.settleUs = toUs;
    const uint16_t spanUs = profile.hiUs > profile.loUs ? (uint16_t)(profile.hiUs - profile.loUs) : 0;
    if (!profile.calibrated || spanUs == 0 || fromUs == toUs || profile.throwMs < SERVO_THROW_MS_MIN) {
        return plan;
    }

    // Decided here, when the target is set: an overshoot changes where this
    // plan ends and nothing else about it.
    const uint16_t aimUs = profile.easing == SERVO_EASE_OVERSHOOT ? servoMotionOvershootAim(fromUs, toUs, profile.loUs, profile.hiUs) : toUs;

    const float fullThrowMs = (float)profile.throwMs;
    // A ramp longer than half the throw would leave no time to slow down, so
    // the profile's ramp is held to half -- a full throw is then a triangle and
    // still takes exactly throw_ms.
    float rampMs = (float)(profile.accelMs == 0 ? 1 : profile.accelMs);
    if (rampMs > fullThrowMs / 2.0f) {
        rampMs = fullThrowMs / 2.0f;
    }
    const float cruiseUsPerMs = (float)spanUs / (fullThrowMs - rampMs);
    const float accelUsPerMs2 = cruiseUsPerMs / rampMs;

    const float distanceUs = (float)(aimUs > fromUs ? aimUs - fromUs : fromUs - aimUs);
    float durationMs = 0.0f;
    float usedRampMs = 0.0f;
    if (distanceUs >= cruiseUsPerMs * rampMs) {
        usedRampMs = rampMs;
        durationMs = distanceUs / cruiseUsPerMs + rampMs;
    } else {
        usedRampMs = sqrtf(distanceUs / accelUsPerMs2);
        durationMs = 2.0f * usedRampMs;
    }

    if (durationMs < (float)SERVO_THROW_MS_MIN) {
        return plan;
    }
    plan.toUs = aimUs;
    plan.softStart = profile.easing == SERVO_EASE_SOFT;
    const long roundedDuration = lroundf(durationMs);
    plan.durationMs = roundedDuration > 0xFFFF ? (uint16_t)0xFFFF : (uint16_t)roundedDuration;
    long roundedRamp = lroundf(usedRampMs);
    if (roundedRamp < 1) {
        roundedRamp = 1;
    }
    if (roundedRamp > plan.durationMs / 2) {
        roundedRamp = (long)(plan.durationMs / 2);
    }
    plan.rampMs = (uint16_t)roundedRamp;
    return plan;
}

// -----------------------------------------------------------------------------
// servoMotionSettles()
// The second half of an overshoot. A plan that settles has ended at its aim
// rather than its target; the way back is an ordinary move from the aim to the
// target, shaped `none`, because it is the settle and not a new move with a
// character of its own. ServoTask chains it when the first plan arrives.
// -----------------------------------------------------------------------------
inline bool servoMotionSettles(const ServoMotionRamp& ramp) {
    return ramp.settleUs != ramp.toUs;
}

// -----------------------------------------------------------------------------
// servoMotionSettleBack()
// The way back from an overshoot's aim to its target: servoMotionPlan() again,
// from where the first plan arrived, shaped `none`.
// -----------------------------------------------------------------------------
inline ServoMotionRamp servoMotionSettleBack(const ServoMotionRamp& arrived, const ServoMotionProfile& profile, uint32_t nowMs) {
    ServoMotionProfile settle = profile;
    settle.easing = SERVO_EASE_NONE;
    return servoMotionPlan(arrived.toUs, arrived.settleUs, settle, nowMs);
}

// -----------------------------------------------------------------------------
// servoMotionArrivalMs()
// How long after it is sent a move from fromUs to toUs comes to rest on toUs:
// the plan, and on an overshoot the settle ServoTask chains after it. Zero is
// a snap -- an Output nobody has measured jumps (ADR 0052), and how fast the
// servo itself gets there is not something this model knows.
//
// This is the figure the Rehearsal times a body move with (#439): the
// browser's answer is this function's, generated from this file, so it is
// the answer ServoTask's own plan gives for the same row and the same move.
// -----------------------------------------------------------------------------
inline uint32_t servoMotionArrivalMs(uint16_t fromUs, uint16_t toUs, const ServoMotionProfile& profile) {
    const ServoMotionRamp out = servoMotionPlan(fromUs, toUs, profile, 0);
    if (!servoMotionSettles(out)) {
        return (uint32_t)out.durationMs;
    }
    const ServoMotionRamp back = servoMotionSettleBack(out, profile, (uint32_t)out.durationMs);
    return (uint32_t)out.durationMs + (uint32_t)back.durationMs;
}
