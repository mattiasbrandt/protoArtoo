// =============================================================================
// include/droid_parts.h
//
// Auto-generated from docs/droid-parts.yaml by tools/generate_droid_parts_catalog.py
// DO NOT EDIT MANUALLY
//
// Source digest: sha256 54ecbe192dfed50343a39420da9fcb18233b0e363e6ec70a6f76667304fde624
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
#include <stdint.h>
#include <string.h>

constexpr size_t DROID_PART_COUNT = 67;

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
    "bodyPanel1",  // body_doors
    "bodyPanel2",  // body_doors
    "bodyPanel3",  // body_doors
    "bodyPanel4",  // body_doors
    "bodyPanel5",  // body_doors
    "bodyPanel6",  // body_doors
    "bodyPanel7",  // body_doors
    "bodyPanel8",  // body_doors
    "chargebay",  // body_doors
    "dataport",  // body_doors
    "doorFL",  // body_doors
    "doorFR",  // body_doors
    "doorRL",  // body_doors
    "doorRR",  // body_doors
    "smallDoor",  // body_doors
    "gripArm",  // body_arms
    "gripClaw",  // body_arms
    "interArm",  // body_arms
    "interTool",  // body_arms
    "utilLo",  // body_arms
    "utilUp",  // body_arms
    "cbi",  // body_lights
    "dataPanel",  // body_lights
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
// Variant, and the two halves are answered independently - an MK4.1 dome on
// an MK4 Basic body is an ordinary droid rather than an error, so nothing
// below compares one half against the other (ADR 0047, #333).
//
// These are the designs a Droid Build may STORE. A roadmap design - one the
// catalog draws as coming and nobody can pick yet - is left out on purpose,
// so droidDesignChoiceIsKnown() refuses it on every write path (#368).
// -----------------------------------------------------------------------------
constexpr size_t DROID_DESIGN_COUNT = 3;

// The longest design id and the longest variant id, so a struct storing an
// answer sizes its fields against the vocabulary rather than against a guess.
constexpr size_t DROID_DESIGN_ID_MAX_LEN = 4;
constexpr size_t DROID_VARIANT_ID_MAX_LEN = 7;

inline constexpr const char* const DROID_DESIGN_VARIANTS_MK4[] = {"basic", "complex"};

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
    {"mk41", nullptr, 0},
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
// The spellings a variant was stored under before it was renamed
//
// A Droid Build is stored verbatim on the device and in every backup, so a
// renamed variant keeps its old spelling readable here rather than resetting
// every droid that answered it (the catalog's `legacy_ids`, #409).
// -----------------------------------------------------------------------------
struct DroidVariantLegacyRow {
    const char* design;
    const char* legacy;
    const char* variant;
};

constexpr size_t DROID_VARIANT_LEGACY_COUNT = 1;

inline constexpr DroidVariantLegacyRow DROID_VARIANT_LEGACY[DROID_VARIANT_LEGACY_COUNT] = {
    {"mk4", "simple", "basic"},
};

// -----------------------------------------------------------------------------
// droidDesignVariantCanonical()
// The variant id a stored spelling names today: the input itself unless it is
// a legacy spelling of this design's, in which case the variant it became.
// -----------------------------------------------------------------------------
inline const char* droidDesignVariantCanonical(const char* designId, const char* variant) {
    if (designId == nullptr || variant == nullptr) {
        return variant;
    }
    for (size_t i = 0; i < DROID_VARIANT_LEGACY_COUNT; ++i) {
        if (strcmp(DROID_VARIANT_LEGACY[i].design, designId) == 0 &&
            strcmp(DROID_VARIANT_LEGACY[i].legacy, variant) == 0) {
            return DROID_VARIANT_LEGACY[i].variant;
        }
    }
    return variant;
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

constexpr size_t DROID_BUILD_DEFAULT_FITTED_COUNT = 49;

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
    "smallDoor",
    "chargebay",
    "bodyPanel1",
    "bodyPanel2",
    "bodyPanel3",
    "bodyPanel4",
    "bodyPanel5",
    "bodyPanel6",
    "bodyPanel7",
    "bodyPanel8",
};

