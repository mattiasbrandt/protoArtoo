// =============================================================================
// include/failsafe_boot_sbus.h
//
// Pure decision logic for boot-time SBUS failsafe arming.
// Extracted from main.cpp for native testability (ADR 0005).
// =============================================================================
#pragma once

#include <cstdint>

// Boot SBUS failsafe guard decision.
// main passes the boot-active plan's existing SBUS1 watchdog decision so boot
// safety and RcInputTask share one routing-aware source of truth. Routed SBUS2
// watchdog coverage remains owned by issue #167.
bool bootSbusSafeGuardDecision(bool sbus1WatchdogEnabled);
