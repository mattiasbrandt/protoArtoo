// =============================================================================
// src/web/api_audio.cpp
//
// Audio REST API
//   POST /api/audio          — structured audio control
//   GET  /api/audio/tracks   — named track assignments + random range
//   POST /api/audio/tracks   — update a named track or random range (NVS-persisted)
//
// POST /api/audio params:
//   action=play   &track=N      — play track N (1-based)
//   action=stop                 — stop playback
//   action=volume &level=N      — set absolute volume (0–30)
//   action=dollar &cmd=$R       — raw $ command (any from the $ command set)
//
// POST /api/audio/tracks params:
//   key=<name>   &track=N       — set named track (1–999)
//   key=rand_min &track=N       — set random pool minimum
//   key=rand_max &track=N       — set random pool maximum
//
// Valid key names: scream faint leia cantina_s sw_theme imp_march cantina_l
//                  startup rand_min rand_max
// =============================================================================

#include "api_audio.h"

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

#include <Preferences.h>
#include <stdio.h>

#include "audio_dollar_parser.h"
#include "audio_task.h"
#include "config.h"
#include "logging.h"
#include "mood.h"
#include "robot_state.h"

static const char* TAG = "WebServer";

void registerAudioRoutes(AsyncWebServer& server) {
    // NOTE: /api/audio/tracks must be registered BEFORE /api/audio because
    // ESPAsyncWebServer matches routes in registration order and /api/audio
    // would otherwise match /api/audio/tracks requests first.

    // ---- GET /api/audio/tracks ----
    server.on("/api/audio/tracks", HTTP_GET, [](AsyncWebServerRequest* req) {
        uint16_t scream, faint, leia, cantinaS, swTheme, impMarch, cantinaL, startup;
        uint16_t randMin, randMax;
        taskENTER_CRITICAL(&robotStateMux);
        scream    = robotState.cfg_snd_scream;
        faint     = robotState.cfg_snd_faint;
        leia      = robotState.cfg_snd_leia;
        cantinaS  = robotState.cfg_snd_cantina_s;
        swTheme   = robotState.cfg_snd_sw_theme;
        impMarch  = robotState.cfg_snd_imp_march;
        cantinaL  = robotState.cfg_snd_cantina_l;
        startup   = robotState.cfg_snd_startup;
        randMin   = robotState.cfg_snd_rand_min;
        randMax   = robotState.cfg_snd_rand_max;
        taskEXIT_CRITICAL(&robotStateMux);

        // Stack-allocated — not static. Static local buffers in async handlers
        // are shared across concurrent requests and would cause data races.
        char body[256];
        snprintf(body, sizeof(body),
                 "{\"scream\":%u,\"faint\":%u,\"leia\":%u,"
                 "\"cantina_s\":%u,\"sw_theme\":%u,\"imp_march\":%u,"
                 "\"cantina_l\":%u,\"startup\":%u,"
                 "\"rand_min\":%u,\"rand_max\":%u}",
                 scream, faint, leia, cantinaS, swTheme, impMarch,
                 cantinaL, startup, randMin, randMax);
        req->send(200, "application/json", body);
    });

    // ---- POST /api/audio/tracks ----
    server.on("/api/audio/tracks", HTTP_POST, [](AsyncWebServerRequest* req) {
        const AsyncWebParameter* keyParam   = req->getParam("key",   true);
        const AsyncWebParameter* trackParam = req->getParam("track", true);
        if (!keyParam || !trackParam) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"requires key and track parameters\"}");
            return;
        }

        int track = trackParam->value().toInt();
        if (track < 1 || track > 999) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"track must be 1–999\"}");
            return;
        }
        uint16_t t = (uint16_t)track;
        const String& key = keyParam->value();

        bool found = true;
        taskENTER_CRITICAL(&robotStateMux);
        if      (key == "scream")    robotState.cfg_snd_scream    = t;
        else if (key == "faint")     robotState.cfg_snd_faint     = t;
        else if (key == "leia")      robotState.cfg_snd_leia      = t;
        else if (key == "cantina_s") robotState.cfg_snd_cantina_s = t;
        else if (key == "sw_theme")  robotState.cfg_snd_sw_theme  = t;
        else if (key == "imp_march") robotState.cfg_snd_imp_march = t;
        else if (key == "cantina_l") robotState.cfg_snd_cantina_l = t;
        else if (key == "startup")   robotState.cfg_snd_startup   = t;
        else if (key == "rand_min")  robotState.cfg_snd_rand_min  = t;
        else if (key == "rand_max")  robotState.cfg_snd_rand_max  = t;
        else                         found = false;
        taskEXIT_CRITICAL(&robotStateMux);

        if (!found) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"unknown key\"}");
            return;
        }

        // Persist to NVS — dedicated mini-write for this field only.
        // audioTrackNvsKey() maps API key → NVS key (pure helper, tested natively).
        const char* nvsKey = audioTrackNvsKey(key.c_str());
        Preferences prefs;
        bool ok = false;
        if (nvsKey && prefs.begin(NVS_NAMESPACE, false)) {
            ok = prefs.putUShort(nvsKey, t) > 0;
            prefs.end();
        }

        PA_LOG_INFO(TAG, "[AUDIO] POST /api/audio/tracks key=%s track=%u",
                    key.c_str(), (unsigned)t);
        if (ok) {
            req->send(200, "application/json", "{\"ok\":true}");
        } else {
            req->send(500, "application/json",
                      "{\"ok\":false,\"error\":\"NVS write failed\"}");
        }
    });

    // POST /api/mood — apply a mood preset (dual-path: audio + dome TX)
    server.on("/api/mood", HTTP_POST, [](AsyncWebServerRequest* req) {
        const AsyncWebParameter* moodParam = req->getParam("mood", true);
        if (!moodParam) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"missing mood parameter\"}");
            return;
        }
        int mood = moodParam->value().toInt();
        // Valid mood IDs: 10 (Quiet), 11 (Full-Awake), 13 (Mid-Awake), 14 (Awake+)
        if (mood != 10 && mood != 11 && mood != 13 && mood != 14) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"mood must be 10, 11, 13, or 14\"}");
            return;
        }
        applyMood((uint8_t)mood);
        PA_LOG_INFO(TAG, "[MOOD] POST /api/mood mood=%d", mood);
        req->send(200, "application/json", "{\"ok\":true}");
    });


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
