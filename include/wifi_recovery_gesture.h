#pragma once
// =============================================================================
// include/wifi_recovery_gesture.h
//
// Network Recovery Mode local entry gesture (ADR 0015).
//
// The gesture is a rapid power-cycle count: physically power-cycling the
// controller WIFI_RECOVERY_GESTURE_THRESHOLD times in a row, each cycle
// landing before the previous boot confirms stable uptime, latches Network
// Recovery Mode for that boot. Counting only true power-on resets means the
// gesture:
//   - requires a real local action (unplug/replug or a power switch), so it
//     stays available even when WiFi is completely unreachable;
//   - never triggers from watchdog resets, panic resets, brownouts, software
//     restarts, or ordinary STA disconnects, because none of those classify
//     as wasPowerOnReset.
//
// This header is Arduino/ESP-IDF-free and compiles in the native test
// environment. The boot shell (src/web/web_server.cpp) classifies
// esp_reset_reason(), owns the persisted counter, and clears it once uptime
// confirms the boot was not part of a rapid-cycle gesture.
// =============================================================================

#include <cstdint>

// Consecutive power-on boots required to latch Network Recovery Mode.
constexpr uint8_t WIFI_RECOVERY_GESTURE_THRESHOLD = 3;

struct WifiRecoveryGestureInput {
    bool wasPowerOnReset = false;  // true only for a true power-on reset
    uint8_t priorCycleCount = 0;   // persisted count carried from the previous boot
};

struct WifiRecoveryGestureResult {
    bool recoveryRequested = false;  // latch Network Recovery Mode for this boot
    uint8_t nextCycleCount = 0;      // value the caller must persist for the next boot
};

// Pure function: no hardware or NVS access.
WifiRecoveryGestureResult wifiEvaluateRecoveryGesture(const WifiRecoveryGestureInput& input);
