// =============================================================================
// include/servo_motion_ramp.h
//
// How a Servo Output gets from where it is to where it was sent, in time
// (ADR 0052, #354).
//
// A Motion Profile stores two times: how long a full throw takes, and how long
// the move spends getting up to speed. The rate is derived from the recorded
// Endpoint Pair and never stored. So this header takes the pair's two ends and
// the two times and plans a trapezoid: accelerate for the profile's own ramp time,
// cruise at the speed that makes a full throw take exactly throw_ms, and slow
// down the same way. A move shorter than two ramps never reaches that speed, so
// it is a triangle -- the same acceleration, turned round half way.
//
// A partial move therefore takes less time than a full throw, but not
// proportionally less: the ramps at each end are paid whatever the distance.
// That is the reference project's hard-won lesson, that acceleration and not
// speed is what binds (#287), and it is why a time is stored rather than a rate.
//
// The profile is the Output's and nothing else's. No command carries a time or
// a shape, so a Body Step, an RC toggle and a browser move all read the same
// row; only a Gesture may override it (ADR 0049), for the one move it asks
// for, through servoMotionOverride() below.
//
// The third thing on the profile is the ease, the shape of the move (ADR 0052):
//
//   none       the trapezoid above, and it stops dead on the number;
//   soft       the acceleration itself comes in: the speed rises along a
//              smoothstep instead of a straight line, so the move breathes into
//              motion rather than stepping into it. The smoothstep covers the
//              same distance over the ramp as the straight line does, so a soft
//              move ends where and when a `none` move would;
//   overshoot  the move aims a twelfth past its target and settles back, but
//              only on a move worth more than an eighth of travel, and never
//              past the recorded ends. It is decided when the target is set, by
//              changing the aim -- the planner lays the move out to the aim and
//              records the target as where it settles, and ServoTask plans the
//              way back (servoMotionSettleBack()) once the aim is reached.
//
// Both rules are r2d2-astromech-simulator v1.79.0's
// (arduino/MaestroPCA/src/MaestroPCA.cpp:318 and :572), taken as rules: that
// code works in quarter-microseconds on a Maestro tick, and this is a planned
// ramp in microseconds and milliseconds.
//
// A new target can arrive while the Output is moving -- a puppet string sends
// one every frame the stick moves (#442), and a sequence step can land part
// way through a move. The move it starts keeps the speed the Output has
// (servoMotionRetarget()): it carries on and speeds up to the cruise if the
// target is ahead with room to slow down for it, and otherwise slows to a stop
// at the profile's acceleration and goes on from rest. Nothing here changes
// for it: a move already at speed is the tail of a move from rest at the same
// acceleration that set off a little earlier from a little behind, so it is a
// plan like any other, with its start in the past. servoMotionSpeedAt() below
// is what reads that speed off the move being replaced.
//
// The planner itself -- the two structs, the overshoot aim, servoMotionPlan(),
// the retarget and the settle back -- is GENERATED from docs/servo-motion.yaml into
// include/servo_motion_model.h, and from the same declaration into
// data/servo_motion.js, so the Rehearsal times a move with the arithmetic this
// firmware moves it with rather than with a copy of it (#287 specific 11,
// #439). Edit the declaration and run tools/generate_servo_motion.py; what stays
// written by hand here is what reads a row and what follows a plan in time.
// Only ServoTask follows a plan in time: the browser asks how long a move from
// rest takes (servoMotionArrivalMs()) and never where a move is or how fast,
// so the evaluators below stay written here for ServoTask alone. That is a
// choice about these functions, not a rule the outputs keep: the generator has
// no per-output switch, so everything declared in docs/servo-motion.yaml ships
// in data/servo_motion.js, servoMotionRetarget() and its two helpers included,
// although no page calls them.
//
// Pure: no FreeRTOS, no Arduino, no clock of its own -- ServoTask passes now in.
// =============================================================================
#pragma once

