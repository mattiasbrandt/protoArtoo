#pragma once
// =============================================================================
// include/reset_reason.h
//
// Single source of truth for mapping an ESP-IDF reset reason to a stable,
// human-readable name. Shared by boot logging (main.cpp) and the /api/status
// telemetry snapshot so a reboot cause survives even after the boot log ring
// buffer rotates out (the dome-side blind spot that motivated this).
// =============================================================================

#include <esp_system.h>

// Returns a static string literal naming `reason`. esp_reset_reason() is stable
// for the whole runtime, so callers may invoke it at any time after boot.
const char* resetReasonName(esp_reset_reason_t reason);
