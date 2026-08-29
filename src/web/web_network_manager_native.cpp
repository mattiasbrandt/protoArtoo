// =============================================================================
// src/web/web_network_manager_native.cpp
//
// Arduino/esp_wifi backend for the network manager seam (#188).
// Implements WiFi event handling, registration, and boot posture application
// using the Arduino WiFi API. The boot posture, connectivity query, and
// event-translation logic are shared with the hosted backend via
// web_network_manager_common.h (#189 slice 2 de-duplication); this file owns
// only the seam entry points and this backend's own Core-1-safe event cache.
// =============================================================================

#include "../../include/web_network_manager.h"

#include <Arduino.h>
#include "../../include/config.h"

// Native radio backend. Whole-file guard, no #else: when this board's backend
// is not the native radio, this translation unit contributes nothing, and the
// composition that does apply lives in its own file (ADR 0021 shape). Keeping a
// "not selected" definition here is what let a stale signature survive as a
// silent overload and break the firebeetle2 link.
#if PA_CAP_NATIVE_WIFI

#include <WiFi.h>

#include "../../include/web_network_manager_common.h"
#include "../../include/wifi_boot_decision.h"

static const char* TAG = "WebServer";

// Event-cached STA connection status for networkManagerStationConnected().
// Updated by handleWiFiEventBackend(); read by Core 1 dome-link loop.
// Using volatile bool avoids heap allocation and vendor calls on Core 1.
static volatile bool g_staConnected = false;

// Event handler for the Arduino WiFi driver. Translates Arduino WiFiEvent_t
// to application behavior: logging and HTTP server startup. Shared switch
// logic lives in web_network_manager_common.cpp; this wrapper only supplies
// this backend's own event cache.
static void handleWiFiEventBackend(WiFiEvent_t event) {
    wifiNetworkManagerHandleEventCommon(event, TAG, &g_staConnected);
}

// Initialize network manager: register WiFi event handler with the Arduino driver.
void networkManagerInitialize() {
    WiFi.onEvent(handleWiFiEventBackend);
}

// Apply WiFi boot posture. Called from webNetworkBootstrap().
void networkManagerApplyBootPosture(WifiBootPosture posture, const WifiConfig& settings) {
    wifiNetworkManagerApplyBootPostureCommon(posture, settings, TAG);
}

// Query WiFi connectivity status. Reads hardware state and derives connectivity fields.
WifiConnectivityStatus networkManagerQueryConnectivity() {
    return wifiNetworkManagerQueryConnectivityCommon();
}

// Query STA connection status via event cache (Core 1 safe).
bool networkManagerStationConnected() {
    return g_staConnected;
}

#endif  // PA_CAP_NATIVE_WIFI
