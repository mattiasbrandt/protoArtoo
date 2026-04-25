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
#include <stdint.h>

#include "api_drive.h"
#include "api_helpers.h"
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
static constexpr uintptr_t UPLOAD_STATE_NONE = 0;
static constexpr uintptr_t UPLOAD_STATE_REJECT_OVERSIZE = 1;
static constexpr uintptr_t UPLOAD_STATE_REJECT_INTERNAL = 2;

static inline uintptr_t getUploadState(const AsyncWebServerRequest* req) {
    return reinterpret_cast<uintptr_t>(req->_tempObject);
}

static inline void setUploadState(AsyncWebServerRequest* req, uintptr_t state) {
    req->_tempObject = reinterpret_cast<void*>(state);
}

static bool setSleepModeState(bool sleepMode, bool* changedOut) {
    uint32_t nowMs = millis();
    bool changed = false;

    taskENTER_CRITICAL(&robotStateMux);
    if (robotState.sleepMode != sleepMode) {
        robotState.sleepMode = sleepMode;
        robotState.sleepSinceMs = sleepMode ? nowMs : 0U;
        robotState.domeSleepSyncPending = true;
        robotState.domeSleepSyncSleepMode = sleepMode;
        changed = true;
    }
    taskEXIT_CRITICAL(&robotStateMux);

    if (changed) {
        requestStatusBroadcastNow();
    }
    if (changedOut != nullptr) {
        *changedOut = changed;
    }
    return true;
}

static bool webControlEnabledForSleepApi() {
    taskENTER_CRITICAL(&robotStateMux);
    bool enabled = robotState.webControlEnabled;
    taskEXIT_CRITICAL(&robotStateMux);
    return enabled;
}

