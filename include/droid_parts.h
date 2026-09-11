// =============================================================================
// include/droid_parts.h
//
// Auto-generated from docs/droid-parts.yaml by tools/generate_droid_parts_catalog.py
// DO NOT EDIT MANUALLY
//
// Source digest: sha256 3ae06f66274e5dde4fb0f2b3a4673aa2e36e982c2519cae4493f3aba9a4e3028
//
// The Droid Parts Catalog's id vocabulary, and only that. A Part is
// identity; an Output Address is only wiring, so there is no parts table
// in firmware beyond these ids - which Output drives which Part is
// answered by the Servo Output rows the builder's own droid stores
// (#301). Names, shorthand, aliases and position live in the browser
// module this generator writes beside this file; a rename there can
// never produce a new id here.
//
// EVERY Part the catalog declares is here, whatever drives it. A Part
// being KNOWN and a Part being DRIVEABLE HERE are separate facts: a
// builder who wires a spare output to the front-left breadpan door
// records `doorFL` on that row, and a dome panel nothing on the body
// drives reports part-not-assigned rather than reading as an id this
// build never heard of. What drives a Part is the `control:` column in
// the catalog and the Servo Output rows on the droid itself - neither
// of them is a question about names (operator decision, 2026-09-11,
// #358).
//
// Beside the ids: the design vocabulary a stored Droid Build is
// checked against, and the answer a fresh controller starts on. A
// Droid Build seeds the Parts and never fences them (ADR 0047), so
// nothing here narrows droidPartIdIsKnown() and no firmware
// behaviour branches on a design - the vocabulary is here for the
// same reason the id table is, to refuse a value the catalog never
// declared at the door rather than store it and puzzle a surface
// with it (#343).
//
// ONE complement reaches firmware: the one the pre-selected design
// fits on a fresh flash, so a controller nobody has opened a browser
// at still comes up with the parts that design carries instead of an
// empty list. Every other design's complement stays in the browser
// module beside this file, which is where a design CHANGE is seeded
// from - one seam, and firmware is not a second copy of it.
// =============================================================================

#pragma once

#include <stddef.h>
#include <string.h>

constexpr size_t DROID_PART_COUNT = 58;

// The longest id here, so a consumer sizing a buffer against the vocabulary
// reads the number rather than counting the table.
constexpr size_t DROID_PART_ID_MAX_LEN = 10;

