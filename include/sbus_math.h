// =============================================================================
// include/sbus_math.h
//
// Pure SBUS channel mapping functions  --  no hardware, no FreeRTOS.
// Extracted for testability. Used by SBUSInputTask and native tests.
// =============================================================================
#pragma once
#include <stdint.h>

#include "config.h"

// Map raw SBUS value (SBUS_MIN..SBUS_MAX) to signed drive value (-1000..+1000)
inline int16_t mapSbusToSpeed(int16_t raw) {
    // Linear map: SBUS_MIN->-1000, SBUS_MAX->+1000
    // Use integer arithmetic to avoid float dependency in native tests
    int32_t mapped = ((int32_t)(raw - SBUS_MIN) * 2000L) / (SBUS_MAX - SBUS_MIN) - 1000L;
    if (mapped < -1000)
        mapped = -1000;
    if (mapped > 1000)
        mapped = 1000;
    return (int16_t)mapped;
}

// Map raw SBUS CH8 value (SBUS_MIN..SBUS_MAX) to 0.0-1.0 scale factor
inline float mapSbusToScale(int16_t raw) {
    float scale = (float)(raw - SBUS_MIN) / (float)(SBUS_MAX - SBUS_MIN);
    if (scale < 0.0f)
        scale = 0.0f;
    if (scale > 1.0f)
        scale = 1.0f;
    return scale;
}

// Map raw SBUS value (SBUS_MIN..SBUS_MAX) to float -1.0..+1.0 for dome/control
inline float mapSbusToFloat(int16_t raw) {
    float val = ((float)(raw - SBUS_MIN) * 2.0f / (float)(SBUS_MAX - SBUS_MIN)) - 1.0f;
    if (val < -1.0f)
        val = -1.0f;
    if (val > 1.0f)
        val = 1.0f;
    return val;
}

// -----------------------------------------------------------------------------
// sbusWatchdogTimeoutCheck()
// Pure function to check if SBUS watchdog should fire based on last valid
// frame timestamp and current time.
//
// params: lastSbusMs     - timestamp of last valid SBUS frame (0 = never)
//         currentMs      - current timestamp (millis())
//         timeoutMs      - timeout threshold for signal loss
// returns: true if watchdog should fire (timeout exceeded or never received)
// -----------------------------------------------------------------------------
inline bool sbusWatchdogTimeoutCheck(uint32_t lastSbusMs, uint32_t currentMs, uint32_t timeoutMs) {
    if (lastSbusMs == 0) {
        return true;  // Never received valid SBUS
    }
    // Unsigned subtraction handles millis() overflow correctly
    return (currentMs - lastSbusMs) > timeoutMs;
}

// -----------------------------------------------------------------------------
// webDriveTimeoutCheck()
// Pure function to check if web drive command has timed out.
// Used by DriveTask to implement Layer 3 safety (web drive timeout).
//
// params: lastDriveCommandMs  - timestamp of last web drive command
//         currentMs           - current timestamp (millis())
//         timeoutMs           - timeout threshold for web drive commands
// returns: true if web drive command has timed out
// -----------------------------------------------------------------------------
inline bool webDriveTimeoutCheck(uint32_t lastDriveCommandMs, uint32_t currentMs,
                                 uint32_t timeoutMs) {
    if (lastDriveCommandMs == 0) {
        return false;  // No command ever sent, not a timeout
    }
    return (currentMs - lastDriveCommandMs) > timeoutMs;
}
