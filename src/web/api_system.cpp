// =============================================================================
// src/web/api_system.cpp
//
// System control API endpoints
//   POST /api/reboot          — request system restart
//   POST /api/manual-command  — execute manual Marcduino command
//
// The OTA upload routes live in api_upload.cpp: they are ported to the
// WebRequest seam (ADR 0021) and bound by the seam route table.
// =============================================================================

#include "api_system.h"

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <esp_core_dump.h>   // coredump fetch/erase (issue #8 observability)
#include <esp_partition.h>
#include <stdint.h>

#include "api_drive.h"
#include "api_helpers.h"
#include "commanded_modes.h"
#include "config_store.h"
#include "logging.h"
#include "robot_state.h"
#include "web_server.h"

static const char* TAG = "WebServer";

// Rate limiting for manual command endpoint (max 10 commands per second)
static const uint32_t MANUAL_CMD_MIN_INTERVAL_MS = 100;
static uint32_t lastManualCmdMs = 0;

static bool setSleepModeState(bool sleepMode, bool* changedOut) {
    bool changed = commandedSetSleep(sleepMode, SRC_WEB_API);

    if (changedOut != nullptr) {
        *changedOut = changed;
    }
    return true;
}

void registerSystemRoutes(AsyncWebServer& server) {
    server.on("/api/sleep", HTTP_POST, [](AsyncWebServerRequest* req) {
        bool changed = false;
        setSleepModeState(true, &changed);

        char body[96];
        if (!formatSleepControlJson(body, sizeof(body), true, changed)) {
            req->send(500, "application/json",
                      "{\"ok\":false,\"error\":\"sleep response overflow\"}");
            return;
        }

        if (changed) {
            requestStatusBroadcastNow();
        }
        PA_LOG_INFO(TAG, "[WEB] POST /api/sleep changed=%s", changed ? "true" : "false");
        req->send(200, "application/json", body);
    });

    server.on("/api/wake", HTTP_POST, [](AsyncWebServerRequest* req) {
        bool changed = false;
        setSleepModeState(false, &changed);

        char body[96];
        if (!formatSleepControlJson(body, sizeof(body), false, changed)) {
            req->send(500, "application/json",
                      "{\"ok\":false,\"error\":\"wake response overflow\"}");
            return;
        }

        if (changed) {
            requestStatusBroadcastNow();
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
        requestStatusBroadcastNow();
        req->send(200, "application/json", "{\"ok\":true}");
        requestSystemRestart(500);
    });

    // POST /upload/firmware and POST /upload/filesystem moved to
    // src/web/api_upload.cpp when they were ported to the WebRequest seam;
    // they are registered by the seam route table.

    // ---- Coredump (issue #8 observability) ----
    // The framework saves an ELF coredump to the `coredump` data partition on a
    // PANIC. These let an agent fetch + clear it over HTTP — the seated controller
    // cannot be USB-read (GPIO15/SBUS strapping). Decode with:
    //   esp-coredump info_corefile -c coredump.elf .pio/build/<env>/firmware.elf
    server.on("/api/coredump/status", HTTP_GET, [](AsyncWebServerRequest* req) {
        size_t addr = 0, size = 0;
        const bool present = (esp_core_dump_image_get(&addr, &size) == ESP_OK && size > 0);
        char body[80];
        snprintf(body, sizeof(body), "{\"present\":%s,\"size\":%u}",
                 present ? "true" : "false", (unsigned)size);
        req->send(200, "application/json", body);
    });

    server.on("/api/coredump", HTTP_GET, [](AsyncWebServerRequest* req) {
        size_t addr = 0, size = 0;
        if (esp_core_dump_image_get(&addr, &size) != ESP_OK || size == 0) {
            req->send(404, "application/json", "{\"ok\":false,\"error\":\"no coredump\"}");
            return;
        }
        const esp_partition_t* part = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, nullptr);
        if (part == nullptr) {
            req->send(500, "application/json",
                      "{\"ok\":false,\"error\":\"no coredump partition\"}");
            return;
        }
        // Stream straight from flash in chunks — no large heap buffer (the device
        // is heap-constrained, #8). The coredump image starts at partition offset 0.
        AsyncWebServerResponse* resp = req->beginChunkedResponse(
            "application/octet-stream",
            [part, size](uint8_t* buf, size_t maxLen, size_t index) -> size_t {
                if (index >= size) return 0;
                size_t toRead = size - index;
                if (toRead > maxLen) toRead = maxLen;
                if (esp_partition_read(part, index, buf, toRead) != ESP_OK) return 0;
                return toRead;
            });
        resp->addHeader("Content-Disposition", "attachment; filename=coredump.elf");
        req->send(resp);
    });

    server.on("/api/coredump/erase", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (esp_core_dump_image_erase() != ESP_OK) {
            req->send(500, "application/json", "{\"ok\":false,\"error\":\"erase failed\"}");
            return;
        }
        PA_LOG_INFO(TAG, "[WEB] coredump erased");
        req->send(200, "application/json", "{\"ok\":true}");
    });
}
