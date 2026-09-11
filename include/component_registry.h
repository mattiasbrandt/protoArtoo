// =============================================================================
// include/component_registry.h
//
// The Component Registry (ADR 0042 as amended 2026-09-09; #303, #340).
//
// One declaration per product lives in include/component_registry.inc; this
// header is where that declaration becomes types, tables and the handful of
// questions the rest of the firmware asks of it:
//
//   - which products exist, in which Component Family, at which status
//   - which of them this image actually carries a driver for
//   - which families therefore have a Component Member setting, and what the
//     setting may be set to
//
// Every row reaches the image, including a part nothing drives: a roadmap row
// is an id, a name, a status and a protocol, and carrying it is what lets a
// controller name a part its own web assets have never heard of. Drivers are
// carried only for supported parts, so a row present with no driver is the
// normal way an unbuilt part exists rather than a defect.
//
// Adding a product:
//   1. Add one PA_COMPONENT_PART row to include/component_registry.inc, taking
//      the next free `value`. Nothing else in this header changes.
//   2. If it is supported, give it a driver and point `included` at whatever
//      decides that the image carries one.
//   3. Run `make test-tools` -- test_component_registry_drift.py reports what
//      the row no longer agrees with, and rewrites nothing.
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "audio_driver.h"  // AudioDriver::AUDIO_CAP_* -- the Sound family's vocabulary
#include "config.h"        // PA_BOARD, PA_CAP_* -- what the `included` expressions consult

// -----------------------------------------------------------------------------
// ComponentStatus -- what the project claims about a product, never what a
// given image carries. Two values, and "might" is deliberately not one of
// them: anything softer belongs in a discussion, not the droid's own UI (#303).
// -----------------------------------------------------------------------------
enum ComponentStatus : uint8_t {
    COMPONENT_STATUS_SUPPORTED = 0,  // implemented and drivable
    COMPONENT_STATUS_ROADMAP = 1,    // intended, not built
};

// -----------------------------------------------------------------------------
// ComponentCategoryId -- one enumerator per Component Family, from the manifest.
// -----------------------------------------------------------------------------
enum ComponentCategoryId : uint8_t {
#define PA_COMPONENT_CATEGORY(enumerator, id, name, member_key) enumerator,
#define PA_COMPONENT_PART(value, id, name, category, protocol, status, capabilities, gate, included)
#include "component_registry.inc"
#undef PA_COMPONENT_PART
#undef PA_COMPONENT_CATEGORY
    COMPONENT_CATEGORY_COUNT,
};

struct ComponentCategoryEntry {
    ComponentCategoryId id;
    const char* token;       // stable JSON token, e.g. "sound"
    const char* name;        // operator-visible, e.g. "Sound"
    const char* memberKey;   // NVS key of this family's Component Member, or nullptr
};

struct ComponentPartEntry {
    uint8_t value;               // stable numeric id -- what a Component Member persists
    const char* id;              // stable JSON token, e.g. "chirp"
    const char* name;            // operator-visible, e.g. "CHIRP Audio Trigger"
    ComponentCategoryId category;
    const char* protocol;        // Component Protocol token; shared between products
    ComponentStatus status;
    uint8_t capabilities;        // the family's own bitmask, declared per part
    const char* gate;            // required Board Capability Gate name, or nullptr = universal
    bool included;               // this image carries a driver for this part
};

// Both tables are extern const so they live in flash on embedded targets, the
// same shape ACTION_REGISTRY uses (include/action_registry.h). Defined in
// src/component_registry.cpp.
extern const ComponentCategoryEntry COMPONENT_CATEGORIES[];
extern const size_t COMPONENT_CATEGORY_TABLE_SIZE;
extern const ComponentPartEntry COMPONENT_PARTS[];
extern const size_t COMPONENT_PART_COUNT;

// -----------------------------------------------------------------------------
// Lookups. All are O(n) linear scans over a 21-row flash table, called from
// boot and from Core 0 web handlers -- never from a real-time loop.
// -----------------------------------------------------------------------------

