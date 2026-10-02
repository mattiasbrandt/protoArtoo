// =============================================================================
// include/dome_bearing.h
//
// The Dome Bearing (ADR 0051 as amended 2026-09-30, CONTEXT.md "Dome Bearing",
// #445): where the dome BELIEVES it is pointing, as an angle from the droid's
// own front, clockwise viewed from above - so front is 0, and a quarter turn
// clockwise from above is 90. Never a heading in the room; there is no sensor
// for one.
//
// It is integrated, not measured. DomeTask writes every pulse the dome gets,
// and moves the belief by the turn that pulse makes at the calibrated rate over
// the measured time it was on the wire (domeBearingRateDegPerMs()). Two facts
// make that possible and nothing on the droid knows either, so the builder
// records them:
//
//   - the full-turn time, and the speed it was taken at, as a share of the
//     ESC's FULL pulse range - the scale of the pulse DomeTask writes after the
//     speed limit has scaled it, so a later change of the limit cannot make
//     the two disagree;
//   - which way a positive command turns the dome, seen from above.
//
// UNKNOWN IS ITS OWN VALUE, never 0 and never a number a reader can take
// without looking: a reading that is not believed carries NaN, so a lost belief
// cannot render as "pointing front". Only an estop, Sleep Mode and a boot make
// it unknown, and only the builder's "front is here" makes it believed again.
//
// Pure: no Arduino, no FreeRTOS, no RobotState. DomeTask owns the belief and
// the side effects (src/tasks/dome_task.cpp); robot_state.h reads it.
// =============================================================================
#pragma once

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "dome_math.h"  // domePulsesInOrder(), domeSpeedToPulseUs()

// -----------------------------------------------------------------------------
// Which way a positive command turns the dome, seen from above. Unset until the
// builder says: the sequence editor's left/right words assume a sign nobody
// has checked on a droid, and ShadowMD carries `invertDomeDirection` for the
// same reason. The words are the Gesture's spelling of the two directions
// (seqGestureDirectionToString()), so one droid has one word for each.
// -----------------------------------------------------------------------------
enum DomeTurnDirection : uint8_t {
    DOME_TURN_DIR_UNSET = 0,
    DOME_TURN_DIR_CW    = 1,
    DOME_TURN_DIR_CCW   = 2,
    DOME_TURN_DIR_COUNT = 3,
};

inline const char* domeTurnDirectionName(uint8_t direction) {
    switch (direction) {
        case DOME_TURN_DIR_UNSET:
            return "unset";
        case DOME_TURN_DIR_CW:
            return "cw";
        case DOME_TURN_DIR_CCW:
            return "ccw";
        default:
            return nullptr;
    }
}

// The full-turn time's bounds, in ms. 0 is "never timed"; ShadowMD's
// time360DomeTurn takes 2000..8000, and this is wider so a slow scan can be
// timed too.
constexpr uint16_t DOME_FULL_TURN_MS_MAX = 60000;

// The calibration as the integration reads it: the three recorded facts and
// the pulse set they are read against. Filled from DomeConfig by DomeTask.
struct DomeTurnCalibration {
    uint16_t neutralUs;
    uint16_t minPulseUs;
    uint16_t maxPulseUs;
    uint16_t fullTurnMs;   // 0 = never timed
    uint8_t  fullTurnPct;  // share of the ESC's full range it was timed at; 0 = never set
    uint8_t  positiveTurn; // DomeTurnDirection
};

// All three recorded, and a pulse set the mapping can use. Without every one
// of them a turn cannot be followed, so no belief can be held.
inline bool domeTurnCalibrated(const DomeTurnCalibration& cal) {
    return cal.fullTurnMs > 0 && cal.fullTurnPct > 0 && cal.fullTurnPct <= 100 &&
           (cal.positiveTurn == DOME_TURN_DIR_CW || cal.positiveTurn == DOME_TURN_DIR_CCW) &&
           domePulsesInOrder(cal.minPulseUs, cal.neutralUs, cal.maxPulseUs);
}

