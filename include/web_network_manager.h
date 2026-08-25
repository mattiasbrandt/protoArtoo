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

#include "config_store.h"
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

// Query current WiFi connectivity status. Reads the wireless radio state
// (mode, signal strength, connection status, network addresses) and derives
// project-owned connectivity flags for API payloads.
// Vendor-free caller semantics: no vendor types cross the seam (ADR 0021);
// each backend reads its hardware and returns a snapshot. Strings use copy-out:
// the caller supplies buffers; the backend fills them and returns the snapshot.
// Returns: connectivity flags + string copies (AP IP, STA IP, STA SSID)
// No allocation, no blocking, no global side effects.
// thread-safe: yes (each call reads volatile device state independently)
struct WifiConnectivityStatus {
    bool wifiConnected;       // true if WiFi is available (AP active OR STA connected)
    bool wifiClientConnected; // true if at least one station is attached to soft AP
    long wifiRssi;            // signal strength in dBm (0 when STA disconnected)
    bool staConnected;        // true when STA is connected to upstream AP
    bool staEnabled;          // true when STA mode is active (STA or AP+STA)
    char apIp[16];            // AP IP address (dotted-quad + NUL), "0.0.0.0" if AP not active
    char staIp[16];           // STA IP address (dotted-quad + NUL), empty if not connected
    char staSsid[WIFI_SSID_MAX_LEN + 1];  // STA network name, empty if not connected
};

WifiConnectivityStatus networkManagerQueryConnectivity();

