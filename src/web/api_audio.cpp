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

#include "api_helpers.h"
#include "audio_dollar_parser.h"
#include "audio_task.h"
#include "config.h"
#include "logging.h"
#include "mood.h"
#include "robot_state.h"

static const char* TAG = "WebServer";

static bool isSleepModeActive() {
    taskENTER_CRITICAL(&robotStateMux);
    bool sleeping = robotState.sleepMode;
    taskEXIT_CRITICAL(&robotStateMux);
    return sleeping;
}

void registerAudioRoutes(AsyncWebServer& server) {
    // NOTE: /api/audio/tracks must be registered BEFORE /api/audio because
    // ESPAsyncWebServer matches routes in registration order and /api/audio
    // would otherwise match /api/audio/tracks requests first.

    // ---- GET /api/audio/tracks ----
    server.on("/api/audio/tracks", HTTP_GET, [](AsyncWebServerRequest* req) {
        uint16_t scream, faint, leia, cantinaS, swTheme, impMarch, cantinaL, startup;
        uint16_t randMin, randMax;
        uint16_t intQuiet, intMid, intFull, intAwake;
        uint8_t volume;
        taskENTER_CRITICAL(&robotStateMux);
        scream = robotState.cfg_snd_scream;
        faint = robotState.cfg_snd_faint;
        leia = robotState.cfg_snd_leia;
        cantinaS = robotState.cfg_snd_cantina_s;
        swTheme = robotState.cfg_snd_sw_theme;
        impMarch = robotState.cfg_snd_imp_march;
        cantinaL = robotState.cfg_snd_cantina_l;
        startup = robotState.cfg_snd_startup;
        randMin = robotState.cfg_snd_rand_min;
        randMax = robotState.cfg_snd_rand_max;
        volume = robotState.cfg_audioVolume;
        intQuiet = robotState.cfg_snd_int_quiet;
        intMid = robotState.cfg_snd_int_mid;
        intFull = robotState.cfg_snd_int_full;
        intAwake = robotState.cfg_snd_int_awake;
        taskEXIT_CRITICAL(&robotStateMux);

        // Stack-allocated — not static. Static local buffers in async handlers
        // are shared across concurrent requests and would cause data races.
        char body[384];
        snprintf(body, sizeof(body),
                 "{\"scream\":%u,\"faint\":%u,\"leia\":%u,"
                 "\"cantina_s\":%u,\"sw_theme\":%u,\"imp_march\":%u,"
                 "\"cantina_l\":%u,\"startup\":%u,"
                 "\"rand_min\":%u,\"rand_max\":%u,\"volume\":%u,"
                 "\"snd_int_quiet\":%u,\"snd_int_mid\":%u,"
                 "\"snd_int_full\":%u,\"snd_int_awake\":%u}",
                 scream, faint, leia, cantinaS, swTheme, impMarch, cantinaL, startup, randMin,
                 randMax, volume, intQuiet, intMid, intFull, intAwake);
        req->send(200, "application/json", body);
    });

    // ---- POST /api/audio/tracks ----
    server.on("/api/audio/tracks", HTTP_POST, [](AsyncWebServerRequest* req) {
        const AsyncWebParameter* keyParam = req->getParam("key", true);
        const AsyncWebParameter* trackParam = req->getParam("track", true);
        if (!keyParam || !trackParam) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"requires key and track parameters\"}");
            return;
        }

        // Resolve key before range validation — interval keys accept 0–3600 s,
        // track-number keys accept 1–999.
        String keyValue = keyParam->value();
        const char* key = keyValue.c_str();
        bool isInterval = (strncmp(key, "snd_int_", 8) == 0);
        int track = trackParam->value().toInt();
        if (isInterval) {
            if (track < 0 || track > 3600) {
                req->send(400, "application/json",
                          "{\"ok\":false,\"error\":\"interval must be 0\u20133600 s\"}");
                return;
            }
        } else {
            if (track < 1 || track > 999) {
                req->send(400, "application/json",
                          "{\"ok\":false,\"error\":\"track must be 1\u2013999\"}");
                return;
            }
        }
        uint16_t t = (uint16_t)track;
        // Resolve the target field pointer before entering the critical section.
        // String comparisons must not run inside portMUX — they are safe here
        // (no allocation) but keeping critical sections minimal is good practice.
        uint16_t* fieldPtr = nullptr;
        taskENTER_CRITICAL(&robotStateMux);
        if (strcmp(key, "scream") == 0)
            fieldPtr = &robotState.cfg_snd_scream;
        else if (strcmp(key, "faint") == 0)
            fieldPtr = &robotState.cfg_snd_faint;
        else if (strcmp(key, "leia") == 0)
            fieldPtr = &robotState.cfg_snd_leia;
        else if (strcmp(key, "cantina_s") == 0)
            fieldPtr = &robotState.cfg_snd_cantina_s;
        else if (strcmp(key, "sw_theme") == 0)
            fieldPtr = &robotState.cfg_snd_sw_theme;
        else if (strcmp(key, "imp_march") == 0)
            fieldPtr = &robotState.cfg_snd_imp_march;
        else if (strcmp(key, "cantina_l") == 0)
            fieldPtr = &robotState.cfg_snd_cantina_l;
        else if (strcmp(key, "startup") == 0)
            fieldPtr = &robotState.cfg_snd_startup;
        else if (strcmp(key, "rand_min") == 0)
            fieldPtr = &robotState.cfg_snd_rand_min;
        else if (strcmp(key, "rand_max") == 0)
            fieldPtr = &robotState.cfg_snd_rand_max;
        else if (strcmp(key, "snd_int_quiet") == 0)
            fieldPtr = &robotState.cfg_snd_int_quiet;
        else if (strcmp(key, "snd_int_mid") == 0)
            fieldPtr = &robotState.cfg_snd_int_mid;
        else if (strcmp(key, "snd_int_full") == 0)
            fieldPtr = &robotState.cfg_snd_int_full;
        else if (strcmp(key, "snd_int_awake") == 0)
            fieldPtr = &robotState.cfg_snd_int_awake;
        if (fieldPtr)
            *fieldPtr = t;
        taskEXIT_CRITICAL(&robotStateMux);

        if (!fieldPtr) {
            req->send(400, "application/json", "{\"ok\":false,\"error\":\"unknown key\"}");
            return;
        }

        // Persist to NVS — dedicated mini-write for this field only.
        // audioTrackNvsKey() maps API key → NVS key (pure helper, tested natively).
        const char* nvsKey = audioTrackNvsKey(key);
        Preferences prefs;
        bool ok = false;
        if (nvsKey && prefs.begin(NVS_NAMESPACE, false)) {
            ok = prefs.putUShort(nvsKey, t) > 0;
            prefs.end();
        }

        PA_LOG_INFO(TAG, "[AUDIO] POST /api/audio/tracks key=%s track=%u", key, (unsigned)t);
        if (ok) {
            req->send(200, "application/json", "{\"ok\":true}");
        } else {
            req->send(500, "application/json", "{\"ok\":false,\"error\":\"NVS write failed\"}");
        }
    });

    // ---- GET /api/audio — live module status ----
    // Returns driver name, module link state, and last-known module status.
    // Device and total-tracks are populated from begin()-time cached queries.
    // Play state, current track, and link_ok are updated by status queries.
    // Polling during playback may interfere with some module RX paths; see
    // driver capabilities for safe-polling flag.
    server.on("/api/audio", HTTP_GET, [](AsyncWebServerRequest* req) {
        bool linkOk;
        uint8_t playState, device;
        uint16_t totalTracks, currentTrack;
        bool active;
        taskENTER_CRITICAL(&robotStateMux);
        linkOk = robotState.audio_module_link_ok;
        playState = robotState.audio_module_play_state;
        device = robotState.audio_module_device;
        totalTracks = robotState.audio_module_total_tracks;
        currentTrack = robotState.audio_module_current_track;
        active = robotState.audioActive;
        taskEXIT_CRITICAL(&robotStateMux);

        uint8_t caps = audioGetCapabilities();
        char body[192];
        formatAudioStatusJson(body, sizeof(body), audioGetDriverName(), caps, linkOk, active,
                              playState, device, totalTracks, currentTrack);
        req->send(200, "application/json", body);
    });

    // POST /api/mood — apply a mood preset (dual-path: audio + dome TX)
    server.on("/api/mood", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (isSleepModeActive()) {
            req->send(423, "application/json",
                      "{\"error\":\"sleeping\",\"hint\":\"POST /api/wake\"}");
            return;
        }
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

    // ---- POST /api/audio/query — on-demand module status poll ----
    // Enqueues AUDIO_CMD_QUERY_STATUS; AudioTask runs queryModuleState() and
    // updates RobotState. The result is available via GET /api/audio after
    // ~1.5 s (3 × 300 ms query timeout + queue latency).
    server.on("/api/audio/query", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (!audioQueueQueryStatus(SRC_WEB_API)) {
            req->send(503, "application/json",
                      "{\"ok\":false,\"error\":\"audio command queue full\"}");
            return;
        }
        PA_LOG_INFO(TAG, "[AUDIO] POST /api/audio/query — status poll enqueued");
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
            if (isSleepModeActive()) {
                req->send(423, "application/json",
                          "{\"error\":\"sleeping\",\"hint\":\"POST /api/wake\"}");
                return;
            }
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
            if (!audioQueuePlayTrack((uint16_t)track, SRC_WEB_API)) {
                req->send(503, "application/json",
                          "{\"ok\":false,\"error\":\"audio command queue full\"}");
                return;
            }
            PA_LOG_INFO(TAG, "[AUDIO] POST /api/audio play track=%d", track);
            req->send(200, "application/json", "{\"ok\":true}");
            return;
        }

        // ---- stop ----
        if (action == "stop") {
            if (!audioQueueStop(SRC_WEB_API)) {
                req->send(503, "application/json",
                          "{\"ok\":false,\"error\":\"audio command queue full\"}");
                return;
            }
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

            // Apply immediately in AudioTask runtime
            if (!audioQueueSetVolume((uint8_t)level, SRC_WEB_API)) {
                req->send(503, "application/json",
                          "{\"ok\":false,\"error\":\"audio command queue full\"}");
                return;
            }
            // Persist as the new default volume so it survives reboot
            taskENTER_CRITICAL(&robotStateMux);
            robotState.cfg_audioVolume = (uint8_t)level;
            taskEXIT_CRITICAL(&robotStateMux);
            bool saved = saveConfigToNvs();

            PA_LOG_INFO(TAG, "[AUDIO] POST /api/audio volume level=%d saved=%s", level,
                        saved ? "true" : "false");
            if (!saved) {
                req->send(500, "application/json",
                          "{\"ok\":false,\"error\":\"volume applied but NVS save failed\"}");
                return;
            }
            req->send(200, "application/json", "{\"ok\":true}");
            return;
        }

        // ---- dollar (raw $ command) ----
        if (action == "dollar") {
            if (isSleepModeActive()) {
                req->send(423, "application/json",
                          "{\"error\":\"sleeping\",\"hint\":\"POST /api/wake\"}");
                return;
            }
            const AsyncWebParameter* cmdParam = req->getParam("cmd", true);
            if (!cmdParam || cmdParam->value().length() == 0 || cmdParam->value()[0] != '$') {
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
            if (!audioQueueDollar(cmdParam->value().c_str(), SRC_WEB_API)) {
                req->send(503, "application/json",
                          "{\"ok\":false,\"error\":\"audio command queue full\"}");
                return;
            }
            PA_LOG_INFO(TAG, "[AUDIO] POST /api/audio dollar cmd=%s", cmdParam->value().c_str());
            req->send(200, "application/json", "{\"ok\":true}");
            return;
        }

        req->send(400, "application/json",
                  "{\"ok\":false,\"error\":\"unknown action — use play/stop/volume/dollar\"}");
    });
}
