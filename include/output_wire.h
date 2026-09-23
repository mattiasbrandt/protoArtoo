// =============================================================================
// include/output_wire.h
//
// What one of the body controller's Outputs may do, answered in one place
// (#416).
//
// A wire carries a servo or a light - a Light Type on its Output (ADR 0067) -
// and three parts of the firmware ask about it, each for its own reason:
//
//   ServoTask    may LEDC go on this pin?             outputWirePinKeptForLight()
//   AuxLedTask   is a strip driven on this wire?      outputWireStripDriven()
//   bulk centre  does this row have a centre?         outputWireCentreable()
//
// THE THREE ANSWERS DIFFER ON PURPOSE. Keeping LEDC off a pin is the wide one:
// it reads only the Light Type, so a builder who has declared a strip on a wire
// has said that pin is not a servo's whether or not they have ticked it in.
// Driving a strip is the narrow one: the board must allow a light there, and
// the wire must be ticked in too. A wire with a Light Type and no tick
// therefore ends with NEITHER side on the pin, which is the safe way round - a
// servo PWM on a WS2812B's data line, or a strip clocked out onto a servo, is
// the fault; nothing driven is not. Aligning them would be a behaviour change
// with its own decision, not a tidy-up.
//
// One wire, three index spaces, and this is the one mapping between them:
//
//   armId              ServoCommand::armId and ServoTask's own s_arm[]
//   BOARD_OUTPUTS index robotState.auxLed and AuxLedTask's own s_wires[]
//   Output Address     (driver, channel), how a Servo Output row names it
//
// armId and the BOARD_OUTPUTS index are the same number: the table is laid out
// in armId order (include/board_outputs.h). The Output Address of index i is
// (SERVO_DRIVER_LEDC, BOARD_OUTPUTS[i].channel). The tasks keep their own
// index; this header translates rather than retiring either.
//
// Pure: no config cache, no lock, no heap, no clock. The caller reads the wired
// tick and the row's component and hands them in, so this is safe on the
// Core 1 servo path and compiles in the native build.
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "board_outputs.h"     // BOARD_OUTPUTS - the Outputs, in armId order
#include "ledc_pwm.h"          // LEDC_CH_MAX
#include "robot_state.h"       // ServoComponentType, SERVO_ARM_COUNT
#include "servo_output_row.h"  // ServoOutputRow

// What the caller read about one Output, for the two questions that need it.
struct OutputWireInputs {
    bool wired;                    // ticked as wired (SystemConfig enable_arm1..enable_aux3)
    ServoComponentType component;  // what its Servo Output row says is on the end of it
};

namespace output_wire_detail {
// Whether a row's component names a Light Type. The one place a component is
// compared against SERVO_COMP_RGB to answer "is this a light"; the three
// questions below build on it and no caller asks it directly.
inline bool carriesLight(ServoComponentType component) {
    return component == SERVO_COMP_RGB;
}
}  // namespace output_wire_detail

// -----------------------------------------------------------------------------
// outputWirePinKeptForLight()
// Whether LEDC must stay off this Output's pin because a light may be on it.
// ServoTask's answer, read once at init (ADR 0027).
//
// The Light Type alone decides. The wired tick is deliberately NOT part of it,
// and neither is `lightCapable`: see the header note for why the wide answer is
// the safe one here.
// -----------------------------------------------------------------------------
inline bool outputWirePinKeptForLight(const OutputWireInputs& in, size_t boardIndex) {
    return boardIndex < BOARD_OUTPUT_COUNT && output_wire_detail::carriesLight(in.component);
}

// -----------------------------------------------------------------------------
// outputWireStripDriven()
// Whether AuxLedTask drives a strip on this Output. All three halves matter:
// the board allows a light on this line (`lightCapable`, a board fact), the
// builder ticked the wire in - a wire nobody has plugged in is not lit - and
// its row names a Light Type - neither is a wire carrying a servo.
// -----------------------------------------------------------------------------
inline bool outputWireStripDriven(const OutputWireInputs& in, size_t boardIndex) {
    return boardIndex < BOARD_OUTPUT_COUNT && BOARD_OUTPUTS[boardIndex].lightCapable &&
           in.wired && output_wire_detail::carriesLight(in.component);
}

// -----------------------------------------------------------------------------
// outputWireCentreable()
// Whether "back to centre" has anything to move on this row.
//
// A light has no centre. The row's component is what this droid knows about
// the end of the wire: Part KIND - the catalog fact that `psiFront` is a light
// - is not in firmware at all (include/droid_parts.h compiles the id vocabulary
// and nothing else; the Kind lives in data/droid_part_kind.js). So the surface
// counts light rows by Kind and this counts them by component, and the two
// agree for every row whose builder described it. Where they could differ - a
// light Part on a row still recorded as a servo - the droid drives it, because
// the row is the builder's own statement about what is on that wire and the
// firmware has nothing truer.
// -----------------------------------------------------------------------------
inline bool outputWireCentreable(const ServoOutputRow& row) {
    return !output_wire_detail::carriesLight(row.component);
}

// -----------------------------------------------------------------------------
// The mapping: armId <-> BOARD_OUTPUTS index <-> Output Address
//
// These two keep ServoTask's armId vocabulary, which the tasks and
// servoCmdQueue still speak, and are reached through include/servo_helpers.h
// as they always were. What changed is that they read BOARD_OUTPUTS instead of
// holding a second table of the same channels.
// -----------------------------------------------------------------------------
static_assert(SERVO_ARM_COUNT == BOARD_OUTPUT_COUNT,
              "armId is a BOARD_OUTPUTS index: ServoTask must have one arm per Output");

// The LEDC channel armId drives, or LEDC_CH_MAX for an armId that is not an
// Output - including 255, the ARM1+ARM2 broadcast, which is two Outputs and
// not one.
inline uint8_t servo_arm_id_to_ledc_channel(uint8_t arm_id) {
    return arm_id < BOARD_OUTPUT_COUNT ? BOARD_OUTPUTS[arm_id].channel : (uint8_t)LEDC_CH_MAX;
}

// The inverse: which armId addresses this LEDC channel, if any.
//
// A caller that starts from an Output Address rather than from an arm name needs
// this direction  --  the Servo Output rows record a driver and a channel
// (ADR 0041), and servoCmdQueue speaks armId.
//
// Returns false for LEDC_CH_DOME (a brushless ESC, not an Output) and for
// anything out of range, leaving *out untouched. A bool rather than a sentinel
// value: 255 already means the ARM1+ARM2 broadcast on ServoCommand::armId
// (include/robot_state.h), so "not an arm" would have to invent a second magic
// number to sit beside the one that means "both".
inline bool servo_ledc_channel_to_arm_id(uint8_t channel, uint8_t* out) {
    if (out == nullptr) {
        return false;
    }
    for (size_t index = 0; index < BOARD_OUTPUT_COUNT; ++index) {
        if (BOARD_OUTPUTS[index].channel == channel) {
            *out = (uint8_t)index;
            return true;
        }
    }
    return false;
}
