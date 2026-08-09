// =============================================================================
// include/failsafe_boot_sbus.h
//
// Pure decision logic for boot-time SBUS failsafe arming.
// Extracted from main.cpp for native testability (ADR 0005).
// =============================================================================
#pragma once

#include <cstdint>

// Boot SBUS failsafe guard decision.
// Returns true if SBUS_WATCHDOG should be armed at boot.
// SAFETY: SBUS mode leaves drive unlocked until first frame arrives;
// boot must activate SBUS_WATCHDOG failsafe to prevent drift before RC signal.
bool bootSbusSafeGuardDecision(bool sbusMode, bool anyChannelEnabled);
