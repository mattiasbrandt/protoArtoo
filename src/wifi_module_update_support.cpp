// =============================================================================
// src/wifi_module_update_support.cpp
//
// WiFi Module Update Support classifier -- pure function over explicit
// inputs. See include/wifi_module_update_support.h for design rationale.
// =============================================================================

#include "wifi_module_update_support.h"

const char* wifiModuleUpdateSupportName(WifiModuleUpdateSupport support) {
    switch (support) {
        case WifiModuleUpdateSupport::Unknown:
            return "unknown";
        case WifiModuleUpdateSupport::NotSupported:
            return "not_supported";
        case WifiModuleUpdateSupport::Supported:
            return "supported";
    }
    return "unknown";
}

WifiModuleUpdateSupportResult wifiModuleClassifyUpdateSupport(
    const WifiModuleUpdateSupportInput& in) {
    WifiModuleUpdateSupportResult out{};

    if (!in.linkReady) {
        // Never asked. versionReadOk and the version fields are ignored:
        // a stale or speculative read must not leak into unknown.
        out.support = WifiModuleUpdateSupport::Unknown;
        out.versionPresent = false;
        return out;
    }

    if (!in.versionReadOk) {
        out.support = WifiModuleUpdateSupport::NotSupported;
        out.versionPresent = false;
        return out;
    }

    // versionPresent follows versionReadOk, not the numeric value. A module
    // that genuinely reports 0.0.0 is supported with version 0.0.0 (ADR 0034).
    out.support = WifiModuleUpdateSupport::Supported;
    out.versionPresent = true;
    out.versionMajor = in.versionMajor;
    out.versionMinor = in.versionMinor;
    out.versionPatch = in.versionPatch;
    return out;
}

WifiModuleVersionAsk wifiModuleDecideVersionAsk(bool linkReady, bool cacheOccupied) {
    if (!linkReady) {
        return WifiModuleVersionAsk::SkipUnknown;
    }
    if (cacheOccupied) {
        return WifiModuleVersionAsk::UseCached;
    }
    return WifiModuleVersionAsk::Ask;
}

WifiModuleUploadDecision wifiModuleClassifyUploadGate(const WifiModuleUploadGateInput& in) {
    if (!in.linkReady) {
        return WifiModuleUploadDecision::LinkNotReady;
    }
    if (in.support == WifiModuleUpdateSupport::Unknown) {
        return WifiModuleUploadDecision::Unknown;
    }
    if (in.support == WifiModuleUpdateSupport::NotSupported) {
        return WifiModuleUploadDecision::NotSupported;
    }
    // Version-equality refusal only when a version was actually read.
    // Unknown never reaches here, so it cannot look like "already current".
    if (in.versionPresent && in.versionMajor == in.hostMajor &&
        in.versionMinor == in.hostMinor && in.versionPatch == in.hostPatch) {
        return WifiModuleUploadDecision::AlreadyCurrent;
    }
    return WifiModuleUploadDecision::Allow;
}

const char* wifiModuleUploadGateErrorToken(WifiModuleUploadDecision decision) {
    switch (decision) {
        case WifiModuleUploadDecision::Allow:
            return nullptr;
        case WifiModuleUploadDecision::LinkNotReady:
            return "wifi-module-link-not-ready";
        case WifiModuleUploadDecision::Unknown:
            return "wifi-module-unknown";
        case WifiModuleUploadDecision::NotSupported:
            return "wifi-module-not-supported";
        case WifiModuleUploadDecision::AlreadyCurrent:
            return "wifi-module-already-current";
    }
    return "wifi-module-unknown";
}
