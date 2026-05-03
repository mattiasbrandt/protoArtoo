// =============================================================================
// include/trigger_debounce.h
//
// Pure Tier 2 trigger debounce state and transition helpers.
// =============================================================================
#pragma once

#include <stdint.h>

#include "rc_mapping.h"

struct TriggerDebounceState {
    bool lastPressed;
    RcSwitchState lastSwitchState;
    RcSwitchState pendingSwitchState;
    bool switchStateInit;
    uint8_t pendingCount;
    uint32_t lastEdgeMs;
};

struct TriggerDebounceResult {
    bool fired;
    bool pressed;
};

inline TriggerDebounceResult triggerDebounceDigital(TriggerDebounceState* state, bool pressed) {
    TriggerDebounceResult result = {};
    if (state == nullptr || pressed == state->lastPressed) {
        return result;
    }

    state->lastPressed = pressed;
    result.fired = true;
    result.pressed = pressed;
    return result;
}

inline TriggerDebounceResult triggerDebounceAnalog(TriggerDebounceState* state,
                                                   const RcTriggerBinding& binding,
                                                   int rawValue, uint32_t nowMs,
                                                   uint8_t confirmFrames,
                                                   uint32_t oneShotDebounceMs) {
    TriggerDebounceResult result = {};
    if (state == nullptr || binding.target == ROBOT_ACTION_NONE ||
        binding.source == RC_BINDING_NONE || !robotActionValidForTier2(binding.target) ||
        !robotActionIsButton(binding.target)) {
        return result;
    }

    RcSwitchState switchState = rcTriggerToSwitchState(rawValue, binding);
    if (switchState == RC_SWITCH_INVALID) {
        return result;
    }

    if (!state->switchStateInit) {
        state->lastSwitchState = switchState;
        state->pendingSwitchState = switchState;
        state->switchStateInit = true;
        state->pendingCount = 0;
        return result;
    }

    if (switchState == state->lastSwitchState) {
        state->pendingCount = 0;
        state->pendingSwitchState = switchState;
        return result;
    }

    if (state->pendingCount == 0 || state->pendingSwitchState != switchState) {
        state->pendingSwitchState = switchState;
        state->pendingCount = 1;
        return result;
    }

    state->pendingCount++;
    if (state->pendingCount < confirmFrames) {
        return result;
    }

    state->pendingCount = 0;
    state->lastSwitchState = switchState;

    if (robotActionIsOneShotButton(binding.target)) {
        if ((uint32_t)(nowMs - state->lastEdgeMs) < oneShotDebounceMs) {
            return result;
        }
        state->lastEdgeMs = nowMs;
        state->lastPressed = true;
        result.fired = true;
        result.pressed = true;
        return result;
    }

    state->lastPressed = switchState == RC_SWITCH_HIGH;
    result.fired = true;
    result.pressed = state->lastPressed;
    return result;
}