// -----------------------------------------------------------------------------
// The belief as a reader gets it. `believed` false is UNKNOWN, and then `deg`
// is NaN: there is no number, so a reader that skipped the flag would draw
// nothing rather than draw front.
// -----------------------------------------------------------------------------
struct DomeBearingReading {
    bool  believed;
    float deg;  // 0 <= deg < 360 while believed; NaN while unknown
};

// The state word every surface reads beside the number: `believed`, or
// `unknown` - the status document's `domeBearing` and the Console's field.
inline const char* domeBearingStateWord(const DomeBearingReading& reading) {
    return reading.believed ? "believed" : "unknown";
}

// The number as the status document and the Console write it: degrees to one
// decimal, 0.0..359.9, or `null` for an unknown bearing - never a number.
constexpr size_t DOME_BEARING_DEG_TEXT_MAX = 8;  // "359.9" or "null", and its terminator

inline void domeBearingFormatDeg(const DomeBearingReading& reading, char* buf, size_t bufSize) {
    if (!reading.believed || isnan(reading.deg)) {
        snprintf(buf, bufSize, "null");
        return;
    }
    // Rounded to tenths and wrapped, so 359.96 reads 0.0 rather than 360.0.
    long tenths = lroundf(reading.deg * 10.0f) % 3600L;
    if (tenths < 0) {
        tenths += 3600L;
    }
    snprintf(buf, bufSize, "%ld.%ld", tenths / 10L, tenths % 10L);
}

// 0 <= result < 360.
inline float domeBearingWrap(float deg) {
    float wrapped = fmodf(deg, 360.0f);
    if (wrapped < 0.0f) {
        wrapped += 360.0f;
    }
    // fmodf of a value a hair under a multiple of 360 can round up to 360.
    return (wrapped >= 360.0f) ? 0.0f : wrapped;
}

// The short way from `fromDeg` to `toDeg`: -180 < result <= 180, positive
// clockwise from above. ShadowMD's rule: under half a turn one way, else the
// other way round.
inline float domeBearingShortWay(float fromDeg, float toDeg) {
    float delta = domeBearingWrap(toDeg - fromDeg);
    if (delta > 180.0f) {
        delta -= 360.0f;
    }
    return delta;
}

// -----------------------------------------------------------------------------
// domeBearingRateDegPerMs()
// How fast the pulse on the wire turns the dome, in degrees per ms, positive
// clockwise from above. 0 when uncalibrated, at neutral, or with the pulse set
// out of order (then domeSpeedToPulseUs() writes neutral for every speed).
//
// The pulse is read as a share of the ESC's FULL half-range on its side of
// neutral - not of the speed limit - because that is the scale the full-turn
// speed was recorded on. A pulse half way from neutral to max, timed at 50,
// turns at exactly the calibrated rate; at 25 it turns at half of it. Linear,
// as the reference simulator integrates its throttle (`domeYaw +=
// (effDome()/127) * CFG.domeRate * dt`); an ESC's dead band is part of what
// "believed" admits.
// -----------------------------------------------------------------------------
inline float domeBearingRateDegPerMs(uint16_t pulseUs, const DomeTurnCalibration& cal) {
    if (!domeTurnCalibrated(cal)) {
        return 0.0f;
    }
    float share = 0.0f;
    if (pulseUs > cal.neutralUs && cal.maxPulseUs > cal.neutralUs) {
        share = (float)(pulseUs - cal.neutralUs) / (float)(cal.maxPulseUs - cal.neutralUs);
    } else if (pulseUs < cal.neutralUs && cal.neutralUs > cal.minPulseUs) {
        share = -(float)(cal.neutralUs - pulseUs) / (float)(cal.neutralUs - cal.minPulseUs);
    }
    const float timedShare = (float)cal.fullTurnPct / 100.0f;
    const float sign = (cal.positiveTurn == DOME_TURN_DIR_CW) ? 1.0f : -1.0f;
    return sign * (share / timedShare) * (360.0f / (float)cal.fullTurnMs);
}

