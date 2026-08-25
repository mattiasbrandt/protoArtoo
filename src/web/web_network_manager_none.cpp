// =============================================================================
// src/web/web_network_manager_none.cpp
//
// No network backend for the network manager seam (#188).
// Used when no WiFi backend is declared for the board (e.g., FireBeetle 2
// which uses Hosted WiFi, or a hypothetical zero-backend Board Variant per ADR 0032).
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
// firebeetle2_full link. #189 narrows this guard when it adds
// web_network_manager_hosted.cpp.
#if !PA_CAP_NATIVE_WIFI

void networkManagerInitialize() {
}

void networkManagerApplyBootPosture(WifiBootPosture posture, const WifiConfig& settings) {
    (void)posture;
    (void)settings;
}

#endif  // !PA_CAP_NATIVE_WIFI
