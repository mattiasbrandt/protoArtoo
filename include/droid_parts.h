// =============================================================================
// include/droid_parts.h
//
// Auto-generated from docs/droid-parts.yaml by tools/generate_droid_parts_catalog.py
// DO NOT EDIT MANUALLY
//
// Source digest: sha256 b746fa5162b68337cd108f0024fe720efde48ae92183bef4be34ecafb70a7ebc
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
