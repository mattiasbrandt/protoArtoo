// =============================================================================
// src/web/api_system_serializers.cpp
//
// Pure JSON serialization helper for sleep/wake control.
// No Arduino, no FreeRTOS, no hardware dependencies - testable in native env.
// =============================================================================

#include "api_system.h"

#include <cstdio>

bool formatSleepControlJson(char* buf, size_t bufSize, bool sleepMode, bool changed) {
    if (buf == nullptr || bufSize == 0) {
        return false;
    }

    int written = snprintf(buf, bufSize, "{\"ok\":true,\"sleepMode\":%s,\"changed\":%s}",
                           sleepMode ? "true" : "false", changed ? "true" : "false");
    return written > 0 && (size_t)written < bufSize;
}
