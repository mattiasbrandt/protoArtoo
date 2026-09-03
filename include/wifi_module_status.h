// =============================================================================
// include/wifi_module_status.h
//
// Read-only snapshot accessor for WiFi Module Update Support, exposed on
// GET /api/status (#241). Only defined on boards where PA_CAP_HOSTED_WIFI
// is set (src/web/web_network_manager_hosted.cpp); web_server.cpp's call
// site is itself guarded by the same capability gate, so this header
// carries no #if of its own -- it is unreachable, not undefined, on boards
// without the capability.
// =============================================================================
#pragma once

#include "wifi_module_update_support.h"

struct WifiModuleStatusSnapshot {
    WifiModuleUpdateSupportResult classification;
    uint32_t hostMajor = 0;
    uint32_t hostMinor = 0;
    uint32_t hostPatch = 0;
};

// Snapshot for /api/status. Classifies on each call (no cache): if the
// link is not ready the version RPC is not asked; if it is, this calls
// esp_hosted_get_coprocessor_fwversion() directly (ADR 0034 -- not the
// Arduino hostedGetSlaveVersion() wrapper, which cannot express unknown).
// Defined in web_network_manager_hosted.cpp. Core 0 web path only.
WifiModuleStatusSnapshot wifiModuleQueryUpdateSupport();
