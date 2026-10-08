// =============================================================================
// include/servo_helpers.h
//
// Pure helpers for which Outputs ServoTask drives, from what it read at start.
// No queues and no hardware -- safe to include in native unit tests.
//
// ServoTask snapshots two facts per Output at start (ADR 0027), one bit per
// slot (include/servo_backend.h): the wired tick, and whether a Light Type is
// on its wire (include/output_wire.h outputWirePinKeptForLight()). These read
// the two masks, so the rule lives here rather than inline in the task, where
// the native build cannot reach it.
// =============================================================================
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ledc_pwm.h"       // LedcChannel, LEDC_CH_DOME
#include "servo_backend.h"  // SERVO_OUTPUT_SLOT_COUNT, servoOutputSlotAddress()

// -----------------------------------------------------------------------------
// servo_output_enabled()
// Whether ServoTask drives this slot's Output as a servo: wired at start, and
// no light on its wire.
//
// `lit_mask` carries the bit of every Output whose wire carries a Light Type
// (ADR 0067). It replaced a single aux_led_pin slot number, which could only
// ever name one lit wire; a droid may have several, each on its own wire
// (#413). An Output whose bit is set is never a servo output, whatever its tick
// says, because the strip's signal line and a servo's PWM cannot share a pin.
//
// A slot past the last Output answers false.
// -----------------------------------------------------------------------------
inline bool servo_output_enabled(uint8_t slot, uint32_t wired_mask, uint32_t lit_mask) {
    if (slot >= SERVO_OUTPUT_SLOT_COUNT) {
        return false;
    }
    const uint32_t bit = 1u << slot;
    return (wired_mask & bit) != 0 && (lit_mask & bit) == 0;
}

// -----------------------------------------------------------------------------
// servo_target_enabled()
// Whether a command's target passes ServoTask's toggle gate. One Output is its
// slot's servo_output_enabled(); an address no member drives has no slot and
// never passes.
//
// SERVO_OUTPUT_BOTH_ARMS, the board's first two Outputs, passes when both were
// wired at start. Whether a light is on either wire is not asked here: it is
// each Output's own check, made when ServoTask drives it, so a light on one
// arm leaves the other moving.
// -----------------------------------------------------------------------------
inline bool servo_target_enabled(ServoOutputAddress output, uint32_t wired_mask,
                                 uint32_t lit_mask) {
    if (output == SERVO_OUTPUT_BOTH_ARMS) {
        return servo_output_enabled(servoOutputSlotOf(boardOutputAddress(0)), wired_mask, 0) &&
               servo_output_enabled(servoOutputSlotOf(boardOutputAddress(1)), wired_mask, 0);
    }
    return servo_output_enabled(servoOutputSlotOf(output), wired_mask, lit_mask);
}

// -----------------------------------------------------------------------------
// servo_enabled_ledc_mask()
// The LEDC channel bitmask ledcPwmInit() takes: every LEDC Output
// servo_output_enabled() says ServoTask drives, plus the dome ESC's channel
// when its toggle is on.
//
// Each bit position corresponds to a LedcChannel:
//   bit 0 = LEDC_CH_ARM1
//   bit 1 = LEDC_CH_ARM2
//   bit 2 = LEDC_CH_DOME
//   bit 3 = LEDC_CH_AUX1
//   bit 4 = LEDC_CH_AUX2
//   bit 5 = LEDC_CH_AUX3
//
// DOME is the toggle alone: the dome ESC is not an Output and no light goes on
// it. An Output another member drives sets no bit. Returns 0 if no channel is
// enabled.
// -----------------------------------------------------------------------------
inline uint8_t servo_enabled_ledc_mask(uint32_t wired_mask, uint32_t lit_mask, bool dome) {
    uint8_t mask = 0;

    for (uint8_t slot = 0; slot < SERVO_OUTPUT_SLOT_COUNT; ++slot) {
        const ServoOutputAddress output = servoOutputSlotAddress(slot);
        if (output.driver == SERVO_DRIVER_LEDC && output.channel < LEDC_CH_MAX &&
            servo_output_enabled(slot, wired_mask, lit_mask)) {
            mask |= (uint8_t)(1u << output.channel);
        }
    }
    if (dome) {
        mask |= (uint8_t)(1u << LEDC_CH_DOME);
    }

    return mask;
}