void registerSystemRoutes(AsyncWebServer& server) {
    server.on("/api/sleep", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (!webControlEnabledForSleepApi()) {
            req->send(409, "application/json",
                      "{\"ok\":false,\"error\":\"web control is not enabled\"}");
            return;
        }

        bool changed = false;
        setSleepModeState(true, &changed);

        char body[96];
        if (!formatSleepControlJson(body, sizeof(body), true, changed)) {
            req->send(500, "application/json",
                      "{\"ok\":false,\"error\":\"sleep response overflow\"}");
            return;
        }

        PA_LOG_INFO(TAG, "[WEB] POST /api/sleep changed=%s", changed ? "true" : "false");
        req->send(200, "application/json", body);
    });

    server.on("/api/wake", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (!webControlEnabledForSleepApi()) {
            req->send(409, "application/json",
                      "{\"ok\":false,\"error\":\"web control is not enabled\"}");
            return;
        }

        bool changed = false;
        setSleepModeState(false, &changed);

        char body[96];
        if (!formatSleepControlJson(body, sizeof(body), false, changed)) {
            req->send(500, "application/json",
                      "{\"ok\":false,\"error\":\"wake response overflow\"}");
            return;
        }

        PA_LOG_INFO(TAG, "[WEB] POST /api/wake changed=%s", changed ? "true" : "false");
        req->send(200, "application/json", body);
    });

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
        const String& rawCommand = commandParam->value();

        taskENTER_CRITICAL(&robotStateMux);
        bool sleepMode = robotState.sleepMode;
        taskEXIT_CRITICAL(&robotStateMux);

        if (sleepMode && rawCommand.length() > 0) {
            const char prefix = rawCommand[0];
            bool blockedBySleep = prefix == '$' || prefix == ':' || prefix == '#' ||
                                  prefix == '*' || prefix == '@' || prefix == '%' ||
                                  prefix == '&' || prefix == '!';
            if (blockedBySleep) {
                req->send(423, "application/json",
                          "{\"error\":\"sleeping\",\"hint\":\"POST /api/wake\"}");
                return;
            }
        }

        if (!executeManualCommand(rawCommand)) {
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
            uintptr_t uploadState = getUploadState(req);
            setUploadState(req, UPLOAD_STATE_NONE);
            if (uploadState == UPLOAD_STATE_REJECT_OVERSIZE) {
                PA_LOG_ERROR(TAG, "POST /upload/firmware - rejected: payload too large");
                req->send(413, "application/json",
                          "{\"ok\":false,\"error\":\"firmware image exceeds upload size limit\"}");
                return;
            }
            if (uploadState == UPLOAD_STATE_REJECT_INTERNAL || Update.hasError()) {
                PA_LOG_ERROR(TAG, "POST /upload/firmware - update failed");
                req->send(500, "application/json", "{\"ok\":false,\"error\":\"update failed\"}");
                return;
            }

            PA_LOG_INFO(TAG, "[WEB] POST /upload/firmware - update complete, reboot scheduled");
            req->send(200, "application/json", "{\"ok\":true}");
            requestSystemRestart(1000);
        },
        [](AsyncWebServerRequest* req, const String& filename, size_t index, uint8_t* data,
           size_t len, bool final) {
            if (getUploadState(req) != UPLOAD_STATE_NONE) {
                return;
            }

            if (index == 0) {
                size_t contentLength = req->contentLength();
                if (contentLength > MAX_FIRMWARE_SIZE) {
                    PA_LOG_ERROR(TAG, "Firmware upload rejected: size %u exceeds limit %u",
                                 (unsigned)contentLength, (unsigned)MAX_FIRMWARE_SIZE);
                    setUploadState(req, UPLOAD_STATE_REJECT_OVERSIZE);
                    return;
                }
                PA_LOG_INFO(TAG, "OTA firmware upload started: %s (%u bytes)", filename.c_str(),
                            (unsigned)contentLength);
                // Use UPDATE_SIZE_UNKNOWN: req->contentLength() is the full multipart
                // body (including boundary overhead), not the raw firmware binary size.
                // Passing the exact content-length causes Update.end() to fail a size
                // check and silently roll back to the old firmware. UPDATE_SIZE_UNKNOWN
                // skips that check and accepts however many bytes are written.
                if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
                    Update.printError(Serial);
                    setUploadState(req, UPLOAD_STATE_REJECT_INTERNAL);
                    return;
                }
            }

            if (len > 0 && Update.write(data, len) != len) {
                Update.printError(Serial);
                setUploadState(req, UPLOAD_STATE_REJECT_INTERNAL);
                return;
            }

            if (final && !Update.end(true)) {
                Update.printError(Serial);
                setUploadState(req, UPLOAD_STATE_REJECT_INTERNAL);
            }
        });

    // Filesystem OTA — U_SPIFFS targets the spiffs/littlefs partition.
    // LittleFS is automatically unmounted by the Update library during write.
    server.on(
        "/upload/filesystem", HTTP_POST,
        [](AsyncWebServerRequest* req) {
            uintptr_t uploadState = getUploadState(req);
            setUploadState(req, UPLOAD_STATE_NONE);
            if (uploadState == UPLOAD_STATE_REJECT_OVERSIZE) {
                PA_LOG_ERROR(TAG, "POST /upload/filesystem - rejected: payload too large");
                req->send(
                    413, "application/json",
                    "{\"ok\":false,\"error\":\"filesystem image exceeds upload size limit\"}");
                return;
            }
            if (uploadState == UPLOAD_STATE_REJECT_INTERNAL || Update.hasError()) {
                PA_LOG_ERROR(TAG, "POST /upload/filesystem - update failed");
                req->send(500, "application/json",
                          "{\"ok\":false,\"error\":\"filesystem update failed\"}");
                return;
            }

            PA_LOG_INFO(TAG, "[WEB] POST /upload/filesystem - update complete, reboot scheduled");
            req->send(200, "application/json", "{\"ok\":true}");
            requestSystemRestart(1000);
        },
        [](AsyncWebServerRequest* req, const String& filename, size_t index, uint8_t* data,
           size_t len, bool final) {
            if (getUploadState(req) != UPLOAD_STATE_NONE) {
                return;
            }

            if (index == 0) {
                size_t contentLength = req->contentLength();
                if (contentLength > MAX_FILESYSTEM_SIZE) {
                    PA_LOG_ERROR(TAG, "Filesystem upload rejected: size %u exceeds limit %u",
                                 (unsigned)contentLength, (unsigned)MAX_FILESYSTEM_SIZE);
                    setUploadState(req, UPLOAD_STATE_REJECT_OVERSIZE);
                    return;
                }
                PA_LOG_INFO(TAG, "OTA filesystem upload started: %s (%u bytes)", filename.c_str(),
                            (unsigned)contentLength);
                // Use UPDATE_SIZE_UNKNOWN for the same reason as firmware: multipart
                // content-length includes boundary overhead that Update.end() would
                // mismatch against written bytes, causing silent rollback.
                if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_SPIFFS)) {
                    Update.printError(Serial);
                    setUploadState(req, UPLOAD_STATE_REJECT_INTERNAL);
                    return;
                }
            }
            if (len > 0 && Update.write(data, len) != len) {
                Update.printError(Serial);
                setUploadState(req, UPLOAD_STATE_REJECT_INTERNAL);
                return;
            }
            if (final && !Update.end(true)) {
                Update.printError(Serial);
                setUploadState(req, UPLOAD_STATE_REJECT_INTERNAL);
            }
        });
}
