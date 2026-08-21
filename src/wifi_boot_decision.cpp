// =============================================================================
// src/wifi_boot_decision.cpp
//
// Pure WiFi Boot Posture decision implementation. See include/wifi_boot_decision.h.
// =============================================================================

#include "wifi_boot_decision.h"

WifiBootPosture wifiDecideBootPosture(const WifiBootDecisionInput& input) {
    if (input.networkRecoveryRequested) {
        return WifiBootPosture::NETWORK_RECOVERY;
    }

    if (!input.settings.provisioned) {
        if (input.developerShortcut.available) {
            return input.developerShortcut.mode == WifiMode::STANDALONE_AP
                       ? WifiBootPosture::STANDALONE_AP_MODE
                       : WifiBootPosture::CLIENT_MODE;
        }
        return WifiBootPosture::PROVISIONING;
    }

    return input.settings.mode == WifiMode::STANDALONE_AP ? WifiBootPosture::STANDALONE_AP_MODE
                                                            : WifiBootPosture::CLIENT_MODE;
}
