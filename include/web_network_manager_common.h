// =============================================================================
// include/web_network_manager_common.h
//
// Shared implementation for the network manager seam's Arduino-WiFi-API
// radio backends (web_network_manager_native.cpp, web_network_manager_hosted.cpp).
// Covers the parts of the seam that do not differ between the two backends:
// STA credential resolution, boot posture application, connectivity query,
// and ordinary Arduino WiFi event translation.
//
// Review found the two backends byte-identical except comments (~90 shared
// lines, 16 differing -- all comments). The bounded transport-failure recovery
// ladder is where they genuinely diverge (an ESP-Hosted-only concern, living
// entirely in web_network_manager_hosted.cpp -- #189),
// so this file exists to carry the still-identical part once instead of
// letting the copy grow a second, drifting version of the posture rules.
//
// This is not the seam header (include/web_network_manager.h): that header
// stays vendor-free by design (no #include <WiFi.h>, no vendor types in its
// signatures). This one is an internal helper the seam's two Arduino-WiFi
// backends share, so vendor types (WiFiEvent_t) are fine here.
//
// Whole-file guarded the same way its two callers are: compiled only when at
// least one Arduino-WiFi radio backend is selected, so a zero-backend board
// (ADR 0032) carries none of this.
// =============================================================================
#pragma once

#include "config.h"

#if PA_CAP_NATIVE_WIFI || PA_CAP_HOSTED_WIFI

#include <WiFi.h>

#include "config_store.h"
#include "web_network_manager.h"
#include "wifi_boot_decision.h"

// Resolve the STA credentials CLIENT_MODE (and the hosted recovery ladder's
// post-recovery rejoin, #189) actually use: the caller-supplied
// settings, falling back to the Developer WiFi Shortcut (secrets.h, ADR
// 0015) only when no runtime SSID has ever been saved. Both the boot-time
// apply and the mid-boot rejoin need the same answer, so this is the one
// place it is computed.
void wifiNetworkManagerResolveStaCredentialsCommon(const WifiConfig& settings,
                                                     const char** ssidOut,
                                                     const char** passwordOut);

// Shared boot posture switch (WiFi.mode/softAP/begin), identical across both
// Arduino-WiFi backends -- see the two backends' file-header comments for
// why hosted's boot path does not diverge (only its post-co-processor-reboot
// rejoin does, and that stays entirely in web_network_manager_hosted.cpp).
void wifiNetworkManagerApplyBootPostureCommon(WifiBootPosture posture,
                                                const WifiConfig& settings,
                                                const char* logTag);

// Shared connectivity query (WiFi.getMode/status/RSSI + copy-out strings).
WifiConnectivityStatus wifiNetworkManagerQueryConnectivityCommon();

// Shared Arduino WiFi event translation: logging + HTTP server startup +
// updates *staConnected. staConnected is backend-owned (each TU keeps its
// own volatile bool Core-1-safe event cache), so it is passed by pointer
// rather than owned here.
void wifiNetworkManagerHandleEventCommon(WiFiEvent_t event, const char* logTag,
                                           volatile bool* staConnected);

#endif  // PA_CAP_NATIVE_WIFI || PA_CAP_HOSTED_WIFI
