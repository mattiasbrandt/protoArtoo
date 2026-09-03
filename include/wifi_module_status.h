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
    bool linkReady = false;
    uint32_t hostMajor = 0;
    uint32_t hostMinor = 0;
    uint32_t hostPatch = 0;
};

// Snapshot for /api/status and the upload gate (#241, ADR 0034).
// Asks esp_hosted_get_coprocessor_fwversion() once after the link is ready
// and caches that classification until linkReady drops (not initialized or
// supervisor Degraded). Does not use hostedGetSlaveVersion().
// Defined in web_network_manager_hosted.cpp. Core 0 only.
WifiModuleStatusSnapshot wifiModuleQueryUpdateSupport();
