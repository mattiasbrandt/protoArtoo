// =============================================================================
// src/failsafe_boot_sbus.cpp
//
// Pure decision logic for boot-time SBUS failsafe arming.
// Extracted from main.cpp for native testability (ADR 0005).
// =============================================================================

#include "failsafe_boot_sbus.h"

bool bootSbusSafeGuardDecision(bool sbusMode, bool anyChannelEnabled) {
    // Arm SBUS_WATCHDOG if SBUS mode is active and at least one RC channel is enabled.
    // This leaves the drive locked (failsafe active) until the first SBUS frame arrives,
    // preventing drift before RC signal is established.
    return sbusMode && anyChannelEnabled;
}
