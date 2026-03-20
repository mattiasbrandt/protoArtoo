// =============================================================================
// src/web/api_estop.cpp
//
// ESTOP API endpoints
//   POST /api/estop        — latch estop (requires explicit clear to resume)
//   POST /api/estop/clear  — clear estop (explicit human gate)
// =============================================================================

#include "api_estop.h"

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

#include "logging.h"
#include "robot_state.h"

static const char* TAG = "WebServer";

void registerEstopRoutes(AsyncWebServer& server) {
    server.on("/api/estop/clear", HTTP_POST, [](AsyncWebServerRequest* req) {
        taskENTER_CRITICAL(&robotStateMux);
        robotState.estop = false;
        if (robotState.failsafeSource == FS_ESTOP_CMD) {
            robotState.failsafeSource = FS_NONE;
        }
        taskEXIT_CRITICAL(&robotStateMux);
        PA_LOG_INFO(TAG, "[WEB] POST /api/estop/clear - estop cleared");
        req->send(200, "application/json", "{\"ok\":true}");
    });

    server.on("/api/estop", HTTP_POST, [](AsyncWebServerRequest* req) {
        taskENTER_CRITICAL(&robotStateMux);
        robotState.estop = true;
        recordFailsafeTriggerLocked(FS_ESTOP_CMD, millis());
        taskEXIT_CRITICAL(&robotStateMux);
        PA_LOG_INFO(TAG, "[WEB] POST /api/estop - estop latched");
        req->send(200, "application/json", "{\"ok\":true}");
    });
}
