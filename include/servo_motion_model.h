// =============================================================================
// include/servo_motion_model.h
//
// Auto-generated from docs/servo-motion.yaml by tools/generate_servo_motion.py
// DO NOT EDIT MANUALLY
//
// Source digest: sha256 6ec80e8471082212aa2f848d72b46505da398dab60fd9f0ab1c969ac7cce5de9
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
//
// Every plan is laid out from rest. A move that starts already moving is the
// tail of one: startMs and fromUs are where and when a move from rest at the
// profile's acceleration would have set off to be going that fast where the
// Output is now (servoMotionRetarget()), so the Output is only ever placed on
// it from that moment on.
// -----------------------------------------------------------------------------
struct ServoMotionRamp {
    uint32_t startMs;     // when the plan left rest; in the past on a moving start
    uint16_t fromUs;      // where it left rest; behind the Output on a moving start
    uint16_t toUs;        // where this plan ends: the target, or an overshoot's aim
    // Where the move comes to rest. Equal to toUs except on the way out of an
    // overshoot, where toUs is the aim and this is the target it settles back
    // to -- which is the number every surface shows as where the move ends.
    uint16_t settleUs;
    uint16_t durationMs;  // 0 = a snap: toUs is where the output is, at once
    uint16_t rampMs;      // time spent getting up to speed, and slowing down
    bool softStart;       // `soft`: the speed rises along a smoothstep
    // This plan is a stop on the way: the Output could not reach settleUs
    // going the way it was going without braking harder than its profile,
    // so toUs is where it comes to rest, and the move to settleUs then
    // starts from rest with the profile's own ease (servoMotionSettleBack()).
    // It sits in what was the record's padding, so it costs no RAM.
    bool restarts;
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
// servoMotionRampMs()
// How long a move by this profile spends getting up to speed, and slowing
// down: the profile's own ramp time, and at least a millisecond.
// -----------------------------------------------------------------------------
inline float servoMotionRampMs(const ServoMotionProfile& profile) {
    const float fullThrowMs = (float)profile.throwMs;
    // A ramp longer than half the throw would leave no time to slow down, so
    // the profile's ramp is held to half -- a full throw is then a triangle and
    // still takes exactly throw_ms.
    float rampMs = (float)(profile.accelMs == 0 ? 1 : profile.accelMs);
    if (rampMs > fullThrowMs / 2.0f) {
        rampMs = fullThrowMs / 2.0f;
    }
    return rampMs;
}

// -----------------------------------------------------------------------------
// servoMotionCruiseUsPerMs()
// The speed a move by this profile cruises at, in microseconds a
// millisecond: the one that makes a full throw, ramps included, take exactly
// throw_ms. rampMs is servoMotionRampMs(profile). The acceleration is this
// over rampMs, so the profile's two times fix both limits a move keeps to.
// -----------------------------------------------------------------------------
inline float servoMotionCruiseUsPerMs(const ServoMotionProfile& profile, float rampMs) {
    const uint16_t spanUs = profile.hiUs > profile.loUs ? (uint16_t)(profile.hiUs - profile.loUs) : 0;
    return (float)spanUs / ((float)profile.throwMs - rampMs);
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

    const float rampMs = servoMotionRampMs(profile);
    const float cruiseUsPerMs = servoMotionCruiseUsPerMs(profile, rampMs);
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
// servoMotionRetarget()
// Plan a move to toUs for an Output that is at fromUs and going speedUsPerMs
// (signed: positive towards the higher width) -- a new target replacing a
// move part way through, whatever sent it (#442). It keeps to the profile's
// two limits: it speeds up to the cruise speed and never past it, and slows
// down at the profile's acceleration and never harder.
//
// At a speed of zero it is servoMotionPlan(), exactly. Every move from rest
// -- and so every move a saved sequence makes from rest, and every time the
// Rehearsal gives -- is the move it was.
//
// Going the way the target lies, with room to slow down before it: no stop.
// A move from rest at the profile's acceleration is going speedUsPerMs at
// one moment of its ramp, so the plan is that move, begun that long ago from
// that far behind the Output, and the Output carries on along it
// (ServoMotionRamp).
//
// A target behind the Output, or one too close to slow down for: the plan
// slows it to a stop at the profile's acceleration and says so (`restarts`),
// and the rest is planned from rest where it stopped, once it gets there
// (servoMotionSettleBack()).
//
// The ease, on a move that is already moving:
//   - `soft` shapes a start from rest and nothing else. Carrying on is not a
//     start, so it is the plain ramp; the rest after a stop is one, so it is
//     soft.
//   - `overshoot` aims when a move from rest over the same distance would --
//     from where the Output is to the target -- and settles back as it does
//     from rest. The rest after a stop aims, or not, over its own distance,
//     as every move from rest does.
//
// Two limits the profile cannot keep by itself, both reachable only when the
// move in progress was faster than this profile -- a Gesture's own throw
// (servoMotionOverride()), or the row changed under the move:
//   - a speed over the cruise is taken as the cruise. That is the one step
//     in speed this planner makes;
//   - a stop never carries the Output past the recorded end it is heading
//     for. Every point of a ramp lies between two widths already inside the
//     component's band (ADR 0041) and a brake point past the end need not,
//     so the stop is planned from the speed that ends it on the end.
//
// A moving plan too short to ramp is the move from rest instead, never a
// snap: ServoTask writes a snap's target, not a brake point or an aim.
// -----------------------------------------------------------------------------
inline ServoMotionRamp servoMotionRetarget(uint16_t fromUs, float speedUsPerMs, uint16_t toUs, const ServoMotionProfile& profile, uint32_t startMs) {
    const ServoMotionRamp rest = servoMotionPlan(fromUs, toUs, profile, startMs);
    if (speedUsPerMs == 0.0f || !profile.calibrated || profile.hiUs <= profile.loUs || profile.throwMs < SERVO_THROW_MS_MIN) {
        return rest;
    }

    const float rampMs = servoMotionRampMs(profile);
    const float cruiseUsPerMs = servoMotionCruiseUsPerMs(profile, rampMs);
    const float accelUsPerMs2 = cruiseUsPerMs / rampMs;
    const bool rising = speedUsPerMs > 0.0f;
    float speed = rising ? speedUsPerMs : -speedUsPerMs;
    if (speed > cruiseUsPerMs) {
        speed = cruiseUsPerMs;
    }
    // How long ago, to the millisecond, a move from rest would have set off to
    // be going this fast now, and how far behind the Output. The distance is
    // the one that whole millisecond gives, so the plan passes through the
    // Output now rather than half a millisecond either side of it.
    long sinceMs = lroundf(speed / accelUsPerMs2);
    long behindUs = lroundf(0.5f * accelUsPerMs2 * (float)sinceMs * (float)sinceMs);

    const uint16_t aimUs = profile.easing == SERVO_EASE_OVERSHOOT ? servoMotionOvershootAim(fromUs, toUs, profile.loUs, profile.hiUs) : toUs;
    // behindUs is also how far the Output needs to stop: the move from rest
    // turned round covers it again slowing down.
    const int32_t aheadUs = rising ? (int32_t)aimUs - (int32_t)fromUs : (int32_t)fromUs - (int32_t)aimUs;
    const bool carriesOn = aheadUs >= (int32_t)behindUs;
    if (!carriesOn) {
        // Room to stop in: up to the recorded end it is heading for. Past it
        // already, there is none, and the move is the one from rest.
        const int32_t roomUs = rising ? (int32_t)profile.hiUs - (int32_t)fromUs : (int32_t)fromUs - (int32_t)profile.loUs;
        if (roomUs < 1) {
            return rest;
        }
        if (roomUs < (int32_t)behindUs) {
            // The fastest it can be going and still stop on the end, whole
            // milliseconds rounded down so the stop lands on it or short.
            sinceMs = lroundf(sqrtf(2.0f * (float)roomUs / accelUsPerMs2) - 0.5f);
            behindUs = lroundf(0.5f * accelUsPerMs2 * (float)sinceMs * (float)sinceMs);
        }
    }
    // Slow enough to stop inside a millisecond is at rest.
    if (sinceMs < 1 || behindUs < 1) {
        return rest;
    }
    const int32_t originUs = rising ? (int32_t)fromUs - (int32_t)behindUs : (int32_t)fromUs + (int32_t)behindUs;
    if (originUs < 0 || originUs > 0xFFFF) {
        return rest;
    }
    const int32_t stopUs = rising ? (int32_t)fromUs + (int32_t)behindUs : (int32_t)fromUs - (int32_t)behindUs;

    // The ramp this is the tail of is the plain one: `soft` shapes a start
    // from rest, and neither carrying on nor stopping is one.
    ServoMotionProfile plain = profile;
    plain.easing = SERVO_EASE_NONE;
    ServoMotionRamp plan = servoMotionPlan((uint16_t)originUs, carriesOn ? aimUs : (uint16_t)stopUs, plain, startMs - (uint32_t)sinceMs);
    if (plan.durationMs == 0) {
        return rest;
    }
    plan.settleUs = toUs;
    plan.restarts = !carriesOn;
    return plan;
}

// -----------------------------------------------------------------------------
// servoMotionSettles()
// The second half of an overshoot, or of a move that had to stop first. A
// plan that settles has ended at its aim, or where it stopped, rather than
// its target; ServoTask chains the move to the target
// (servoMotionSettleBack()) when the first plan arrives.
// -----------------------------------------------------------------------------
inline bool servoMotionSettles(const ServoMotionRamp& ramp) {
    return ramp.settleUs != ramp.toUs;
}

// -----------------------------------------------------------------------------
// servoMotionSettleBack()
// The move from where a plan that settles arrived to its target:
// servoMotionPlan() again, from rest.
//
// From an overshoot's aim it is shaped `none`, because it is the settle and
// not a new move with a character of its own. From a stop (`restarts`) it is
// the rest of the move, and a start from rest like any other, so it has the
// profile's own ease -- an overshoot of its own included.
//
// The profile is the row's, as ServoTask reads it on arrival: a Gesture's
// throw or ease shaped the plan that arrived and not this one.
// -----------------------------------------------------------------------------
inline ServoMotionRamp servoMotionSettleBack(const ServoMotionRamp& arrived, const ServoMotionProfile& profile, uint32_t nowMs) {
    ServoMotionProfile settle = profile;
    if (!arrived.restarts) {
        settle.easing = SERVO_EASE_NONE;
    }
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
