// =============================================================================
// include/rc_puppet.h
//
// A puppet string: an RC Channel that moves one Part in proportion to the stick
// (#442, ADR 0061).
//
// A string is an RC Map entry like any cue - the same trigger slot, the same
// (source, channel) and the same calibration - whose action is
// SERVO_ACTION_PUPPET_PART and whose payload is the Part's catalog id, never an
// Output Address, so a re-addressed Part keeps its string. Because it is an RC
// Map entry, a control is a string or a cue or a drive axis and never two of
// them: POST /api/rc/map refuses one (source, channel) twice, so there is no
// state in which a channel carries both roles to resolve.
//
// DEFLECTION IS A POSITION. Half the stick's travel is the Part's throw: from
// centre towards the binding's positive end, the Part goes from its close end
// to its open end in proportion, and the other half holds it closed. A released
// stick therefore commands the close end - a real position, never a
// de-energise - and a stick that is reversed on the radio, or a DS-650 button
// channel that idles high, is the binding's own `reverse`, not a flag on the
// Output. The lerp runs along the Output's directional Endpoint Pair
// (ADR 0041), so a reversed linkage needs nothing here either. No integrator:
// the target is a pure function of the stick, so there is nothing to drift.
//
// NOTHING MOVES UNTIL THE STICK DOES. A string commands nothing on the first
// frame it sees: it takes the stick where it is as a baseline and engages only
// once the stick has moved RC_PUPPET_PICKUP_PERMILLE away from it. So saving
// a string, booting with one, an estop clearing and the radio coming back after
// a gap move no Part by themselves - the builder's next touch of the stick is
// what takes the Part. Until then the Output is wherever its own boot behaviour
// or its last move left it.
//
// A BOUNDED RATE. A stick changes every SBUS frame, about every 7-14 ms, and a
// ServoCommand per frame would fill servoCmdQueue under a cue. A string sends a
// new target only when it has moved RC_PUPPET_STEP_PERMILLE (1% of the throw)
// from the last one sent, or reached an end, and never twice within
// RC_PUPPET_MIN_INTERVAL_MS, one ServoTask frame: at most 50 commands a second
// per string, and none while the stick is still. A target the queue refused is
// not recorded as sent, so it goes again on the next frame.
//
// Pure: no FreeRTOS, no Arduino, no clock. RCInputTask passes the frame's time
// in, so the rule can be read away from the 200 Hz loop that applies it - the
// same shape as include/servo_hold.h.
// =============================================================================
#pragma once

#include <stdint.h>
#include <string.h>

#include "droid_parts.h"       // DROID_PART_ID_MAX_LEN
#include "rc_action_types.h"   // RcTriggerBinding, applyRcTriggerCalibration()

// Whether a string on this (source, channel) can ever move its Part: an SBUS
// channel with an analog reading. PWM input runs no trigger slot at all (its
// dispatch clears the trigger count), and SBUS CH17/CH18 are on/off flags that
// applyRcAnalogCalibration() reads as 0 (rcBindingSupportsAnalog()). POST
// /api/rc/map refuses a string anywhere else rather than store one that sits
// still.
inline bool rcPuppetChannelCanMove(RcBindingSource source, uint8_t channel) {
    const RcBindingConfig binding = makeRcBindingConfig(source, channel, 0, 0, 0, 0, false);
    return (source == RC_BINDING_SBUS1 || source == RC_BINDING_SBUS2) &&
           rcBindingSupportsAnalog(binding);
}

// One full throw, in the unit a string reasons in: 0 is the close end, 1000 the
// open end. Integral, so two frames that read the same stick compare equal.
constexpr uint16_t RC_PUPPET_FULL_PERMILLE = 1000;

// How far the stick moves before a string sends again: 1% of the throw. A
// stick resting a hair off the last target leaves the Part within 1% of it.
constexpr uint16_t RC_PUPPET_STEP_PERMILLE = 10;

// How far the stick moves from where it was first seen before the string takes
// its Part: 5% of the throw, clear of the jitter a resting stick reports.
constexpr uint16_t RC_PUPPET_PICKUP_PERMILLE = 50;

// The least time between two targets from one string: one ServoTask frame.
constexpr uint32_t RC_PUPPET_MIN_INTERVAL_MS = 20;

// What one trigger slot's string is doing. Kept per slot by RCInputTask, beside
// the debounce state the same slot keeps for a cue.
struct RcPuppetState {
    // Which string this state is for. A slot's content changes when the RC Map
    // is saved, and a state left over from another channel or Part would hand
    // the new string an engagement it never earned.
    RcBindingSource source;
    uint8_t channel;
    char part[DROID_PART_ID_MAX_LEN + 1];

    bool seen;              // a frame has given this string its baseline
    bool engaged;           // the stick has moved past the pickup: it owns the Part
    bool sent;              // the queue has accepted a target since it engaged
    uint16_t baseline;      // where the stick was first seen, permille
    uint16_t sentPermille;  // the last target the queue accepted
    uint32_t sentMs;        // when it accepted it
    uint32_t seenMs;        // the last frame this string saw
};

// What a string asks for on one frame.
struct RcPuppetAsk {
    bool send;         // send `permille` as the Part's new target
    uint16_t permille;
};

