// =============================================================================
// include/droid_build.h
//
// The Droid Build: which droid a builder says they built, and which Parts are
// actually on it (ADR 0047, #333, #343).
//
// A Droid Build is a Dome Design and a Body Design, each at a Design Variant,
// together with the Fitted Parts they seeded and any Common Addition the
// builder added. The two halves are answered independently: an MK3 body under
// an MK4 dome is an ordinary droid, so nothing here compares one half against
// the other, and no answer to one half constrains the other.
//
// A DESIGN SEEDS, IT NEVER FENCES. The Fitted Parts are the truth; a design is
// a running start at them. Nothing downstream is gated on this structure: a
// Part outside the Fitted set is still authorable, still saveable and still
// wirable, and droidPartIdIsKnown() - the Protocol Check vocabulary gate - is
// the whole catalog whatever a Droid Build says. That is the one thing this
// model can get wrong that would undo the decision it is named for.
//
// No firmware behaviour branches on a Droid Build. What firmware does is store
// it and refuse a value the catalog never declared, which is form, not intent:
// the same job droidPartIdIsKnown() does for a Part id.
//
// This structure is deliberately NOT part of ConfigSnapshot. The snapshot
// crosses three nested stack frames on the serial config-write path and its
// size is pinned to a measured task-stack chain (include/config_store.h), and
// nothing on a real-time path reads a Droid Build - so it sits on its own NVS
// keys beside the addressed Servo Output rows, for the same reason they do
// (include/config_serializer.h).
//
// Pure: no NVS, no FreeRTOS, no Arduino String. Header-only, so anything handed
// a catalog spelling can resolve it without pulling in a driver.
// =============================================================================

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "droid_parts.h"  // the generated vocabulary and the pre-selected answer

// -----------------------------------------------------------------------------
// How wide an answer is stored
//
// Declared with headroom over the generated maxima rather than sized to them,
// the way SERVO_OUTPUT_PART_ID_MAX is: a design id longer than the longest one
// the catalog declares today must be a catalog edit, not a storage-layout
// change with a stored-value migration behind it. The static_asserts are what
// make the headroom a fact rather than a hope - a catalog that outgrows these
// fails the build here rather than truncating an id into storage.
// -----------------------------------------------------------------------------
constexpr size_t DROID_DESIGN_ID_MAX = 12;
constexpr size_t DROID_VARIANT_ID_MAX = 12;

static_assert(DROID_DESIGN_ID_MAX_LEN <= DROID_DESIGN_ID_MAX,
              "a declared design id is longer than the field a Droid Build stores it in");
static_assert(DROID_VARIANT_ID_MAX_LEN <= DROID_VARIANT_ID_MAX,
              "a declared variant id is longer than the field a Droid Build stores it in");

// -----------------------------------------------------------------------------
// DroidDesignChoice
// One half of the answer: a design, and the variant of it this droid was built
// at. `variant` is empty exactly when the design declares no variant set -
// which is a real answer ("this design has no second axis"), not a missing one.
// -----------------------------------------------------------------------------
struct DroidDesignChoice {
    char design[DROID_DESIGN_ID_MAX + 1];
    char variant[DROID_VARIANT_ID_MAX + 1];
};

// -----------------------------------------------------------------------------
// DroidFittedParts
// Membership of the catalog, one bit per Part in emission order.
//
// The bit index is an index into DROID_PART_IDS and is meaningful only inside
// one image: the catalog can grow, and a part added in the middle would move
// every bit after it. So this form is never what is STORED - the stored form is
// the ids themselves (see the parse below), which survive a catalog that grew.
// -----------------------------------------------------------------------------
constexpr size_t DROID_FITTED_PARTS_BYTES = (DROID_PART_COUNT + 7) / 8;

struct DroidFittedParts {
    uint8_t bits[DROID_FITTED_PARTS_BYTES];
};

// The longest stored id list: every Part fitted, each at its longest, comma
// separated. n ids of at most L characters with n-1 commas never exceeds
// n * (L + 1) - 1, so this bound holds with a byte to spare.
constexpr size_t DROID_FITTED_PARTS_STR_MAX =
    DROID_PART_COUNT * (DROID_PART_ID_MAX_LEN + 1);

