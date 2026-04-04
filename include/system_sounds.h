#pragma once

#include <stdint.h>

#include "robot_state.h"

// Drive speed-limit mode buckets for T09 system sound events.
// Slow:   scale < 0.34
// Normal: 0.34 <= scale < 0.67
// Turbo:  scale >= 0.67
//
// scale values are clamped to [0.0, 1.0] before classification.
enum SystemDriveModeBucket : uint8_t {
    SYSTEM_DRIVE_MODE_SLOW = 0,
    SYSTEM_DRIVE_MODE_NORMAL,
    SYSTEM_DRIVE_MODE_TURBO,
};

inline SystemDriveModeBucket classifySystemDriveMode(float scale) {
    if (scale < 0.0f) scale = 0.0f;
    if (scale > 1.0f) scale = 1.0f;
    if (scale < 0.34f) return SYSTEM_DRIVE_MODE_SLOW;
    if (scale < 0.67f) return SYSTEM_DRIVE_MODE_NORMAL;
    return SYSTEM_DRIVE_MODE_TURBO;
}

typedef bool (*SystemSoundQueueFn)(uint16_t track, CommandSource src);

// Queue a configured system sound track with 0-as-silent guard.
// Returns true only when a non-zero track was accepted by the queue function.
inline bool queueSystemSoundTrack(uint16_t track, SystemSoundQueueFn queueFn,
                                  CommandSource src = SRC_INTERNAL) {
    if (track == 0 || queueFn == nullptr) return false;
    return queueFn(track, src);
}
