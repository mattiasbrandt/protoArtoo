// =============================================================================
// include/servo_run.h
//
// A Find by Moving run's hold on a free Servo Output (ADR 0050, ADR 0064;
// operator, 2026-09-30 on #411: "Pulse free Outputs in a run").
//
// An Output with no Part on it is free, and a free Output is never ticked
// wired, so ServoTask puts no pulse on it at start (GLOSSARY.md "Wiring").
// A run looking for which wire moves a Part has to twitch exactly those
// Outputs, so for the length of the run the firmware takes a free servo Output
// the moment a nudge names it, and lets it go again when the run moves on.
//
// THE DIAL'S HOLD, NOT A SECOND ONE. A run's hold is a ServoHoldState driven by
// include/servo_hold.h and bounded by the same two numbers the dial's is
// (SERVO_HOLD_EXPIRY_MS, SERVO_HOLD_CEILING_MS; ADR 0064). A nudge is the
// arrival: the first nudge on a free Output takes it, a later one refreshes the
// expiry and never the ceiling, and when nudges stop - the run stepped on to the
// next Output, the builder stopped, the tab closed - the expiry lets it go
// within seconds. Nothing a page sends can move the ceiling. There is no
// keepalive: a nudge is over well inside the expiry.
//
// What the rule here adds is only what the dial never needed: WHICH Outputs a
// run may take, that it holds ONE at a time, what it lets go of when a nudge
// names another, the width it takes a free Output at, and the word a run's
// Output goes limp with.
//
// Pure: no FreeRTOS, no Arduino, no clock - ServoTask and the servo route read
// the facts and pass them in, the same shape as include/servo_hold.h.
// =============================================================================
#pragma once

#include <stdint.h>

#include "robot_state.h"       // ServoLimpReason
#include "servo_hold.h"        // ServoHoldBound
#include "servo_output_row.h"  // ServoPulseBand

// No Output is held by a run. A run holds at most one Output, so ServoTask
// keeps which one as a single slot (include/servo_backend.h) rather than a bit
// per Output: two held at once is not a state it can be in.
constexpr uint8_t SERVO_RUN_NONE = 0xFF;

// What decides whether a run may take an Output, each read where it lives.
struct ServoRunTakeInputs {
    bool drivenNow;      // ServoTask drives it this boot: it is someone's already
    bool wiredAtStart;   // its wired tick at start - a Part's Output waiting on nothing
    bool litAtStart;     // LEDC was kept off its pin at start for a light (output_wire.h)
    bool lightNow;       // its wire names a Light Type now
    uint8_t partCount;   // Parts on its row now
    bool backendReady;   // its backend can put a pulse on it: the LEDC timer came up, or the
                         // PCA9685 was chosen and is answering - so a channel can be attached
};

// -----------------------------------------------------------------------------
// servoRunMayTake()
// A run takes only a FREE servo Output: nothing drives it, no Part is on it,
// and nothing about its wire is a light's. An Output a Part is on moves through
// its Part, and a Part put on a free Output since the droid started waits for
// its restart (its tick follows the Part, and is read at start); a light's wire
// may have a strip on the pin, where a servo pulse and a WS2812B's data cannot
// share it. Every answer is "no" unless all of them say "free".
// -----------------------------------------------------------------------------
inline bool servoRunMayTake(const ServoRunTakeInputs& in) {
    return in.backendReady && !in.drivenNow && !in.wiredAtStart && !in.litAtStart && !in.lightNow &&
           in.partCount == 0;
}

// What a nudge on a free Output does to the run's hold.
enum ServoRunNudgeAct : uint8_t {
    SERVO_RUN_REFUSE = 0,  // the Output is not free: nothing is taken, and nothing nudged
    SERVO_RUN_KEEP,        // the run holds it already: the nudge refreshes the hold
    SERVO_RUN_TAKE,        // the run takes it now
};

struct ServoRunNudgeStep {
    ServoRunNudgeAct act;
    uint8_t letGo;  // the Output the run lets go of first, or SERVO_RUN_NONE
};

