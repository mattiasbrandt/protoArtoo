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
// row; only a Gesture may override it (ADR 0049), and none exists yet.
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
// Pure: no FreeRTOS, no Arduino, no clock of its own -- ServoTask passes now in.
// =============================================================================
#pragma once

#include <math.h>
#include <stdint.h>

#include "servo_output_row.h"  // SERVO_THROW_MS_MIN, ServoEasing, servoOutputEffectiveEasing()

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
    uint16_t loUs;        // the lower recorded end
    uint16_t hiUs;        // the higher recorded end
    uint16_t throwMs;     // how long a full throw takes
    uint16_t accelMs;     // how long the move spends getting up to speed
    ServoEasing easing;   // the shape that runs: servoOutputEffectiveEasing()
    bool calibrated;      // somebody measured the ends against the linkage
};

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
// rather than one pointing back the way the move came.
// -----------------------------------------------------------------------------
inline uint16_t servoMotionOvershootAim(uint16_t fromUs, uint16_t toUs, uint16_t loUs,
                                        uint16_t hiUs) {
    const int32_t delta = (int32_t)toUs - (int32_t)fromUs;
    const int32_t distance = delta < 0 ? -delta : delta;
    if (hiUs <= loUs || distance <= (int32_t)(hiUs - loUs) / 8) {
        return toUs;
    }
    const int32_t over = distance / 12;
    int32_t aim = (int32_t)toUs + (delta > 0 ? over : -over);
    if (aim < (int32_t)loUs) {
        aim = loUs;
    }
    if (aim > (int32_t)hiUs) {
        aim = hiUs;
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
inline ServoMotionRamp servoMotionPlan(uint16_t fromUs, uint16_t toUs,
                                       const ServoMotionProfile& profile, uint32_t startMs) {
    ServoMotionRamp plan = {};
    plan.startMs = startMs;
    plan.fromUs = fromUs;
    plan.toUs = toUs;
    plan.settleUs = toUs;
    const uint16_t spanUs = profile.hiUs > profile.loUs ? profile.hiUs - profile.loUs : 0;
    if (!profile.calibrated || spanUs == 0 || fromUs == toUs ||
        profile.throwMs < SERVO_THROW_MS_MIN) {
        return plan;
    }

    // Decided here, when the target is set: an overshoot changes where this
    // plan ends and nothing else about it.
    const uint16_t aimUs = profile.easing == SERVO_EASE_OVERSHOOT
                               ? servoMotionOvershootAim(fromUs, toUs, profile.loUs, profile.hiUs)
                               : toUs;

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
    plan.durationMs = (roundedDuration > 0xFFFF) ? (uint16_t)0xFFFF : (uint16_t)roundedDuration;
    long roundedRamp = lroundf(usedRampMs);
    if (roundedRamp < 1) {
        roundedRamp = 1;
    }
    if (roundedRamp > plan.durationMs / 2) {
        roundedRamp = plan.durationMs / 2;
    }
    plan.rampMs = (uint16_t)roundedRamp;
    return plan;
}

// -----------------------------------------------------------------------------
// servoMotionSettles() / servoMotionSettleBack()
// The second half of an overshoot. A plan that settles has ended at its aim
// rather than its target; the way back is an ordinary move from the aim to the
// target, shaped `none`, because it is the settle and not a new move with a
// character of its own. ServoTask chains it when the first plan arrives.
// -----------------------------------------------------------------------------
inline bool servoMotionSettles(const ServoMotionRamp& ramp) {
    return ramp.settleUs != ramp.toUs;
}

inline ServoMotionRamp servoMotionSettleBack(const ServoMotionRamp& arrived,
                                             const ServoMotionProfile& profile, uint32_t nowMs) {
    ServoMotionProfile settle = profile;
    settle.easing = SERVO_EASE_NONE;
    return servoMotionPlan(arrived.toUs, arrived.settleUs, settle, nowMs);
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
// servoMotionPositionAt()
// Where the output should be at nowMs: exactly fromUs at the start, exactly toUs
// from the moment the move is over, and on the trapezoid in between.
//
// The cruise speed is re-derived from the rounded duration and ramp, so the three
// pieces meet without a step: d = peak * (duration - ramp) holds for both the
// trapezoid and the triangle.
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
    const float distanceUs =
        (float)(ramp.toUs > ramp.fromUs ? ramp.toUs - ramp.fromUs : ramp.fromUs - ramp.toUs);
    const float peakUsPerMs = distanceUs / (total - rampMs);
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
