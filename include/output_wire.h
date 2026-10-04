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
//                when does it let go after arriving?  outputWireReleaseAfterMs()
//   AuxLedTask   is a strip driven on this wire?      outputWireStripDriven()
//   bulk centre  does this row have a centre?         outputWireCentreable()
//
// THE FIRST THREE ANSWERS DIFFER ON PURPOSE. Keeping LEDC off a pin is the wide one:
// it reads only the Light Type, so a builder who has declared a strip on a wire
// has said that pin is not a servo's whether or not they have ticked it in.
// Driving a strip is the narrow one: the board must allow a light there, and
// the wire must be ticked in too. A wire with a Light Type and no tick
// therefore ends with NEITHER side on the pin, which is the safe way round - a
// servo PWM on a WS2812B's data line, or a strip clocked out onto a servo, is
// the fault; nothing driven is not. Aligning them would be a behaviour change
// with its own decision, not a tidy-up.
//
// The questions take a BOARD_OUTPUTS index, because a light can only go on the
// board's own wires (`lightCapable`) and the wired ticks are stored per board
// Output. Everything on the servo path names an Output by its Output Address
// instead (#444); include/servo_backend.h holds the one mapping between the
// two, boardOutputAddress() and boardOutputIndexOf().
//
// Pure: no config cache, no lock, no heap, no clock. The caller reads the wired
// tick and the row's component and hands them in, so this is safe on the
// Core 1 servo path and compiles in the native build.
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "board_outputs.h"     // BOARD_OUTPUTS - the board's own Outputs
#include "robot_state.h"       // ServoComponentType
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
// outputWireReleaseAfterMs()
// How long this row's Output holds after a move arrives before it lets go - its
// Output Release (ADR 0043, #443) - or SERVO_RELEASE_MS_NEVER.
//
// Never for a light, whatever the row stores: release exists so a jammed or
// fought servo cannot grind, and a light can fight nothing (GLOSSARY.md "Output
// Release"). The stored number is kept rather than cleared, as the LED count is
// kept on a servo's row, so naming a light by mistake and naming the servo back
// does not cost the builder the time they set.
// -----------------------------------------------------------------------------
inline uint16_t outputWireReleaseAfterMs(const ServoOutputRow& row) {
    return output_wire_detail::carriesLight(row.component) ? SERVO_RELEASE_MS_NEVER
                                                           : row.release_ms;
}
