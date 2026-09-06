// =============================================================================
// include/wifi_boot_decision.h
//
// WiFi Boot Posture decision layer (ADR 0015  --  runtime WiFi provisioning).
//
// Design:
//   - wifiDecideBootPosture() is a pure function: no hardware access, no
//     FreeRTOS/Arduino calls, no globals. It consumes Device WiFi Settings
//     plus explicit recovery/Developer WiFi Shortcut inputs and returns the
//     requested boot posture.
//   - The boot shell (src/web/web_network_bootstrap.cpp) gathers these inputs,
//     calls this function once at boot, and executes the returned posture
//     through the concrete WiFi APIs (via the network manager seam,
//     web_network_manager.h). This module decides WHICH posture; the shell
//     decides HOW to enter it.
//   - Public release binaries never populate WifiDeveloperShortcut  --  it exists
//     only so self-built firmware can keep using compile-time secrets.h
//     defaults (the Developer WiFi Shortcut) during local development.
//   - Network Recovery Mode is entered only by an explicit local gesture
//     (input.networkRecoveryRequested); ordinary WiFi Client Mode connection
//     trouble is not a valid input here and must never be inferred into
//     automatic AP fallback.
//
// This header is Arduino-free and compiles in the native test environment.
// =============================================================================
#pragma once

#include "config_store.h"

// The requested boot posture, as decided from Device WiFi Settings plus
// recovery/developer inputs.
enum class WifiBootPosture : uint8_t {
    PROVISIONING = 0,       // Unprovisioned Controller  --  start WiFi Provisioning
    CLIENT_MODE = 1,        // WiFi Client Mode  --  join the saved/dev-default network
    STANDALONE_AP_MODE = 2, // Standalone AP Mode  --  host the controller's own network
    NETWORK_RECOVERY = 3,   // Network Recovery Mode  --  explicit local repair posture
};

// Developer WiFi Shortcut: source-build-only convenience. Lets a self-build
// developer boot straight into a known posture, compiled from src/secrets.h,
// without going through WiFi Provisioning first. `available` must be false
// for public release binaries  --  the decision never requires it to resolve a
// posture for a provisioned or explicitly-recovering controller.
struct WifiDeveloperShortcut {
    bool available = false;
    WifiMode mode = WifiMode::CLIENT;
};

struct WifiBootDecisionInput {
    WifiConfig settings;                    // persisted Device WiFi Settings
    bool networkRecoveryRequested = false;  // explicit local recovery gesture latched this boot
    WifiDeveloperShortcut developerShortcut;
};

// wifiDecideBootPosture: pure function of its inputs.
//
// Precedence:
//   1. networkRecoveryRequested always wins  --  Network Recovery Mode is an
//      explicit local action, independent of provisioning/mode state.
//   2. An Unprovisioned Controller (settings.provisioned == false) starts
//      WiFi Provisioning, unless a Developer WiFi Shortcut is available, in
//      which case it boots straight into the shortcut's posture.
//   3. A provisioned controller honors its saved WifiMode.
WifiBootPosture wifiDecideBootPosture(const WifiBootDecisionInput& input);
