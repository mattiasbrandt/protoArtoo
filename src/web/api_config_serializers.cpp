// =============================================================================
// src/web/api_config_serializers.cpp
//
// Pure JSON serialization helper for config status.
// No Arduino, no FreeRTOS, no hardware dependencies - testable in native env.
// =============================================================================

#include "api_config.h"

#include <cstdio>

void formatConfigJson(char* buf, size_t bufSize, int16_t speedLimitMax,
                      uint32_t webDriveTimeoutMs) {
    snprintf(buf, bufSize, "{\"speedLimitMax\":%d,\"webDriveTimeoutMs\":%lu}", (int)speedLimitMax,
             (unsigned long)webDriveTimeoutMs);
}
