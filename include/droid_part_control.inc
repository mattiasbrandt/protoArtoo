// Droid Part control-path inventory (CONTEXT.md "Droid Parts Catalog"; #301, #356).
//
// A control path is HOW protoArtoo can reach a Part: the dome's serial link,
// a local PWM channel, or nothing yet. It says what is DRIVABLE, never what is
// NAMEABLE - every Part docs/droid-parts.yaml declares reaches both generated
// outputs, because a Part is identity and a builder must be able to name the
// breadpan door they just wired whoever ends up driving it (operator decision,
// 2026-09-11, #358).
//
// This manifest is the firmware's declaration of that set, and it is what makes
// "the build fails when the YAML names a control path the firmware does not
// define" true rather than aspirational: the catalog does not get to invent a
// control path. tools/generate_droid_parts_catalog.py reads the rows below and
// refuses a `control:` token that is not one of them. Neither side keeps its
// own copy of the list, which is the whole reason the list lives here.
//
// This file is an unguarded X-macro manifest, intentionally included
// repeatedly. Define PA_PART_CONTROL(enumerator, yaml_token) before including
// it. Adding #pragma once here breaks the mechanism: include/droid_part_control.h
// includes it twice in one translation unit with two different macro
// definitions - once for the enum, once for the token table - and the second
// expansion would silently become a no-op.
//
// `yaml_token` is the spelling docs/droid-parts.yaml uses. It carries a hyphen,
// so it is a string rather than a macro token. The mapping between the
// catalog's vocabulary and the firmware's enum has exactly one home, and this
// is it.
//
// Which half of that is live today (#375): these rows ARE consulted, by the
// generator and the drift check, which read them as text and refuse a
// `control:` token that is not among them. The C++ side built from them --
// include/droid_part_control.h's enum and token functions -- is not; nothing
// under src/ includes it. It is staged for C1a (#347), and that header carries
// the same disclosure at the top.

#ifndef PA_PART_CONTROL
  #error "Define PA_PART_CONTROL before including this inventory"
#endif

// Present on the droid design, nothing drives it yet. Not the same as a row
// that declares no control path at all - a dome fixture is an orientation
// reference and never becomes driveable, while a `none` part is waiting for
// somebody to wire it.
PA_PART_CONTROL(DROID_PART_CONTROL_NONE, "none")

// AstroPixelsPlus serial, addressed by the fork's :OPnn/:CLnn panel numbers.
// The dome resolves panel intent itself under Catalog Authority, so the body
// drives nothing on this path - it still names every Part on it (#301, #358).
PA_PART_CONTROL(DROID_PART_CONTROL_DOME_LINK, "dome-link")

// A local LEDC PWM channel on the body controller. The only path the body
// drives today, which is a statement about wiring rather than about the
// vocabulary: a Servo Output row may record any Part the catalog declares.
PA_PART_CONTROL(DROID_PART_CONTROL_BODY_LEDC, "body-ledc")