// What "nothing is fitted" is written as. A deliberately empty droid and a
// record nobody has written must not read alike - the first is a builder's
// answer and the second is a fresh controller - and an empty string cannot tell
// them apart. This is the spelling a Servo Output row's Part list already uses
// for the same reason (include/servo_output_row.h).
constexpr char DROID_FITTED_PARTS_NONE[] = "-";

// -----------------------------------------------------------------------------
// DroidBuildConfig
// The whole answer: both halves and what is on the droid.
// -----------------------------------------------------------------------------
struct DroidBuildConfig {
    DroidDesignChoice dome;
    DroidDesignChoice body;
    DroidFittedParts fitted;
};

// -----------------------------------------------------------------------------
// What a load had to repair
//
// A stored answer this image's catalog no longer declares is a damaged record,
// repaired to the default and counted here rather than silently kept - the same
// treatment a damaged Servo Output row gets. The builder's answer is never
// thrown away for being unfamiliar to a SURFACE; it is thrown away only when
// this image's own vocabulary cannot name it.
// -----------------------------------------------------------------------------
struct DroidBuildRepairReport {
    bool domeRepaired;
    bool bodyRepaired;
    uint8_t partsDropped;  // stored ids this image's catalog no longer declares
};

inline bool droidBuildRepairReportIsClean(const DroidBuildRepairReport& report) {
    return !report.domeRepaired && !report.bodyRepaired && report.partsDropped == 0;
}

// -----------------------------------------------------------------------------
// droidFittedPartsClear() / HasIndex() / SetIndex()
// The bit operations, named so a caller never open-codes the shift.
// -----------------------------------------------------------------------------
inline void droidFittedPartsClear(DroidFittedParts* parts) {
    if (parts != nullptr) {
        memset(parts->bits, 0, sizeof(parts->bits));
    }
}

inline bool droidFittedPartsHasIndex(const DroidFittedParts& parts, size_t index) {
    if (index >= DROID_PART_COUNT) {
        return false;
    }
    return (parts.bits[index / 8] & (uint8_t)(1u << (index % 8))) != 0;
}

inline void droidFittedPartsSetIndex(DroidFittedParts* parts, size_t index, bool fitted) {
    if (parts == nullptr || index >= DROID_PART_COUNT) {
        return;
    }
    const uint8_t mask = (uint8_t)(1u << (index % 8));
    if (fitted) {
        parts->bits[index / 8] |= mask;
    } else {
        parts->bits[index / 8] &= (uint8_t)~mask;
    }
}

// -----------------------------------------------------------------------------
// droidFittedPartsHas() / droidFittedPartsFit()
// The same questions asked with a Part id. Fitting an id the catalog never
// declared returns false and changes nothing: an unnameable Part cannot be on
// a droid this image can talk about.
// -----------------------------------------------------------------------------
inline bool droidFittedPartsHas(const DroidFittedParts& parts, const char* id) {
    return droidFittedPartsHasIndex(parts, droidPartIndexOf(id));
}

inline bool droidFittedPartsFit(DroidFittedParts* parts, const char* id) {
    const size_t index = droidPartIndexOf(id);
    if (index >= DROID_PART_COUNT) {
        return false;
    }
    droidFittedPartsSetIndex(parts, index, true);
    return true;
}

inline size_t droidFittedPartsCount(const DroidFittedParts& parts) {
    size_t fitted = 0;
    for (size_t i = 0; i < DROID_PART_COUNT; ++i) {
        if (droidFittedPartsHasIndex(parts, i)) {
            ++fitted;
        }
    }
    return fitted;
}

// -----------------------------------------------------------------------------
// droidFittedPartsNextIndex()
// The next fitted Part at or after `from`, or DROID_PART_COUNT when there is
// none. A caller joining the ids walks with this rather than with a whole-set
// formatter, because the joined string is DROID_FITTED_PARTS_STR_MAX bytes and
// a frame that size on the config-write path is what the Console task's
// measured stack chain is counted in (#226). The join therefore happens in the
// serializer, into a heap-backed String, and this is the half of it that is
// pure enough to live beside the model.
// -----------------------------------------------------------------------------
inline size_t droidFittedPartsNextIndex(const DroidFittedParts& parts, size_t from) {
    for (size_t i = from; i < DROID_PART_COUNT; ++i) {
        if (droidFittedPartsHasIndex(parts, i)) {
            return i;
        }
    }
    return DROID_PART_COUNT;
}

