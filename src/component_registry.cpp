// =============================================================================
// src/component_registry.cpp
//
// The Component Registry tables, expanded from include/component_registry.inc.
// Contract and field meanings: include/component_registry.h.
//
// No Arduino, no FreeRTOS, no hardware -- the tables are flash data and the
// lookups are plain scans, so the native suite drives them directly.
// =============================================================================

#include "component_registry.h"

#include <string.h>

const ComponentCategoryEntry COMPONENT_CATEGORIES[] = {
#define PA_COMPONENT_CATEGORY(enumerator, id, name, member_key) {enumerator, id, name, member_key},
#define PA_COMPONENT_PART(value, id, name, category, protocol, status, capabilities, gate, included)
#include "component_registry.inc"
#undef PA_COMPONENT_PART
#undef PA_COMPONENT_CATEGORY
};

const size_t COMPONENT_CATEGORY_TABLE_SIZE =
    sizeof(COMPONENT_CATEGORIES) / sizeof(COMPONENT_CATEGORIES[0]);

const ComponentPartEntry COMPONENT_PARTS[] = {
#define PA_COMPONENT_CATEGORY(enumerator, id, name, member_key)
#define PA_COMPONENT_PART(value, id, name, category, protocol, status, capabilities, gate, included) \
    {value, id, name, category, protocol, status, (uint8_t)(capabilities), gate, (included) != 0},
#include "component_registry.inc"
#undef PA_COMPONENT_PART
#undef PA_COMPONENT_CATEGORY
};

const size_t COMPONENT_PART_COUNT = sizeof(COMPONENT_PARTS) / sizeof(COMPONENT_PARTS[0]);

// The enum's terminator and the table the manifest actually produced must agree;
// they are two separate expansions of one file and nothing else would notice
// them diverging.
static_assert(sizeof(COMPONENT_CATEGORIES) / sizeof(COMPONENT_CATEGORIES[0]) ==
                  (size_t)COMPONENT_CATEGORY_COUNT,
              "ComponentCategoryId and COMPONENT_CATEGORIES disagree on the category count");

// A roadmap part has no driver by construction (ADR 0042 amended 2026-09-09).
// Asserted here rather than trusted to whoever edits a row, because the whole
// selectable-member count rests on it.
#define PA_COMPONENT_CATEGORY(enumerator, id, name, member_key)
#define PA_COMPONENT_PART(value, id, name, category, protocol, status, capabilities, gate, included) \
    static_assert((status) != COMPONENT_STATUS_ROADMAP || (included) == 0,                           \
                  "roadmap row " id " declares a driver; drivers are carried only for "              \
                  "supported parts");
#include "component_registry.inc"
#undef PA_COMPONENT_PART
#undef PA_COMPONENT_CATEGORY

const ComponentPartEntry* componentPartByValue(uint8_t value) {
    for (size_t i = 0; i < COMPONENT_PART_COUNT; ++i) {
        if (COMPONENT_PARTS[i].value == value) {
            return &COMPONENT_PARTS[i];
        }
    }
    return nullptr;
}

const ComponentPartEntry* componentPartById(const char* id) {
    if (id == nullptr) {
        return nullptr;
    }
    for (size_t i = 0; i < COMPONENT_PART_COUNT; ++i) {
        if (strcmp(COMPONENT_PARTS[i].id, id) == 0) {
            return &COMPONENT_PARTS[i];
        }
    }
    return nullptr;
}

const ComponentCategoryEntry* componentCategory(ComponentCategoryId category) {
    for (size_t i = 0; i < COMPONENT_CATEGORY_TABLE_SIZE; ++i) {
        if (COMPONENT_CATEGORIES[i].id == category) {
            return &COMPONENT_CATEGORIES[i];
        }
    }
    return nullptr;
}

// The Sound family's factory default, and the last place in the firmware that
// names a sound module by identity. PA_AUDIO_DRIVER no longer decides what the
// image can drive -- every image carries all three modules -- so all it still
// answers is which one a controller that has never been told starts with.
#if PA_AUDIO_DRIVER == AUDIO_SOFT_UART
static constexpr const char* kDefaultSoundMemberId = "dy_sv5w";
#elif PA_AUDIO_DRIVER == AUDIO_CHIRP
static constexpr const char* kDefaultSoundMemberId = "chirp";
#elif PA_AUDIO_DRIVER == AUDIO_MP3TRIGGER
static constexpr const char* kDefaultSoundMemberId = "mp3_trigger";
#elif PA_AUDIO_DRIVER == AUDIO_DFPLAYER
#error "AUDIO_DFPLAYER is a roadmap row with no driver - it cannot be a build's default sound member (see include/component_registry.inc)"
#else
#error "PA_AUDIO_DRIVER build flag is not set or has an unknown value"
#endif

uint8_t componentCategoryDefaultMember(ComponentCategoryId category) {
    if (category == COMPONENT_CATEGORY_SOUND) {
        const ComponentPartEntry* part = componentPartById(kDefaultSoundMemberId);
        if (part != nullptr && componentPartIsSelectable(*part)) {
            return part->value;
        }
    }

    // Every other family either has one selectable member or none, so the first
    // selectable row is both the only answer and the right one. A family that
    // grows a second driver and wants a different default declares it the way
    // Sound does above.
    for (size_t i = 0; i < COMPONENT_PART_COUNT; ++i) {
        if (COMPONENT_PARTS[i].category == category && componentPartIsSelectable(COMPONENT_PARTS[i])) {
            return COMPONENT_PARTS[i].value;
        }
    }
    return 0;
}

const ComponentPartEntry* componentResolveMember(ComponentCategoryId category, uint8_t stored) {
    const ComponentPartEntry* part = componentPartByValue(stored);
    if (part != nullptr && part->category == category && componentPartIsSelectable(*part)) {
        return part;
    }
    return componentPartByValue(componentCategoryDefaultMember(category));
}
