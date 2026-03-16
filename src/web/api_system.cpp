// =============================================================================
// src/web/api_system.cpp
//
// System control API endpoints
//   POST /api/reboot          — request system restart
//   POST /upload/firmware     — OTA firmware binary update (U_FLASH)
//   POST /upload/filesystem   — OTA filesystem image update (U_SPIFFS / LittleFS)
//   POST /api/manual-command  — execute manual Marcduino command
// =============================================================================

#include "api_system.h"

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <Update.h>

#include "api_drive.h"
#include "logging.h"
#include "robot_state.h"
#include "web_server.h"

static const char* TAG = "WebServer";

// Rate limiting for manual command endpoint (max 10 commands per second)
static const uint32_t MANUAL_CMD_MIN_INTERVAL_MS = 100;
static uint32_t lastManualCmdMs = 0;

// Maximum allowed OTA upload size (4MB firmware, 1.5MB filesystem)
static const size_t MAX_FIRMWARE_SIZE = 4 * 1024 * 1024;
static const size_t MAX_FILESYSTEM_SIZE = 1536 * 1024;

void registerSystemRoutes(AsyncWebServer& server) {
    server.on("/api/manual-command", HTTP_POST, [](AsyncWebServerRequest* req) {
        uint32_t now = millis();
        if ((now - lastManualCmdMs) < MANUAL_CMD_MIN_INTERVAL_MS) {
            req->send(429, "application/json", "{\"ok\":false,\"error\":\"rate limit exceeded\"}");
            return;
        }
        lastManualCmdMs = now;

        const AsyncWebParameter* commandParam = req->getParam("command", true);
        if (commandParam == nullptr) {
            req->send(400, "application/json", "{\"ok\":false,\"error\":\"missing command\"}");
            return;
        }

        if (!executeManualCommand(commandParam->value())) {
            req->send(400, "application/json", "{\"ok\":false,\"error\":\"unsupported command\"}");
            return;
        }

        PA_LOG_INFO(TAG, "[WEB] POST /api/manual-command - executed %s",
                    commandParam->value().c_str());
        req->send(200, "application/json", "{\"ok\":true}");
    });

    server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest* req) {
        PA_LOG_INFO(TAG, "[WEB] POST /api/reboot - restart requested");
        req->send(200, "application/json", "{\"ok\":true}");
        requestSystemRestart(500);
    });

    server.on(
        "/upload/firmware", HTTP_POST,
        [](AsyncWebServerRequest* req) {
            bool ok = !Update.hasError();
            if (ok) {
                PA_LOG_INFO(TAG, "[WEB] POST /upload/firmware - update complete, reboot scheduled");
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
                size_t contentLength = req->contentLength();
                if (contentLength > MAX_FIRMWARE_SIZE) {
                    PA_LOG_ERROR(TAG, "Firmware upload rejected: size %u exceeds limit %u",
                                 (unsigned)contentLength, (unsigned)MAX_FIRMWARE_SIZE);
                    return;
                }
                PA_LOG_INFO(TAG, "OTA upload started: %s", filename.c_str());
                if (!Update.begin(contentLength, U_FLASH)) {
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

    // Filesystem OTA — U_SPIFFS targets the spiffs/littlefs partition.
    // LittleFS is automatically unmounted by the Update library during write.
    server.on(
        "/upload/filesystem", HTTP_POST,
        [](AsyncWebServerRequest* req) {
            bool ok = !Update.hasError();
            if (ok) {
                PA_LOG_INFO(TAG,
                            "[WEB] POST /upload/filesystem - update complete, reboot scheduled");
                req->send(200, "application/json", "{\"ok\":true}");
                requestSystemRestart(1000);
            } else {
                PA_LOG_ERROR(TAG, "POST /upload/filesystem - update failed");
                req->send(500, "application/json",
                          "{\"ok\":false,\"error\":\"filesystem update failed\"}");
            }
        },
        [](AsyncWebServerRequest* req, const String& filename, size_t index, uint8_t* data,
           size_t len, bool final) {
            (void)req;
            if (index == 0) {
                size_t contentLength = req->contentLength();
                if (contentLength > MAX_FILESYSTEM_SIZE) {
                    PA_LOG_ERROR(TAG, "Filesystem upload rejected: size %u exceeds limit %u",
                                 (unsigned)contentLength, (unsigned)MAX_FILESYSTEM_SIZE);
                    return;
                }
                PA_LOG_INFO(TAG, "Filesystem OTA upload started: %s", filename.c_str());
                if (!Update.begin(contentLength, U_SPIFFS)) {
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
}