// -----------------------------------------------------------------------------
// Where each Part sits, and the sets a Gesture spreads across (ADR 0046, #438)
//
// Bearings are degrees clockwise viewed from above, in TENTHS, by the
// catalog's convention: 0 is dead astern and 180 dead ahead (operator
// decision, 2026-09-30 on #438). The convention is held in ONE constant,
// DROID_BEARING_DEAD_AHEAD_TENTHS, so "from the front" is measured from it
// and nowhere else. -1 is a Part the catalog gives no bearing: every body
// Part today, placed by a position word until one is measured.
//
// A set is the Parts one Gesture token means, on one half of the droid,
// in emission order. Their order round the droid is the Gesture's to work
// out from the bearings above when it runs, never this table's.
// -----------------------------------------------------------------------------
constexpr int16_t DROID_BEARING_NONE = -1;
constexpr int16_t DROID_BEARING_DEAD_AHEAD_TENTHS = 1800;

inline constexpr int16_t DROID_PART_BEARING_TENTHS[DROID_PART_COUNT] = {
    1500,  // pie1
    900,  // pie2
    300,  // pie3
    3300,  // pie4
    2700,  // pie5
    2100,  // pie6
    1425,  // panel1
    1280,  // panel2
    1140,  // panel3
    940,  // panel4
    755,  // panel5
    625,  // panel6
    450,  // panel7
    240,  // panel8
    3000,  // panel9
    2420,  // panel10
    2170,  // panel11
    2040,  // panel12
    1940,  // panel13
    1840,  // panel14
    2040,  // logicFront
    3000,  // logicRear
    755,  // magicPanel
    1840,  // psiFront
    240,  // psiRear
    625,  // upperPanel
    1650,  // hp1Pan
    1650,  // hp1Tilt
    3500,  // hp2Pan
    3500,  // hp2Tilt
    380,  // hp3Pan
    380,  // hp3Tilt
    80,  // domeBtn1
    200,  // domeBtn2
    DROID_BEARING_NONE,  // bodyPanel1
    DROID_BEARING_NONE,  // bodyPanel2
    DROID_BEARING_NONE,  // bodyPanel3
    DROID_BEARING_NONE,  // bodyPanel4
    DROID_BEARING_NONE,  // bodyPanel5
    DROID_BEARING_NONE,  // bodyPanel6
    DROID_BEARING_NONE,  // bodyPanel7
    DROID_BEARING_NONE,  // bodyPanel8
    DROID_BEARING_NONE,  // chargebay
    DROID_BEARING_NONE,  // dataport
    DROID_BEARING_NONE,  // doorFL
    DROID_BEARING_NONE,  // doorFR
    DROID_BEARING_NONE,  // doorRL
    DROID_BEARING_NONE,  // doorRR
    DROID_BEARING_NONE,  // smallDoor
    DROID_BEARING_NONE,  // gripArm
    DROID_BEARING_NONE,  // gripClaw
    DROID_BEARING_NONE,  // interArm
    DROID_BEARING_NONE,  // interTool
    DROID_BEARING_NONE,  // utilLo
    DROID_BEARING_NONE,  // utilUp
    DROID_BEARING_NONE,  // cbi
    DROID_BEARING_NONE,  // dataPanel
    DROID_BEARING_NONE,  // other1
    DROID_BEARING_NONE,  // other2
    DROID_BEARING_NONE,  // other3
    DROID_BEARING_NONE,  // other4
    DROID_BEARING_NONE,  // other5
    DROID_BEARING_NONE,  // other6
    DROID_BEARING_NONE,  // other7
    DROID_BEARING_NONE,  // other8
    DROID_BEARING_NONE,  // other9
    DROID_BEARING_NONE,  // other10
};