// -----------------------------------------------------------------------------
// servoRunOnNudge()
// A nudge has named the Output in `slot`, which the run holds or not
// (`heldSlot`, ServoTask's slot or SERVO_RUN_NONE), and which
// servoRunMayTake() says is free or not NOW - asked on every nudge, not only
// the first, because a Part or a Light Type can land on an Output while the
// run holds it (#411 slice 4).
//
//   not free   REFUSE; and if the run held it, it lets go of it: an Output a
//              Part is on moves through its Part, and a light's wire may have a
//              strip on the pin.
//   held       KEEP.
//   free       TAKE, and let go of the Output the run held before, so stepping
//              on to the next Output never leaves two free servos energized.
//              The run holds one Output, and the firmware is what keeps it at
//              one: the page that steps it on can die between two nudges.
// -----------------------------------------------------------------------------
inline ServoRunNudgeStep servoRunOnNudge(uint8_t heldSlot, uint8_t slot, bool mayTake) {
    if (!mayTake) {
        return {SERVO_RUN_REFUSE, heldSlot == slot ? slot : SERVO_RUN_NONE};
    }
    if (heldSlot == slot) {
        return {SERVO_RUN_KEEP, SERVO_RUN_NONE};
    }
    return {SERVO_RUN_TAKE, heldSlot};
}

// -----------------------------------------------------------------------------
// servoRunFirstWidthUs()
// The width a run takes a free Output at: its recorded centre, moved into the
// part of the band a nudge can be symmetric about - [lo + amplitude,
// hi - amplitude], 1100-1900 us for the cautious band.
//
// A recorded centre can sit anywhere the Output's component takes (up to
// 2500 us on an MG90S), and a nudge refuses to plan from outside the band
// (include/servo_nudge.h): taken there, the Output would sit pulsed and never
// twitch until the expiry let it go. At the band's own edge the nudge would
// shift its pair inward and read as a one-sided move to an end. So the centre
// is kept where it is inside the symmetric range - most rows record 1500 us -
// and clamped onto it otherwise. A band too narrow to hold a pair answers its
// middle, which the nudge then refuses as it refuses any such band.
// -----------------------------------------------------------------------------
inline uint16_t servoRunFirstWidthUs(uint16_t centreUs, ServoPulseBand band, uint16_t amplitudeUs) {
    if (band.hi < band.lo) {
        return band.lo;
    }
    if ((uint32_t)amplitudeUs * 2u > (uint32_t)(band.hi - band.lo)) {
        return (uint16_t)(band.lo + (band.hi - band.lo) / 2);
    }
    const uint16_t lo = (uint16_t)(band.lo + amplitudeUs);
    const uint16_t hi = (uint16_t)(band.hi - amplitudeUs);
    if (centreUs < lo) {
        return lo;
    }
    if (centreUs > hi) {
        return hi;
    }
    return centreUs;
}

// -----------------------------------------------------------------------------
// servoRunOutputLive()
// Whether ServoTask drives an Output right now: enabled since start, or a free
// Output a run holds. Every path that writes a pulse and every path that takes
// one off asks this - the estop's and Sleep Mode's release of every Output
// included (ADR 0043) - so an Output a run is driving is let go by a halt
// exactly as an enabled one is, and never left pulsing because it was not
// enabled at start.
// -----------------------------------------------------------------------------
inline bool servoRunOutputLive(bool enabledAtStart, bool runHeld) {
    return enabledAtStart || runHeld;
}

// -----------------------------------------------------------------------------
// servoRunLimpReason()
// Why an Output a bound let go is limp. A dial's Output says which bound fired,
// because the dial offers to take it again (ADR 0064). A run's Output goes back
// to what it was before the run - a free Output with no pulse, limp since the
// droid started - because nothing is waiting to take it again, and "the dial
// stopped asking" would be a sentence about something that never held it.
// -----------------------------------------------------------------------------
inline ServoLimpReason servoRunLimpReason(bool runHeld, ServoHoldBound bound) {
    if (runHeld) {
        return SERVO_LIMP_OFF;
    }
    return bound == SERVO_HOLD_BOUND_CEILING ? SERVO_LIMP_CEILING : SERVO_LIMP_EXPIRED;
}
