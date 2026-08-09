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

#include "config.h"    // AUX_LED_PIN_* selection constants
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
// servo_arm_enabled()
// Return true if the given armId is enabled, given the per-arm enable flags and
// AUX LED header reservation state.
//
// armId 255 (broadcast) is allowed only when both arm1 and arm2 are enabled.
// Any other unknown armId returns false.
//
// aux_led_pin_selection follows config aux_led_pin values:
//   0=disabled, 1=AUX1 reserved, 2=AUX2 reserved, 3=AUX3 reserved.
// ----------------------------------------------------------------------------
inline bool servo_arm_enabled(uint8_t arm_id, bool arm1, bool arm2, bool aux1, bool aux2,
                              bool aux3, uint8_t aux_led_pin_selection) {
    const bool aux1_effective = aux1 && aux_led_pin_selection != AUX_LED_PIN_AUX1;
    const bool aux2_effective = aux2 && aux_led_pin_selection != AUX_LED_PIN_AUX2;
    const bool aux3_effective = aux3 && aux_led_pin_selection != AUX_LED_PIN_AUX3;

    switch (arm_id) {
        case 0:
            return arm1;
        case 1:
            return arm2;
        case 2:
            return aux1_effective;
        case 3:
            return aux2_effective;
        case 4:
            return aux3_effective;
        case 255:
            return arm1 && arm2;
        default:
            return false;
    }
}
