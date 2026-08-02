// =============================================================================
// src/web/api_wifi_apply.cpp
//
// Apply Core for POST /api/wifi (ADR 0011, ADR 0015). See api_wifi_apply.h.
// =============================================================================

#include "api_wifi_apply.h"

#include <string.h>

namespace {

void setError(WifiApplyResult* result, const char* message) {
    result->ok = false;
    snprintf(result->errorMessage, sizeof(result->errorMessage), "%s", message);
}

bool wifiModeFromString(const char* raw, WifiMode* out) {
    if (raw == nullptr || out == nullptr) {
        return false;
    }
    if (strcmp(raw, "client") == 0) {
        *out = WifiMode::CLIENT;
        return true;
    }
    if (strcmp(raw, "standalone_ap") == 0) {
        *out = WifiMode::STANDALONE_AP;
        return true;
    }
    return false;
}

// paramSsid: validates a supplied SSID param against WIFI_SSID_MAX_LEN and
// copies it into `out`. An empty string is a valid value here (mode-vs-SSID
// usability is checked separately, after all fields are applied).
bool paramSsid(const ConfigParamSource& params, const char* name, char* out, size_t outSize,
                char* errBuf, size_t errBufSize) {
    const char* raw = configParamGet(params, name);
    if (raw == nullptr) {
        return true;  // not supplied - leave `out` unchanged
    }
    if (strlen(raw) > outSize - 1) {
        snprintf(errBuf, errBufSize, "%s must be at most %u characters", name,
                 (unsigned)(outSize - 1));
        return false;
    }
    snprintf(out, outSize, "%s", raw);
    return true;
}

// paramPasswordMaxLen: validates a supplied password param against a max
// length only (used for the STA password, whose validity is the remote
// network's concern, not ours).
bool paramPasswordMaxLen(const ConfigParamSource& params, const char* name, char* out,
                          size_t outSize, char* errBuf, size_t errBufSize) {
    const char* raw = configParamGet(params, name);
    if (raw == nullptr) {
        return true;
    }
    if (strlen(raw) > outSize - 1) {
        snprintf(errBuf, errBufSize, "%s must be at most %u characters", name,
                 (unsigned)(outSize - 1));
        return false;
    }
    snprintf(out, outSize, "%s", raw);
    return true;
}

// paramApPassword: validates the AP password against ESP32 SoftAP
// requirements — empty (open network) or WIFI_PASSWORD_MIN_LEN..MAX_LEN.
bool paramApPassword(const ConfigParamSource& params, const char* name, char* out, size_t outSize,
                      char* errBuf, size_t errBufSize) {
    const char* raw = configParamGet(params, name);
    if (raw == nullptr) {
        return true;
    }
    size_t len = strlen(raw);
    if (len != 0 && (len < WIFI_PASSWORD_MIN_LEN || len > WIFI_PASSWORD_MAX_LEN)) {
        snprintf(errBuf, errBufSize, "%s must be empty or %u..%u characters", name,
                 (unsigned)WIFI_PASSWORD_MIN_LEN, (unsigned)WIFI_PASSWORD_MAX_LEN);
        return false;
    }
    snprintf(out, outSize, "%s", raw);
    return true;
}

}  // namespace

void wifiApply(const ConfigParamSource& params, WifiConfig* working, WifiApplyResult* result) {
    *result = WifiApplyResult{};

    bool anySupplied = configParamHas(params, "wifiMode") || configParamHas(params, "staSsid") ||
                        configParamHas(params, "staPassword") || configParamHas(params, "apSsid") ||
                        configParamHas(params, "apPassword");
    if (!anySupplied) {
        setError(result, "no wifi fields supplied");
        return;
    }

    char err[192] = {0};

    if (configParamHas(params, "wifiMode")) {
        WifiMode mode;
        if (!wifiModeFromString(configParamGet(params, "wifiMode"), &mode)) {
            setError(result, "wifiMode must be client or standalone_ap");
            return;
        }
        working->mode = mode;
    }

    if (!paramSsid(params, "staSsid", working->sta_ssid, sizeof(working->sta_ssid), err,
                   sizeof(err))) {
        setError(result, err);
        return;
    }
    if (!paramPasswordMaxLen(params, "staPassword", working->sta_password,
                              sizeof(working->sta_password), err, sizeof(err))) {
        setError(result, err);
        return;
    }
    if (!paramSsid(params, "apSsid", working->ap_ssid, sizeof(working->ap_ssid), err,
                   sizeof(err))) {
        setError(result, err);
        return;
    }
    if (!paramApPassword(params, "apPassword", working->ap_password, sizeof(working->ap_password),
                         err, sizeof(err))) {
        setError(result, err);
        return;
    }

    if (working->mode == WifiMode::CLIENT && working->sta_ssid[0] == '\0') {
        setError(result, "staSsid is required for WiFi Client Mode");
        return;
    }
    if (working->mode == WifiMode::STANDALONE_AP && working->ap_ssid[0] == '\0') {
        setError(result, "apSsid is required for Standalone AP Mode");
        return;
    }

    working->provisioned = true;
    result->ok = true;
}
