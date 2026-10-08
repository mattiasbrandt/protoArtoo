// =============================================================================
// include/servo_hold.h
//
// The calibration dial's hold on one Servo Output (ADR 0064, #364).
//
// A hold is taken by a press -- opening the dial, "take it again", a test sweep
// -- kept alive by the refreshes the page sends after it, and ended by the
// builder (pulses off), by the halt edge, or by one of two firmware bounds: a
// short expiry when hold commands stop arriving (SERVO_HOLD_EXPIRY_MS) and an
// absolute ceiling from when the hold was taken (SERVO_HOLD_CEILING_MS), both in
// include/config.h. Refreshing a hold moves only the expiry. Nothing a page
// sends can move the ceiling, which is what makes it a bound rather than a
// liveness rule a dead browser could extend.
//
// A refresh can only keep a hold that still stands (#417). Once a bound, the
// estop or pulses off has let go, a refresh takes nothing: only a press takes
// the Output again (ADR 0064, "resuming is one press"). Without that split a
// keepalive landing just after the ceiling fired took the Output afresh with a
// new ceiling, and one fired after an estop cleared took a servo nobody was
// watching.
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

// What a hold command asks for. A press takes the Output; the keepalive and
// the dial's own moves only refresh a hold that stands.
enum ServoHoldAsk : uint8_t {
    SERVO_HOLD_ASK_TAKE = 0,  // a press: opening the dial, take it again, a test sweep
    SERVO_HOLD_ASK_REFRESH,   // keep a standing hold alive, and nothing more
};

// What a hold command did.
enum ServoHoldOutcome : uint8_t {
    SERVO_HOLD_TAKEN = 0,   // no hold stood; this one took the Output
    SERVO_HOLD_REFRESHED,   // a hold stood; its expiry moved, its ceiling did not
    SERVO_HOLD_DROPPED,     // a refresh with no hold standing: nothing is taken
};

// -----------------------------------------------------------------------------
// servoHoldCommand()
// A hold command has arrived. A take with no hold standing takes it -- the
// ceiling starts here. Either ask on a standing hold refreshes the expiry and
// nothing else, so not even a press moves the ceiling. A refresh with no hold
// standing is dropped and changes nothing: the caller must not drive for it.
// -----------------------------------------------------------------------------
inline ServoHoldOutcome servoHoldCommand(ServoHoldState* hold, uint32_t nowMs, ServoHoldAsk ask) {
    if (hold == nullptr) {
        return SERVO_HOLD_DROPPED;
    }
    if (!hold->held) {
        if (ask != SERVO_HOLD_ASK_TAKE) {
            return SERVO_HOLD_DROPPED;
        }
        hold->held = true;
        hold->takenMs = nowMs;
        hold->lastCommandMs = nowMs;
        return SERVO_HOLD_TAKEN;
    }
    hold->lastCommandMs = nowMs;
    return SERVO_HOLD_REFRESHED;
}

// -----------------------------------------------------------------------------
// servoHoldEnd()
// The hold is over, whoever ended it. The next press takes it afresh and both
// bounds start again (ADR 0064: resuming restarts both); a refresh does not.
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