#include <math.h>
#include <stdint.h>

#include "servo_motion_model.h"  // ServoMotionProfile, ServoMotionRamp, servoMotionPlan() - generated
#include "servo_output_row.h"    // servoOutputLowUs(), servoOutputEffectiveEasing()

// -----------------------------------------------------------------------------
// servoMotionOverride()
// A Gesture's speed and easing, where it states them, in place of the Output's
// own for one move (ADR 0049, #438). `throwMs` is a full throw's time and 0
// means the Output's own; `easingPlusOne` is a ServoEasing + 1 and 0 means the
// Output's own. What the Gesture leaves unsaid is the Output's, so a door
// keeps its acceleration whatever speed a Gesture asks of it.
//
// An overshoot still never passes the recorded ends: on an Output nobody has
// measured it degrades to none, exactly as the Output's own overshoot does
// (servoOutputEffectiveEasing(), GLOSSARY.md "Motion Profile").
// -----------------------------------------------------------------------------
inline void servoMotionOverride(ServoMotionProfile* profile, uint16_t throwMs, uint8_t easingPlusOne) {
    if (profile == nullptr) return;
    if (throwMs != 0) profile->throwMs = throwMs;
    if (easingPlusOne != 0 && easingPlusOne <= SERVO_EASE_COUNT) {
        const ServoEasing asked = (ServoEasing)(easingPlusOne - 1);
        profile->easing = (asked == SERVO_EASE_OVERSHOOT && !profile->calibrated) ? SERVO_EASE_NONE : asked;
    }
}

// -----------------------------------------------------------------------------
// servoMotionProfileOf()
// The profile a row moves by. The ease comes through
// servoOutputEffectiveEasing() and from nowhere else, so an overshoot on an
// Output nobody has measured degrades to `none` in exactly one place.
// -----------------------------------------------------------------------------
inline ServoMotionProfile servoMotionProfileOf(const ServoOutputRow& row) {
    ServoMotionProfile profile = {};
    profile.loUs = servoOutputLowUs(row);
    profile.hiUs = servoOutputHighUs(row);
    profile.throwMs = row.throw_ms;
    profile.accelMs = row.accel_ms;
    profile.easing = servoOutputEffectiveEasing(row);
    profile.calibrated = row.calibrated;
    return profile;
}

// -----------------------------------------------------------------------------
// servoMotionArrived()
// True once the move is over. Unsigned subtraction, so a move that spans the
// millis() rollover still ends when it should.
// -----------------------------------------------------------------------------
inline bool servoMotionArrived(const ServoMotionRamp& ramp, uint32_t nowMs) {
    return ramp.durationMs == 0 || (uint32_t)(nowMs - ramp.startMs) >= ramp.durationMs;
}

// -----------------------------------------------------------------------------
// servoMotionDistanceUs() / servoMotionPeakUsPerMs()
// How far a plan goes, and the cruise speed re-derived from its rounded
// duration and ramp. One derivation for the position and the speed below, so
// the speed a retarget starts with is the slope of the positions written.
// -----------------------------------------------------------------------------
inline float servoMotionDistanceUs(const ServoMotionRamp& ramp) {
    return (float)(ramp.toUs > ramp.fromUs ? ramp.toUs - ramp.fromUs : ramp.fromUs - ramp.toUs);
}

inline float servoMotionPeakUsPerMs(const ServoMotionRamp& ramp) {
    return servoMotionDistanceUs(ramp) / ((float)ramp.durationMs - (float)ramp.rampMs);
}