// Which half of the droid owns each Part: true for the dome's, false for
// the body's. The escape-hatch slots belong to no design and are spare
// BODY outputs, so they read as the body's. Who performs a Gesture is
// decided by this (Coordinator Resolution).
inline constexpr bool DROID_PART_ON_DOME[DROID_PART_COUNT] = {
    true,  // pie1
    true,  // pie2
    true,  // pie3
    true,  // pie4
    true,  // pie5
    true,  // pie6
    true,  // panel1
    true,  // panel2
    true,  // panel3
    true,  // panel4
    true,  // panel5
    true,  // panel6
    true,  // panel7
    true,  // panel8
    true,  // panel9
    true,  // panel10
    true,  // panel11
    true,  // panel12
    true,  // panel13
    true,  // panel14
    true,  // logicFront
    true,  // logicRear
    true,  // magicPanel
    true,  // psiFront
    true,  // psiRear
    true,  // upperPanel
    true,  // hp1Pan
    true,  // hp1Tilt
    true,  // hp2Pan
    true,  // hp2Tilt
    true,  // hp3Pan
    true,  // hp3Tilt
    true,  // domeBtn1
    true,  // domeBtn2
    false,  // bodyPanel1
    false,  // bodyPanel2
    false,  // bodyPanel3
    false,  // bodyPanel4
    false,  // bodyPanel5
    false,  // bodyPanel6
    false,  // bodyPanel7
    false,  // bodyPanel8
    false,  // chargebay
    false,  // dataport
    false,  // doorFL
    false,  // doorFR
    false,  // doorRL
    false,  // doorRR
    false,  // smallDoor
    false,  // gripArm
    false,  // gripClaw
    false,  // interArm
    false,  // interTool
    false,  // utilLo
    false,  // utilUp
    false,  // cbi
    false,  // dataPanel
    false,  // other1
    false,  // other2
    false,  // other3
    false,  // other4
    false,  // other5
    false,  // other6
    false,  // other7
    false,  // other8
    false,  // other9
    false,  // other10
};

inline constexpr uint8_t DROID_SET_MEMBERS_RING[] = {6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19};
inline constexpr uint8_t DROID_SET_MEMBERS_PIES[] = {0, 1, 2, 3, 4, 5};
inline constexpr uint8_t DROID_SET_MEMBERS_DOME[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19};
inline constexpr uint8_t DROID_SET_MEMBERS_BREADPAN[] = {44, 45, 46, 47};
inline constexpr uint8_t DROID_SET_MEMBERS_BODYDOORS[] = {34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48};

struct DroidPartSet {
    const char* id;
    bool dome;               // true: the dome performs it; false: the body expands it
    uint8_t count;
    const uint8_t* members;  // indices into DROID_PART_IDS
};

constexpr size_t DROID_PART_SET_COUNT = 5;

inline constexpr DroidPartSet DROID_PART_SETS[DROID_PART_SET_COUNT] = {
    {"ring", true, 14, DROID_SET_MEMBERS_RING},
    {"pies", true, 6, DROID_SET_MEMBERS_PIES},
    {"dome", true, 20, DROID_SET_MEMBERS_DOME},
    {"breadpan", false, 4, DROID_SET_MEMBERS_BREADPAN},
    {"bodyDoors", false, 15, DROID_SET_MEMBERS_BODYDOORS},
};

// -----------------------------------------------------------------------------
// droidPartSetFind()
// The set a Gesture token names, or nullptr for a token this build does not
// declare.
// -----------------------------------------------------------------------------
inline const DroidPartSet* droidPartSetFind(const char* id) {
    if (id == nullptr) {
        return nullptr;
    }
    for (size_t i = 0; i < DROID_PART_SET_COUNT; ++i) {
        if (strcmp(DROID_PART_SETS[i].id, id) == 0) {
            return &DROID_PART_SETS[i];
        }
    }
    return nullptr;
}
