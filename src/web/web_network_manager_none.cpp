// =============================================================================
// src/web/web_network_manager_none.cpp
//
// No native network backend for the network manager seam (#188).
// Used when PA_CAP_NATIVE_WIFI is 0: FireBeetle 2 will use Hosted WiFi
// when #189 lands, or a board with no network backend at all (ADR 0032).
// =============================================================================

#include "../../include/web_network_manager.h"

#include "../../include/config.h"
#include "../../include/config_store.h"
#include "../../include/wifi_boot_decision.h"

// No network backend in this build. FireBeetle 2 now has its own Hosted
// backend (web_network_manager_hosted.cpp, #189), so this file compiles only
// for a Board Variant that declares neither PA_CAP_NATIVE_WIFI nor
// PA_CAP_HOSTED_WIFI - the ADR 0032 zero-backend composition. Such a board
// runs every droid function and simply never starts a web server.
#if !PA_CAP_NATIVE_WIFI && !PA_CAP_HOSTED_WIFI

void networkManagerInitialize() {
}

void networkManagerApplyBootPosture(WifiBootPosture posture, const WifiConfig& settings) {
    (void)posture;
    (void)settings;
}

// Query WiFi connectivity status. No network backend, so all status is offline.
WifiConnectivityStatus networkManagerQueryConnectivity() {
    return WifiConnectivityStatus{
        .wifiConnected = false,
        .wifiClientConnected = false,
        .wifiRssi = 0,
        .staConnected = false,
        .staEnabled = false,
        .apIp = {},      // empty string
        .staIp = {},     // empty string
        .staSsid = {},   // empty string
    };
}

// Query STA connection status. No network backend, so always disconnected.
bool networkManagerStationConnected() {
    return false;
}

#endif  // !PA_CAP_NATIVE_WIFI && !PA_CAP_HOSTED_WIFI
