// =============================================================================
// include/api_wifi_apply.h
//
// Apply Core for POST /api/wifi (ADR 0011 pattern, ADR 0015 Device WiFi
// Settings). See src/web/api_wifi_apply.cpp.
//
// wifiApply(): pure function - no FreeRTOS, no request object, no
//   logging, no NVS, no WiFi hardware access. Reads parameters through a
//   ConfigParamSource, validates and mutates `working` in place, and writes a
//   field-level error on failure. Callers persist `working` themselves
//   (configSaveWifi) only when result->ok is true; this is a Staged Network
//   Switch (ADR 0015) - settings are saved, not hot-applied.
// =============================================================================
#pragma once

#include "api_param_source.h"
#include "config_cache.h"

struct WifiApplyResult {
    bool ok = false;
    char errorMessage[192] = {0};
};

// `working` must already hold the currently persisted WifiConfig (caller
// reads it via configCacheReadWifi before calling). Only parameters the
// caller supplied are changed. Password fields are write-only and
// omission-preserving: omitted -> keep the stored value, supplied (including
// an explicit empty string) -> overwrite. On success, working->provisioned
// is set true and result->ok is true.
void wifiApply(const ConfigParamSource& params, WifiConfig* working, WifiApplyResult* result);