// -----------------------------------------------------------------------------
// servoMotionPositionAt()
// Where the output should be at nowMs: exactly fromUs at the start, exactly toUs
// from the moment the move is over, and on the trapezoid in between.
//
// The cruise speed is re-derived from the rounded duration and ramp, so the three
// pieces meet without a step: d = peak * (duration - ramp) holds for both the
// trapezoid and the triangle (servoMotionPeakUsPerMs()).
//
// A plan with a moving start needs nothing of its own here: its startMs is in
// the past and its fromUs behind the Output, and it is only ever read from the
// moment it was planned on (servoMotionRetarget()).
//
// A soft start replaces only the speeding-up piece. Its speed is
// peak * (3s^2 - 2s^3) for s = t / ramp, which starts and ends with no
// acceleration at all, and whose distance, peak * ramp * (s^3 - s^4 / 2), comes
// to peak * ramp / 2 at s = 1 -- exactly the straight ramp's. So the piece after
// it starts from the same place at the same speed, and the move still ends at
// the same moment.
// -----------------------------------------------------------------------------
inline uint16_t servoMotionPositionAt(const ServoMotionRamp& ramp, uint32_t nowMs) {
    if (servoMotionArrived(ramp, nowMs)) {
        return ramp.toUs;
    }
    const float t = (float)(uint32_t)(nowMs - ramp.startMs);
    const float total = (float)ramp.durationMs;
    const float rampMs = (float)ramp.rampMs;
    const float distanceUs = servoMotionDistanceUs(ramp);
    const float peakUsPerMs = servoMotionPeakUsPerMs(ramp);
    const float accelUsPerMs2 = peakUsPerMs / rampMs;

    float travelled = 0.0f;
    if (t < rampMs && ramp.softStart) {
        const float s = t / rampMs;
        travelled = peakUsPerMs * rampMs * (s * s * s - 0.5f * s * s * s * s);
    } else if (t < rampMs) {
        travelled = 0.5f * accelUsPerMs2 * t * t;
    } else if (t < total - rampMs) {
        travelled = 0.5f * accelUsPerMs2 * rampMs * rampMs + peakUsPerMs * (t - rampMs);
    } else {
        const float left = total - t;
        travelled = distanceUs - 0.5f * accelUsPerMs2 * left * left;
    }
    if (travelled < 0.0f) {
        travelled = 0.0f;
    } else if (travelled > distanceUs) {
        travelled = distanceUs;
    }
    const long step = lroundf(travelled);
    return (ramp.toUs > ramp.fromUs) ? (uint16_t)(ramp.fromUs + step)
                                     : (uint16_t)(ramp.fromUs - step);
}

// -----------------------------------------------------------------------------
// servoMotionSpeedAt()
// How fast the output is going at nowMs on this plan, in microseconds a
// millisecond, signed: positive towards the higher width. Zero once the plan has
// arrived -- an overshoot's aim, a stop, a leg's dwell -- because the Output
// stands there before anything follows.
//
// The derivative of servoMotionPositionAt(), piece by piece, from the same
// re-derived peak: a soft start's speed is peak * (3s^2 - 2s^3), a straight
// ramp's grows as accel * t, the cruise is the peak, and the slowing down is
// the ramp turned round. It is what a move replacing this one starts with
// (servoMotionRetarget()), read at the same moment as its position.
// -----------------------------------------------------------------------------
inline float servoMotionSpeedAt(const ServoMotionRamp& ramp, uint32_t nowMs) {
    if (servoMotionArrived(ramp, nowMs)) {
        return 0.0f;
    }
    const float t = (float)(uint32_t)(nowMs - ramp.startMs);
    const float total = (float)ramp.durationMs;
    const float rampMs = (float)ramp.rampMs;
    const float peakUsPerMs = servoMotionPeakUsPerMs(ramp);

    float speed = peakUsPerMs;
    if (t < rampMs && ramp.softStart) {
        const float s = t / rampMs;
        speed = peakUsPerMs * s * s * (3.0f - 2.0f * s);
    } else if (t < rampMs) {
        speed = peakUsPerMs * t / rampMs;
    } else if (t > total - rampMs) {
        speed = peakUsPerMs * (total - t) / rampMs;
    }
    return (ramp.toUs > ramp.fromUs) ? speed : -speed;
}
