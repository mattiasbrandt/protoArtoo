// =============================================================================
// include/servo_motion_ramp.h
//
// How a Servo Output gets from where it is to where it was sent, in time
// (ADR 0052, #354).
//
// A Motion Profile stores two times: how long a full throw takes, and how long
// the move spends getting up to speed. The rate is derived from the recorded
// Endpoint Pair and never stored. So this header takes the pair's span and the
// two times and plans a trapezoid: accelerate for the profile's own ramp time,
// cruise at the speed that makes a full throw take exactly throw_ms, and slow
// down the same way. A move shorter than two ramps never reaches that speed, so
// it is a triangle -- the same acceleration, turned round half way.
//
// A partial move therefore takes less time than a full throw, but not
// proportionally less: the ramps at each end are paid whatever the distance.
// That is the reference project's hard-won lesson, that acceleration and not
// speed is what binds (#287), and it is why a time is stored rather than a rate.
//
// The profile is the Output's and nothing else's. No command carries a time, so
// a Body Step, an RC toggle and a browser move all read the same row; only a
// Gesture may override it (ADR 0049), and none exists yet.
//
// Pure: no FreeRTOS, no Arduino, no clock of its own -- ServoTask passes now in.
// =============================================================================
#pragma once

#include <math.h>
#include <stdint.h>

#include "servo_output_row.h"  // SERVO_THROW_MS_MIN - one ServoTask frame

struct ServoMotionRamp {
    uint16_t fromUs;
    uint16_t toUs;
    uint32_t startMs;
    uint16_t durationMs;  // 0 = a snap: toUs is where the output is, at once
    uint16_t rampMs;      // time spent getting up to speed, and slowing down
};

// -----------------------------------------------------------------------------
// servoMotionPlan()
// Plan a move from fromUs to toUs on an output whose Endpoint Pair spans spanUs.
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
inline ServoMotionRamp servoMotionPlan(uint16_t fromUs, uint16_t toUs, uint16_t spanUs,
                                       uint16_t throwMs, uint16_t accelMs, bool calibrated,
                                       uint32_t startMs) {
    ServoMotionRamp plan = {fromUs, toUs, startMs, 0, 0};
    if (!calibrated || spanUs == 0 || fromUs == toUs || throwMs < SERVO_THROW_MS_MIN) {
        return plan;
    }

    const float fullThrowMs = (float)throwMs;
    // A ramp longer than half the throw would leave no time to slow down, so
    // the profile's ramp is held to half -- a full throw is then a triangle and
    // still takes exactly throw_ms.
    float rampMs = (float)(accelMs == 0 ? 1 : accelMs);
    if (rampMs > fullThrowMs / 2.0f) {
        rampMs = fullThrowMs / 2.0f;
    }
    const float cruiseUsPerMs = (float)spanUs / (fullThrowMs - rampMs);
    const float accelUsPerMs2 = cruiseUsPerMs / rampMs;

    const float distanceUs = (float)(toUs > fromUs ? toUs - fromUs : fromUs - toUs);
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
    if (t < rampMs) {
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
