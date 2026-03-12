// =============================================================================
// include/sbus_math.h
//
// Pure SBUS channel mapping functions — no hardware, no FreeRTOS.
// Extracted for testability. Used by SBUSInputTask and native tests.
// =============================================================================
#pragma once
#include <stdint.h>
#include "config.h"

// Map raw SBUS value (SBUS_MIN..SBUS_MAX) to signed drive value (-1000..+1000)
inline int16_t mapSbusToSpeed(int16_t raw) {
    // Linear map: SBUS_MIN→-1000, SBUS_MAX→+1000
    // Use integer arithmetic to avoid float dependency in native tests
    int32_t mapped = ((int32_t)(raw - SBUS_MIN) * 2000L) / (SBUS_MAX - SBUS_MIN) - 1000L;
    if (mapped < -1000) mapped = -1000;
    if (mapped >  1000) mapped =  1000;
    return (int16_t)mapped;
}

// Map raw SBUS CH8 value (SBUS_MIN..SBUS_MAX) to 0.0–1.0 scale factor
inline float mapSbusToScale(int16_t raw) {
    float scale = (float)(raw - SBUS_MIN) / (float)(SBUS_MAX - SBUS_MIN);
    if (scale < 0.0f) scale = 0.0f;
    if (scale > 1.0f) scale = 1.0f;
    return scale;
}
