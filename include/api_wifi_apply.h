// =============================================================================
// include/api_wifi_apply.h
//
// Apply Core for POST /api/wifi (ADR 0011 pattern, ADR 0015 Device WiFi
// Settings), plus its ADR 0034 Commit Step. See src/web/api_wifi_apply.cpp.
//
// wifiApply(): pure function - no FreeRTOS, no request object, no
//   logging, no NVS, no WiFi hardware access. Reads parameters through a
//   ConfigParamSource, validates and mutates `working` in place, and writes a
//   field-level error on failure. Callers persist `working` via
//   wifiCommitApplied() only when result->ok is true; this is a Staged
//   Network Switch (ADR 0015) - settings are saved, not hot-applied.
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

// Commit Step for the POST /api/wifi Apply Core (ADR 0034, amended
// 2026-08-27): the complete transport-independent tail of a Device WiFi
// Settings write - persist to NVS (configSaveWifi), stage the config cache
// with the new settings (Staged Network Switch, ADR 0015 - saved, not
// hot-applied), and broadcast the new status. This is wifiApply()'s sibling,
// extracted per #227 phase 1 ahead of the Controller Console's WiFi write
// path (T11, #227 phase 2): the HTTP handler (handleWifiPost, api_config.cpp)
// and the future Console adapter both call this instead of each carrying
// their own copy of the sequence.
//
// `working` must already hold wifiApply()'s output (result->ok == true).
// Kept beside wifiApply() rather than beside the handler (unlike the
// config/audio Commit Step precedents, which live next to their HTTP
// handler) because the WiFi handler shares api_config.cpp with the
// unrelated config and RC-map handlers, while this module is the one a
// future Console WiFi adapter needs in isolation.
//
// WifiCommitOutcome carries the settings just staged plus the two runtime
// facts the response needs (pendingApply vs. the WiFi posture actually
// applied at boot, and whether the controller is currently in Network
// Recovery), both read after the cache is staged so every caller renders the
// same numbers instead of re-deriving them.
struct WifiCommitOutcome {
    bool persisted = false;       // false -> caller reports "failed to persist wifi settings"
    WifiConfig config;            // the settings just staged (mirrors *working)
    bool pendingApply = false;    // wifiConfigsDiffer(config, active-at-boot settings)
    bool networkRecovery = false; // configCacheReadActiveWifiRecovery() at commit time
};
WifiCommitOutcome wifiCommitApplied(WifiConfig* working);
