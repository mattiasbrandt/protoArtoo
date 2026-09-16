// =============================================================================
// include/servo_hold.h
//
// The calibration dial's hold on one Servo Output (ADR 0064, #364).
//
// A hold is taken by the first hold command for an Output, kept alive by every
// one after it, and ended by the builder (pulses off), by the halt edge, or by
// one of two firmware bounds: a short expiry when hold commands stop arriving
// (SERVO_HOLD_EXPIRY_MS) and an absolute ceiling from when the hold was taken
// (SERVO_HOLD_CEILING_MS), both in include/config.h. Refreshing a hold moves
// only the expiry. Nothing a page sends can move the ceiling, which is what
// makes it a bound rather than a liveness rule a dead browser could extend.
//
// Pure: no FreeRTOS, no Arduino, no clock. ServoTask owns the state and passes
// millis(); this header owns the rule, so the rule can be tested away from the
// 50 Hz loop that applies it -- the same shape as include/servo_nudge.h.
// =============================================================================
#pragma once

#include <stdint.h>

struct ServoHoldState {
    bool held;               // a dial has this Output
    uint32_t takenMs;        // when the hold was taken; the ceiling counts from here
    uint32_t lastCommandMs;  // when the last hold command arrived; the expiry counts from here
};

enum ServoHoldBound : uint8_t {
    SERVO_HOLD_BOUND_NONE = 0,  // the hold stands
    SERVO_HOLD_BOUND_EXPIRY,    // hold commands stopped arriving
    SERVO_HOLD_BOUND_CEILING,   // held for the most a dial may
};

// -----------------------------------------------------------------------------
// servoHoldCommand()
// A hold command has arrived. Takes the hold when none stands -- the ceiling
// starts here -- and otherwise refreshes the expiry and nothing else. Returns
// true when this command took the hold, so the caller can say so.
// -----------------------------------------------------------------------------
inline bool servoHoldCommand(ServoHoldState* hold, uint32_t nowMs) {
    if (hold == nullptr) {
        return false;
    }
    const bool taken = !hold->held;
    if (taken) {
        hold->held = true;
        hold->takenMs = nowMs;
    }
    hold->lastCommandMs = nowMs;
    return taken;
}

// -----------------------------------------------------------------------------
// servoHoldEnd()
// The hold is over, whoever ended it. The next hold command takes it afresh and
// both bounds start again (ADR 0064: resuming restarts both).
// -----------------------------------------------------------------------------
inline void servoHoldEnd(ServoHoldState* hold) {
    if (hold != nullptr) {
        hold->held = false;
    }
}

// -----------------------------------------------------------------------------
// servoHoldBoundHit()
// Which bound, if either, has fired on a standing hold. The ceiling is judged
// first: a hold that has run for the whole ceiling is reported as that even
// when its commands have also stopped, because that is the reason a surface
// should give. Unsigned subtraction handles millis() wrapping.
// -----------------------------------------------------------------------------
inline ServoHoldBound servoHoldBoundHit(const ServoHoldState& hold, uint32_t nowMs,
                                        uint32_t expiryMs, uint32_t ceilingMs) {
    if (!hold.held) {
        return SERVO_HOLD_BOUND_NONE;
    }
    if ((uint32_t)(nowMs - hold.takenMs) >= ceilingMs) {
        return SERVO_HOLD_BOUND_CEILING;
    }
    if ((uint32_t)(nowMs - hold.lastCommandMs) >= expiryMs) {
        return SERVO_HOLD_BOUND_EXPIRY;
    }
    return SERVO_HOLD_BOUND_NONE;
}
