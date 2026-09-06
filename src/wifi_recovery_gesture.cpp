// =============================================================================
// src/wifi_recovery_gesture.cpp
//
// Pure Network Recovery Mode gesture evaluation. See
// include/wifi_recovery_gesture.h.
// =============================================================================

#include "wifi_recovery_gesture.h"

// NVS key for persisting the power-cycle count across boots.
const char* kWifiRecoveryCycleKey = "wifiRecovN";

WifiRecoveryGestureResult wifiEvaluateRecoveryGesture(const WifiRecoveryGestureInput& input) {
    WifiRecoveryGestureResult result;

    if (!input.wasPowerOnReset) {
        // Any non-power-on boot (watchdog, panic, brownout, software restart)
        // is not part of the gesture and resets the count.
        return result;
    }

    // priorCycleCount is persisted NVS state (external input): clamp before
    // incrementing so a corrupted/out-of-range stored value can never wrap
    // uint8_t back below the threshold instead of triggering recovery.
    uint8_t clamped = input.priorCycleCount < WIFI_RECOVERY_GESTURE_THRESHOLD
                           ? input.priorCycleCount
                           : static_cast<uint8_t>(WIFI_RECOVERY_GESTURE_THRESHOLD - 1);
    uint8_t count = static_cast<uint8_t>(clamped + 1);
    if (count >= WIFI_RECOVERY_GESTURE_THRESHOLD) {
        result.recoveryRequested = true;
        result.nextCycleCount = 0;  // gesture consumed; start counting fresh next time
    } else {
        result.nextCycleCount = count;
    }
    return result;
}
