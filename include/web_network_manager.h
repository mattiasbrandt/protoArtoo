// =============================================================================
// include/web_network_manager.h
//
// Project-owned network manager seam (Slice 1 of #188). Handles WiFi event
// registration and boot posture application.
//
// Architecture:
//   - Vendor-free interface, no #include <WiFi.h> or vendor types in signatures
//   - Two backends per ADR 0021 pattern:
//     src/web/web_network_manager_native.cpp -- Arduino/esp_wifi device backend
//     src/native_test_stubs.cpp              -- host-test backend
//   - Event handling (translation, logging, server startup) is the backend's job
//   - Boot posture application is the backend's job
//   - Copy-out semantics for strings: no vendor lifetime crosses the seam
//   - Zero backends is a legal composition (ADR 0032): no at-least-one assertion
// =============================================================================
#pragma once

#include "wifi_boot_decision.h"

// Initialize network manager: register WiFi event handler and prepare for
// bootstrap. Called once at boot from webServerInit() (web_server.cpp).
// Idempotent. If no network backend is configured, this does nothing and
// the web server is never started (ADR 0032, zero-backend legality).
void networkManagerInitialize();

// Apply WiFi boot posture: enter the requested WiFi mode (provisioning,
// client mode, standalone AP, or recovery).
// Called from webNetworkBootstrap() in web_network_bootstrap.cpp.
void networkManagerApplyBootPosture(WifiBootPosture posture, const WifiConfig& settings);

