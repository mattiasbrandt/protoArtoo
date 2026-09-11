// =============================================================================
// include/droid_parts.h
//
// Auto-generated from docs/droid-parts.yaml by tools/generate_droid_parts_catalog.py
// DO NOT EDIT MANUALLY
//
// Source digest: sha256 5f90098e371e7136f426fbf27180d47a2aa1b6a03899538f0f5f33c270212935
//
// The Droid Parts Catalog's id vocabulary, and only that. A Part is
// identity; an Output Address is only wiring, so there is no parts table
// in firmware beyond these ids - which Output drives which Part is
// answered by the Servo Output rows the builder's own droid stores
// (#301). Names, shorthand, aliases and position live in the browser
// module this generator writes beside this file; a rename there can
// never produce a new id here.
//
// Only Parts the body drives are here. A dome-link Part, or one nothing
// drives yet, reaches the browser alone: the dome owns execution of
// panel intent under Catalog Authority, so firmware carries only ids it
// can resolve to an Output of its own.
// =============================================================================

#pragma once

#include <stddef.h>
#include <string.h>

#include "droid_part_control.h"

// One per control path this file emitted a Part under. The catalog does not
// get to decide which paths reach firmware: a Part generated here whose
// control path the firmware does not drive fails the build rather than
// shipping an id no Output can ever claim.
static_assert(droidPartControlReachesFirmware(DROID_PART_CONTROL_BODY_LEDC),
              "a control path the firmware does not drive reached the "
              "generated id table: DROID_PART_CONTROL_BODY_LEDC");

constexpr size_t DROID_PART_COUNT = 12;

// The longest id here, so a consumer sizing a buffer against the vocabulary
// reads the number rather than counting the table.
constexpr size_t DROID_PART_ID_MAX_LEN = 7;

inline constexpr const char* const DROID_PART_IDS[DROID_PART_COUNT] = {
    "utilLo",  // body_arms
    "utilUp",  // body_arms
    "other1",  // other_slots
    "other2",  // other_slots
    "other3",  // other_slots
    "other4",  // other_slots
    "other5",  // other_slots
    "other6",  // other_slots
    "other7",  // other_slots
    "other8",  // other_slots
    "other9",  // other_slots
    "other10",  // other_slots
};

// -----------------------------------------------------------------------------
// droidPartIdIsKnown()
// The Protocol Check vocabulary gate: is this a Part this build models at all.
// It answers nothing about wiring - a known Part with no Output is still a
// known Part, and saying so is the whole point (#301).
// -----------------------------------------------------------------------------
inline bool droidPartIdIsKnown(const char* id) {
    if (id == nullptr || id[0] == '\0') {
        return false;
    }
    for (size_t i = 0; i < DROID_PART_COUNT; ++i) {
        if (strcmp(DROID_PART_IDS[i], id) == 0) {
            return true;
        }
    }
    return false;
}

// -----------------------------------------------------------------------------
// droidPartIdAt()
// The vocabulary in emission order, for a caller listing it. Returns an empty
// string past the end rather than a null nobody checks.
// -----------------------------------------------------------------------------
inline const char* droidPartIdAt(size_t index) {
    return (index < DROID_PART_COUNT) ? DROID_PART_IDS[index] : "";
}
