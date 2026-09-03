// =============================================================================
// include/wifi_module_update_support.h
//
// WiFi Module Update Support classifier -- host-testable step core (#241).
//
// Whether a fitted WiFi Module will accept an update is a fact about an image
// protoArtoo does not build and cannot declare, so it is discovered by asking
// the module (ADR 0034). This header owns only that classification:
//
//     unknown        -- link not ready, so the version RPC was never asked
//     not_supported  -- link ready, version RPC asked, refused
//     supported      -- link ready, version RPC asked, answered
//
// "link ready" is a bool input supplied by the device shell, not something
// this core probes. Device I/O -- hostedIsInitialized(), the supervisor
// phase, and esp_hosted_get_coprocessor_fwversion() -- stays in
// src/web/web_network_manager_hosted.cpp.
//
// No FreeRTOS, Arduino, RobotState, esp_err_t, or ESP-IDF types live here:
// native tests have no ESP-IDF headers. Deliberately free of
// PA_CAP_HOSTED_WIFI too (same as hosted_link_supervisor): the step core is
// compiled on every board; the device glue is gated.
//
// A successful read of 0.0.0 is supported, not unknown. ADR 0034 rejected
// that sentinel: versionPresent follows versionReadOk, not the numeric value.
// =============================================================================
#pragma once

#include <stdint.h>

enum class WifiModuleUpdateSupport : uint8_t {
    Unknown,
    NotSupported,
    Supported,
};

struct WifiModuleUpdateSupportInput {
    bool linkReady = false;
    bool versionReadOk = false;  // ignored when !linkReady
    uint32_t versionMajor = 0;
    uint32_t versionMinor = 0;
    uint32_t versionPatch = 0;
};

struct WifiModuleUpdateSupportResult {
    WifiModuleUpdateSupport support = WifiModuleUpdateSupport::Unknown;
    bool versionPresent = false;
    uint32_t versionMajor = 0;
    uint32_t versionMinor = 0;
    uint32_t versionPatch = 0;
};

WifiModuleUpdateSupportResult wifiModuleClassifyUpdateSupport(
    const WifiModuleUpdateSupportInput& in);

// "unknown" | "not_supported" | "supported"
const char* wifiModuleUpdateSupportName(WifiModuleUpdateSupport support);

// -----------------------------------------------------------------------
// Version-RPC ask cache (device glue holds the bytes; this decides)
//
// The version RPC times out on a factory module. Status and SSE both call
// the snapshot helper, so the device shell asks once after the link is
// ready and keeps that result until linkReady drops.
// -----------------------------------------------------------------------

enum class WifiModuleVersionAsk : uint8_t {
    SkipUnknown,  // link not ready -- never asked; caller must clear cache
    UseCached,    // link ready and a result is already held
    Ask,          // link ready and not yet asked
};

WifiModuleVersionAsk wifiModuleDecideVersionAsk(bool linkReady, bool cacheOccupied);

// -----------------------------------------------------------------------
// Fail-closed upload gate -- before hostedBeginUpdate()
// -----------------------------------------------------------------------

enum class WifiModuleUploadDecision : uint8_t {
    Allow,
    LinkNotReady,
    Unknown,
    NotSupported,
    AlreadyCurrent,
};

struct WifiModuleUploadGateInput {
    bool linkReady = false;
    WifiModuleUpdateSupport support = WifiModuleUpdateSupport::Unknown;
    bool versionPresent = false;
    uint32_t versionMajor = 0;
    uint32_t versionMinor = 0;
    uint32_t versionPatch = 0;
    uint32_t hostMajor = 0;
    uint32_t hostMinor = 0;
    uint32_t hostPatch = 0;
};

WifiModuleUploadDecision wifiModuleClassifyUploadGate(const WifiModuleUploadGateInput& in);

// JSON `error` token. nullptr when Allow.
const char* wifiModuleUploadGateErrorToken(WifiModuleUploadDecision decision);
