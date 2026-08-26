// =============================================================================
// src/web/api_identity.cpp
//
// Droid identity API endpoints
//   GET  /api/identity  - current cosmetic droid name and mDNS opt-in
//   POST /api/identity  - persist validated droid name and mDNS opt-in
//
// First route ported to the WebRequest seam (ADR 0021): the same handler
// source compiles and serves under every backend and names no vendor type.
// =============================================================================

#include "api_identity.h"

#include <Preferences.h>
#include <stdio.h>

#include "api_helpers.h"
#include "api_json_response.h"
#include "config.h"
#include "config_store.h"
#include "config_cache.h"
#include "logging.h"
#include "web_request.h"

static const char* TAG = "WebServer";

namespace {

void sendIdentityResponse(WebRequest& req, const SystemConfig& system) {
    // Fixed buffer for identity JSON serialization including the manifest.
    // IDENTITY_JSON_MAX_BYTES = 384 B; usable JSON is 383 B (1 byte for NUL).
    // Worst case: 32-char droid name (DROID_NAME_MAX_LEN), mdnsUseName false,
    // and every manifest value false (5 chars beats true's 4 chars) = 258 B JSON.
    // Current manifest: 3 capabilities (18+18+31=67 chars) + 3 flags (14+16+18=48 chars) = 298 B JSON.
    // Headroom: 383 - 298 = 85 B. Cost per additional row: string literal `,\"<name>\":false` costs
    // ~(name_len + 13) bytes. Every capability/flag added to the manifest grows this payload toward the ceiling.
    char body[IDENTITY_JSON_MAX_BYTES] = {};
    if (!formatIdentityJson(body, sizeof(body), system.droid_name, system.mdns_use_name)) {
        webSendJsonError(req, 500, "identity response overflow");
        return;
    }
    req.send(200, "application/json", body);
}

}  // namespace

void handleIdentityGet(WebRequest& req) {
    ConfigSnapshot snap = {};
    configCacheRead(&snap);
    sendIdentityResponse(req, snap.system);
}

void handleIdentityPost(WebRequest& req) {
    // Oversized relative to DROID_NAME_MAX_LEN so an over-long submission
    // still reaches normalizeDroidName() as an over-long string and is
    // rejected, instead of being truncated into a silently valid name.
    char rawName[DROID_NAME_MAX_LEN * 2 + 2] = {};
    if (!req.param("droidName", rawName, sizeof(rawName))) {
        webSendJsonError(req, 400, "droidName is required");
        return;
    }

    char normalized[DROID_NAME_MAX_LEN + 1] = {};
    if (!normalizeDroidName(rawName, normalized, sizeof(normalized))) {
        webSendJsonError(req, 400, "droidName must be 1..32 lowercase letters, numbers, or hyphens; spaces are not allowed");
        return;
    }

    bool mdnsUseName = false;
    char rawMdns[16] = {};
    if (req.param("mdnsUseName", rawMdns, sizeof(rawMdns))) {
        if (!parseBoolValue(rawMdns, &mdnsUseName)) {
            webSendJsonError(req, 400, "mdnsUseName must be true/false or 1/0");
            return;
        }
    }

    ConfigSnapshot working = {};
    configCacheRead(&working);
    snprintf(working.system.droid_name, sizeof(working.system.droid_name), "%s", normalized);
    working.system.mdns_use_name = mdnsUseName;
    configCacheApply(working);

    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) {
        webSendJsonError(req, 500, "failed to persist identity");
        return;
    }

    if (!configSaveSystem(prefs, working.system)) {
        prefs.end();
        webSendJsonError(req, 500, "failed to persist identity");
        return;
    }
    prefs.end();

    PA_LOG_INFO(TAG, "[WEB] POST /api/identity name=%s mdnsUseName=%s",
                working.system.droid_name, working.system.mdns_use_name ? "true" : "false");
    sendIdentityResponse(req, working.system);
}
