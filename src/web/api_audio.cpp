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
//   key=<name>   &track=N       — set named/category/system track (1–999, or 0–999 where allowed)
//   key=rand_min &track=N       — set random pool minimum
//   key=rand_max &track=N       — set random pool maximum
//
// Valid key names: scream faint leia cantina_s sw_theme imp_march cantina_l
//                  startup doodoo failure disco mahna inlove macho gangnam
//                  uptown celebr stayin harlem pbjtime
//                  sys_boot sys_mode_n sys_mode_s sys_mode_t sys_drv_on sys_dome_on
//                  snd_cat_*_lo snd_cat_*_hi, rand_min rand_max
// =============================================================================

#include "api_audio.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <stdio.h>

#include "api_helpers.h"
#include "audio_dollar_parser.h"
#include "audio_task.h"
#include "config.h"
#include "logging.h"
#include "mood.h"
#include "mood_sound_mapping.h"
#include "robot_state.h"

static const char* TAG = "WebServer";

static bool isSleepModeActive() {
    taskENTER_CRITICAL(&robotStateMux);
    bool sleeping = robotState.sleepMode;
    taskEXIT_CRITICAL(&robotStateMux);
    return sleeping;
}

void registerMoodMapRoutes(AsyncWebServer& server) {
    server.on("/api/audio/mood-map", HTTP_GET, [](AsyncWebServerRequest* req) {
        MoodCategoryMaskConfig masks{};
        taskENTER_CRITICAL(&robotStateMux);
        masks.quiet = (uint16_t)(robotState.cfg_snd_moodcat_quiet & MOOD_CATEGORY_MASK_MAX);
        masks.mid = (uint16_t)(robotState.cfg_snd_moodcat_mid & MOOD_CATEGORY_MASK_MAX);
        masks.full = (uint16_t)(robotState.cfg_snd_moodcat_full & MOOD_CATEGORY_MASK_MAX);
        masks.awakeplus =
            (uint16_t)(robotState.cfg_snd_moodcat_awakeplus & MOOD_CATEGORY_MASK_MAX);
        taskEXIT_CRITICAL(&robotStateMux);

        char body[128];
        size_t n = formatMoodCategoryMapJson(body, sizeof(body), masks);
        if (n >= sizeof(body)) {
            req->send(500, "application/json",
                      "{\"ok\":false,\"error\":\"mood-map response overflow\"}");
            return;
        }
        req->send(200, "application/json", body);
    });

    server.on("/api/audio/mood-map", HTTP_POST, [](AsyncWebServerRequest* req) {
        uint16_t quiet = 0;
        uint16_t mid = 0;
        uint16_t full = 0;
        uint16_t awakeplus = 0;

        auto parseMaskText = [&](const String& raw, const char* key, uint16_t* out) -> bool {
            uint32_t value = 0;
            if (!parseUint32Value(raw.c_str(), &value)) {
                char err[96];
                snprintf(err, sizeof(err),
                         "{\"ok\":false,\"error\":\"%s must be a non-negative integer\"}", key);
                req->send(400, "application/json", err);
                return false;
            }
            if (!isValidMoodCategoryMaskValue(value)) {
                char err[96];
                snprintf(err, sizeof(err),
                         "{\"ok\":false,\"error\":\"%s must be 0..4095\"}", key);
                req->send(400, "application/json", err);
                return false;
            }
            *out = (uint16_t)value;
            return true;
        };

        const AsyncWebParameter* quietParam = req->getParam("quiet", true);
        const AsyncWebParameter* midParam = req->getParam("mid", true);
        const AsyncWebParameter* fullParam = req->getParam("full", true);
        const AsyncWebParameter* awakeplusParam = req->getParam("awakeplus", true);
        const bool hasAnyForm = quietParam || midParam || fullParam || awakeplusParam;

        if (hasAnyForm) {
            if (!(quietParam && midParam && fullParam && awakeplusParam)) {
                req->send(400, "application/json",
                          "{\"ok\":false,\"error\":\"requires quiet, mid, full, awakeplus\"}");
                return;
            }
            if (!parseMaskText(quietParam->value(), "quiet", &quiet) ||
                !parseMaskText(midParam->value(), "mid", &mid) ||
                !parseMaskText(fullParam->value(), "full", &full) ||
                !parseMaskText(awakeplusParam->value(), "awakeplus", &awakeplus)) {
                return;
            }
        } else if (req->hasParam("plain", true)) {
            JsonDocument bodyDoc;
            const String rawBody = req->getParam("plain", true)->value();
            DeserializationError jsonErr = deserializeJson(bodyDoc, rawBody.c_str());
            if (jsonErr) {
                req->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid json body\"}");
                return;
            }

            auto parseMaskJson = [&](const char* key, uint16_t* out) -> bool {
                JsonVariantConst value = bodyDoc[key];
                if (value.isNull()) {
                    char err[96];
                    snprintf(err, sizeof(err),
                             "{\"ok\":false,\"error\":\"missing %s\"}", key);
                    req->send(400, "application/json", err);
                    return false;
                }

                if (value.is<uint32_t>()) {
                    uint32_t parsed = value.as<uint32_t>();
                    if (!isValidMoodCategoryMaskValue(parsed)) {
                        char err[96];
                        snprintf(err, sizeof(err),
                                 "{\"ok\":false,\"error\":\"%s must be 0..4095\"}", key);
                        req->send(400, "application/json", err);
                        return false;
                    }
                    *out = (uint16_t)parsed;
                    return true;
                }

                if (value.is<int32_t>()) {
                    int32_t parsed = value.as<int32_t>();
                    if (parsed < 0 || !isValidMoodCategoryMaskValue((uint32_t)parsed)) {
                        char err[96];
                        snprintf(err, sizeof(err),
                                 "{\"ok\":false,\"error\":\"%s must be 0..4095\"}", key);
                        req->send(400, "application/json", err);
                        return false;
                    }
                    *out = (uint16_t)parsed;
                    return true;
                }

                if (value.is<const char*>()) {
                    return parseMaskText(String(value.as<const char*>()), key, out);
                }

                char err[96];
                snprintf(err, sizeof(err),
                         "{\"ok\":false,\"error\":\"%s must be integer\"}", key);
                req->send(400, "application/json", err);
                return false;
            };

            if (!parseMaskJson("quiet", &quiet) || !parseMaskJson("mid", &mid) ||
                !parseMaskJson("full", &full) || !parseMaskJson("awakeplus", &awakeplus)) {
                return;
            }
        } else {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"requires form fields or json body\"}");
            return;
        }

        taskENTER_CRITICAL(&robotStateMux);
        robotState.cfg_snd_moodcat_quiet = quiet;
        robotState.cfg_snd_moodcat_mid = mid;
        robotState.cfg_snd_moodcat_full = full;
        robotState.cfg_snd_moodcat_awakeplus = awakeplus;
        taskEXIT_CRITICAL(&robotStateMux);

        if (!saveConfigToNvs()) {
            req->send(500, "application/json", "{\"ok\":false,\"error\":\"NVS write failed\"}");
            return;
        }

        PA_LOG_INFO(TAG, "[AUDIO] POST /api/audio/mood-map q=%u m=%u f=%u a=%u",
                    (unsigned)quiet, (unsigned)mid, (unsigned)full, (unsigned)awakeplus);
        req->send(200, "application/json", "{\"ok\":true}");
    });
}

