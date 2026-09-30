// =============================================================================
// include/servo_release.h
//
// Output Release in normal operation (ADR 0043, #443): a Servo Output whose row
// sets a release time lets go that long after its move ARRIVES.
//
// Arrival, not the command, because ServoTask plans every move itself
// (ADR 0052) and so knows exactly when one ends: a ramp's last frame, an
// overshoot's settle back, the last leg of a nudge or a travel, or the same
// frame for a snap. The delay is then one honest number - how long to hold
// after getting there - and nothing has to guess how long the move took. (The
// dome fork's speed-scaled delay and its 30 s cap exist to stand in for that
// missing signal; ADR 0043 does not adopt them.)
//
// A pending release belongs to the move that armed it and to nothing later:
// whatever ends or starts a move on the Output cancels it, and so does every
// other way its pulse comes off (pulses off, a dial bound, estop, Sleep Mode).
// ServoTask calls servoReleaseCancel() from those paths.
//
// The calibration dial suppresses it (ADR 0064): an arrival under a hold arms
// nothing, and a hold being taken cancels what was pending. A release fires
// exactly when the builder has stopped moving a Part to look at it, and a
// struggling servo is only audible while it is being driven; the dial's own two
// bounds (include/servo_hold.h) are what end a held Output instead.
//
// Pure: no FreeRTOS, no Arduino, no clock. ServoTask owns the state and passes
// millis() - the same shape as include/servo_hold.h, so the rule is tested away
// from the 50 Hz loop that applies it.
// =============================================================================
#pragma once

#include <stdint.h>

#include "servo_output_row.h"  // SERVO_RELEASE_MS_NEVER

// One Output's pending release. `afterMs` is SERVO_RELEASE_MS_NEVER when none
// is pending, so a zero-filled state - every Output at boot - owes nothing.
struct ServoReleaseTimer {
    uint32_t arrivedMs;  // when the move that armed it arrived; the delay counts from here
    uint16_t afterMs;    // how long it holds after that, or SERVO_RELEASE_MS_NEVER
};

// -----------------------------------------------------------------------------
// servoReleaseCancel()
// Nothing is owed any more: a new command, a hold, or the pulse coming off by
// some other route.
// -----------------------------------------------------------------------------
inline void servoReleaseCancel(ServoReleaseTimer* timer) {
    if (timer != nullptr) {
        timer->afterMs = SERVO_RELEASE_MS_NEVER;
    }
}

// -----------------------------------------------------------------------------
// servoReleaseArm()
// A move has arrived. `releaseMs` is the Output's release time as its row
// holds it (never for a light, include/output_wire.h), and `held` whether a
// hold stands on it. Whatever was pending before is replaced, not added to:
// the release now counts from THIS arrival. Nothing is armed at "never" or
// under a hold. True when a release is now pending.
// -----------------------------------------------------------------------------
inline bool servoReleaseArm(ServoReleaseTimer* timer, uint32_t nowMs, uint16_t releaseMs,
                            bool held) {
    if (timer == nullptr) {
        return false;
    }
    servoReleaseCancel(timer);
    if (releaseMs == SERVO_RELEASE_MS_NEVER || held) {
        return false;
    }
    timer->arrivedMs = nowMs;
    timer->afterMs = releaseMs;
    return true;
}

// Whether a release is pending on the Output at all.
inline bool servoReleasePending(const ServoReleaseTimer& timer) {
    return timer.afterMs != SERVO_RELEASE_MS_NEVER;
}

// -----------------------------------------------------------------------------
// servoReleaseDue()
// Whether the pending release has come due. Judged on elapsed time with
// unsigned subtraction, so a millis() wrap between arrival and now changes
// nothing (servoHoldBoundHit() does the same).
// -----------------------------------------------------------------------------
inline bool servoReleaseDue(const ServoReleaseTimer& timer, uint32_t nowMs) {
    return servoReleasePending(timer) && (uint32_t)(nowMs - timer.arrivedMs) >= timer.afterMs;
}