// -----------------------------------------------------------------------------
// domeBearingFacingFrontDeg()
// The Dome Bearing at which a dome Part faces the droid's front - the one place
// a Part's `bearing_deg` and the Dome Bearing meet (CONTEXT.md, Flagged
// Ambiguities). Two frames, never mixed up:
//
//   - a Part's bearing is fixed on the dome, in the catalog's frame: 0 dead
//     astern, 180 dead ahead, clockwise from above (DROID_PART_BEARING_TENTHS,
//     include/droid_parts.h);
//   - the Dome Bearing is where the dome's own front points, from the droid's
//     front, clockwise from above.
//
// Turning the dome clockwise by B carries a Part at bearing b to b + B, and it
// faces front when that is dead ahead: B = 180 - b.
//
// CHECK VALUE, so a sign error is caught by reading rather than by driving:
// panel14 (P14, the Front PSI's, 202 degrees) faces front at a Dome Bearing of
// 338 - a short turn anticlockwise - and panel8 (P8, the Rear PSI's, 19
// degrees) at 161. A result that sends the Front PSI's panel round by 180 has
// the frames mixed up.
// -----------------------------------------------------------------------------
inline float domeBearingFacingFrontDeg(int16_t partBearingTenths) {
    return domeBearingWrap(180.0f - (float)partBearingTenths / 10.0f);
}

// -----------------------------------------------------------------------------
// domeBearingTurnPlan()
// A turn to a Dome Bearing, the ShadowMD way (`domeStopTurnTime =
// domeStartTurnTime + (angle / 360) * time360DomeTurn`): the short way round,
// at the speed the full turn was timed at, stopped on time.
//
// Two differences from ShadowMD. The turn starts from the BELIEVED bearing, not
// from an assumed home. And the time is worked out from the pulse that will
// actually go out: a speed limit below the timed speed caps the pulse, and the
// turn then runs longer at the slower rate rather than stopping short. The
// belief afterwards is whatever the integration says, never the target, so a
// slip shows as the next turn landing short and never as a readout claiming
// the target.
//
// `speed` is in DomeCommand units, before the limit scales it; `durationMs` 0
// means there is nothing to turn - already there, or no pulse could turn it (a
// limit of 0).
// -----------------------------------------------------------------------------
struct DomeTurnPlan {
    float    speed;
    uint32_t durationMs;
};

// Closer than this and the dome is already there: a turn shorter than one
// DomeTask tick could not be stopped on time anyway.
constexpr float DOME_BEARING_ARRIVED_DEG = 1.0f;

inline DomeTurnPlan domeBearingTurnPlan(float fromDeg, float toDeg, const DomeTurnCalibration& cal,
                                        uint8_t speedLimitPct) {
    DomeTurnPlan plan = {0.0f, 0};
    const float delta = domeBearingShortWay(fromDeg, toDeg);
    if (!domeTurnCalibrated(cal) || speedLimitPct == 0 || fabsf(delta) < DOME_BEARING_ARRIVED_DEG) {
        return plan;
    }
    // Clockwise from above, and which sign of command makes that.
    const bool clockwise = delta > 0.0f;
    const bool positiveIsClockwise = cal.positiveTurn == DOME_TURN_DIR_CW;
    const float sign = (clockwise == positiveIsClockwise) ? 1.0f : -1.0f;
    // The timed share of the full range, as a share of the limited range the
    // command speed is scaled into. Over 1 means the limit is below the timed
    // speed: the command is full, and the pulse is the limit's.
    float magnitude = (float)cal.fullTurnPct / (float)speedLimitPct;
    if (magnitude > 1.0f) {
        magnitude = 1.0f;
    }
    const float speed = sign * magnitude;
    const uint16_t pulseUs =
        domeSpeedToPulseUs(speed, cal.neutralUs, cal.minPulseUs, cal.maxPulseUs, speedLimitPct);
    const float rate = fabsf(domeBearingRateDegPerMs(pulseUs, cal));
    if (rate <= 0.0f) {
        return plan;
    }
    plan.speed = speed;
    plan.durationMs = (uint32_t)(fabsf(delta) / rate + 0.5f);
    return plan;
}
