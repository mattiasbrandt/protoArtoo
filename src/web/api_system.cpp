// =============================================================================
// src/web/api_system.cpp
//
// System control API endpoints
//   POST /api/sleep            — enter cosmetic sleep mode
//   POST /api/wake             — leave cosmetic sleep mode
//   POST /api/manual-command   — execute manual Marcduino command
//   POST /api/reboot           — request system restart
//   GET  /api/coredump/status  — is a saved coredump present
//   GET  /api/coredump         — stream the saved coredump image
//   POST /api/coredump/erase   — clear the saved coredump
//
// All written against the project-owned WebRequest seam (ADR 0021) and bound
// by the seam route table. The OTA upload routes live in api_upload.cpp.
// =============================================================================

#include "api_system.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <esp_core_dump.h>
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

namespace {

void sendSleepControlResponse(WebRequest& req, bool sleepMode, bool changed, const char* label) {
    char body[96];
    if (!formatSleepControlJson(body, sizeof(body), sleepMode, changed)) {
        char err[96];
        snprintf(err, sizeof(err), "{\"ok\":false,\"error\":\"%s response overflow\"}", label);
        req.send(500, "application/json", err);
        return;
    }
    if (changed) {
        requestStatusBroadcastNow();
    }
    PA_LOG_INFO(TAG, "[WEB] POST /api/%s changed=%s", label, changed ? "true" : "false");
    req.send(200, "application/json", body);
}

// The coredump partition and image size for an in-flight /api/coredump read.
//
// File-scope because the seam's chunked body producer is a plain function
// pointer with no capture: handlers serialize on one task under both backends,
// so exactly one coredump read can be in flight, and this is the same
// single-server-task argument the shared response buffers already rest on.
const esp_partition_t* s_coredumpPartition = nullptr;
size_t s_coredumpSize = 0;

size_t fillCoredumpResponse(uint8_t* out, size_t capacity, size_t offset) {
    if (s_coredumpPartition == nullptr || offset >= s_coredumpSize) {
        return 0;
    }
    size_t toRead = s_coredumpSize - offset;
    if (toRead > capacity) {
        toRead = capacity;
    }
    if (esp_partition_read(s_coredumpPartition, offset, out, toRead) != ESP_OK) {
        return 0;
    }
    return toRead;
}

}  // namespace

void handleSleepPost(WebRequest& req) {
    bool changed = false;
    setSleepModeState(true, &changed);
    sendSleepControlResponse(req, true, changed, "sleep");
}

void handleWakePost(WebRequest& req) {
    bool changed = false;
    setSleepModeState(false, &changed);
    sendSleepControlResponse(req, false, changed, "wake");
}

void handleManualCommandPost(WebRequest& req) {
    uint32_t now = millis();
    if ((now - lastManualCmdMs) < MANUAL_CMD_MIN_INTERVAL_MS) {
        req.send(429, "application/json", "{\"ok\":false,\"error\":\"rate limit exceeded\"}");
        return;
    }
    lastManualCmdMs = now;

    // Borrowed, not copied: a Marcduino command is short, but the value has to
    // outlive the guard checks below and reach executeManualCommand() intact.
    const char* rawCommand = req.paramRef("command");
    if (rawCommand == nullptr) {
        req.send(400, "application/json", "{\"ok\":false,\"error\":\"missing command\"}");
        return;
    }

    taskENTER_CRITICAL(&robotStateMux);
    bool sleepMode = robotState.sleepMode;
    taskEXIT_CRITICAL(&robotStateMux);

    if (sleepMode && rawCommand[0] != '\0') {
        const char prefix = rawCommand[0];
        bool blockedBySleep = prefix == '$' || prefix == ':' || prefix == '#' ||
                              prefix == '*' || prefix == '@' || prefix == '%' ||
                              prefix == '&' || prefix == '!';
        if (blockedBySleep) {
            req.send(423, "application/json",
                     "{\"error\":\"sleeping\",\"hint\":\"POST /api/wake\"}");
            return;
        }
    }

    if (!executeManualCommand(rawCommand)) {
        req.send(400, "application/json", "{\"ok\":false,\"error\":\"unsupported command\"}");
        return;
    }

    PA_LOG_INFO(TAG, "[WEB] POST /api/manual-command - executed %s", rawCommand);
    req.send(200, "application/json", "{\"ok\":true}");
}

void handleRebootPost(WebRequest& req) {
    PA_LOG_INFO(TAG, "[WEB] POST /api/reboot - restart requested");
    requestStatusBroadcastNow();
    req.send(200, "application/json", "{\"ok\":true}");
    requestSystemRestart(500);
}

// ---- Coredump (observability) ----
// The framework saves an ELF coredump to the `coredump` data partition on a
// PANIC. These let an agent fetch + clear it over HTTP — the seated controller
// cannot be USB-read (GPIO15/SBUS strapping). Decode with:
//   esp-coredump info_corefile -c coredump.elf .pio/build/<env>/firmware.elf
void handleCoredumpStatusGet(WebRequest& req) {
    size_t addr = 0, size = 0;
    const bool present = (esp_core_dump_image_get(&addr, &size) == ESP_OK && size > 0);
    char body[80];
    snprintf(body, sizeof(body), "{\"present\":%s,\"size\":%u}", present ? "true" : "false",
             (unsigned)size);
    req.send(200, "application/json", body);
}

void handleCoredumpGet(WebRequest& req) {
    size_t addr = 0, size = 0;
    if (esp_core_dump_image_get(&addr, &size) != ESP_OK || size == 0) {
        req.send(404, "application/json", "{\"ok\":false,\"error\":\"no coredump\"}");
        return;
    }
    const esp_partition_t* part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, nullptr);
    if (part == nullptr) {
        req.send(500, "application/json", "{\"ok\":false,\"error\":\"no coredump partition\"}");
        return;
    }

    // Streamed straight from flash a chunk at a time — no large heap buffer on
    // a heap-constrained device. The coredump image starts at partition
    // offset 0.
    s_coredumpPartition = part;
    s_coredumpSize = size;
    if (!req.sendChunked("application/octet-stream", fillCoredumpResponse)) {
        req.send(500, "application/json", "{\"ok\":false,\"error\":\"response alloc failed\"}");
    }
}

void handleCoredumpErasePost(WebRequest& req) {
    if (esp_core_dump_image_erase() != ESP_OK) {
        req.send(500, "application/json", "{\"ok\":false,\"error\":\"erase failed\"}");
        return;
    }
    PA_LOG_INFO(TAG, "[WEB] coredump erased");
    req.send(200, "application/json", "{\"ok\":true}");
}
