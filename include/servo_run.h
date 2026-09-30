// =============================================================================
// include/servo_run.h
//
// A Find by Moving run's hold on a free Servo Output (ADR 0050, ADR 0064;
// operator, 2026-09-30 on #411: "Pulse free Outputs in a run").
//
// An Output with no Part on it is free, and a free Output is never ticked
// wired, so ServoTask puts no pulse on it at start (CONTEXT.md "Wiring").
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
// run may take, and the word a run's Output goes limp with.
//
// Pure: no FreeRTOS, no Arduino, no clock - ServoTask and the servo route read
// the facts and pass them in, the same shape as include/servo_hold.h.
// =============================================================================
#pragma once

#include <stdint.h>

#include "robot_state.h"  // ServoLimpReason
#include "servo_hold.h"   // ServoHoldBound

// What decides whether a run may take an Output, each read where it lives.
struct ServoRunTakeInputs {
    bool drivenNow;      // ServoTask drives it this boot: it is someone's already
    bool wiredAtStart;   // its wired tick at start - a Part's Output waiting on nothing
    bool litAtStart;     // LEDC was kept off its pin at start for a light (output_wire.h)
    bool lightNow;       // its wire names a Light Type now
    uint8_t partCount;   // Parts on its row now
    bool ledcReady;      // the LEDC timer came up, so a channel can be attached
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
    return in.ledcReady && !in.drivenNow && !in.wiredAtStart && !in.litAtStart && !in.lightNow &&
           in.partCount == 0;
}

// -----------------------------------------------------------------------------
// servoRunArmLive()
// Whether ServoTask drives an arm right now: enabled since start, or a free
// Output a run holds. Every path that writes a pulse and every path that takes
// one off asks this - the estop's and Sleep Mode's release of every Output
// included (ADR 0043) - so an Output a run is driving is let go by a halt
// exactly as an enabled one is, and never left pulsing because it was not
// enabled at start.
// -----------------------------------------------------------------------------
inline bool servoRunArmLive(bool enabledAtStart, bool runHeld) {
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