// The row with this stable numeric value, or nullptr if no row has it. A
// controller that stored a value this image no longer declares gets nullptr,
// which is what lets the caller fall back rather than index out of bounds.
const ComponentPartEntry* componentPartByValue(uint8_t value);

// The row with this token, or nullptr.
const ComponentPartEntry* componentPartById(const char* id);

const ComponentCategoryEntry* componentCategory(ComponentCategoryId category);

namespace component_registry_detail {
// constexpr string compare, so the lookup below folds at compile time. Only
// ever fed manifest tokens and string literals, both NUL-terminated.
constexpr bool idEquals(const char* a, const char* b) {
    while (*a != '\0' && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}
}  // namespace component_registry_detail

// The capability word declared for one product, read straight out of the
// manifest at compile time. A driver RETURNS this rather than restating its own
// bits, so a registry row and its driver cannot drift apart -- there is nothing
// left to compare. Returns 0 for an id no row declares, which is the same
// answer every row that declares no capabilities gives.
constexpr uint8_t componentPartCapabilities(const char* id) {
#define PA_COMPONENT_CATEGORY(enumerator, token, name, member_key)
#define PA_COMPONENT_PART(value, part_id, name, category, protocol, status, capabilities, gate, \
                          included)                                                             \
    if (component_registry_detail::idEquals(id, part_id)) {                                     \
        return (uint8_t)(capabilities);                                                         \
    }
#include "component_registry.inc"
#undef PA_COMPONENT_PART
#undef PA_COMPONENT_CATEGORY
    return 0;
}

// A part is SELECTABLE when the project supports it and this image carries a
// driver for it. That pair -- not the Board Capability Gate's option set -- is
// what "more than one selectable member" counts: the Gate answers what the
// board can be wired for, and what the running image carries is a different
// question (ADR 0042 amended 2026-09-09, CONTEXT.md "Component Member").
inline bool componentPartIsSelectable(const ComponentPartEntry& part) {
    return part.status == COMPONENT_STATUS_SUPPORTED && part.included;
}

// How many selectable members this family has in this image.
//
// constexpr, and over the manifest rather than the table, so a `static_assert`
// can hold something to it: src/tasks/audio_task.cpp uses it to fail the build
// when a selectable Sound member has no driver instance to run on.
constexpr uint8_t componentCategorySelectableCount(ComponentCategoryId category) {
    uint8_t count = 0;
#define PA_COMPONENT_CATEGORY(enumerator, token, name, member_key)
#define PA_COMPONENT_PART(value, id, name, part_category, protocol, status, capabilities, gate, \
                          included)                                                             \
    if ((part_category) == category && (status) == COMPONENT_STATUS_SUPPORTED &&                 \
        ((included) != 0)) {                                                                    \
        ++count;                                                                                \
    }
#include "component_registry.inc"
#undef PA_COMPONENT_PART
#undef PA_COMPONENT_CATEGORY
    return count;
}

// The family's Component Member setting exists only where more than one member
// is selectable. Both halves are derived from the table above, so a family that
// grows a second driver reports a member setting the moment the row lands --
// and the declared memberKey is checked against this in
// test/test_native/test_component_registry rather than being trusted.
inline bool componentCategoryHasMemberSetting(ComponentCategoryId category) {
    return componentCategorySelectableCount(category) > 1;
}

// The stored member value to use when a controller has never chosen one, or
// chose a part this image no longer carries. Returns 0 when the family has no
// selectable member at all, which no caller can act on and every caller can
// test for.
//
// For Sound this is where PA_AUDIO_DRIVER ends up: the build flag no longer
// decides what the image can drive -- every image carries all three modules --
// so all it still answers is which one a controller that has never been told
// starts with. That is a factory default, not a capability, which is why it is
// the one place left in the firmware that names a sound module by identity.
uint8_t componentCategoryDefaultMember(ComponentCategoryId category);

// Resolve a persisted member value to a row this image can actually drive.
// Returns the default above when `stored` names nothing, names a part in
// another family, or names one this image does not carry -- so a controller
// carried across firmware builds degrades to a working module rather than to
// silence.
const ComponentPartEntry* componentResolveMember(ComponentCategoryId category, uint8_t stored);
