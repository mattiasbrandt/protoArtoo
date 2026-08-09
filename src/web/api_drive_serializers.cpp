// =============================================================================
// src/web/api_drive_serializers.cpp
//
// Pure JSON serialization helper for drive-related responses.
// No Arduino, no FreeRTOS, no hardware dependencies — testable in native env.
// =============================================================================

#include "api_drive.h"

#include <cstdio>

#include "config.h"
#include "drive_speed_preset.h"

bool formatSpeedPresetResponseJson(char* buf, size_t bufSize, SpeedPresetId preset,
                                   int16_t speedLimitMax) {
    if (buf == nullptr || bufSize == 0) {
        return false;
    }
    if (!speedPresetIdIsValid(preset)) {
        return false;
    }
    if (speedLimitMax < 0 || speedLimitMax > SPEED_LIMIT_MAX) {
        return false;
    }

    int written = snprintf(buf, bufSize, "{\"ok\":true,\"preset\":\"%s\",\"speedLimitMax\":%d}",
                           speedPresetIdToString(preset), (int)speedLimitMax);
    return written > 0 && (size_t)written < bufSize;
}
