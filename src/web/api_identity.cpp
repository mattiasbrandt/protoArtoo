// =============================================================================
// src/web/api_identity.cpp
//
// Droid identity API endpoints
//   GET  /api/identity  — current cosmetic droid name and mDNS opt-in
//   POST /api/identity  — persist normalized droid name and mDNS opt-in
// =============================================================================

#include "api_identity.h"

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>

#include "api_helpers.h"
#include "config.h"
#include "config_store.h"
#include "logging.h"

static const char* TAG = "WebServer";

namespace {

void sendIdentityResponse(AsyncWebServerRequest* req, const SystemConfig& system) {
    char body[96] = {};
    if (!formatIdentityJson(body, sizeof(body), system.droid_name, system.mdns_use_name)) {
        req->send(500, "application/json",
                  "{\"ok\":false,\"error\":\"identity response overflow\"}");
        return;
    }
    req->send(200, "application/json", body);
}

}  // namespace

void registerIdentityRoutes(AsyncWebServer& server) {
    server.on("/api/identity", HTTP_GET, [](AsyncWebServerRequest* req) {
        ConfigSnapshot snap = {};
        configCacheRead(&snap);
        sendIdentityResponse(req, snap.system);
    });

    server.on("/api/identity", HTTP_POST, [](AsyncWebServerRequest* req) {
        const AsyncWebParameter* nameParam = req->getParam("droidName", true);
        if (nameParam == nullptr) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"droidName is required\"}");
            return;
        }

        char normalized[DROID_NAME_MAX_LEN + 1] = {};
        const String rawName = nameParam->value();
        if (!normalizeDroidName(rawName.c_str(), normalized, sizeof(normalized))) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"droidName must normalize to 1..32 DNS-safe characters\"}");
            return;
        }

        bool mdnsUseName = false;
        const AsyncWebParameter* mdnsParam = req->getParam("mdnsUseName", true);
        if (mdnsParam != nullptr) {
            if (!parseBoolValue(mdnsParam->value().c_str(), &mdnsUseName)) {
                req->send(400, "application/json",
                          "{\"ok\":false,\"error\":\"mdnsUseName must be true/false or 1/0\"}");
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
            req->send(500, "application/json",
                      "{\"ok\":false,\"error\":\"failed to persist identity\"}");
            return;
        }

        if (!configSaveSystem(prefs, working.system)) {
            prefs.end();
            req->send(500, "application/json",
                      "{\"ok\":false,\"error\":\"failed to persist identity\"}");
            return;
        }
        prefs.end();

        PA_LOG_INFO(TAG, "[WEB] POST /api/identity name=%s mdnsUseName=%s",
                    working.system.droid_name, working.system.mdns_use_name ? "true" : "false");
        sendIdentityResponse(req, working.system);
    });
}
