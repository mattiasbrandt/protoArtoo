// Droid Part control-path inventory (CONTEXT.md "Droid Parts Catalog"; #301, #356).
//
// A control path is HOW protoArtoo can reach a Part, and - because #301 made
// them the same fact - HOW FAR that Part travels when docs/droid-parts.yaml is
// generated. A Part the body drives reaches both generated outputs; a Part the
// dome drives, or one nothing drives yet, reaches the browser only, because the
// dome owns execution of panel intent under Catalog Authority.
//
// This manifest is the firmware's declaration of that set, and it is what makes
// "the build fails when the YAML names a control path the firmware does not
// define" true rather than aspirational: the catalog does not get to invent a
// control path. tools/generate_droid_parts_catalog.py reads the rows below and
// refuses a `control:` token that is not one of them, and the header it writes
// static_asserts that every path it emitted a firmware part under is a path
// this file says reaches firmware. Neither side keeps its own copy of the list,
// which is the whole reason the list lives here.
//
// This file is an unguarded X-macro manifest, intentionally included
// repeatedly. Define PA_PART_CONTROL(enumerator, yaml_token, reaches_firmware)
// before including it. Adding #pragma once here breaks the mechanism:
// include/droid_part_control.h includes it twice in one translation unit with
// two different macro definitions - once for the enum, once for the token
// table - and the second expansion would silently become a no-op.
//
// `yaml_token` is the spelling docs/droid-parts.yaml uses. It carries a hyphen,
// so it is a string rather than a macro token, and it is the one column no C++
// expansion below happens to need - the generator does. Keeping it here anyway
// is the point: the mapping between the catalog's vocabulary and the firmware's
// enum has exactly one home.

#ifndef PA_PART_CONTROL
  #error "Define PA_PART_CONTROL before including this inventory"
#endif

// Present on the droid design, nothing drives it yet. Not the same as a row
// that declares no control path at all - a dome fixture is an orientation
// reference and never becomes driveable, while a `none` part is waiting for
// somebody to wire it.
PA_PART_CONTROL(DROID_PART_CONTROL_NONE, "none", 0)

// AstroPixelsPlus serial, addressed by the fork's :OPnn/:CLnn panel numbers.
// Browser only: the dome resolves panel intent itself, and the body never sees
// a dome part name (#301).
PA_PART_CONTROL(DROID_PART_CONTROL_DOME_LINK, "dome-link", 0)

// A local LEDC PWM channel on the body controller. The only path that reaches
// firmware today, because it is the only one whose Parts firmware can resolve
// to an Output of its own.
PA_PART_CONTROL(DROID_PART_CONTROL_BODY_LEDC, "body-ledc", 1)