// -----------------------------------------------------------------------------
// rcPuppetPermille()
// Where the stick puts the Part, on its half axis: the binding's calibration
// (centre, deadband, ends and reverse) gives -1..+1, the negative half and the
// deadband are the close end, and the positive half spans the throw. Rounded
// to the nearest permille.
// -----------------------------------------------------------------------------
inline uint16_t rcPuppetPermille(int raw, const RcTriggerBinding& binding) {
    const float value = applyRcTriggerCalibration(raw, binding, nullptr);
    if (value <= 0.0f) {
        return 0;
    }
    if (value >= 1.0f) {
        return RC_PUPPET_FULL_PERMILLE;
    }
    return (uint16_t)(value * (float)RC_PUPPET_FULL_PERMILLE + 0.5f);
}

// -----------------------------------------------------------------------------
// rcPuppetTargetUs()
// Where a permille of the throw lands on one Output's Endpoint Pair: that far
// from `close` towards `open`, read as stored. The pair is directional, so a
// reversed linkage (open < close) needs no flag and nothing here sorts it
// (ADR 0041). ServoTask clamps the result to the component's band, as it does
// every width it drives (resolveOutputPulse()).
// -----------------------------------------------------------------------------
inline uint16_t rcPuppetTargetUs(uint16_t openUs, uint16_t closeUs, uint16_t permille) {
    const int32_t span = (int32_t)openUs - (int32_t)closeUs;
    // Round half away from zero, so a reversed span rounds the same distance a
    // forward one does - seqBodyTargetUs()'s rule.
    const int32_t bias = (span >= 0) ? 500 : -500;
    const int32_t travelled = (span * (int32_t)permille + bias) / (int32_t)RC_PUPPET_FULL_PERMILLE;
    const int32_t target = (int32_t)closeUs + travelled;
    return (target < 0) ? 0 : (target > 0xFFFF) ? (uint16_t)0xFFFF : (uint16_t)target;
}

// -----------------------------------------------------------------------------
// rcPuppetStep()
// One frame of one string. `binding` is the slot's string; `permille` where
// the stick puts the Part now (rcPuppetPermille()); `mayMove` false while the
// estop is latched, when a string must let go exactly as it does over a gap,
// so the Part does not jump when the estop clears.
//
// `gapMs` is how long a string may see no frame before it lets go and picks
// its Part up again from a fresh baseline: the SBUS watchdog's timeout as the
// builder set it (drive.sbusTimeoutMs, 50-1000 ms), passed in by the caller,
// so the string lets go when the watchdog trips and never stays engaged
// through a failsafe the drive has already declared. Frames stop for a lost
// signal, a failsafe frame (never dispatched) or a receiver unplugged; the
// stick may be anywhere when they come back.
//
// Order matters and is the rule:
//   a different string in the slot, a gap, or no leave to move -> start over
//   first frame                       -> baseline, nothing sent
//   not engaged, inside the pickup    -> nothing sent
//   not engaged, past the pickup      -> engage, send where the stick is
//   engaged, same target, or too soon -> nothing sent
//   engaged, moved a step or at an end -> send
// The caller records an accepted send with rcPuppetSent(); one it could not
// send is asked for again on the next frame.
// -----------------------------------------------------------------------------
inline RcPuppetAsk rcPuppetStep(RcPuppetState* state, const RcTriggerBinding& binding,
                                uint16_t permille, uint32_t nowMs, uint32_t gapMs, bool mayMove) {
    RcPuppetAsk ask = {false, 0};
    if (state == nullptr) {
        return ask;
    }

    const bool sameString = state->source == binding.source && state->channel == binding.channel &&
                            strncmp(state->part, binding.marcduinoPayload, sizeof(state->part)) == 0;
    const bool gap = state->seen && (uint32_t)(nowMs - state->seenMs) > gapMs;
    if (!sameString || gap || !mayMove) {
        *state = {};
        state->source = binding.source;
        state->channel = binding.channel;
        strncpy(state->part, binding.marcduinoPayload, sizeof(state->part) - 1);
    }
    state->seenMs = nowMs;
    if (!mayMove) {
        return ask;
    }

    if (!state->seen) {
        state->seen = true;
        state->baseline = permille;
        return ask;
    }

    if (!state->engaged) {
        const uint16_t moved = (permille > state->baseline) ? (uint16_t)(permille - state->baseline)
                                                            : (uint16_t)(state->baseline - permille);
        if (moved < RC_PUPPET_PICKUP_PERMILLE) {
            return ask;
        }
        state->engaged = true;
        ask.send = true;
        ask.permille = permille;
        return ask;
    }

    // Engaged with nothing accepted yet - the pickup's target was refused -
    // asks again on every frame until one goes.
    if (state->sent && permille == state->sentPermille) {
        return ask;
    }
    if (state->sent && (uint32_t)(nowMs - state->sentMs) < RC_PUPPET_MIN_INTERVAL_MS) {
        return ask;
    }
    const uint16_t moved = (permille > state->sentPermille)
                               ? (uint16_t)(permille - state->sentPermille)
                               : (uint16_t)(state->sentPermille - permille);
    const bool atEnd = permille == 0 || permille == RC_PUPPET_FULL_PERMILLE;
    if (state->sent && moved < RC_PUPPET_STEP_PERMILLE && !atEnd) {
        return ask;
    }
    ask.send = true;
    ask.permille = permille;
    return ask;
}

// -----------------------------------------------------------------------------
// rcPuppetSent()
// servoCmdQueue accepted a string's target.
// -----------------------------------------------------------------------------
inline void rcPuppetSent(RcPuppetState* state, uint16_t permille, uint32_t nowMs) {
    if (state == nullptr) {
        return;
    }
    state->sent = true;
    state->sentPermille = permille;
    state->sentMs = nowMs;
}
