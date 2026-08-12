// =============================================================================
// src/failsafe_boot_sbus.cpp
//
// Pure decision logic for boot-time SBUS failsafe arming.
// Extracted from main.cpp for native testability (ADR 0005).
// =============================================================================

#include "failsafe_boot_sbus.h"

bool bootSbusSafeGuardDecision(bool sbus1WatchdogEnabled) {
    // The Step Core already resolved mode, routing, and decoder enablement.
    // Mirroring that decision keeps boot arming aligned with the watchdog the
    // RC task will actually run without expanding #167's routed-SBUS2 scope.
    return sbus1WatchdogEnabled;
}
