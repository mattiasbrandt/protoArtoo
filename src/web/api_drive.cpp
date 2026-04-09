// =============================================================================
// src/web/api_drive.cpp
//
// Drive and web control API endpoints
//   POST /api/drive                   — browser drive command (timeout-protected)
//   POST /api/drive/speed-preset      — apply Slow/Normal/Turbo speed preset
//   POST /api/web-control/enable      — enable browser control
//   POST /api/web-control/disable     — disable browser control
// =============================================================================

#include "api_drive.h"

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "api_helpers.h"
#include "audio_task.h"
#include "drive_speed_preset.h"
#include "system_sounds.h"
#include "dome_link.h"
#include "dome_rx_parser.h"
#include "mood.h"
#include "logging.h"
#include "robot_state.h"
#include "web_server.h"

// External declaration for saveConfigToNvs
extern bool saveConfigToNvs();

static const char* TAG = "WebServer";


static bool isSleepModeActive() {
    taskENTER_CRITICAL(&robotStateMux);
    bool sleeping = robotState.sleepMode;
    taskEXIT_CRITICAL(&robotStateMux);
    return sleeping;
}
namespace {

// ManualCommand — recognized command tokens for POST /api/manual-command.
// Internal to this translation unit; not exposed in any header.
enum ManualCommand : uint8_t {
    MC_UNKNOWN = 0,
    MC_ESTOP,
    MC_CLEAR_ESTOP,
    MC_ENABLE_WEB_CONTROL,
    MC_DISABLE_WEB_CONTROL,
    MC_REBOOT,
    MC_STATIONARY_MODE,
    MC_DRIVING_MODE,
};


void setStationaryModeWithSound(bool stationary) {
    uint16_t driveTrack = 0;
    bool wasStationary = false;
    taskENTER_CRITICAL(&robotStateMux);
    wasStationary = robotState.stationary;
    robotState.stationary = stationary;
    robotState.cfg_stationary = stationary;
    if (wasStationary && !stationary) {
        driveTrack = robotState.cfg_snd_sys_drv_on;
    }
    taskEXIT_CRITICAL(&robotStateMux);
    queueSystemSoundTrack(driveTrack, audioQueuePlayTrack, SRC_INTERNAL);
}

ManualCommand resolveManualCommand(const char* command) {
    if (command == nullptr) {
        return MC_UNKNOWN;
    }
    if (strcmp(command, "estop") == 0) {
        return MC_ESTOP;
    }
    if (strcmp(command, "clear_estop") == 0) {
        return MC_CLEAR_ESTOP;
    }
    if (strcmp(command, "enable_web_control") == 0) {
        return MC_ENABLE_WEB_CONTROL;
    }
    if (strcmp(command, "disable_web_control") == 0) {
        return MC_DISABLE_WEB_CONTROL;
    }
    if (strcmp(command, "reboot") == 0) {
        return MC_REBOOT;
    }
    if (strcmp(command, "#st") == 0) {
        return MC_STATIONARY_MODE;
    }
    if (strcmp(command, "#sm") == 0) {
        return MC_DRIVING_MODE;
    }
    return MC_UNKNOWN;
}

}  // namespace


bool executeManualCommand(const String& raw) {
    if (raw.length() == 0) {
        return false;
    }

    // Marcduino commands are case-sensitive — route them directly on raw
    // WITHOUT copying or case-folding. Only the keyword commands below need
    // toLowerCase(), and we defer that copy until we actually need it.
    const char prefix = raw[0];

    // $ — audio commands: route to AudioTask
    if (prefix == '$') {
        return audioQueueDollar(raw.c_str(), SRC_WEB_API);
    }

    // : and # — body-processed Marcduino: servo sequences, panel cmds, config
    if (prefix == ':' || prefix == '#') {
        // Mood commands (:SE10/11/13/14) are not valid body sequences so
        // parseMarcduinoCommand() would silently discard them. Intercept first.
        uint8_t moodId = moodIdFromSeCommand(raw.c_str());
        if (moodId != 0) {
            applyMood(moodId);
            return true;
        }
        parseMarcduinoCommand(raw.c_str());
        return true;  // always accept — body handles or discards per routing table
    }

    // * @ % & ! — dome-bound Marcduino: forward to dome TX queue
    if (prefix == '*' || prefix == '@' || prefix == '%' ||
        prefix == '&' || prefix == '!') {
        domeQueueTx(raw.c_str());
        return true;
    }

    // Keyword commands (estop, reboot, etc.) — case-insensitive.
    // Only allocate the lowercase copy here, not for every Marcduino command.
    String command = raw;
    command.toLowerCase();
    ManualCommand cmd = resolveManualCommand(command.c_str());

    switch (cmd) {
        case MC_ESTOP:
            taskENTER_CRITICAL(&robotStateMux);
            robotState.estop = true;
            recordFailsafeTriggerLocked(FS_ESTOP_CMD, millis());
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

        case MC_STATIONARY_MODE:
            setStationaryModeWithSound(true);
            saveConfigToNvs();
            return true;

        case MC_DRIVING_MODE:
            setStationaryModeWithSound(false);
            saveConfigToNvs();
            return true;

        case MC_UNKNOWN:
        default:
            return false;
    }
}

static bool parseDomeSpeedValue(const char* raw, float* out) {
    if (raw == nullptr || out == nullptr) {
        return false;
    }

    char* end = nullptr;
    float value = strtof(raw, &end);
    if (end == raw || end == nullptr || *end != '\0') {
        return false;
    }
    if (value < -1.0f || value > 1.0f) {
        return false;
    }

    *out = value;
    return true;
}

