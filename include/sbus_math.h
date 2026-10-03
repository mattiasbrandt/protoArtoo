// =============================================================================
// include/sbus_math.h
//
// Pure SBUS signal-loss timing  --  no hardware, no FreeRTOS.
// Extracted for testability. Used by the SBUS watchdog (include/sbus_watchdog.h).
// =============================================================================
#pragma once
#include <stdint.h>

#include "config.h"

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