inline constexpr const char* const DROID_PART_IDS[DROID_PART_COUNT] = {
    "pie1",  // dome_pies
    "pie2",  // dome_pies
    "pie3",  // dome_pies
    "pie4",  // dome_pies
    "pie5",  // dome_pies
    "pie6",  // dome_pies
    "panel1",  // dome_panels
    "panel2",  // dome_panels
    "panel3",  // dome_panels
    "panel4",  // dome_panels
    "panel5",  // dome_panels
    "panel6",  // dome_panels
    "panel7",  // dome_panels
    "panel8",  // dome_panels
    "panel9",  // dome_panels
    "panel10",  // dome_panels
    "panel11",  // dome_panels
    "panel12",  // dome_panels
    "panel13",  // dome_panels
    "panel14",  // dome_panels
    "logicFront",  // dome_lights
    "logicRear",  // dome_lights
    "magicPanel",  // dome_lights
    "psiFront",  // dome_lights
    "psiRear",  // dome_lights
    "upperPanel",  // dome_lights
    "hp1Pan",  // holoprojectors
    "hp1Tilt",  // holoprojectors
    "hp2Pan",  // holoprojectors
    "hp2Tilt",  // holoprojectors
    "hp3Pan",  // holoprojectors
    "hp3Tilt",  // holoprojectors
    "domeBtn1",  // dome_fixtures
    "domeBtn2",  // dome_fixtures
    "chargebay",  // body_doors
    "dataport",  // body_doors
    "doorFL",  // body_doors
    "doorFR",  // body_doors
    "doorRL",  // body_doors
    "doorRR",  // body_doors
    "drawer",  // body_doors
    "smallDoor",  // body_doors
    "gripArm",  // body_arms
    "gripClaw",  // body_arms
    "interArm",  // body_arms
    "interTool",  // body_arms
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
// droidPartIndexOf()
// Where a Part sits in the table above, or DROID_PART_COUNT for an id this
// build does not model. The index is emission order and is meaningful only
// inside one image - the catalog can grow - so it is for addressing a Part
// in memory, never for storing which Part was meant.
// -----------------------------------------------------------------------------
inline size_t droidPartIndexOf(const char* id) {
    if (id == nullptr || id[0] == '\0') {
        return DROID_PART_COUNT;
    }
    for (size_t i = 0; i < DROID_PART_COUNT; ++i) {
        if (strcmp(DROID_PART_IDS[i], id) == 0) {
            return i;
        }
    }
    return DROID_PART_COUNT;
}

// -----------------------------------------------------------------------------
// droidPartIdIsKnown()
// The Protocol Check vocabulary gate: is this a Part this build models at all.
// It answers nothing about wiring - a known Part with no Output is still a
// known Part, and saying so is the whole point (#301).
// -----------------------------------------------------------------------------
inline bool droidPartIdIsKnown(const char* id) {
    return droidPartIndexOf(id) < DROID_PART_COUNT;
}

// -----------------------------------------------------------------------------
// droidPartIdAt()
// The vocabulary in emission order, for a caller listing it. Returns an empty
// string past the end rather than a null nobody checks.
// -----------------------------------------------------------------------------
inline const char* droidPartIdAt(size_t index) {
    return (index < DROID_PART_COUNT) ? DROID_PART_IDS[index] : "";
}

// -----------------------------------------------------------------------------
// The design vocabulary
//
// A Droid Build names a Dome Design and a Body Design, each at a Design
// Variant, and the two halves are answered independently - an MK3 body under
// an MK4 dome is an ordinary droid rather than an error, so nothing below
// compares one half against the other (ADR 0047, #333).
// -----------------------------------------------------------------------------
constexpr size_t DROID_DESIGN_COUNT = 2;

// The longest design id and the longest variant id, so a struct storing an
// answer sizes its fields against the vocabulary rather than against a guess.
constexpr size_t DROID_DESIGN_ID_MAX_LEN = 3;
constexpr size_t DROID_VARIANT_ID_MAX_LEN = 7;

inline constexpr const char* const DROID_DESIGN_VARIANTS_MK4[] = {"simple", "complex"};

// A design and the variant set that is its own. `variants` is nullptr where a
// design declares none, which is a different statement from an empty set: the
// second control disappears rather than offering an empty axis.
struct DroidDesignRow {
    const char* id;
    const char* const* variants;
    size_t variantCount;
};

inline constexpr DroidDesignRow DROID_DESIGNS[DROID_DESIGN_COUNT] = {
    {"mk4", DROID_DESIGN_VARIANTS_MK4, 2},
    {"own", nullptr, 0},
};

// -----------------------------------------------------------------------------
// droidDesignRow()
// The row for a design id, or nullptr for one this catalog never declared.
// -----------------------------------------------------------------------------
inline const DroidDesignRow* droidDesignRow(const char* id) {
    if (id == nullptr || id[0] == '\0') {
        return nullptr;
    }
    for (size_t i = 0; i < DROID_DESIGN_COUNT; ++i) {
        if (strcmp(DROID_DESIGNS[i].id, id) == 0) {
            return &DROID_DESIGNS[i];
        }
    }
    return nullptr;
}

// -----------------------------------------------------------------------------
// droidDesignVariantIsKnown()
// Is `variant` one this design declares. An empty variant is the right answer
// for a design that declares no variant set at all, and the wrong one for a
// design that does - which is what keeps a half-answered Droid Build out of
// storage.
// -----------------------------------------------------------------------------
inline bool droidDesignVariantIsKnown(const char* designId, const char* variant) {
    const DroidDesignRow* row = droidDesignRow(designId);
    if (row == nullptr || variant == nullptr) {
        return false;
    }
    if (row->variantCount == 0) {
        return variant[0] == '\0';
    }
    for (size_t i = 0; i < row->variantCount; ++i) {
        if (strcmp(row->variants[i], variant) == 0) {
            return true;
        }
    }
    return false;
}

// -----------------------------------------------------------------------------
// The answer a fresh controller starts on
//
// The catalog's pre-selected design at its own default variant, for both
// halves, together with the complement that variant seeds. It is recorded as
// an answer rather than left absent so a builder reads it and corrects it;
// an empty droid map would never prompt them to (ADR 0047, #333).
//
// This is the ONLY complement in firmware. A design CHANGE is seeded by the
// browser seam against the catalog module beside this file, so nothing here
// has to be consulted again once a builder has answered.
// -----------------------------------------------------------------------------
constexpr const char* DROID_BUILD_DEFAULT_DESIGN = "mk4";
constexpr const char* DROID_BUILD_DEFAULT_VARIANT = "complex";

constexpr size_t DROID_BUILD_DEFAULT_FITTED_COUNT = 44;

inline constexpr const char* const DROID_BUILD_DEFAULT_FITTED_IDS[DROID_BUILD_DEFAULT_FITTED_COUNT] = {
    "pie1",
    "pie2",
    "pie3",
    "pie4",
    "pie5",
    "pie6",
    "panel1",
    "panel2",
    "panel3",
    "panel4",
    "panel5",
    "panel6",
    "panel7",
    "panel8",
    "panel9",
    "panel10",
    "panel11",
    "panel12",
    "panel13",
    "panel14",
    "magicPanel",
    "upperPanel",
    "psiRear",
    "logicRear",
    "logicFront",
    "psiFront",
    "hp1Pan",
    "hp1Tilt",
    "hp2Pan",
    "hp2Tilt",
    "hp3Pan",
    "hp3Tilt",
    "domeBtn1",
    "domeBtn2",
    "doorFL",
    "doorFR",
    "doorRL",
    "doorRR",
    "dataport",
    "chargebay",
    "smallDoor",
    "drawer",
    "utilUp",
    "utilLo",
};
