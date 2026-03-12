// =============================================================================
// src/web/api_estop.cpp
//
// WiFi AP + minimal web server for protoArtoo Phase 1.
// Hosts the estop endpoints required for Phase 1 safety testing.
// Full web UI (dashboard, config, OTA) is Phase 2.
//
// Endpoints:
//   POST /api/estop        — latch estop (requires explicit clear to resume)
//   POST /api/estop/clear  — clear estop (explicit human gate)
//   GET  /api/status       — JSON status snapshot
// =============================================================================

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>

#include "config.h"
#include "robot_state.h"

static const char* TAG = "WebServer";
static AsyncWebServer server(80);

// -----------------------------------------------------------------------------
// webServerInit()
// Start WiFi AP and register all Phase 1 endpoints.
// Must be called from setup() on Core 0 (WiFi runs on Core 0).
// -----------------------------------------------------------------------------
void webServerInit() {
    // Start WiFi in AP mode
    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_AP_SSID);
    Serial.printf("[%s] AP started — SSID: %s  IP: %s\n", TAG, WIFI_AP_SSID,
                  WiFi.softAPIP().toString().c_str());

    // POST /api/estop — latch emergency stop
    server.on("/api/estop", HTTP_POST, [](AsyncWebServerRequest* req) {
        taskENTER_CRITICAL(&robotStateMux);
        robotState.estop = true;
        robotState.failsafeSource = FS_ESTOP_CMD;
        robotState.failsafeTriggerCount++;
        taskEXIT_CRITICAL(&robotStateMux);
        Serial.printf("[%s] POST /api/estop — estop latched\n", TAG);
        req->send(200, "application/json", "{\"ok\":true}");
    });

    // POST /api/estop/clear — explicit human gate to resume
    server.on("/api/estop/clear", HTTP_POST, [](AsyncWebServerRequest* req) {
        taskENTER_CRITICAL(&robotStateMux);
        robotState.estop = false;
        // Only clear failsafeSource if it was an estop command
        if (robotState.failsafeSource == FS_ESTOP_CMD) {
            robotState.failsafeSource = FS_NONE;
        }
        taskEXIT_CRITICAL(&robotStateMux);
        Serial.printf("[%s] POST /api/estop/clear — estop cleared\n", TAG);
        req->send(200, "application/json", "{\"ok\":true}");
    });

    // GET /api/status — JSON status snapshot
    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* req) {
        bool estop;
        bool sbusSignalLost;
        bool sbusHwFailsafe;
        bool webDriveExpired;
        int failsafeSource;
        int driveSpeed;
        int driveSteer;
        float speedLimitScale;
        bool stationary;
        unsigned long failsafeCount;

        taskENTER_CRITICAL(&robotStateMux);
        estop = robotState.estop;
        sbusSignalLost = robotState.sbusSignalLost;
        sbusHwFailsafe = robotState.sbusHwFailsafe;
        webDriveExpired = robotState.webDriveExpired;
        failsafeSource = (int)robotState.failsafeSource;
        driveSpeed = robotState.driveSpeed;
        driveSteer = robotState.driveSteer;
        speedLimitScale = robotState.speedLimitScale;
        stationary = robotState.stationary;
        failsafeCount = robotState.failsafeTriggerCount;
        taskEXIT_CRITICAL(&robotStateMux);

        char body[256];
        snprintf(body, sizeof(body),
                 "{\"estop\":%s,\"sbusSignalLost\":%s,\"sbusHwFailsafe\":%s,"
                 "\"webDriveExpired\":%s,\"failsafeSource\":%d,\"driveSpeed\":%d,"
                 "\"driveSteer\":%d,\"speedLimitScale\":%.3f,\"stationary\":%s,"
                 "\"failsafeCount\":%lu}",
                 estop ? "true" : "false", sbusSignalLost ? "true" : "false",
                 sbusHwFailsafe ? "true" : "false", webDriveExpired ? "true" : "false",
                 failsafeSource, driveSpeed, driveSteer, (double)speedLimitScale,
                 stationary ? "true" : "false", failsafeCount);
        req->send(200, "application/json", body);
    });

    // 404 handler
    server.onNotFound([](AsyncWebServerRequest* req) {
        req->send(404, "application/json", "{\"ok\":false,\"error\":\"not found\"}");
    });

    server.begin();
    Serial.printf("[%s] HTTP server started on port 80\n", TAG);
}
