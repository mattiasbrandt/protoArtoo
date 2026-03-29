// =============================================================================
// src/web/api_rc.cpp
//
// RC diagnostics API endpoints
//   GET  /api/rc       — JSON RC diagnostics snapshot
//   POST /api/rc/debug — Toggle verbose RC logging on/off
// =============================================================================

#include "api_rc.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <stdlib.h>
#include <string.h>

#include "logging.h"
#include "robot_state.h"
#include "rc_diagnostics_snapshot.h"

static const char* TAG = "RC";
static constexpr size_t RC_DEBUG_BODY_MAX = 128;

void registerRcRoutes(AsyncWebServer& server) {
    server.on("/api/rc", HTTP_GET, [](AsyncWebServerRequest* req) {
        RcDiagnosticsSnapshot snap;
        captureRcDiagnosticsSnapshot(&snap);
        JsonDocument doc;
        if (!populateRcDiagnosticsJson(doc, snap)) {
            req->send(500, "application/json", "{\"ok\":false,\"error\":\"rc json build failed\"}");
            return;
        }
        auto* stream = req->beginResponseStream("application/json");
        if (stream == nullptr) {
            req->send(500, "application/json",
                      "{\"ok\":false,\"error\":\"response stream alloc failed\"}");
            return;
        }
        serializeJson(doc, *stream);
        req->send(stream);
        PA_LOG_DEBUG(TAG, "GET /api/rc");
    });

    server.on(
        "/api/rc/debug", HTTP_POST, [](AsyncWebServerRequest* req) {}, NULL,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            if (index == 0) {
                if (total == 0 || total > RC_DEBUG_BODY_MAX) {
                    req->send(413, "application/json",
                              "{\"ok\":false,\"error\":\"payload too large\"}");
                    req->_tempObject = nullptr;
                    return;
                }
                char* body = (char*)malloc(total + 1);
                if (body == nullptr) {
                    req->send(500, "application/json",
                              "{\"ok\":false,\"error\":\"request buffer alloc failed\"}");
                    req->_tempObject = nullptr;
                    return;
                }
                req->_tempObject = body;
            }

            char* body = (char*)req->_tempObject;
            if (body == nullptr) {
                return;
            }
            if ((index + len) > total) {
                free(body);
                req->_tempObject = nullptr;
                req->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid body chunks\"}");
                return;
            }

            if (len > 0) {
                memcpy(body + index, data, len);
            }

            if ((index + len) != total) {
                return;
            }

            body[total] = '\0';
            JsonDocument doc;
            if (deserializeJson(doc, body, total)) {
                free(body);
                req->_tempObject = nullptr;
                req->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid json\"}");
                return;
            }
            free(body);
            req->_tempObject = nullptr;

            bool enabled = doc["enabled"] | false;

            taskENTER_CRITICAL(&robotStateMux);
            robotState.rcDebugMode = enabled;
            taskEXIT_CRITICAL(&robotStateMux);

            PA_LOG_INFO(TAG, "RC debug mode %s", enabled ? "enabled" : "disabled");

            req->send(200, "application/json", "{\"ok\":true}");
        });
}
