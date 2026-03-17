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

#include "logging.h"
#include "robot_state.h"
#include "web_server.h"

static const char* TAG = "RC";

void registerRcRoutes(AsyncWebServer& server) {
    server.on("/api/rc", HTTP_GET, [](AsyncWebServerRequest* req) {
        // Static buffer: ESPAsyncWebServer serialises handler invocations on the
        // async TCP task — concurrent GET /api/rc requests are not possible, so
        // this buffer is safe without an additional mutex.
        // req->send() copies the string synchronously before returning.
        static char rcBuf[2048];
        if (!buildRcDiagnosticsJson(rcBuf, sizeof(rcBuf))) {
            req->send(500, "application/json", "{\"ok\":false,\"error\":\"rc json build failed\"}");
            return;
        }
        req->send(200, "application/json", rcBuf);
        PA_LOG_DEBUG(TAG, "GET /api/rc");
    });

    server.on(
        "/api/rc/debug", HTTP_POST, [](AsyncWebServerRequest* req) {}, NULL,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
            StaticJsonDocument<64> doc;
            if (deserializeJson(doc, data, len)) {
                req->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid json\"}");
                return;
            }

            bool enabled = doc["enabled"] | false;

            taskENTER_CRITICAL(&robotStateMux);
            robotState.rcDebugMode = enabled;
            taskEXIT_CRITICAL(&robotStateMux);

            PA_LOG_INFO(TAG, "RC debug mode %s", enabled ? "enabled" : "disabled");

            req->send(200, "application/json", "{\"ok\":true}");
        });
}
