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

// No native network backend in this build. Today that means FireBeetle 2, whose
// Hosted backend arrives with #189; it is also the ADR 0032 composition for a
// Board Variant that declares no network backend at all - such a board runs every
// droid function and simply never starts a web server.
//
// The guard is deliberately !PA_CAP_NATIVE_WIFI and NOT
// (!PA_CAP_NATIVE_WIFI && !PA_CAP_HOSTED_WIFI): no Hosted implementation exists
// yet, so tightening it would leave FireBeetle 2 with no definition and break the
// firebeetle2 link. #189 narrows this guard when it adds
// web_network_manager_hosted.cpp.
#if !PA_CAP_NATIVE_WIFI

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

#endif  // !PA_CAP_NATIVE_WIFI
