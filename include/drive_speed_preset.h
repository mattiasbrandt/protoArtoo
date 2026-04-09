#pragma once

#include <stdint.h>
#include <string.h>

enum class SpeedPresetId : uint8_t {
    Slow = 0,
    Normal,
    Turbo,
};

inline bool speedPresetIdIsValid(SpeedPresetId preset) {
    switch (preset) {
        case SpeedPresetId::Slow:
        case SpeedPresetId::Normal:
        case SpeedPresetId::Turbo:
            return true;
        default:
            return false;
    }
}

inline SpeedPresetId normalizeSpeedPresetId(uint8_t raw) {
    switch (raw) {
        case (uint8_t)SpeedPresetId::Slow:
            return SpeedPresetId::Slow;
        case (uint8_t)SpeedPresetId::Normal:
            return SpeedPresetId::Normal;
        case (uint8_t)SpeedPresetId::Turbo:
            return SpeedPresetId::Turbo;
        default:
            return SpeedPresetId::Normal;
    }
}

inline SpeedPresetId nextSpeedPreset(SpeedPresetId current) {
    switch (current) {
        case SpeedPresetId::Slow:
            return SpeedPresetId::Normal;
        case SpeedPresetId::Normal:
            return SpeedPresetId::Turbo;
        case SpeedPresetId::Turbo:
            return SpeedPresetId::Slow;
        default:
            return SpeedPresetId::Normal;
    }
}

inline bool speedPresetValuesAreUnique(int16_t slow, int16_t normal, int16_t turbo) {
    return slow != normal && slow != turbo && normal != turbo;
}

inline bool resolveSpeedPresetForLimit(int16_t currentLimit, int16_t slow, int16_t normal,
                                       int16_t turbo, SpeedPresetId* out) {
    if (out == nullptr) {
        return false;
    }
    if (!speedPresetValuesAreUnique(slow, normal, turbo)) {
        return false;
    }
    if (currentLimit == slow) {
        *out = SpeedPresetId::Slow;
        return true;
    }
    if (currentLimit == normal) {
        *out = SpeedPresetId::Normal;
        return true;
    }
    if (currentLimit == turbo) {
        *out = SpeedPresetId::Turbo;
        return true;
    }
    return false;
}

inline int16_t speedPresetValueForId(SpeedPresetId preset, int16_t slow, int16_t normal, int16_t turbo) {
    switch (preset) {
        case SpeedPresetId::Slow:
            return slow;
        case SpeedPresetId::Normal:
            return normal;
        case SpeedPresetId::Turbo:
            return turbo;
        default:
            return normal;
    }
}

inline const char* speedPresetIdToString(SpeedPresetId preset) {
    switch (preset) {
        case SpeedPresetId::Slow:
            return "slow";
        case SpeedPresetId::Normal:
            return "normal";
        case SpeedPresetId::Turbo:
            return "turbo";
        default:
            return "normal";
    }
}

inline bool parseSpeedPresetId(const char* raw, SpeedPresetId* out) {
    if (raw == nullptr || out == nullptr) {
        return false;
    }
    if (strcmp(raw, "slow") == 0) {
        *out = SpeedPresetId::Slow;
        return true;
    }
    if (strcmp(raw, "normal") == 0) {
        *out = SpeedPresetId::Normal;
        return true;
    }
    if (strcmp(raw, "turbo") == 0) {
        *out = SpeedPresetId::Turbo;
        return true;
    }
    return false;
}

bool applySpeedPresetRuntime(SpeedPresetId preset);
bool applySpeedPresetPersisted(SpeedPresetId preset);
