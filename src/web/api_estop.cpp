// =============================================================================
// src/web/api_estop.cpp
//
// Endpoints:
//   POST /api/estop        — latch estop (requires explicit clear to resume)
//   POST /api/estop/clear  — clear estop (explicit human gate)
//   POST /api/drive        — browser drive command (timeout-protected)
//   GET  /api/status       — JSON status snapshot
// =============================================================================

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <Update.h>
#include <WiFi.h>

#include "api_helpers.h"
#include "config.h"
#include "robot_state.h"
#include "web_server.h"

static const char* TAG = "WebServer";

namespace {

void buildConfigJson(char* buffer, size_t bufferSize) {
    int16_t speedLimitMax;
    uint32_t webDriveTimeoutMs;
    bool ch8ModeLock;

    taskENTER_CRITICAL(&robotStateMux);
    speedLimitMax = robotState.cfg_speedLimitMax;
    webDriveTimeoutMs = robotState.cfg_webDriveTimeoutMs;
    ch8ModeLock = robotState.cfg_ch8ModeLock;
    taskEXIT_CRITICAL(&robotStateMux);

    formatConfigJson(buffer, bufferSize, speedLimitMax, webDriveTimeoutMs, ch8ModeLock);
}

void buildWifiJson(char* buffer, size_t bufferSize) {
    wl_status_t staStatus = WiFi.status();
    bool staConnected = staStatus == WL_CONNECTED;
    bool staEnabled = WiFi.getMode() == WIFI_STA || WiFi.getMode() == WIFI_AP_STA;

    formatWifiJson(buffer, bufferSize, WIFI_AP_SSID, WiFi.softAPIP().toString().c_str(), staEnabled,
                   staConnected, staConnected ? WiFi.localIP().toString().c_str() : "");
}

void buildSerialJson(char* buffer, size_t bufferSize) {
    bool domeLinkActive = domeConnected();
    unsigned long hbRx;
    unsigned long hbTx;

    taskENTER_CRITICAL(&robotStateMux);
    hbRx = (unsigned long)robotState.domeHbRx;
    hbTx = (unsigned long)robotState.bodyHbTx;
    taskEXIT_CRITICAL(&robotStateMux);

    formatSerialJson(buffer, bufferSize, domeLinkActive, hbRx, hbTx);
}

void buildHealthJson(char* buffer, size_t bufferSize) {
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

bool executeManualCommand(const String& raw) {
    String command = raw;
    command.trim();
    command.toLowerCase();

    ManualCommand cmd = resolveManualCommand(command.c_str());

    switch (cmd) {
        case MC_ESTOP:
            taskENTER_CRITICAL(&robotStateMux);
            robotState.estop = true;
            robotState.failsafeSource = FS_ESTOP_CMD;
            robotState.failsafeTriggerCount++;
            taskEXIT_CRITICAL(&robotStateMux);
            return true;

        case MC_CLEAR_ESTOP:
            taskENTER_CRITICAL(&robotStateMux);
            robotState.estop = false;
            if (robotState.failsafeSource == FS_ESTOP_CMD) {
                robotState.failsafeSource = FS_NONE;
            }
            taskEXIT_CRITICAL(&robotStateMux);
            return true;

        case MC_ENABLE_WEB_CONTROL:
            taskENTER_CRITICAL(&robotStateMux);
            robotState.webControlEnabled = true;
            taskEXIT_CRITICAL(&robotStateMux);
            return true;

        case MC_DISABLE_WEB_CONTROL:
            taskENTER_CRITICAL(&robotStateMux);
            robotState.webControlEnabled = false;
            taskEXIT_CRITICAL(&robotStateMux);
            setDriveCommand(0, 0, SRC_INTERNAL);
            return true;

        case MC_REBOOT:
            requestSystemRestart(500);
            return true;

        case MC_UNKNOWN:
        default:
            return false;
    }
}

}  // namespace

// -----------------------------------------------------------------------------
// Register all currently supported HTTP routes.
// -----------------------------------------------------------------------------
void webRegisterRoutes(AsyncWebServer& server) {
    // POST /api/estop/clear — explicit human gate to resume
    server.on("/api/estop/clear", HTTP_POST, [](AsyncWebServerRequest* req) {
        taskENTER_CRITICAL(&robotStateMux);
        robotState.estop = false;
        // Only clear failsafeSource if it was an estop command
        if (robotState.failsafeSource == FS_ESTOP_CMD) {
            robotState.failsafeSource = FS_NONE;
        }
        taskEXIT_CRITICAL(&robotStateMux);
        PA_LOG_INFO(TAG, "POST /api/estop/clear - estop cleared");
        req->send(200, "application/json", "{\"ok\":true}");
    });

    server.on("/api/web-control/enable", HTTP_POST, [](AsyncWebServerRequest* req) {
        taskENTER_CRITICAL(&robotStateMux);
        robotState.webControlEnabled = true;
        taskEXIT_CRITICAL(&robotStateMux);
        PA_LOG_INFO(TAG, "POST /api/web-control/enable - browser control enabled");
        req->send(200, "application/json", "{\"ok\":true}");
    });

    server.on("/api/web-control/disable", HTTP_POST, [](AsyncWebServerRequest* req) {
        taskENTER_CRITICAL(&robotStateMux);
        robotState.webControlEnabled = false;
        taskEXIT_CRITICAL(&robotStateMux);
        setDriveCommand(0, 0, SRC_INTERNAL);
        PA_LOG_INFO(TAG, "POST /api/web-control/disable - browser control disabled");
        req->send(200, "application/json", "{\"ok\":true}");
    });

    // POST /api/estop — latch emergency stop
    server.on("/api/estop", HTTP_POST, [](AsyncWebServerRequest* req) {
        taskENTER_CRITICAL(&robotStateMux);
        robotState.estop = true;
        robotState.failsafeSource = FS_ESTOP_CMD;
        robotState.failsafeTriggerCount++;
        taskEXIT_CRITICAL(&robotStateMux);
        PA_LOG_INFO(TAG, "POST /api/estop - estop latched");
        req->send(200, "application/json", "{\"ok\":true}");
    });

    server.on("/api/drive", HTTP_POST, [](AsyncWebServerRequest* req) {
        const AsyncWebParameter* speedParam = req->getParam("speed", true);
        const AsyncWebParameter* steerParam = req->getParam("steer", true);

        if (speedParam == nullptr || steerParam == nullptr) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"missing speed or steer\"}");
            return;
        }

        int16_t speed = 0;
        int16_t steer = 0;
        if (!parseDriveValue(speedParam->value().c_str(), &speed) ||
            !parseDriveValue(steerParam->value().c_str(), &steer)) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"speed and steer must be integers\"}");
            return;
        }

        bool blocked;
        bool sbusHealthy;
        int16_t maxOut;

        taskENTER_CRITICAL(&robotStateMux);
        sbusHealthy = !robotState.sbusSignalLost && !robotState.sbusHwFailsafe;
        blocked = robotState.estop || robotState.stationary ||
                  (!sbusHealthy && !robotState.webControlEnabled);
        maxOut = robotState.cfg_speedLimitMax;
        taskEXIT_CRITICAL(&robotStateMux);

        if (blocked) {
            req->send(409, "application/json",
                      "{\"ok\":false,\"error\":\"drive blocked by safety state\"}");
            return;
        }

        int16_t clampedSpeed = constrain(speed, (int)-maxOut, (int)maxOut);
        int16_t clampedSteer = constrain(steer, (int)-maxOut, (int)maxOut);
        setDriveCommand(clampedSpeed, clampedSteer, SRC_WEB_API);

        taskENTER_CRITICAL(&robotStateMux);
        robotState.webDriveExpired = false;
        if (robotState.failsafeSource == FS_WEB_TIMEOUT) {
            robotState.failsafeSource = FS_NONE;
        }
        taskEXIT_CRITICAL(&robotStateMux);

        req->send(200, "application/json", "{\"ok\":true}");
    });

    server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest* req) {
        char body[128];
        buildConfigJson(body, sizeof(body));
        req->send(200, "application/json", body);
    });

    server.on("/api/config", HTTP_POST, [](AsyncWebServerRequest* req) {
        bool updateSpeedLimit = false;
        bool updateWebTimeout = false;
        bool updateCh8ModeLock = false;
        int16_t speedLimitMax = 0;
        uint32_t webDriveTimeoutMs = 0;
        bool ch8ModeLock = false;

        if (req->hasParam("speedLimitMax", true)) {
            uint32_t parsed = 0;
            if (!parseUint32Value(req->getParam("speedLimitMax", true)->value().c_str(), &parsed) ||
                parsed > SPEED_LIMIT_MAX) {
                req->send(400, "application/json",
                          "{\"ok\":false,\"error\":\"speedLimitMax must be 0..600\"}");
                return;
            }
            speedLimitMax = (int16_t)parsed;
            updateSpeedLimit = true;
        }

        if (req->hasParam("webDriveTimeoutMs", true)) {
            if (!parseUint32Value(req->getParam("webDriveTimeoutMs", true)->value().c_str(),
                                  &webDriveTimeoutMs) ||
                webDriveTimeoutMs < 100 || webDriveTimeoutMs > 5000) {
                req->send(400, "application/json",
                          "{\"ok\":false,\"error\":\"webDriveTimeoutMs must be 100..5000\"}");
                return;
            }
            updateWebTimeout = true;
        }

        if (req->hasParam("ch8ModeLock", true)) {
            if (!parseBoolValue(req->getParam("ch8ModeLock", true)->value().c_str(),
                                &ch8ModeLock)) {
                req->send(400, "application/json",
                          "{\"ok\":false,\"error\":\"ch8ModeLock must be true/false or 1/0\"}");
                return;
            }
            updateCh8ModeLock = true;
        }

        if (!updateSpeedLimit && !updateWebTimeout && !updateCh8ModeLock) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"no supported config fields supplied\"}");
            return;
        }

        taskENTER_CRITICAL(&robotStateMux);
        if (updateSpeedLimit) {
            robotState.cfg_speedLimitMax = speedLimitMax;
        }
        if (updateWebTimeout) {
            robotState.cfg_webDriveTimeoutMs = webDriveTimeoutMs;
        }
        if (updateCh8ModeLock) {
            robotState.cfg_ch8ModeLock = ch8ModeLock;
        }
        taskEXIT_CRITICAL(&robotStateMux);

        if (!saveConfigToNvs()) {
            req->send(500, "application/json",
                      "{\"ok\":false,\"error\":\"failed to persist config\"}");
            return;
        }

        char body[128];
        buildConfigJson(body, sizeof(body));
        req->send(200, "application/json", body);
    });

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

    server.on("/api/logs", HTTP_GET, [](AsyncWebServerRequest* req) {
        static char body[8192];
        copyRecentLogs(body, sizeof(body) - 1);
        req->send(200, "text/plain", body);
    });

    server.on("/api/manual-command", HTTP_POST, [](AsyncWebServerRequest* req) {
        const AsyncWebParameter* commandParam = req->getParam("command", true);
        if (commandParam == nullptr) {
            req->send(400, "application/json", "{\"ok\":false,\"error\":\"missing command\"}");
            return;
        }

        if (!executeManualCommand(commandParam->value())) {
            req->send(400, "application/json", "{\"ok\":false,\"error\":\"unsupported command\"}");
            return;
        }

        PA_LOG_INFO(TAG, "POST /api/manual-command - executed %s", commandParam->value().c_str());
        req->send(200, "application/json", "{\"ok\":true}");
    });

    server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest* req) {
        PA_LOG_INFO(TAG, "POST /api/reboot - restart requested");
        req->send(200, "application/json", "{\"ok\":true}");
        requestSystemRestart(500);
    });

    server.on(
        "/upload/firmware", HTTP_POST,
        [](AsyncWebServerRequest* req) {
            bool ok = !Update.hasError();
            if (ok) {
                PA_LOG_INFO(TAG, "POST /upload/firmware - update complete, reboot scheduled");
                req->send(200, "application/json", "{\"ok\":true}");
                requestSystemRestart(1000);
            } else {
                PA_LOG_ERROR(TAG, "POST /upload/firmware - update failed");
                req->send(500, "application/json", "{\"ok\":false,\"error\":\"update failed\"}");
            }
        },
        [](AsyncWebServerRequest* req, const String& filename, size_t index, uint8_t* data,
           size_t len, bool final) {
            (void)req;

            if (index == 0) {
                PA_LOG_INFO(TAG, "OTA upload started: %s", filename.c_str());
                if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
                    Update.printError(Serial);
                    return;
                }
            }

            if (len > 0 && Update.write(data, len) != len) {
                Update.printError(Serial);
            }

            if (final && !Update.end(true)) {
                Update.printError(Serial);
            }
        });

    // GET /api/status — JSON status snapshot
    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* req) {
        char body[384];
        buildStatusJson(body, sizeof(body));
        req->send(200, "application/json", body);
    });

    // 404 handler
    server.onNotFound([](AsyncWebServerRequest* req) {
        req->send(404, "application/json", "{\"ok\":false,\"error\":\"not found\"}");
    });

    PA_LOG_DEBUG(TAG, "HTTP routes registered");
}
