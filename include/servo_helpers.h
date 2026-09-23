// =============================================================================
// include/servo_helpers.h
//
// Pure helpers for servo arm ID mapping and enable-flag logic.
// No Arduino, no FreeRTOS, no queues  --  safe to include in native unit tests.
//
// Extracted from src/tasks/servo_task.cpp so the mapping and enable logic
// can be exercised without hardware dependencies.
// =============================================================================
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "config.h"    // PA_BOARD, the pin plan behind the channels below
#include "ledc_pwm.h"  // LedcChannel enum, LEDC_CH_MAX
// -----------------------------------------------------------------------------
// servo_arm_id_to_ledc_channel()
// Map armId to LEDC channel index.
//
//   0 -> LEDC_CH_ARM1
//   1 -> LEDC_CH_ARM2
//   2 -> LEDC_CH_AUX1
//   3 -> LEDC_CH_AUX2
//   4 -> LEDC_CH_AUX3
//   any other -> LEDC_CH_MAX  (invalid sentinel)
// -----------------------------------------------------------------------------
inline uint8_t servo_arm_id_to_ledc_channel(uint8_t arm_id) {
    switch (arm_id) {
        case 0:
            return LEDC_CH_ARM1;
        case 1:
            return LEDC_CH_ARM2;
        case 2:
            return LEDC_CH_AUX1;
        case 3:
            return LEDC_CH_AUX2;
        case 4:
            return LEDC_CH_AUX3;
        default:
            return LEDC_CH_MAX;
    }
}

// -----------------------------------------------------------------------------
// servo_ledc_channel_to_arm_id()
// The inverse: which armId addresses this LEDC channel, if any.
//
// A caller that starts from an Output Address rather than from an arm name needs
// this direction  --  the Servo Output rows record a driver and a channel
// (ADR 0041), and servoCmdQueue speaks armId. The two vocabularies are kept
// apart on purpose and this is the one bridge, not a reconciliation of them
// (include/ledc_pwm.h).
//
// Returns false for LEDC_CH_DOME (a brushless ESC, not addressable as a servo)
// and for anything out of range, leaving *out untouched. A bool rather than a
// sentinel value: 255 already means the ARM1+ARM2 broadcast on
// ServoCommand::armId (include/robot_state.h), so "not an arm" would have to
// invent a second magic number to sit beside the one that means "both".
// -----------------------------------------------------------------------------
inline bool servo_ledc_channel_to_arm_id(uint8_t channel, uint8_t* out) {
    if (out == nullptr) {
        return false;
    }
    for (uint8_t arm_id = 0; arm_id < 5; ++arm_id) {
        if (servo_arm_id_to_ledc_channel(arm_id) == channel) {
            *out = arm_id;
            return true;
        }
    }
    return false;
}

// -----------------------------------------------------------------------------
// servo_arm_enabled()
// Return true if the given armId is enabled, given the per-arm enable flags and
// which arms carry a light instead of a servo.
//
// armId 255 (broadcast) is allowed only when both arm1 and arm2 are enabled.
// Any other unknown armId returns false.
//
// `lit_arm_mask` carries bit `arm_id` for every output whose wire carries a
// Light Type (ADR 0067). It replaced a single aux_led_pin slot number, which
// could only ever name one lit wire; a droid may have several, each on its own
// wire (#413). An arm whose bit is set is never a servo output, whatever its
// toggle says, because the strip's signal line and a servo's PWM cannot share
// a pin.
// ----------------------------------------------------------------------------
inline bool servo_arm_enabled(uint8_t arm_id, bool arm1, bool arm2, bool aux1, bool aux2,
                              bool aux3, uint8_t lit_arm_mask) {
    const bool lit = arm_id < 8 && (lit_arm_mask & (uint8_t)(1u << arm_id)) != 0;

    switch (arm_id) {
        case 0:
            return arm1 && !lit;
        case 1:
            return arm2 && !lit;
        case 2:
            return aux1 && !lit;
        case 3:
            return aux2 && !lit;
        case 4:
            return aux3 && !lit;
        case 255:
            return arm1 && arm2;
        default:
            return false;
    }
}

// -----------------------------------------------------------------------------
// servo_enabled_ledc_mask()
// Build a LEDC channel bitmask from enable flags, excluding every channel whose
// wire carries a light.
//
// Each bit position corresponds to a LedcChannel:
//   bit 0 = LEDC_CH_ARM1
//   bit 1 = LEDC_CH_ARM2
//   bit 2 = LEDC_CH_DOME
//   bit 3 = LEDC_CH_AUX1
//   bit 4 = LEDC_CH_AUX2
//   bit 5 = LEDC_CH_AUX3
//
// A channel's bit is set if its corresponding toggle is true AND its arm is not
// in `lit_arm_mask` (see servo_arm_enabled() for what that mask is). DOME is
// included unconditionally: the dome ESC is not an Output and no light goes on
// it. Returns 0 if no channels are enabled.
// -----------------------------------------------------------------------------
inline uint8_t servo_enabled_ledc_mask(bool arm1, bool arm2, bool aux1, bool aux2, bool aux3,
                                       bool dome, uint8_t lit_arm_mask) {
    uint8_t mask = 0;

    if (servo_arm_enabled(0, arm1, arm2, aux1, aux2, aux3, lit_arm_mask)) {
        mask |= (1 << 0);  // LEDC_CH_ARM1
    }
    if (servo_arm_enabled(1, arm1, arm2, aux1, aux2, aux3, lit_arm_mask)) {
        mask |= (1 << 1);  // LEDC_CH_ARM2
    }
    if (dome) {
        mask |= (1 << 2);  // LEDC_CH_DOME
    }
    if (servo_arm_enabled(2, arm1, arm2, aux1, aux2, aux3, lit_arm_mask)) {
        mask |= (1 << 3);  // LEDC_CH_AUX1
    }
    if (servo_arm_enabled(3, arm1, arm2, aux1, aux2, aux3, lit_arm_mask)) {
        mask |= (1 << 4);  // LEDC_CH_AUX2
    }
    if (servo_arm_enabled(4, arm1, arm2, aux1, aux2, aux3, lit_arm_mask)) {
        mask |= (1 << 5);  // LEDC_CH_AUX3
    }

    return mask;
}