// -----------------------------------------------------------------------------
// droidFittedPartsParse()
// Read a comma-separated Part id list into the set. Returns how many ids were
// dropped because this image's catalog does not declare them.
//
// An empty string is a real and deliberate answer - a droid with nothing fitted
// yet - and is never the same as a key that was never written. The caller is
// what tells those two apart, because only it can see whether the record
// exists (src/config_serializer.cpp).
//
// Whitespace around an id is tolerated so a hand-edited NVS value reads back;
// a token longer than any declared id is counted as dropped rather than
// truncated into a match.
// -----------------------------------------------------------------------------
inline uint8_t droidFittedPartsParse(const char* raw, DroidFittedParts* out) {
    if (out == nullptr) {
        return 0;
    }
    droidFittedPartsClear(out);
    if (raw == nullptr) {
        return 0;
    }
    uint8_t dropped = 0;
    char token[DROID_PART_ID_MAX_LEN + 1] = {};
    size_t used = 0;
    bool overlong = false;
    for (const char* cursor = raw;; ++cursor) {
        const char c = *cursor;
        if (c != ',' && c != '\0') {
            if (c == ' ' || c == '\t') {
                continue;
            }
            if (used < DROID_PART_ID_MAX_LEN) {
                token[used++] = c;
            } else {
                overlong = true;
            }
            continue;
        }
        token[used] = '\0';
        if (used > 0 || overlong) {
            if (overlong || !droidFittedPartsFit(out, token)) {
                if (dropped < UINT8_MAX) {
                    ++dropped;
                }
            }
        }
        if (c == '\0') {
            break;
        }
        used = 0;
        overlong = false;
    }
    return dropped;
}

// -----------------------------------------------------------------------------
// droidDesignChoiceSet()
// Copy an answer into a half, truncating nothing: an id that does not fit is
// refused rather than stored short, because a truncated id names a design the
// catalog never declared.
// -----------------------------------------------------------------------------
inline bool droidDesignChoiceSet(DroidDesignChoice* choice, const char* design,
                                 const char* variant) {
    if (choice == nullptr || design == nullptr || variant == nullptr) {
        return false;
    }
    if (strlen(design) > DROID_DESIGN_ID_MAX || strlen(variant) > DROID_VARIANT_ID_MAX) {
        return false;
    }
    snprintf(choice->design, sizeof(choice->design), "%s", design);
    snprintf(choice->variant, sizeof(choice->variant), "%s", variant);
    return true;
}

// -----------------------------------------------------------------------------
// droidDesignChoiceIsKnown()
// Is this half an answer the catalog declares - both the design and, for a
// design that publishes a variant set, the variant. Deliberately silent about
// the other half.
// -----------------------------------------------------------------------------
inline bool droidDesignChoiceIsKnown(const DroidDesignChoice& choice) {
    return droidDesignVariantIsKnown(choice.design, choice.variant);
}

// -----------------------------------------------------------------------------
// droidBuildDefaults()
// The answer a fresh controller starts on: the catalog's pre-selected design at
// its own default variant, on both halves, fitted with the complement that
// variant seeds.
//
// It is a recorded answer rather than an absence on purpose. A builder reads
// "MK4" and corrects it; an empty droid map never prompts them to, which is the
// silence ADR 0047 refused.
// -----------------------------------------------------------------------------
inline void droidBuildDefaults(DroidBuildConfig* out) {
    if (out == nullptr) {
        return;
    }
    droidDesignChoiceSet(&out->dome, DROID_BUILD_DEFAULT_DESIGN, DROID_BUILD_DEFAULT_VARIANT);
    droidDesignChoiceSet(&out->body, DROID_BUILD_DEFAULT_DESIGN, DROID_BUILD_DEFAULT_VARIANT);
    droidFittedPartsClear(&out->fitted);
    for (size_t i = 0; i < DROID_BUILD_DEFAULT_FITTED_COUNT; ++i) {
        droidFittedPartsFit(&out->fitted, DROID_BUILD_DEFAULT_FITTED_IDS[i]);
    }
}
