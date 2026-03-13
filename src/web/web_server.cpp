// =============================================================================
// src/web/web_server.cpp
//
// WiFi and AsyncWebServer bootstrap for protoArtoo.
// =============================================================================

#include "web_server.h"

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <WiFi.h>

#include "config.h"
#include "robot_state.h"

#if __has_include("secrets.h")
#include "secrets.h"
#endif

static const char* TAG = "WebServer";
static AsyncWebServer server(80);
static AsyncEventSource events("/api/events");
static bool littleFsReady = false;
static bool routesRegistered = false;
static bool serverStarted = false;
static bool eventTaskStarted = false;

namespace {

bool hasStaConfig() {
#if __has_include("secrets.h")
    return PA_STA_SSID[0] != '\0';
#else
    return false;
#endif
}

const char* apPassword() {
#if __has_include("secrets.h")
    return PA_AP_PASSWORD;
#else
    return nullptr;
#endif
}

const char* staSsid() {
#if __has_include("secrets.h")
    return PA_STA_SSID;
#else
    return nullptr;
#endif
}

const char* staPassword() {
#if __has_include("secrets.h")
    return PA_STA_PASSWORD;
#else
    return nullptr;
#endif
}

}  // namespace

void buildStatusJson(char* buffer, size_t bufferSize) {
    bool estop;
    bool webControlEnabled;
    bool sbusSignalLost;
    bool sbusHwFailsafe;
    bool webDriveExpired;
    bool wifiClientConnected;
    int failsafeSource;
    int driveSpeed;
    int driveSteer;
    float speedLimitScale;
    bool stationary;
    unsigned long failsafeCount;
    unsigned long uptimeMs;
    unsigned long heapFree;
    unsigned long heapMin;
    long wifiRssi;

    taskENTER_CRITICAL(&robotStateMux);
    estop = robotState.estop;
    webControlEnabled = robotState.webControlEnabled;
    sbusSignalLost = robotState.sbusSignalLost;
    sbusHwFailsafe = robotState.sbusHwFailsafe;
    webDriveExpired = robotState.webDriveExpired;
    wifiClientConnected = WiFi.status() == WL_CONNECTED;
    failsafeSource = (int)robotState.failsafeSource;
    driveSpeed = robotState.driveSpeed;
    driveSteer = robotState.driveSteer;
    speedLimitScale = robotState.speedLimitScale;
    stationary = robotState.stationary;
    failsafeCount = robotState.failsafeTriggerCount;
    taskEXIT_CRITICAL(&robotStateMux);
    uptimeMs = millis();
    heapFree = ESP.getFreeHeap();
    heapMin = ESP.getMinFreeHeap();
    wifiRssi = wifiClientConnected ? WiFi.RSSI() : 0;

    snprintf(buffer, bufferSize,
             "{\"estop\":%s,\"webControlEnabled\":%s,\"sbusSignalLost\":%s,\"sbusHwFailsafe\":%s,"
             "\"webDriveExpired\":%s,\"failsafeSource\":%d,\"driveSpeed\":%d,"
             "\"driveSteer\":%d,\"speedLimitScale\":%.3f,\"stationary\":%s,"
             "\"failsafeCount\":%lu,\"uptimeMs\":%lu,\"firmwareVersion\":\"%s\","
             "\"heapFree\":%lu,\"heapMin\":%lu,\"wifiRssi\":%ld,\"wifiClientConnected\":%s,"
             "\"littleFsReady\":%s}",
             estop ? "true" : "false", webControlEnabled ? "true" : "false",
             sbusSignalLost ? "true" : "false", sbusHwFailsafe ? "true" : "false",
             webDriveExpired ? "true" : "false", failsafeSource, driveSpeed, driveSteer,
             (double)speedLimitScale, stationary ? "true" : "false", failsafeCount, uptimeMs,
             PA_FIRMWARE_VERSION, heapFree, heapMin, wifiRssi,
             wifiClientConnected ? "true" : "false", littleFsReady ? "true" : "false");
}

bool webLittleFsMounted() {
    return littleFsReady;
}

void eventStreamTask(void*) {
    char body[384];

    for (;;) {
        if (serverStarted && events.count() > 0) {
            buildStatusJson(body, sizeof(body));
            events.send(body, "status", millis());
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void startHttpServerOnce() {
    if (serverStarted) {
        return;
    }

    if (!routesRegistered) {
        events.onConnect([](AsyncEventSourceClient* client) {
            char body[384];
            buildStatusJson(body, sizeof(body));
            client->send(body, "status", millis());
        });
        server.addHandler(&events);

        webRegisterRoutes(server);

        if (littleFsReady) {
            server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
        }

        routesRegistered = true;
    }

    server.begin();
    serverStarted = true;
    PA_LOG_INFO(TAG, "HTTP server started on port 80");
}

void handleWiFiEvent(WiFiEvent_t event) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_AP_START:
            PA_LOG_INFO(TAG, "AP started - SSID: %s  IP: %s", WIFI_AP_SSID,
                        WiFi.softAPIP().toString().c_str());
            startHttpServerOnce();
            break;
        case ARDUINO_EVENT_WIFI_STA_START:
            PA_LOG_INFO(TAG, "WiFi client connect started");
            break;
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            PA_LOG_INFO(TAG, "WiFi client got IP: %s", WiFi.localIP().toString().c_str());
            startHttpServerOnce();
            break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            PA_LOG_DEBUG(TAG, "WiFi client disconnected");
            break;
        default:
            break;
    }
}

void webServerInit() {
    if (routesRegistered || serverStarted) {
        PA_LOG_DEBUG(TAG, "web bootstrap already initialised");
        return;
    }

    littleFsReady = LittleFS.begin(true);
    if (littleFsReady) {
        PA_LOG_INFO(TAG, "LittleFS mounted");
    } else {
        PA_LOG_ERROR(TAG, "LittleFS mount failed - API only mode");
    }

    WiFi.onEvent(handleWiFiEvent);

    if (!eventTaskStarted) {
        xTaskCreatePinnedToCore(eventStreamTask, "WebEvents", 4096, nullptr, 1, nullptr, 0);
        eventTaskStarted = true;
    }

    if (hasStaConfig()) {
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAP(WIFI_AP_SSID, apPassword());
        WiFi.begin(staSsid(), staPassword());
        PA_LOG_INFO(TAG, "WiFi bootstrap: AP+WiFi client enabled");
    } else {
        WiFi.mode(WIFI_AP);
        WiFi.softAP(WIFI_AP_SSID);
        PA_LOG_INFO(TAG, "WiFi bootstrap: AP-only mode");
    }
}
