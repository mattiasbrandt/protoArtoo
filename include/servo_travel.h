// =============================================================================
// include/servo_travel.h
//
// A Part run through its travel and back (ADR 0063, #352): the one command a
// deliberate press on a body view sends, so a builder pointing at a door on a
// picture of their droid can watch that door open, close and come back.
//
// It is the SAME three-leg out-and-back a Find by Moving nudge makes
// (include/servo_nudge.h), and ServoTask drives both through one machine. What
// differs is where the two ends are, and the difference is the whole of why
// there are two headers:
//
//   a nudge is planned from the width on the pin, deliberately small, and never
//   reaches for a recorded end -- because the Output it twitches is unclaimed
//   and therefore almost always unmeasured;
//
//   a travel goes to the recorded ends and nowhere else -- because the Output
//   it moves drives a known Part, so its Endpoint Pair is exactly what the
//   builder wants to see used.
//
// The pair is DIRECTIONAL and stays that way. `open` is whichever width the
// builder recorded as open, larger or smaller than close (ADR 0041), so the
// legs are "open it, close it, put it back" rather than "go high, go low" --
// and a reversed linkage runs the same travel with the same words. Nothing here
// sorts the pair, which is how the invert flag ADR 0041 refuses stays refused.
//
// An Output with no recorded travel has none to run: an unmeasured row's two
// ends are whatever it was stored with, so driving to them would be a move of
// hundreds of microseconds on a linkage nobody has measured. The caller checks
// `calibrated` and this refuses a pair whose ends are the same width, which is
// a travel of zero and nothing to watch.
//
// No band is applied here. Every recorded end is already inside the component's
// band -- ADR 0041 puts that clamp at every door onto a row -- and each leg goes
// through resolveArmPulse() on the way to the pin anyway, so a second clamp here
// would be a rule in two places.
//
// Pure: no FreeRTOS, no Arduino, no clock -- ServoTask drives the legs.
// =============================================================================
#pragma once

#include <stdint.h>

// The legs of a travel, in the order ServoTask drives them: out to the open
// end, across to the close end, and back to the width it started from. The same
// three a nudge has, because it is the same motion over a different pair.
constexpr uint8_t SERVO_TRAVEL_LEG_COUNT = 3;

struct ServoTravelPlan {
    uint16_t homeUs;   // where the travel returns to: the width it started from
    uint16_t openUs;   // the end the builder recorded as open
    uint16_t closeUs;  // the end the builder recorded as close
};

// -----------------------------------------------------------------------------
// servoTravelPlan()
// The travel from nowUs out to the recorded ends and back. False, with *out
// untouched, when the two ends are the same width -- there is no travel to run
// and nothing for a builder to watch.
// -----------------------------------------------------------------------------
inline bool servoTravelPlan(uint16_t nowUs, uint16_t openUs, uint16_t closeUs,
                            ServoTravelPlan* out) {
    if (out == nullptr || openUs == closeUs) {
        return false;
    }
    out->homeUs = nowUs;
    out->openUs = openUs;
    out->closeUs = closeUs;
    return true;
}

// -----------------------------------------------------------------------------
// servoTravelLegTarget()
// Where leg 1..SERVO_TRAVEL_LEG_COUNT of the travel ends. Any other leg number
// is the return, so a caller that has run off the end is sent home, never out --
// the same rule servoNudgeLegTarget() keeps for the same reason.
// -----------------------------------------------------------------------------
inline uint16_t servoTravelLegTarget(const ServoTravelPlan& plan, uint8_t leg) {
    switch (leg) {
        case 1:
            return plan.openUs;
        case 2:
            return plan.closeUs;
        default:
            return plan.homeUs;
    }
}
