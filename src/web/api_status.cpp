// =============================================================================
// src/web/api_status.cpp
//
// Status and telemetry API endpoints
//   GET /api/status  — JSON status snapshot
//   GET /api/health  — health telemetry
//   GET /api/logs    — recent log buffer
//   GET /api/wifi    — WiFi status
//   GET /api/serial  — serial port status
// =============================================================================

#include "api_status.h"

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>

#include "api_helpers.h"
#include "config.h"
#include "dome_task.h"
#include "log_buffer.h"
#include "logging.h"
#include "robot_state.h"
#include "web_server.h"

static void buildWifiJson(char* buffer, size_t bufferSize) {
    wl_status_t staStatus = WiFi.status();
    bool staConnected = staStatus == WL_CONNECTED;
    bool staEnabled = WiFi.getMode() == WIFI_STA || WiFi.getMode() == WIFI_AP_STA;
    long wifiRssi = staConnected ? WiFi.RSSI() : 0;

    formatWifiJson(buffer, bufferSize, WIFI_AP_SSID, WiFi.softAPIP().toString().c_str(), staEnabled,
                   staConnected, staConnected ? WiFi.localIP().toString().c_str() : "", wifiRssi);
}

static void buildSerialJson(char* buffer, size_t bufferSize) {
    bool domeLinkActive = domeConnected();
    unsigned long hbRx;
    unsigned long hbTx;

    taskENTER_CRITICAL(&robotStateMux);
    hbRx = (unsigned long)robotState.domeHbRx;
    hbTx = (unsigned long)robotState.bodyHbTx;
    taskEXIT_CRITICAL(&robotStateMux);

    formatSerialJson(buffer, bufferSize, domeLinkActive, hbRx, hbTx);
}

static void buildHealthJson(char* buffer, size_t bufferSize) {
    bool estop;
    bool sbusSignalLost;
    bool sbusHwFailsafe;
    bool webControlEnabled;
    bool wifiConnected;
    bool wifiClientConnected;
    bool fsReady;
    unsigned long heapFree;
    unsigned long heapMin;
    long wifiRssi;

    taskENTER_CRITICAL(&robotStateMux);
    estop = robotState.estop;
    sbusSignalLost = robotState.sbusSignalLost;
    sbusHwFailsafe = robotState.sbusHwFailsafe;
    webControlEnabled = robotState.webControlEnabled;
    taskEXIT_CRITICAL(&robotStateMux);

    wifiConnected = WiFi.status() == WL_CONNECTED;
    wifiClientConnected = wifiConnected;
    fsReady = webLittleFsMounted();
    heapFree = ESP.getFreeHeap();
    heapMin = ESP.getMinFreeHeap();
    wifiRssi = wifiConnected ? WiFi.RSSI() : 0;

    formatHealthJson(buffer, bufferSize, estop, sbusSignalLost, sbusHwFailsafe, webControlEnabled,
                     wifiConnected, wifiClientConnected, fsReady, heapFree, heapMin, wifiRssi);
}

void registerStatusRoutes(AsyncWebServer& server) {
    server.on("/api/wifi", HTTP_GET, [](AsyncWebServerRequest* req) {
        char body[160];
        buildWifiJson(body, sizeof(body));
        req->send(200, "application/json", body);
    });

    server.on("/api/serial", HTTP_GET, [](AsyncWebServerRequest* req) {
        char body[768];
        buildSerialJson(body, sizeof(body));
        req->send(200, "application/json", body);
    });

    server.on("/api/health", HTTP_GET, [](AsyncWebServerRequest* req) {
        char body[256];
        buildHealthJson(body, sizeof(body));
        req->send(200, "application/json", body);
    });

    // Logs endpoint uses a mutex to prevent concurrent access to the static buffer.
    // Alternative approaches (chunked response, per-request allocation) are more
    // complex and unnecessary for this infrequent debug endpoint.
    static portMUX_TYPE logsMux = portMUX_INITIALIZER_UNLOCKED;
    server.on("/api/logs", HTTP_GET, [](AsyncWebServerRequest* req) {
        // Buffer sized to match ring capacity: LOG_BUFFER_LINES * LOG_LINE_MAX + 1.
        // Was 4096 static (always allocated); now sized to actual ring content max.
        static char body[LOG_BUFFER_LINES * LOG_LINE_MAX + 1];
        taskENTER_CRITICAL(&logsMux);
        copyRecentLogs(body, sizeof(body) - 1);
        taskEXIT_CRITICAL(&logsMux);
        req->send(200, "text/plain", body);
    });

    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* req) {
        char body[1024];
        buildStatusJson(body, sizeof(body));
        req->send(200, "application/json", body);
    });
}
