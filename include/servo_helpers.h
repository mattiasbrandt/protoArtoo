// =============================================================================
// include/servo_helpers.h
//
// Pure helpers for servo arm enable-flag logic, and the way in to the armId
// mapping, which include/output_wire.h holds. No queues and no hardware  --
// safe to include in native unit tests.
//
// Extracted from src/tasks/servo_task.cpp so the mapping and enable logic
// can be exercised without hardware dependencies.
// =============================================================================
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "config.h"    // PA_BOARD, the pin plan behind the channels below
#include "ledc_pwm.h"  // LedcChannel enum, LEDC_CH_MAX
// servo_arm_id_to_ledc_channel() and servo_ledc_channel_to_arm_id(), the armId
// <-> Output Address bridge. They live with the one mapping between armId, the
// BOARD_OUTPUTS index and the Output Address (#416), and every caller that has
// always reached them through this header still does.
#include "output_wire.h"

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