void registerAudioRoutes(AsyncWebServer& server) {
    // NOTE: /api/audio/tracks must be registered BEFORE /api/audio because
    // ESPAsyncWebServer matches routes in registration order and /api/audio
    // would otherwise match /api/audio/tracks requests first.

    // ---- GET /api/audio/tracks ----
    server.on("/api/audio/tracks", HTTP_GET, [](AsyncWebServerRequest* req) {
        uint16_t scream, faint, leia, cantinaS, swTheme, impMarch, cantinaL, startup;
        uint16_t doodoo, failure, disco, mahna, inlove, macho;
        uint16_t gangnam, uptown, celebr, stayin, harlem, pbjtime;
        uint16_t sysBoot, sysModeN, sysModeS, sysModeT, sysDrvOn, sysDomeOn;
        uint16_t catGenLo, catGenHi, catChatLo, catChatHi, catHapLo, catHapHi;
        uint16_t catProcLo, catProcHi, catSadLo, catSadHi, catSentLo, catSentHi;
        uint16_t catHumLo, catHumHi, catScrmLo, catScrmHi, catOohLo, catOohHi;
        uint16_t catAlrmLo, catAlrmHi, catPfftLo, catPfftHi, catWhisLo, catWhisHi;
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
        doodoo = robotState.cfg_snd_doodoo;
        failure = robotState.cfg_snd_failure;
        disco = robotState.cfg_snd_disco;
        mahna = robotState.cfg_snd_mahna;
        inlove = robotState.cfg_snd_inlove;
        macho = robotState.cfg_snd_macho;
        gangnam = robotState.cfg_snd_gangnam;
        uptown = robotState.cfg_snd_uptown;
        celebr = robotState.cfg_snd_celebr;
        stayin = robotState.cfg_snd_stayin;
        harlem = robotState.cfg_snd_harlem;
        pbjtime = robotState.cfg_snd_pbjtime;
        sysBoot = robotState.cfg_snd_sys_boot;
        sysModeN = robotState.cfg_snd_sys_mode_n;
        sysModeS = robotState.cfg_snd_sys_mode_s;
        sysModeT = robotState.cfg_snd_sys_mode_t;
        sysDrvOn = robotState.cfg_snd_sys_drv_on;
        sysDomeOn = robotState.cfg_snd_sys_dome_on;
        catGenLo = robotState.cfg_snd_cat_gen_lo;
        catGenHi = robotState.cfg_snd_cat_gen_hi;
        catChatLo = robotState.cfg_snd_cat_chat_lo;
        catChatHi = robotState.cfg_snd_cat_chat_hi;
        catHapLo = robotState.cfg_snd_cat_hap_lo;
        catHapHi = robotState.cfg_snd_cat_hap_hi;
        catProcLo = robotState.cfg_snd_cat_proc_lo;
        catProcHi = robotState.cfg_snd_cat_proc_hi;
        catSadLo = robotState.cfg_snd_cat_sad_lo;
        catSadHi = robotState.cfg_snd_cat_sad_hi;
        catSentLo = robotState.cfg_snd_cat_sent_lo;
        catSentHi = robotState.cfg_snd_cat_sent_hi;
        catHumLo = robotState.cfg_snd_cat_hum_lo;
        catHumHi = robotState.cfg_snd_cat_hum_hi;
        catScrmLo = robotState.cfg_snd_cat_scrm_lo;
        catScrmHi = robotState.cfg_snd_cat_scrm_hi;
        catOohLo = robotState.cfg_snd_cat_ooh_lo;
        catOohHi = robotState.cfg_snd_cat_ooh_hi;
        catAlrmLo = robotState.cfg_snd_cat_alrm_lo;
        catAlrmHi = robotState.cfg_snd_cat_alrm_hi;
        catPfftLo = robotState.cfg_snd_cat_pfft_lo;
        catPfftHi = robotState.cfg_snd_cat_pfft_hi;
        catWhisLo = robotState.cfg_snd_cat_whis_lo;
        catWhisHi = robotState.cfg_snd_cat_whis_hi;
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
        char body[2048];
        snprintf(body, sizeof(body),
                 "{\"scream\":%u,\"faint\":%u,\"leia\":%u,"
                 "\"cantina_s\":%u,\"sw_theme\":%u,\"imp_march\":%u,"
                 "\"cantina_l\":%u,\"startup\":%u,"
                 "\"doodoo\":%u,\"failure\":%u,\"disco\":%u,\"mahna\":%u,"
                 "\"inlove\":%u,\"macho\":%u,\"gangnam\":%u,\"uptown\":%u,"
                 "\"celebr\":%u,\"stayin\":%u,\"harlem\":%u,\"pbjtime\":%u,"
                 "\"sys_boot\":%u,\"sys_mode_n\":%u,\"sys_mode_s\":%u,"
                 "\"sys_mode_t\":%u,\"sys_drv_on\":%u,\"sys_dome_on\":%u,"
                 "\"snd_cat_gen_lo\":%u,\"snd_cat_gen_hi\":%u,"
                 "\"snd_cat_chat_lo\":%u,\"snd_cat_chat_hi\":%u,"
                 "\"snd_cat_hap_lo\":%u,\"snd_cat_hap_hi\":%u,"
                 "\"snd_cat_proc_lo\":%u,\"snd_cat_proc_hi\":%u,"
                 "\"snd_cat_sad_lo\":%u,\"snd_cat_sad_hi\":%u,"
                 "\"snd_cat_sent_lo\":%u,\"snd_cat_sent_hi\":%u,"
                 "\"snd_cat_hum_lo\":%u,\"snd_cat_hum_hi\":%u,"
                 "\"snd_cat_scrm_lo\":%u,\"snd_cat_scrm_hi\":%u,"
                 "\"snd_cat_ooh_lo\":%u,\"snd_cat_ooh_hi\":%u,"
                 "\"snd_cat_alrm_lo\":%u,\"snd_cat_alrm_hi\":%u,"
                 "\"snd_cat_pfft_lo\":%u,\"snd_cat_pfft_hi\":%u,"
                 "\"snd_cat_whis_lo\":%u,\"snd_cat_whis_hi\":%u,"
                 "\"rand_min\":%u,\"rand_max\":%u,\"volume\":%u,"
                 "\"snd_int_quiet\":%u,\"snd_int_mid\":%u,"
                 "\"snd_int_full\":%u,\"snd_int_awake\":%u}",
                 scream, faint, leia, cantinaS, swTheme, impMarch, cantinaL, startup, doodoo,
                 failure, disco, mahna, inlove, macho, gangnam, uptown, celebr, stayin,
                 harlem, pbjtime, sysBoot, sysModeN, sysModeS, sysModeT, sysDrvOn, sysDomeOn,
                 catGenLo, catGenHi, catChatLo, catChatHi, catHapLo, catHapHi, catProcLo,
                 catProcHi, catSadLo, catSadHi, catSentLo, catSentHi, catHumLo, catHumHi,
                 catScrmLo, catScrmHi, catOohLo, catOohHi, catAlrmLo, catAlrmHi, catPfftLo,
                 catPfftHi, catWhisLo, catWhisHi, randMin, randMax, volume, intQuiet, intMid,
                 intFull, intAwake);
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
        // track-number keys accept 1–999, except T08/T09 keys and snd_cat_* keys
        // which allow 0–999 (0 = silent/unset).
        String keyValue = keyParam->value();
        const char* key = keyValue.c_str();
        bool isInterval = (strncmp(key, "snd_int_", 8) == 0);
        bool isCategoryRangeKey = (strncmp(key, "snd_cat_", 8) == 0);
        bool isZeroAllowedTrackKey =
            isCategoryRangeKey || strcmp(key, "doodoo") == 0 || strcmp(key, "failure") == 0 ||
            strcmp(key, "disco") == 0 || strcmp(key, "mahna") == 0 ||
            strcmp(key, "inlove") == 0 || strcmp(key, "macho") == 0 ||
            strcmp(key, "gangnam") == 0 || strcmp(key, "uptown") == 0 ||
            strcmp(key, "celebr") == 0 || strcmp(key, "stayin") == 0 ||
            strcmp(key, "harlem") == 0 || strcmp(key, "pbjtime") == 0 ||
            strcmp(key, "sys_boot") == 0 || strcmp(key, "sys_mode_n") == 0 ||
            strcmp(key, "sys_mode_s") == 0 || strcmp(key, "sys_mode_t") == 0 ||
            strcmp(key, "sys_drv_on") == 0 || strcmp(key, "sys_dome_on") == 0;

        String trackValue = trackParam->value();
        uint32_t track = 0;
        if (!parseUint32Value(trackValue.c_str(), &track)) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"track must be a non-negative integer\"}");
            return;
        }

        if (isInterval) {
            if (track > 3600U) {
                req->send(400, "application/json",
                          "{\"ok\":false,\"error\":\"interval must be 0–3600 s\"}");
                return;
            }
        } else {
            if (track > 999U) {
                req->send(400, "application/json",
                          "{\"ok\":false,\"error\":\"track must be 0–999\"}");
                return;
            }
            if (track == 0U && !isZeroAllowedTrackKey) {
                req->send(400, "application/json",
                          "{\"ok\":false,\"error\":\"track must be 1–999\"}");
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
        else if (strcmp(key, "doodoo") == 0)
            fieldPtr = &robotState.cfg_snd_doodoo;
        else if (strcmp(key, "failure") == 0)
            fieldPtr = &robotState.cfg_snd_failure;
        else if (strcmp(key, "disco") == 0)
            fieldPtr = &robotState.cfg_snd_disco;
        else if (strcmp(key, "mahna") == 0)
            fieldPtr = &robotState.cfg_snd_mahna;
        else if (strcmp(key, "inlove") == 0)
            fieldPtr = &robotState.cfg_snd_inlove;
        else if (strcmp(key, "macho") == 0)
            fieldPtr = &robotState.cfg_snd_macho;
        else if (strcmp(key, "gangnam") == 0)
            fieldPtr = &robotState.cfg_snd_gangnam;
        else if (strcmp(key, "uptown") == 0)
            fieldPtr = &robotState.cfg_snd_uptown;
        else if (strcmp(key, "celebr") == 0)
            fieldPtr = &robotState.cfg_snd_celebr;
        else if (strcmp(key, "stayin") == 0)
            fieldPtr = &robotState.cfg_snd_stayin;
        else if (strcmp(key, "harlem") == 0)
            fieldPtr = &robotState.cfg_snd_harlem;
        else if (strcmp(key, "pbjtime") == 0)
            fieldPtr = &robotState.cfg_snd_pbjtime;
        else if (strcmp(key, "sys_boot") == 0)
            fieldPtr = &robotState.cfg_snd_sys_boot;
        else if (strcmp(key, "sys_mode_n") == 0)
            fieldPtr = &robotState.cfg_snd_sys_mode_n;
        else if (strcmp(key, "sys_mode_s") == 0)
            fieldPtr = &robotState.cfg_snd_sys_mode_s;
        else if (strcmp(key, "sys_mode_t") == 0)
            fieldPtr = &robotState.cfg_snd_sys_mode_t;
        else if (strcmp(key, "sys_drv_on") == 0)
            fieldPtr = &robotState.cfg_snd_sys_drv_on;
        else if (strcmp(key, "sys_dome_on") == 0)
            fieldPtr = &robotState.cfg_snd_sys_dome_on;
        else if (strcmp(key, "snd_cat_gen_lo") == 0)
            fieldPtr = &robotState.cfg_snd_cat_gen_lo;
        else if (strcmp(key, "snd_cat_gen_hi") == 0)
            fieldPtr = &robotState.cfg_snd_cat_gen_hi;
        else if (strcmp(key, "snd_cat_chat_lo") == 0)
            fieldPtr = &robotState.cfg_snd_cat_chat_lo;
        else if (strcmp(key, "snd_cat_chat_hi") == 0)
            fieldPtr = &robotState.cfg_snd_cat_chat_hi;
        else if (strcmp(key, "snd_cat_hap_lo") == 0)
            fieldPtr = &robotState.cfg_snd_cat_hap_lo;
        else if (strcmp(key, "snd_cat_hap_hi") == 0)
            fieldPtr = &robotState.cfg_snd_cat_hap_hi;
        else if (strcmp(key, "snd_cat_proc_lo") == 0)
            fieldPtr = &robotState.cfg_snd_cat_proc_lo;
        else if (strcmp(key, "snd_cat_proc_hi") == 0)
            fieldPtr = &robotState.cfg_snd_cat_proc_hi;
        else if (strcmp(key, "snd_cat_sad_lo") == 0)
            fieldPtr = &robotState.cfg_snd_cat_sad_lo;
        else if (strcmp(key, "snd_cat_sad_hi") == 0)
            fieldPtr = &robotState.cfg_snd_cat_sad_hi;
        else if (strcmp(key, "snd_cat_sent_lo") == 0)
            fieldPtr = &robotState.cfg_snd_cat_sent_lo;
        else if (strcmp(key, "snd_cat_sent_hi") == 0)
            fieldPtr = &robotState.cfg_snd_cat_sent_hi;
        else if (strcmp(key, "snd_cat_hum_lo") == 0)
            fieldPtr = &robotState.cfg_snd_cat_hum_lo;
        else if (strcmp(key, "snd_cat_hum_hi") == 0)
            fieldPtr = &robotState.cfg_snd_cat_hum_hi;
        else if (strcmp(key, "snd_cat_scrm_lo") == 0)
            fieldPtr = &robotState.cfg_snd_cat_scrm_lo;
        else if (strcmp(key, "snd_cat_scrm_hi") == 0)
            fieldPtr = &robotState.cfg_snd_cat_scrm_hi;
        else if (strcmp(key, "snd_cat_ooh_lo") == 0)
            fieldPtr = &robotState.cfg_snd_cat_ooh_lo;
        else if (strcmp(key, "snd_cat_ooh_hi") == 0)
            fieldPtr = &robotState.cfg_snd_cat_ooh_hi;
        else if (strcmp(key, "snd_cat_alrm_lo") == 0)
            fieldPtr = &robotState.cfg_snd_cat_alrm_lo;
        else if (strcmp(key, "snd_cat_alrm_hi") == 0)
            fieldPtr = &robotState.cfg_snd_cat_alrm_hi;
        else if (strcmp(key, "snd_cat_pfft_lo") == 0)
            fieldPtr = &robotState.cfg_snd_cat_pfft_lo;
        else if (strcmp(key, "snd_cat_pfft_hi") == 0)
            fieldPtr = &robotState.cfg_snd_cat_pfft_hi;
        else if (strcmp(key, "snd_cat_whis_lo") == 0)
            fieldPtr = &robotState.cfg_snd_cat_whis_lo;
        else if (strcmp(key, "snd_cat_whis_hi") == 0)
            fieldPtr = &robotState.cfg_snd_cat_whis_hi;
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
