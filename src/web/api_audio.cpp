// =============================================================================
// src/web/api_audio.cpp
//
// Audio REST API
//   POST /api/audio — structured audio control
//
// Accepted form parameters:
//   action=play   &track=N      — play track N (1-based)
//   action=stop                 — stop playback
//   action=volume &level=N      — set absolute volume (0–30)
//   action=dollar &cmd=$R       — raw $ command (any from the $ command set)
//
// All commands are non-blocking: they enqueue to audioCmdQueue (timeout 0)
// and return immediately. The actual serial TX happens in AudioTask on Core 0.
// =============================================================================

#include "api_audio.h"

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

#include "audio_task.h"
#include "logging.h"
#include "robot_state.h"

static const char* TAG = "WebServer";

void registerAudioRoutes(AsyncWebServer& server) {
    server.on("/api/audio", HTTP_POST, [](AsyncWebServerRequest* req) {
        const AsyncWebParameter* actionParam = req->getParam("action", true);
        if (!actionParam) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"missing action parameter\"}");
            return;
        }

        const String& action = actionParam->value();

        // ---- play ----
        if (action == "play") {
            const AsyncWebParameter* trackParam = req->getParam("track", true);
            if (!trackParam) {
                req->send(400, "application/json",
                          "{\"ok\":false,\"error\":\"play requires track parameter\"}");
                return;
            }
            int track = trackParam->value().toInt();
            if (track < 1 || track > 65535) {
                req->send(400, "application/json",
                          "{\"ok\":false,\"error\":\"track must be 1–65535\"}");
                return;
            }
            audioQueuePlayTrack((uint16_t)track, SRC_WEB_API);
            PA_LOG_INFO(TAG, "[AUDIO] POST /api/audio play track=%d", track);
            req->send(200, "application/json", "{\"ok\":true}");
            return;
        }

        // ---- stop ----
        if (action == "stop") {
            audioQueueStop(SRC_WEB_API);
            PA_LOG_INFO(TAG, "[AUDIO] POST /api/audio stop");
            req->send(200, "application/json", "{\"ok\":true}");
            return;
        }

        // ---- volume ----
        if (action == "volume") {
            const AsyncWebParameter* levelParam = req->getParam("level", true);
            if (!levelParam) {
                req->send(400, "application/json",
                          "{\"ok\":false,\"error\":\"volume requires level parameter\"}");
                return;
            }
            int level = levelParam->value().toInt();
            if (level < 0 || level > 30) {
                req->send(400, "application/json",
                          "{\"ok\":false,\"error\":\"level must be 0–30\"}");
                return;
            }
            audioQueueSetVolume((uint8_t)level, SRC_WEB_API);
            PA_LOG_INFO(TAG, "[AUDIO] POST /api/audio volume level=%d", level);
            req->send(200, "application/json", "{\"ok\":true}");
            return;
        }

        // ---- dollar (raw $ command) ----
        if (action == "dollar") {
            const AsyncWebParameter* cmdParam = req->getParam("cmd", true);
            if (!cmdParam || cmdParam->value().length() == 0 ||
                cmdParam->value()[0] != '$') {
                req->send(400, "application/json",
                          "{\"ok\":false,\"error\":\"dollar requires cmd starting with '$'\"}");
                return;
            }
            // Limit cmd length to what audioCmdQueue dollar field can hold
            if (cmdParam->value().length() > 9) {
                req->send(400, "application/json",
                          "{\"ok\":false,\"error\":\"cmd too long (max 9 chars)\"}");
                return;
            }
            audioQueueDollar(cmdParam->value().c_str(), SRC_WEB_API);
            PA_LOG_INFO(TAG, "[AUDIO] POST /api/audio dollar cmd=%s",
                        cmdParam->value().c_str());
            req->send(200, "application/json", "{\"ok\":true}");
            return;
        }

        req->send(400, "application/json",
                  "{\"ok\":false,\"error\":\"unknown action — use play/stop/volume/dollar\"}");
    });
}
