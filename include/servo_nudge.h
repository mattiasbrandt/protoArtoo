// =============================================================================
// include/servo_nudge.h
//
// Find by Moving (ADR 0050, #363): the bounded, symmetric nudge a discovery run
// makes on an unclaimed Servo Output, so a builder can watch which Part twitches
// and say "that one".
//
// The nudge is planned from the width on the pin and from nothing else. A
// ServoCommand carries no target for it, so nothing can ask for a big one:
// ServoTask computes the pair here, about where the Output already is. An
// unclaimed Output is almost always an uncalibrated one, whose first move is a
// jump rather than a ramp (ADR 0052), so the pair is small, it lies about the
// current width, and it never reaches for a recorded end that may not exist.
//
// The pair is held inside the cautious band (SERVO_BAND_STD, 1000-2000 us) by
// shifting it inward as one, never by clipping one side: a nudge that could
// only go one way from the top of the band would read as a move to an end. A
// start that is already outside the band is not nudged at all -- shifting the
// pair in from there would be a move of hundreds of microseconds on a linkage
// nobody has measured, which is the jump ADR 0050 refuses.
//
// Pure: no FreeRTOS, no Arduino, no clock -- ServoTask drives the legs.
// =============================================================================
#pragma once

#include <stdint.h>

#include "servo_output_row.h"  // ServoPulseBand, SERVO_BAND_STD

// +-10% of the cautious band's span, each side of the current width: the
// reference project's Small Nudge, "the one to play with real servos armed"
// (r2d2-astromech-simulator v1.79.0).
constexpr uint16_t SERVO_NUDGE_AMPLITUDE_US = (SERVO_BAND_STD.hi - SERVO_BAND_STD.lo) / 10;

// The legs of a nudge, in the order ServoTask drives them: out to the upper
// side, across to the lower side, and back to the width it started from.
constexpr uint8_t SERVO_NUDGE_LEG_COUNT = 3;

struct ServoNudgePlan {
    uint16_t homeUs;  // where the nudge returns to: the width it started from
    uint16_t loUs;    // the lower side of the pair
    uint16_t hiUs;    // the upper side of the pair
};

// -----------------------------------------------------------------------------
// servoNudgePlan()
// The pair amplitudeUs each side of nowUs, shifted inward as one until both
// sides lie inside band, so the two stay exactly 2 * amplitudeUs apart. False,
// with *out untouched, when nowUs is outside the band or the band is too narrow
// to hold the pair at all.
// -----------------------------------------------------------------------------
inline bool servoNudgePlan(uint16_t nowUs, ServoPulseBand band, uint16_t amplitudeUs,
                           ServoNudgePlan* out) {
    if (out == nullptr || band.hi < band.lo || nowUs < band.lo || nowUs > band.hi) {
        return false;
    }
    if ((uint32_t)amplitudeUs * 2u > (uint32_t)(band.hi - band.lo)) {
        return false;
    }
    int32_t lo = (int32_t)nowUs - (int32_t)amplitudeUs;
    int32_t hi = (int32_t)nowUs + (int32_t)amplitudeUs;
    if (lo < (int32_t)band.lo) {
        hi += (int32_t)band.lo - lo;
        lo = band.lo;
    } else if (hi > (int32_t)band.hi) {
        lo -= hi - (int32_t)band.hi;
        hi = band.hi;
    }
    out->homeUs = nowUs;
    out->loUs = (uint16_t)lo;
    out->hiUs = (uint16_t)hi;
    return true;
}

// -----------------------------------------------------------------------------
// servoNudgeLegTarget()
// Where leg 1..SERVO_NUDGE_LEG_COUNT of the nudge ends. Any other leg number is
// the return, so a caller that has run off the end is sent home, never out.
// -----------------------------------------------------------------------------
inline uint16_t servoNudgeLegTarget(const ServoNudgePlan& plan, uint8_t leg) {
    switch (leg) {
        case 1:
            return plan.hiUs;
        case 2:
            return plan.loUs;
        default:
            return plan.homeUs;
    }
}
