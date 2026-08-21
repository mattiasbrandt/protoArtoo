// =============================================================================
// include/mood_sound_mapping.h
//
// Mood-to-sound-category mapping helpers.
// - Select random tracks from weighted category pools via 12-bit masks
// - Resolve per-mood mask selection
// - Preserve flat random-range fallback behavior
// - Format compact mood-map JSON payloads for API responses
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

static constexpr uint8_t SOUND_CATEGORY_COUNT = 12;
static constexpr uint16_t MOOD_CATEGORY_MASK_MAX = 0x0FFF;
inline bool isValidMoodCategoryMaskValue(uint32_t value) {
    return value <= MOOD_CATEGORY_MASK_MAX;
}

struct SoundCategoryRange {
    uint16_t lo;
    uint16_t hi;
};

struct MoodCategoryMaskConfig {
    uint16_t quiet;
    uint16_t mid;
    uint16_t full;
    uint16_t awakeplus;
};

// Resolve the configured category bitmask for a mood.
// Mood mapping: 10=Quiet, 11=Full-Awake, 13=Mid-Awake, 14=Awake+.
// Mood 0 means unset and forces flat-range fallback.
inline uint16_t moodCategoryMaskForMood(uint8_t mood, const MoodCategoryMaskConfig& cfg,
                                        bool* outUseFlatFallback = nullptr) {
    bool useFlat = false;
    uint16_t mask = cfg.full;
    switch (mood) {
        case 10:
            mask = cfg.quiet;
            break;
        case 13:
            mask = cfg.mid;
            break;
        case 14:
            mask = cfg.awakeplus;
            break;
        case 0:
            useFlat = true;
            mask = 0;
            break;
        case 11:
        default:
            mask = cfg.full;
            break;
    }
    if (outUseFlatFallback) {
        *outUseFlatFallback = useFlat;
    }
    return mask;
}

// Resolve one random track from enabled category ranges.
// Returns false when no enabled valid categories produce an active pool.
inline bool selectRandomTrackFromCategoryMask(const SoundCategoryRange* ranges, size_t count,
                                              uint16_t bitmask, uint32_t randomValue,
                                              uint16_t* outTrack,
                                              uint8_t* outCategoryIndex = nullptr) {
    if (ranges == nullptr || outTrack == nullptr || count == 0) {
        return false;
    }
    if (outCategoryIndex != nullptr) {
        *outCategoryIndex = 0xFF;
    }

    const size_t cappedCount = (count < SOUND_CATEGORY_COUNT) ? count : SOUND_CATEGORY_COUNT;
    uint32_t totalSize = 0;
    for (size_t i = 0; i < cappedCount; ++i) {
        const uint16_t bit = (uint16_t)(1U << i);
        if ((bitmask & bit) == 0) {
            continue;
        }
        const uint16_t lo = ranges[i].lo;
        const uint16_t hi = ranges[i].hi;
        if (lo == 0 || lo > hi) {
            continue;
        }
        totalSize += (uint32_t)hi - (uint32_t)lo + 1U;
    }

    if (totalSize == 0) {
        return false;
    }

    uint32_t r = randomValue % totalSize;
    for (size_t i = 0; i < cappedCount; ++i) {
        const uint16_t bit = (uint16_t)(1U << i);
        if ((bitmask & bit) == 0) {
            continue;
        }
        const uint16_t lo = ranges[i].lo;
        const uint16_t hi = ranges[i].hi;
        if (lo == 0 || lo > hi) {
            continue;
        }
        const uint32_t span = (uint32_t)hi - (uint32_t)lo + 1U;
        if (r < span) {
            *outTrack = (uint16_t)((uint32_t)lo + r);
            if (outCategoryIndex != nullptr) {
                *outCategoryIndex = (uint8_t)i;
            }
            return true;
        }
        r -= span;
    }

    return false;
}

// Legacy flat-range random selection (before mood categories), including max<min guard.
inline uint16_t selectRandomTrackFromFlatRange(uint16_t randMin, uint16_t randMax,
                                               uint32_t randomValue) {
    if (randMax < randMin) {
        randMax = randMin;
    }
    const uint32_t span = (uint32_t)randMax - (uint32_t)randMin + 1U;
    return (uint16_t)((uint32_t)randMin + (randomValue % span));
}

// Select a random track for the active mood.
// Returns false only for invalid arguments (outTrack==nullptr).
inline bool selectRandomTrackForMood(uint8_t mood, const MoodCategoryMaskConfig& masks,
                                     const SoundCategoryRange* ranges, size_t rangeCount,
                                     uint16_t randMin, uint16_t randMax, uint32_t randomValue,
                                     uint16_t* outTrack, bool* outUsedFlatFallback = nullptr,
                                     uint8_t* outCategoryIndex = nullptr) {
    if (outTrack == nullptr) {
        return false;
    }
    if (outCategoryIndex != nullptr) {
        *outCategoryIndex = 0xFF;
    }

    bool useFlatFallback = false;
    const uint16_t mask = moodCategoryMaskForMood(mood, masks, &useFlatFallback);

    if (!useFlatFallback) {
        if (selectRandomTrackFromCategoryMask(ranges, rangeCount, mask, randomValue, outTrack,
                                              outCategoryIndex)) {
            if (outUsedFlatFallback) {
                *outUsedFlatFallback = false;
            }
            return true;
        }
    }

    *outTrack = selectRandomTrackFromFlatRange(randMin, randMax, randomValue);
    if (outUsedFlatFallback) {
        *outUsedFlatFallback = true;
    }
    return true;
}

// Format compact mood-map JSON response body.
inline size_t formatMoodCategoryMapJson(char* out, size_t outSize, const MoodCategoryMaskConfig& cfg) {
    if (out == nullptr || outSize == 0) {
        return 0;
    }
    return (size_t)snprintf(out, outSize, "{\"quiet\":%u,\"mid\":%u,\"full\":%u,\"awakeplus\":%u}",
                            (unsigned)cfg.quiet, (unsigned)cfg.mid, (unsigned)cfg.full,
                            (unsigned)cfg.awakeplus);
}