void registerDriveRoutes(AsyncWebServer& server) {
    server.on("/api/mode", HTTP_POST, [](AsyncWebServerRequest* req) {
        const AsyncWebParameter* modeParam = req->getParam("mode", true);
        if (modeParam == nullptr) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"missing mode parameter\"}");
            return;
        }

        String mode = modeParam->value();
        mode.toLowerCase();

        if (mode == "stationary") {
            setStationaryModeWithSound(true);
            saveConfigToNvs();
            PA_LOG_INFO(TAG, "[WEB] Mode set to stationary");
            req->send(200, "application/json", "{\"ok\":true}");
        } else if (mode == "driving") {
            setStationaryModeWithSound(false);
            saveConfigToNvs();
            PA_LOG_INFO(TAG, "[WEB] Mode set to driving");
            req->send(200, "application/json", "{\"ok\":true}");
        } else {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"invalid mode - use 'stationary' or 'driving'\"}");
        }
    });

    server.on("/api/drive/speed-preset", HTTP_POST, [](AsyncWebServerRequest* req) {
        const AsyncWebParameter* presetParam = req->getParam("preset", true);
        if (presetParam == nullptr) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"missing preset\"}");
            return;
        }

        String presetRaw = presetParam->value();
        presetRaw.toLowerCase();
        SpeedPresetId preset = SpeedPresetId::Normal;
        if (!parseSpeedPresetId(presetRaw.c_str(), &preset)) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"invalid preset - use slow, normal, or turbo\"}");
            return;
        }

        if (!applySpeedPresetPersisted(preset)) {
            req->send(500, "application/json",
                      "{\"ok\":false,\"error\":\"failed to persist speed preset\"}");
            return;
        }

        int16_t speedLimitMax = 0;
        SpeedPresetId activePreset = SpeedPresetId::Normal;
        taskENTER_CRITICAL(&robotStateMux);
        speedLimitMax = robotState.cfg_speedLimitMax;
        activePreset = normalizeSpeedPresetId((uint8_t)robotState.cfg_speedPresetActive);
        taskEXIT_CRITICAL(&robotStateMux);

        char response[96];
        if (!formatSpeedPresetResponseJson(response, sizeof(response), activePreset, speedLimitMax)) {
            req->send(500, "application/json",
                      "{\"ok\":false,\"error\":\"speed preset response overflow\"}");
            return;
        }
        req->send(200, "application/json", response);
    });

    server.on("/api/web-control/enable", HTTP_POST, [](AsyncWebServerRequest* req) {
        taskENTER_CRITICAL(&robotStateMux);
        robotState.webControlEnabled = true;
        taskEXIT_CRITICAL(&robotStateMux);
        PA_LOG_INFO(TAG, "[WEB] POST /api/web-control/enable - browser control enabled");
        req->send(200, "application/json", "{\"ok\":true}");
    });

    server.on("/api/web-control/disable", HTTP_POST, [](AsyncWebServerRequest* req) {
        taskENTER_CRITICAL(&robotStateMux);
        robotState.webControlEnabled = false;
        taskEXIT_CRITICAL(&robotStateMux);
        setDriveCommand(0, 0, SRC_INTERNAL);
        PA_LOG_INFO(TAG, "[WEB] POST /api/web-control/disable - browser control disabled");
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
            PA_LOG_WARN(TAG, "[WEB] POST /api/drive - rejected: blocked by safety state");
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

    server.on("/api/dome", HTTP_POST, [](AsyncWebServerRequest* req) {
        const AsyncWebParameter* speedParam = req->getParam("speed", true);
        if (speedParam == nullptr) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"missing speed\"}");
            return;
        }

        if (isSleepModeActive()) {
            req->send(423, "application/json",
                      "{\"error\":\"sleeping\",\"hint\":\"POST /api/wake\"}");
            return;
        }

        float speed = 0.0f;
        if (!parseDomeSpeedValue(speedParam->value().c_str(), &speed)) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"speed must be a float in range -1.0..1.0\"}");
            return;
        }

        taskENTER_CRITICAL(&robotStateMux);
        bool domeEnabled = robotState.cfg_enable_dome;
        bool webCtrl    = robotState.webControlEnabled;
        taskEXIT_CRITICAL(&robotStateMux);
        if (!domeEnabled) {
            req->send(409, "application/json",
                      "{\"ok\":false,\"error\":\"dome output is disabled\"}");
            return;
        }
        if (!webCtrl) {
            req->send(409, "application/json",
                      "{\"ok\":false,\"error\":\"web control is not enabled\"}");
            return;
        }

        DomeCommand cmd = {};
        cmd.speed = constrain(speed, -1.0f, 1.0f);
        cmd.source = SRC_WEB_API;
        cmd.timestampMs = millis();
        if (xQueueSend(domeCmdQueue, &cmd, 0) != pdTRUE) {
            taskENTER_CRITICAL(&robotStateMux);
            robotState.queueOverflowCount++;
            taskEXIT_CRITICAL(&robotStateMux);
            req->send(503, "application/json",
                      "{\"ok\":false,\"error\":\"dome command queue full\"}");
            return;
        }

        PA_LOG_INFO(TAG, "[WEB] POST /api/dome speed=%.2f", (double)cmd.speed);
        req->send(200, "application/json", "{\"ok\":true}");
    });
}
