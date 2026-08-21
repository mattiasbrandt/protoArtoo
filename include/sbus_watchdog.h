// =============================================================================
// include/sbus_watchdog.h
//
// Pure SBUS watchdog transition policy. Hardware/task code supplies timestamps
// and executes the returned transition.
// =============================================================================
#pragma once

#include <stdint.h>

#include "sbus_math.h"

enum class SbusWatchdogTransition : uint8_t {
    OK = 0,
    JUST_LOST,
    LOST,
    JUST_RESTORED,
};

struct SbusWatchdog {
    bool signalLost;
};

inline void sbusWatchdogReset(SbusWatchdog* watchdog) {
    if (watchdog != nullptr) {
        watchdog->signalLost = false;
    }
}

inline SbusWatchdogTransition sbusWatchdogCheck(SbusWatchdog* watchdog, uint32_t lastSbusMs,
                                                uint32_t currentMs, uint32_t timeoutMs) {
    const bool timedOut = sbusWatchdogTimeoutCheck(lastSbusMs, currentMs, timeoutMs);
    if (watchdog == nullptr) {
        return timedOut ? SbusWatchdogTransition::LOST : SbusWatchdogTransition::OK;
    }

    if (timedOut) {
        if (!watchdog->signalLost) {
            watchdog->signalLost = true;
            return SbusWatchdogTransition::JUST_LOST;
        }
        return SbusWatchdogTransition::LOST;
    }

    if (watchdog->signalLost) {
        watchdog->signalLost = false;
        return SbusWatchdogTransition::JUST_RESTORED;
    }
    return SbusWatchdogTransition::OK;
}
