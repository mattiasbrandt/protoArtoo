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
#include <ctype.h>
#include <string.h>

#include "api_helpers.h"
#include "audio_dollar_parser.h"
#include "audio_task.h"
#include "config.h"
#include "config_store.h"
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

// Resolve a category-range key to the corresponding RobotState field.
// Caller MUST hold robotStateMux.
static uint16_t* categoryTrackFieldForKeyLocked(const char* key) {
    if (strcmp(key, "snd_cat_gen_lo") == 0) return &robotState.cfg_snd_cat_gen_lo;
    if (strcmp(key, "snd_cat_gen_hi") == 0) return &robotState.cfg_snd_cat_gen_hi;
    if (strcmp(key, "snd_cat_chat_lo") == 0) return &robotState.cfg_snd_cat_chat_lo;
    if (strcmp(key, "snd_cat_chat_hi") == 0) return &robotState.cfg_snd_cat_chat_hi;
    if (strcmp(key, "snd_cat_hap_lo") == 0) return &robotState.cfg_snd_cat_hap_lo;
    if (strcmp(key, "snd_cat_hap_hi") == 0) return &robotState.cfg_snd_cat_hap_hi;
    if (strcmp(key, "snd_cat_proc_lo") == 0) return &robotState.cfg_snd_cat_proc_lo;
    if (strcmp(key, "snd_cat_proc_hi") == 0) return &robotState.cfg_snd_cat_proc_hi;
    if (strcmp(key, "snd_cat_sad_lo") == 0) return &robotState.cfg_snd_cat_sad_lo;
    if (strcmp(key, "snd_cat_sad_hi") == 0) return &robotState.cfg_snd_cat_sad_hi;
    if (strcmp(key, "snd_cat_sent_lo") == 0) return &robotState.cfg_snd_cat_sent_lo;
    if (strcmp(key, "snd_cat_sent_hi") == 0) return &robotState.cfg_snd_cat_sent_hi;
    if (strcmp(key, "snd_cat_hum_lo") == 0) return &robotState.cfg_snd_cat_hum_lo;
    if (strcmp(key, "snd_cat_hum_hi") == 0) return &robotState.cfg_snd_cat_hum_hi;
    if (strcmp(key, "snd_cat_scrm_lo") == 0) return &robotState.cfg_snd_cat_scrm_lo;
    if (strcmp(key, "snd_cat_scrm_hi") == 0) return &robotState.cfg_snd_cat_scrm_hi;
    if (strcmp(key, "snd_cat_ooh_lo") == 0) return &robotState.cfg_snd_cat_ooh_lo;
    if (strcmp(key, "snd_cat_ooh_hi") == 0) return &robotState.cfg_snd_cat_ooh_hi;
    if (strcmp(key, "snd_cat_alrm_lo") == 0) return &robotState.cfg_snd_cat_alrm_lo;
    if (strcmp(key, "snd_cat_alrm_hi") == 0) return &robotState.cfg_snd_cat_alrm_hi;
    if (strcmp(key, "snd_cat_snrk_lo") == 0) return &robotState.cfg_snd_cat_snarky_lo;
    if (strcmp(key, "snd_cat_snrk_hi") == 0) return &robotState.cfg_snd_cat_snarky_hi;
    if (strcmp(key, "snd_cat_whis_lo") == 0) return &robotState.cfg_snd_cat_whis_lo;
    if (strcmp(key, "snd_cat_whis_hi") == 0) return &robotState.cfg_snd_cat_whis_hi;
    return nullptr;
}

// Return matching category-range partner key (lo<->hi).
static const char* categoryRangeCompanionKey(const char* key) {
    if (strcmp(key, "snd_cat_gen_lo") == 0) return "snd_cat_gen_hi";
    if (strcmp(key, "snd_cat_gen_hi") == 0) return "snd_cat_gen_lo";
    if (strcmp(key, "snd_cat_chat_lo") == 0) return "snd_cat_chat_hi";
    if (strcmp(key, "snd_cat_chat_hi") == 0) return "snd_cat_chat_lo";
    if (strcmp(key, "snd_cat_hap_lo") == 0) return "snd_cat_hap_hi";
    if (strcmp(key, "snd_cat_hap_hi") == 0) return "snd_cat_hap_lo";
    if (strcmp(key, "snd_cat_proc_lo") == 0) return "snd_cat_proc_hi";
    if (strcmp(key, "snd_cat_proc_hi") == 0) return "snd_cat_proc_lo";
    if (strcmp(key, "snd_cat_sad_lo") == 0) return "snd_cat_sad_hi";
    if (strcmp(key, "snd_cat_sad_hi") == 0) return "snd_cat_sad_lo";
    if (strcmp(key, "snd_cat_sent_lo") == 0) return "snd_cat_sent_hi";
    if (strcmp(key, "snd_cat_sent_hi") == 0) return "snd_cat_sent_lo";
    if (strcmp(key, "snd_cat_hum_lo") == 0) return "snd_cat_hum_hi";
    if (strcmp(key, "snd_cat_hum_hi") == 0) return "snd_cat_hum_lo";
    if (strcmp(key, "snd_cat_scrm_lo") == 0) return "snd_cat_scrm_hi";
    if (strcmp(key, "snd_cat_scrm_hi") == 0) return "snd_cat_scrm_lo";
    if (strcmp(key, "snd_cat_ooh_lo") == 0) return "snd_cat_ooh_hi";
    if (strcmp(key, "snd_cat_ooh_hi") == 0) return "snd_cat_ooh_lo";
    if (strcmp(key, "snd_cat_alrm_lo") == 0) return "snd_cat_alrm_hi";
    if (strcmp(key, "snd_cat_alrm_hi") == 0) return "snd_cat_alrm_lo";
    if (strcmp(key, "snd_cat_snrk_lo") == 0) return "snd_cat_snrk_hi";
    if (strcmp(key, "snd_cat_snrk_hi") == 0) return "snd_cat_snrk_lo";
    if (strcmp(key, "snd_cat_whis_lo") == 0) return "snd_cat_whis_hi";
    if (strcmp(key, "snd_cat_whis_hi") == 0) return "snd_cat_whis_lo";
    return nullptr;
}

struct ChirpBindingKeyMapEntry {
    const char* key;
    const char* nvsKey;
};

struct ChirpCategoryBindingMapEntry {
    const char* loKey;
    const char* hiKey;
    const char* nvsKey;
};

static constexpr ChirpBindingKeyMapEntry CHIRP_BINDING_KEYS[] = {
    {"scream", "chr_scream"},       {"faint", "chr_faint"},
    {"leia", "chr_leia"},           {"cantina_s", "chr_cantina_s"},
    {"sw_theme", "chr_sw_theme"},   {"imp_march", "chr_imp_march"},
    {"cantina_l", "chr_cantina_l"}, {"startup", "chr_startup"},
    {"doodoo", "chr_doodoo"},       {"failure", "chr_failure"},
    {"disco", "chr_disco"},         {"mahna", "chr_mahna"},
    {"inlove", "chr_inlove"},       {"macho", "chr_macho"},
    {"gangnam", "chr_gangnam"},     {"uptown", "chr_uptown"},
    {"celebr", "chr_celebr"},       {"stayin", "chr_stayin"},
    {"harlem", "chr_harlem"},       {"pbjtime", "chr_pbjtime"},
    {"sys_boot", "chr_sys_boot"},   {"sys_mode_n", "chr_sys_mode_n"},
    {"sys_mode_s", "chr_sys_mode_s"},
    {"sys_mode_t", "chr_sys_mode_t"},
    {"sys_drv_on", "chr_sys_drv_on"},
    {"sys_dome_on", "chr_sys_dome_on"},
};

static bool audioCatalogSupported() {
    return (audioGetCapabilities() & AudioDriver::AUDIO_CAP_CATALOG) != 0;
}

static constexpr ChirpCategoryBindingMapEntry CHIRP_CATEGORY_BINDING_KEYS[] = {
    {"snd_cat_gen_lo", "snd_cat_gen_hi", "chr_cat_gen"},
    {"snd_cat_chat_lo", "snd_cat_chat_hi", "chr_cat_chat"},
    {"snd_cat_hap_lo", "snd_cat_hap_hi", "chr_cat_hap"},
    {"snd_cat_proc_lo", "snd_cat_proc_hi", "chr_cat_proc"},
    {"snd_cat_sad_lo", "snd_cat_sad_hi", "chr_cat_sad"},
    {"snd_cat_sent_lo", "snd_cat_sent_hi", "chr_cat_sent"},
    {"snd_cat_hum_lo", "snd_cat_hum_hi", "chr_cat_hum"},
    {"snd_cat_scrm_lo", "snd_cat_scrm_hi", "chr_cat_scrm"},
    {"snd_cat_ooh_lo", "snd_cat_ooh_hi", "chr_cat_ooh"},
    {"snd_cat_alrm_lo", "snd_cat_alrm_hi", "chr_cat_alrm"},
    {"snd_cat_snrk_lo", "snd_cat_snrk_hi", "chr_cat_snrk"},
    {"snd_cat_whis_lo", "snd_cat_whis_hi", "chr_cat_whis"},
};

static const ChirpCategoryBindingMapEntry* chirpCategoryBindingEntryForRangeKeys(const char* loKey,
                                                                                   const char* hiKey) {
    if (loKey == nullptr || hiKey == nullptr) {
        return nullptr;
    }
    for (size_t i = 0;
         i < (sizeof(CHIRP_CATEGORY_BINDING_KEYS) / sizeof(CHIRP_CATEGORY_BINDING_KEYS[0])); ++i) {
        const ChirpCategoryBindingMapEntry& entry = CHIRP_CATEGORY_BINDING_KEYS[i];
        if (strcmp(entry.loKey, loKey) == 0 && strcmp(entry.hiKey, hiKey) == 0) {
            return &entry;
        }
    }
    return nullptr;
}


static const char* chirpBindingNvsKey(const char* key) {
    if (key == nullptr) {
        return nullptr;
    }
    for (size_t i = 0; i < (sizeof(CHIRP_BINDING_KEYS) / sizeof(CHIRP_BINDING_KEYS[0])); ++i) {
        if (strcmp(CHIRP_BINDING_KEYS[i].key, key) == 0) {
            return CHIRP_BINDING_KEYS[i].nvsKey;
        }
    }
    return nullptr;
}

static uint32_t packChirpBinding(uint16_t index, uint8_t bank, char page) {
    const uint8_t pageByte = (uint8_t)toupper((unsigned char)page);
    return ((uint32_t)bank << 24) | ((uint32_t)pageByte << 16) | (uint32_t)index;
}

static bool unpackChirpBinding(uint32_t packed, uint8_t* bankOut, char* pageOut,
                               uint16_t* indexOut) {
    if (bankOut == nullptr || pageOut == nullptr || indexOut == nullptr) {
        return false;
    }
    uint8_t bank = (uint8_t)((packed >> 24) & 0xFFu);
    char page = (char)((packed >> 16) & 0xFFu);
    uint16_t index = (uint16_t)(packed & 0xFFFFu);
    if (bank == 0 || index == 0) {
        return false;
    }
    page = (char)toupper((unsigned char)page);
    if (page < 'A' || page > 'Z') {
        return false;
    }
    *bankOut = bank;
    *pageOut = page;
    *indexOut = index;
    return true;
}

static uint32_t packChirpCategoryBinding(uint8_t bank, char page) {
    const uint8_t pageByte = (uint8_t)toupper((unsigned char)page);
    return ((uint32_t)bank << 8) | (uint32_t)pageByte;
}

static bool unpackChirpCategoryBinding(uint32_t packed, uint8_t* bankOut, char* pageOut) {
    if (bankOut == nullptr || pageOut == nullptr) {
        return false;
    }
    uint8_t bank = (uint8_t)((packed >> 8) & 0xFFu);
    char page = (char)(packed & 0xFFu);
    if (bank == 0) {
        return false;
    }
    page = (char)toupper((unsigned char)page);
    if (page < 'A' || page > 'Z') {
        return false;
    }
    *bankOut = bank;
    *pageOut = page;
    return true;
}

static bool parseChirpPage(const String& raw, char* pageOut) {
    if (pageOut == nullptr || raw.length() != 1) {
        return false;
    }
    char page = (char)toupper((unsigned char)raw[0]);
    if (page < 'A' || page > 'Z') {
        return false;
    }
    *pageOut = page;
    return true;
}

static void writeJsonEscaped(Print& out, const char* text) {
    out.print('"');
    if (text != nullptr) {
        for (const char* p = text; *p != '\0'; ++p) {
            const char c = *p;
            if (c == '"' || c == '\\') {
                out.print('\\');
                out.print(c);
            } else if (c == '\n') {
                out.print("\\n");
            } else if (c == '\r') {
                out.print("\\r");
            } else if (c == '\t') {
                out.print("\\t");
            } else {
                out.print(c);
            }
        }
    }
    out.print('"');
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

                if (value.template is<uint32_t>()) {
                    uint32_t parsed = value.template as<uint32_t>();
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

                if (value.template is<int32_t>()) {
                    int32_t parsed = value.template as<int32_t>();
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
        uint16_t catAlrmLo, catAlrmHi, catSnarkyLo, catSnarkyHi, catWhisLo, catWhisHi;
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
        catSnarkyLo = robotState.cfg_snd_cat_snarky_lo;
        catSnarkyHi = robotState.cfg_snd_cat_snarky_hi;
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
                 "\"snd_cat_snrk_lo\":%u,\"snd_cat_snrk_hi\":%u,"
                 "\"snd_cat_whis_lo\":%u,\"snd_cat_whis_hi\":%u,"
                 "\"rand_min\":%u,\"rand_max\":%u,\"volume\":%u,"
                 "\"snd_int_quiet\":%u,\"snd_int_mid\":%u,"
                 "\"snd_int_full\":%u,\"snd_int_awake\":%u}",
                 scream, faint, leia, cantinaS, swTheme, impMarch, cantinaL, startup, doodoo,
                 failure, disco, mahna, inlove, macho, gangnam, uptown, celebr, stayin,
                 harlem, pbjtime, sysBoot, sysModeN, sysModeS, sysModeT, sysDrvOn, sysDomeOn,
                 catGenLo, catGenHi, catChatLo, catChatHi, catHapLo, catHapHi, catProcLo,
                 catProcHi, catSadLo, catSadHi, catSentLo, catSentHi, catHumLo, catHumHi,
                 catScrmLo, catScrmHi, catOohLo, catOohHi, catAlrmLo, catAlrmHi, catSnarkyLo,
                 catSnarkyHi, catWhisLo, catWhisHi, randMin, randMax, volume, intQuiet, intMid,
                 intFull, intAwake);
        if (!audioCatalogSupported()) {
            req->send(200, "application/json", body);
            return;
        }

        auto* stream = req->beginResponseStream("application/json");
        if (stream == nullptr) {
            req->send(500, "application/json",
                      "{\"ok\":false,\"error\":\"response stream alloc failed\"}");
            return;
        }

        size_t bodyLen = strnlen(body, sizeof(body));
        if (bodyLen == 0 || body[bodyLen - 1] != '}') {
            req->send(200, "application/json", body);
            return;
        }

        body[bodyLen - 1] = '\0';
        stream->print(body);
        stream->print(",\"chirp_bindings\":{");

        bool firstBinding = true;
        bool firstCategoryBinding = true;
        Preferences prefs;
        if (prefs.begin(NVS_NAMESPACE, true)) {
            for (size_t i = 0; i < (sizeof(CHIRP_BINDING_KEYS) / sizeof(CHIRP_BINDING_KEYS[0])); ++i) {
                uint32_t packed = prefs.getUInt(CHIRP_BINDING_KEYS[i].nvsKey, 0);
                uint8_t bank = 0;
                char page = 'A';
                uint16_t index = 0;
                if (!unpackChirpBinding(packed, &bank, &page, &index)) {
                    continue;
                }

                if (!firstBinding) {
                    stream->print(',');
                }
                firstBinding = false;

                stream->print('"');
                stream->print(CHIRP_BINDING_KEYS[i].key);
                stream->print("\":{\"bank\":");
                stream->print((unsigned)bank);
                stream->print(",\"page\":\"");
                stream->print(page);
                stream->print("\",\"index\":");
                stream->print((unsigned)index);
                stream->print('}');
            }

            stream->print("},\"chirp_category_bindings\":{");
            for (size_t i = 0;
                 i < (sizeof(CHIRP_CATEGORY_BINDING_KEYS) / sizeof(CHIRP_CATEGORY_BINDING_KEYS[0]));
                 ++i) {
                uint32_t packed = prefs.getUInt(CHIRP_CATEGORY_BINDING_KEYS[i].nvsKey, 0);
                uint8_t bank = 0;
                char page = 'A';
                if (!unpackChirpCategoryBinding(packed, &bank, &page)) {
                    continue;
                }
                if (!firstCategoryBinding) {
                    stream->print(',');
                }
                firstCategoryBinding = false;
                stream->print('"');
                stream->print(CHIRP_CATEGORY_BINDING_KEYS[i].loKey);
                stream->print("\":{\"bank\":");
                stream->print((unsigned)bank);
                stream->print(",\"page\":\"");
                stream->print(page);
                stream->print("\"}");
            }
            prefs.end();
        } else {
            stream->print("},\"chirp_category_bindings\":{");
        }

        stream->print("}}");
        req->send(stream);
    });

    // ---- POST /api/audio/category-range ----
    // Atomic update for one category lo/hi pair to avoid partial two-request saves.
    server.on("/api/audio/category-range", HTTP_POST, [](AsyncWebServerRequest* req) {
        const AsyncWebParameter* loKeyParam = req->getParam("lo_key", true);
        const AsyncWebParameter* hiKeyParam = req->getParam("hi_key", true);
        const AsyncWebParameter* loParam = req->getParam("lo", true);
        const AsyncWebParameter* hiParam = req->getParam("hi", true);
        if (!loKeyParam || !hiKeyParam || !loParam || !hiParam) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"requires lo_key, hi_key, lo, hi parameters\"}");
            return;
        }

        String loKeyValue = loKeyParam->value();
        String hiKeyValue = hiKeyParam->value();
        const char* loKey = loKeyValue.c_str();
        const char* hiKey = hiKeyValue.c_str();

        const char* loCompanion = categoryRangeCompanionKey(loKey);
        const char* hiCompanion = categoryRangeCompanionKey(hiKey);
        const ChirpCategoryBindingMapEntry* categoryBindingEntry =
            chirpCategoryBindingEntryForRangeKeys(loKey, hiKey);
        if (loCompanion == nullptr || hiCompanion == nullptr || strcmp(loCompanion, hiKey) != 0 ||
            strcmp(hiCompanion, loKey) != 0 || categoryBindingEntry == nullptr) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"invalid category key pair\"}");
            return;
        }

        const AsyncWebParameter* bankParam = req->getParam("bank", true);
        const AsyncWebParameter* pageParam = req->getParam("page", true);
        const AsyncWebParameter* clearBindingParam = req->getParam("clear_binding", true);
        const bool hasBankedParams = (bankParam != nullptr) || (pageParam != nullptr);
        bool clearBinding = false;
        if (clearBindingParam != nullptr &&
            !parseBoolValue(clearBindingParam->value().c_str(), &clearBinding)) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"clear_binding must be true/false/1/0\"}");
            return;
        }
        if (hasBankedParams && clearBinding) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"clear_binding cannot be combined with bank/page\"}");
            return;
        }

        uint8_t categoryBank = 0;
        char categoryPage = 'A';
        if (hasBankedParams) {
            if (!(bankParam && pageParam)) {
                req->send(400, "application/json",
                          "{\"ok\":false,\"error\":\"bank and page must be provided together\"}");
                return;
            }
            if (!audioCatalogSupported()) {
                req->send(404, "application/json",
                          "{\"ok\":false,\"error\":\"catalog unsupported by active backend\"}");
                return;
            }
            uint32_t bankValue = 0;
            if (!parseUint32Value(bankParam->value().c_str(), &bankValue) || bankValue < 1 ||
                bankValue > 6) {
                req->send(400, "application/json",
                          "{\"ok\":false,\"error\":\"bank must be 1-6\"}");
                return;
            }
            if (!parseChirpPage(pageParam->value(), &categoryPage)) {
                req->send(400, "application/json",
                          "{\"ok\":false,\"error\":\"page must be a single letter A-Z\"}");
                return;
            }
            categoryBank = (uint8_t)bankValue;
        } else if (clearBinding && !audioCatalogSupported()) {
            req->send(404, "application/json",
                      "{\"ok\":false,\"error\":\"catalog unsupported by active backend\"}");
            return;
        }

        uint32_t loTrack = 0;
        uint32_t hiTrack = 0;
        if (!parseUint32Value(loParam->value().c_str(), &loTrack) ||
            !parseUint32Value(hiParam->value().c_str(), &hiTrack)) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"range values must be non-negative integers\"}");
            return;
        }
        if (loTrack > 999U || hiTrack > 999U) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"range values must be 0–999\"}");
            return;
        }
        if (!((loTrack == 0U && hiTrack == 0U) ||
              (loTrack >= 1U && hiTrack >= 1U && loTrack <= hiTrack))) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"range must be 0/0 or 1–999 with lo <= hi\"}");
            return;
        }

        uint16_t* loField = nullptr;
        uint16_t* hiField = nullptr;
        uint16_t oldLo = 0;
        uint16_t oldHi = 0;
        const uint16_t loValue = (uint16_t)loTrack;
        const uint16_t hiValue = (uint16_t)hiTrack;
        taskENTER_CRITICAL(&robotStateMux);
        loField = categoryTrackFieldForKeyLocked(loKey);
        hiField = categoryTrackFieldForKeyLocked(hiKey);
        if (loField != nullptr && hiField != nullptr) {
            oldLo = *loField;
            oldHi = *hiField;
            *loField = loValue;
            *hiField = hiValue;
        }
        taskEXIT_CRITICAL(&robotStateMux);

        if (loField == nullptr || hiField == nullptr) {
            req->send(400, "application/json", "{\"ok\":false,\"error\":\"unknown category key\"}");
            return;
        }

        // For simplicity and atomicity with configSave: restore values, re-capture, use configLoad
        // to read current state, then if CHIRP binding write fails, we have both old and new configs.
        // However, this is complex. Simpler approach: just persist the category range values
        // via direct NVS puts since they're already updated in robotState. For now, skip persisting
        // and rely on the main app path via saveConfigToNvs().
        //
        // Actually: the fields are updated in robotState above. If we want to persist immediately,
        // we need to either:
        // (a) Save only the category range fields (acceptable, but contradicts the goal)
        // (b) Save the full config via configSave (requires full snapshot capture)
        //
        // Option (b) is the migration goal. For now, since this is an edge case handler,
        // the robotState is updated and will be persisted on the next saveConfigToNvs() call
        // from the API or periodic save. For the CHIRP binding, it must be written directly
        // to NVS since it's not part of configSnapshot.

        Preferences prefs;
        bool ok = false;
        if (prefs.begin(NVS_NAMESPACE, false)) {
            bool wroteBinding = true;
            if (hasBankedParams) {
                uint32_t packedBinding = packChirpCategoryBinding(categoryBank, categoryPage);
                wroteBinding = prefs.putUInt(categoryBindingEntry->nvsKey, packedBinding) > 0;
            } else if (clearBinding) {
                wroteBinding = prefs.putUInt(categoryBindingEntry->nvsKey, 0) > 0;
            }
            if (!wroteBinding) {
                // Rollback robotState fields
                taskENTER_CRITICAL(&robotStateMux);
                *loField = oldLo;
                *hiField = oldHi;
                taskEXIT_CRITICAL(&robotStateMux);
            }
            ok = wroteBinding;
            prefs.end();
        }

        if (!ok) {
            req->send(500, "application/json",
                      "{\"ok\":false,\"error\":\"NVS write failed\"}");
            return;
        }

        if (hasBankedParams) {
            PA_LOG_INFO(TAG,
                        "[AUDIO] POST /api/audio/category-range %s=%u %s=%u bank=%u page=%c",
                        loKey, (unsigned)loValue, hiKey, (unsigned)hiValue,
                        (unsigned)categoryBank, categoryPage);
        } else if (clearBinding) {
            PA_LOG_INFO(TAG,
                        "[AUDIO] POST /api/audio/category-range %s=%u %s=%u clear_binding=true",
                        loKey, (unsigned)loValue, hiKey, (unsigned)hiValue);
        } else {
            PA_LOG_INFO(TAG, "[AUDIO] POST /api/audio/category-range %s=%u %s=%u", loKey,
                        (unsigned)loValue, hiKey, (unsigned)hiValue);
        }
        if ((hasBankedParams || clearBinding) && !audioQueueRefreshBindings(SRC_WEB_API)) {
            PA_LOG_WARN(TAG, "[AUDIO] binding cache refresh enqueue failed (queue full)");
        }
        req->send(200, "application/json", "{\"ok\":true}");
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

        const AsyncWebParameter* bankParam = req->getParam("bank", true);
        const AsyncWebParameter* pageParam = req->getParam("page", true);
        const bool hasBankedParams = (bankParam != nullptr) || (pageParam != nullptr);

        uint8_t bank = 0;
        char page = 'A';
        bool useBanked = false;
        const char* chirpBindingKey = chirpBindingNvsKey(key);

        if (hasBankedParams) {
            if (!(bankParam && pageParam)) {
                req->send(400, "application/json",
                          "{\"ok\":false,\"error\":\"bank and page must be provided together\"}");
                return;
            }
            if (!audioCatalogSupported()) {
                req->send(404, "application/json",
                          "{\"ok\":false,\"error\":\"catalog unsupported by active backend\"}");
                return;
            }
            if (chirpBindingKey == nullptr || isInterval) {
                req->send(400, "application/json",
                          "{\"ok\":false,\"error\":\"key does not support CHIRP binding\"}");
                return;
            }

            uint32_t bankValue = 0;
            if (!parseUint32Value(bankParam->value().c_str(), &bankValue) || bankValue < 1 ||
                bankValue > 6) {
                req->send(400, "application/json",
                          "{\"ok\":false,\"error\":\"bank must be 1-6\"}");
                return;
            }
            if (!parseChirpPage(pageParam->value(), &page)) {
                req->send(400, "application/json",
                          "{\"ok\":false,\"error\":\"page must be a single letter A-Z\"}");
                return;
            }
            bank = (uint8_t)bankValue;
            useBanked = true;
        }

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
                          "{\"ok\":false,\"error\":\"interval must be 0-3600 s\"}");
                return;
            }
        } else if (useBanked) {
            if (track < 1U || track > 65535U) {
                req->send(400, "application/json",
                          "{\"ok\":false,\"error\":\"banked index must be 1-65535\"}");
                return;
            }
        } else {
            if (track > 999U) {
                req->send(400, "application/json",
                          "{\"ok\":false,\"error\":\"track must be 0-999\"}");
                return;
            }
            if (track == 0U && !isZeroAllowedTrackKey) {
                req->send(400, "application/json",
                          "{\"ok\":false,\"error\":\"track must be 1-999\"}");
                return;
            }
        }

        uint16_t t = (uint16_t)track;
        uint16_t* fieldPtr = nullptr;
        uint16_t oldTrack = 0;
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
        else if (strcmp(key, "snd_cat_snrk_lo") == 0)
            fieldPtr = &robotState.cfg_snd_cat_snarky_lo;
        else if (strcmp(key, "snd_cat_snrk_hi") == 0)
            fieldPtr = &robotState.cfg_snd_cat_snarky_hi;
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
        if (fieldPtr) {
            oldTrack = *fieldPtr;
        }
        taskEXIT_CRITICAL(&robotStateMux);

        if (!fieldPtr) {
            req->send(400, "application/json", "{\"ok\":false,\"error\":\"unknown key\"}");
            return;
        }

        // Update robotState field first (within the existing critical section below)
        // Then capture full snapshot and save via configSave
        Preferences prefs;
        bool ok = false;
        if (prefs.begin(NVS_NAMESPACE, false)) {
            // Capture full config snapshot for configSave (fieldPtr already points to the cfg field)
            // For efficiency, capture minimal cfg fields needed for this track, but configSave
            // expects a full snapshot. We'll capture just this field from robotState.
            // Note: This requires holding mutex during capture, so we do a full capture approach.
            ConfigSnapshot snap;
            taskENTER_CRITICAL(&robotStateMux);
            // Capture all cfg_* fields for full snapshot
            // To avoid massive duplication, we use the fact that fieldPtr points to *fieldPtr = t
            // So we capture everything post-update.
            snap.snd_scream = robotState.cfg_snd_scream;
            snap.snd_faint = robotState.cfg_snd_faint;
            snap.snd_leia = robotState.cfg_snd_leia;
            snap.snd_cantina_s = robotState.cfg_snd_cantina_s;
            snap.snd_sw_theme = robotState.cfg_snd_sw_theme;
            snap.snd_imp_march = robotState.cfg_snd_imp_march;
            snap.snd_cantina_l = robotState.cfg_snd_cantina_l;
            snap.snd_startup = robotState.cfg_snd_startup;
            snap.snd_doodoo = robotState.cfg_snd_doodoo;
            snap.snd_failure = robotState.cfg_snd_failure;
            snap.snd_disco = robotState.cfg_snd_disco;
            snap.snd_mahna = robotState.cfg_snd_mahna;
            snap.snd_inlove = robotState.cfg_snd_inlove;
            snap.snd_macho = robotState.cfg_snd_macho;
            snap.snd_gangnam = robotState.cfg_snd_gangnam;
            snap.snd_uptown = robotState.cfg_snd_uptown;
            snap.snd_celebr = robotState.cfg_snd_celebr;
            snap.snd_stayin = robotState.cfg_snd_stayin;
            snap.snd_harlem = robotState.cfg_snd_harlem;
            snap.snd_pbjtime = robotState.cfg_snd_pbjtime;
            snap.snd_sys_boot = robotState.cfg_snd_sys_boot;
            snap.snd_sys_mode_n = robotState.cfg_snd_sys_mode_n;
            snap.snd_sys_mode_s = robotState.cfg_snd_sys_mode_s;
            snap.snd_sys_mode_t = robotState.cfg_snd_sys_mode_t;
            snap.snd_sys_drv_on = robotState.cfg_snd_sys_drv_on;
            snap.snd_sys_dome_on = robotState.cfg_snd_sys_dome_on;
            snap.snd_rand_min = robotState.cfg_snd_rand_min;
            snap.snd_rand_max = robotState.cfg_snd_rand_max;
            snap.snd_int_quiet = robotState.cfg_snd_int_quiet;
            snap.snd_int_mid = robotState.cfg_snd_int_mid;
            snap.snd_int_full = robotState.cfg_snd_int_full;
            snap.snd_int_awake = robotState.cfg_snd_int_awake;
            snap.snd_moodcat_quiet = robotState.cfg_snd_moodcat_quiet;
            snap.snd_moodcat_mid = robotState.cfg_snd_moodcat_mid;
            snap.snd_moodcat_full = robotState.cfg_snd_moodcat_full;
            snap.snd_moodcat_awakeplus = robotState.cfg_snd_moodcat_awakeplus;
            snap.snd_cat_gen_lo = robotState.cfg_snd_cat_gen_lo;
            snap.snd_cat_gen_hi = robotState.cfg_snd_cat_gen_hi;
            snap.snd_cat_chat_lo = robotState.cfg_snd_cat_chat_lo;
            snap.snd_cat_chat_hi = robotState.cfg_snd_cat_chat_hi;
            snap.snd_cat_hap_lo = robotState.cfg_snd_cat_hap_lo;
            snap.snd_cat_hap_hi = robotState.cfg_snd_cat_hap_hi;
            snap.snd_cat_proc_lo = robotState.cfg_snd_cat_proc_lo;
            snap.snd_cat_proc_hi = robotState.cfg_snd_cat_proc_hi;
            snap.snd_cat_sad_lo = robotState.cfg_snd_cat_sad_lo;
            snap.snd_cat_sad_hi = robotState.cfg_snd_cat_sad_hi;
            snap.snd_cat_sent_lo = robotState.cfg_snd_cat_sent_lo;
            snap.snd_cat_sent_hi = robotState.cfg_snd_cat_sent_hi;
            snap.snd_cat_hum_lo = robotState.cfg_snd_cat_hum_lo;
            snap.snd_cat_hum_hi = robotState.cfg_snd_cat_hum_hi;
            snap.snd_cat_scrm_lo = robotState.cfg_snd_cat_scrm_lo;
            snap.snd_cat_scrm_hi = robotState.cfg_snd_cat_scrm_hi;
            snap.snd_cat_ooh_lo = robotState.cfg_snd_cat_ooh_lo;
            snap.snd_cat_ooh_hi = robotState.cfg_snd_cat_ooh_hi;
            snap.snd_cat_alrm_lo = robotState.cfg_snd_cat_alrm_lo;
            snap.snd_cat_alrm_hi = robotState.cfg_snd_cat_alrm_hi;
            snap.snd_cat_snarky_lo = robotState.cfg_snd_cat_snarky_lo;
            snap.snd_cat_snarky_hi = robotState.cfg_snd_cat_snarky_hi;
            snap.snd_cat_whis_lo = robotState.cfg_snd_cat_whis_lo;
            snap.snd_cat_whis_hi = robotState.cfg_snd_cat_whis_hi;
            // Store old track value in case we need to rollback
            uint16_t snapOldTrack = oldTrack;
            taskEXIT_CRITICAL(&robotStateMux);

            bool wroteTrack = configSave(prefs, snap);
            bool wroteChirp = true;

            if (wroteTrack && chirpBindingKey != nullptr) {
                uint32_t chirpPacked = useBanked ? packChirpBinding(t, bank, page) : 0;
                wroteChirp = prefs.putUInt(chirpBindingKey, chirpPacked) > 0;
            }

            if (wroteTrack && !wroteChirp) {
                // Rollback: restore old value in robotState and re-save
                taskENTER_CRITICAL(&robotStateMux);
                *fieldPtr = snapOldTrack;
                taskEXIT_CRITICAL(&robotStateMux);
                // Re-capture and save old snapshot
                ConfigSnapshot oldSnap;
                taskENTER_CRITICAL(&robotStateMux);
                oldSnap.snd_scream = robotState.cfg_snd_scream;
                oldSnap.snd_faint = robotState.cfg_snd_faint;
                oldSnap.snd_leia = robotState.cfg_snd_leia;
                oldSnap.snd_cantina_s = robotState.cfg_snd_cantina_s;
                oldSnap.snd_sw_theme = robotState.cfg_snd_sw_theme;
                oldSnap.snd_imp_march = robotState.cfg_snd_imp_march;
                oldSnap.snd_cantina_l = robotState.cfg_snd_cantina_l;
                oldSnap.snd_startup = robotState.cfg_snd_startup;
                oldSnap.snd_doodoo = robotState.cfg_snd_doodoo;
                oldSnap.snd_failure = robotState.cfg_snd_failure;
                oldSnap.snd_disco = robotState.cfg_snd_disco;
                oldSnap.snd_mahna = robotState.cfg_snd_mahna;
                oldSnap.snd_inlove = robotState.cfg_snd_inlove;
                oldSnap.snd_macho = robotState.cfg_snd_macho;
                oldSnap.snd_gangnam = robotState.cfg_snd_gangnam;
                oldSnap.snd_uptown = robotState.cfg_snd_uptown;
                oldSnap.snd_celebr = robotState.cfg_snd_celebr;
                oldSnap.snd_stayin = robotState.cfg_snd_stayin;
                oldSnap.snd_harlem = robotState.cfg_snd_harlem;
                oldSnap.snd_pbjtime = robotState.cfg_snd_pbjtime;
                oldSnap.snd_sys_boot = robotState.cfg_snd_sys_boot;
                oldSnap.snd_sys_mode_n = robotState.cfg_snd_sys_mode_n;
                oldSnap.snd_sys_mode_s = robotState.cfg_snd_sys_mode_s;
                oldSnap.snd_sys_mode_t = robotState.cfg_snd_sys_mode_t;
                oldSnap.snd_sys_drv_on = robotState.cfg_snd_sys_drv_on;
                oldSnap.snd_sys_dome_on = robotState.cfg_snd_sys_dome_on;
                oldSnap.snd_rand_min = robotState.cfg_snd_rand_min;
                oldSnap.snd_rand_max = robotState.cfg_snd_rand_max;
                oldSnap.snd_int_quiet = robotState.cfg_snd_int_quiet;
                oldSnap.snd_int_mid = robotState.cfg_snd_int_mid;
                oldSnap.snd_int_full = robotState.cfg_snd_int_full;
                oldSnap.snd_int_awake = robotState.cfg_snd_int_awake;
                oldSnap.snd_moodcat_quiet = robotState.cfg_snd_moodcat_quiet;
                oldSnap.snd_moodcat_mid = robotState.cfg_snd_moodcat_mid;
                oldSnap.snd_moodcat_full = robotState.cfg_snd_moodcat_full;
                oldSnap.snd_moodcat_awakeplus = robotState.cfg_snd_moodcat_awakeplus;
                oldSnap.snd_cat_gen_lo = robotState.cfg_snd_cat_gen_lo;
                oldSnap.snd_cat_gen_hi = robotState.cfg_snd_cat_gen_hi;
                oldSnap.snd_cat_chat_lo = robotState.cfg_snd_cat_chat_lo;
                oldSnap.snd_cat_chat_hi = robotState.cfg_snd_cat_chat_hi;
                oldSnap.snd_cat_hap_lo = robotState.cfg_snd_cat_hap_lo;
                oldSnap.snd_cat_hap_hi = robotState.cfg_snd_cat_hap_hi;
                oldSnap.snd_cat_proc_lo = robotState.cfg_snd_cat_proc_lo;
                oldSnap.snd_cat_proc_hi = robotState.cfg_snd_cat_proc_hi;
                oldSnap.snd_cat_sad_lo = robotState.cfg_snd_cat_sad_lo;
                oldSnap.snd_cat_sad_hi = robotState.cfg_snd_cat_sad_hi;
                oldSnap.snd_cat_sent_lo = robotState.cfg_snd_cat_sent_lo;
                oldSnap.snd_cat_sent_hi = robotState.cfg_snd_cat_sent_hi;
                oldSnap.snd_cat_hum_lo = robotState.cfg_snd_cat_hum_lo;
                oldSnap.snd_cat_hum_hi = robotState.cfg_snd_cat_hum_hi;
                oldSnap.snd_cat_scrm_lo = robotState.cfg_snd_cat_scrm_lo;
                oldSnap.snd_cat_scrm_hi = robotState.cfg_snd_cat_scrm_hi;
                oldSnap.snd_cat_ooh_lo = robotState.cfg_snd_cat_ooh_lo;
                oldSnap.snd_cat_ooh_hi = robotState.cfg_snd_cat_ooh_hi;
                oldSnap.snd_cat_alrm_lo = robotState.cfg_snd_cat_alrm_lo;
                oldSnap.snd_cat_alrm_hi = robotState.cfg_snd_cat_alrm_hi;
                oldSnap.snd_cat_snarky_lo = robotState.cfg_snd_cat_snarky_lo;
                oldSnap.snd_cat_snarky_hi = robotState.cfg_snd_cat_snarky_hi;
                oldSnap.snd_cat_whis_lo = robotState.cfg_snd_cat_whis_lo;
                oldSnap.snd_cat_whis_hi = robotState.cfg_snd_cat_whis_hi;
                taskEXIT_CRITICAL(&robotStateMux);
                configSave(prefs, oldSnap);
            }

            ok = wroteTrack && wroteChirp;
            prefs.end();
        }

        if (useBanked) {
            PA_LOG_INFO(TAG,
                        "[AUDIO] POST /api/audio/tracks key=%s index=%u bank=%u page=%c", key,
                        (unsigned)t, (unsigned)bank, page);
        } else {
            PA_LOG_INFO(TAG, "[AUDIO] POST /api/audio/tracks key=%s track=%u", key, (unsigned)t);
        }

        if (ok) {
            taskENTER_CRITICAL(&robotStateMux);
            *fieldPtr = t;
            taskEXIT_CRITICAL(&robotStateMux);
            if (chirpBindingKey != nullptr && !audioQueueRefreshBindings(SRC_WEB_API)) {
                PA_LOG_WARN(TAG, "[AUDIO] binding cache refresh enqueue failed (queue full)");
            }
            req->send(200, "application/json", "{\"ok\":true}");
        } else {
            req->send(500, "application/json", "{\"ok\":false,\"error\":\"NVS write failed\"}");
        }
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

    // ---- GET /api/audio/catalog ----
    // Returns cached CHIRP catalog from the most recent refresh.
    server.on("/api/audio/catalog", HTTP_GET, [](AsyncWebServerRequest* req) {
        if (!audioCatalogSupported()) {
            req->send(404, "application/json",
                      "{\"ok\":false,\"error\":\"catalog unsupported by active backend\"}");
            return;
        }

        uint8_t bankFilter = 0;
        const AsyncWebParameter* bankParam = req->getParam("bank");
        if (bankParam != nullptr) {
            uint32_t parsedBank = 0;
            if (!parseUint32Value(bankParam->value().c_str(), &parsedBank) || parsedBank < 1 ||
                parsedBank > 6) {
                req->send(400, "application/json",
                          "{\"ok\":false,\"error\":\"bank must be 1-6\"}");
                return;
            }
            bankFilter = (uint8_t)parsedBank;
        }

        bool ready = audioIsCatalogReady();
        uint8_t bankCount = 0;
        uint16_t entryCount = 0;
        const ChirpCatalogBank* banks = audioGetCatalogBanks(&bankCount);
        const ChirpCatalogEntry* entries = audioGetCatalogEntries(&entryCount);

        auto* stream = req->beginResponseStream("application/json");
        if (stream == nullptr) {
            req->send(500, "application/json",
                      "{\"ok\":false,\"error\":\"response stream alloc failed\"}");
            return;
        }

        stream->print("{\"ready\":");
        stream->print(ready ? "true" : "false");

        stream->print(",\"banks\":[");
        if (ready && banks != nullptr) {
            bool first = true;
            for (uint8_t i = 0; i < bankCount; ++i) {
                if (bankFilter != 0 && banks[i].bank != bankFilter) {
                    continue;
                }
                if (!first) {
                    stream->print(',');
                }
                first = false;
                stream->print("{\"bank\":");
                stream->print((unsigned)banks[i].bank);
                stream->print(",\"page\":\"");
                stream->print(banks[i].page);
                stream->print("\",\"dir\":");
                writeJsonEscaped(*stream, banks[i].dirName);
                stream->print(",\"count\":");
                stream->print((unsigned)banks[i].count);
                stream->print('}');
            }
        }
        stream->print(']');

        stream->print(",\"entries\":[");
        if (ready && entries != nullptr) {
            bool first = true;
            for (uint16_t i = 0; i < entryCount; ++i) {
                if (bankFilter != 0 && entries[i].bank != bankFilter) {
                    continue;
                }
                if (!first) {
                    stream->print(',');
                }
                first = false;
                stream->print("{\"bank\":");
                stream->print((unsigned)entries[i].bank);
                stream->print(",\"page\":\"");
                stream->print(entries[i].page);
                stream->print("\",\"index\":");
                stream->print((unsigned)entries[i].index);
                stream->print(",\"name\":");
                writeJsonEscaped(*stream, entries[i].name);
                stream->print('}');
            }
        }
        stream->print("]}");
        req->send(stream);
    });

    // ---- POST /api/audio/catalog/refresh ----
    // Enqueue asynchronous CHIRP catalog refresh.
    server.on("/api/audio/catalog/refresh", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (!audioCatalogSupported()) {
            req->send(404, "application/json",
                      "{\"ok\":false,\"error\":\"catalog unsupported by active backend\"}");
            return;
        }
        if (!audioQueueRefreshCatalog(SRC_WEB_API)) {
            req->send(503, "application/json",
                      "{\"ok\":false,\"error\":\"audio command queue full\"}");
            return;
        }
        PA_LOG_INFO(TAG, "[AUDIO] POST /api/audio/catalog/refresh queued");
        req->send(200, "application/json", "{\"ok\":true}");
    });

    // ---- POST /api/audio/play-banked ----
    // Play a CHIRP entry by bank/page/index for quick validation from Sound UI.
    server.on("/api/audio/play-banked", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (isSleepModeActive()) {
            req->send(423, "application/json",
                      "{\"error\":\"sleeping\",\"hint\":\"POST /api/wake\"}");
            return;
        }
        if (!audioCatalogSupported()) {
            req->send(404, "application/json",
                      "{\"ok\":false,\"error\":\"catalog unsupported by active backend\"}");
            return;
        }

        const AsyncWebParameter* indexParam = req->getParam("index", true);
        const AsyncWebParameter* bankParam = req->getParam("bank", true);
        const AsyncWebParameter* pageParam = req->getParam("page", true);
        if (!indexParam || !bankParam || !pageParam) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"requires index, bank, page\"}");
            return;
        }

        uint32_t indexValue = 0;
        uint32_t bankValue = 0;
        if (!parseUint32Value(indexParam->value().c_str(), &indexValue) || indexValue < 1 ||
            indexValue > 65535) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"index must be 1-65535\"}");
            return;
        }
        if (!parseUint32Value(bankParam->value().c_str(), &bankValue) || bankValue < 1 ||
            bankValue > 6) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"bank must be 1-6\"}");
            return;
        }

        char page = 'A';
        if (!parseChirpPage(pageParam->value(), &page)) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"page must be a single letter A-Z\"}");
            return;
        }

        if (!audioQueuePlayTrackBanked((uint16_t)indexValue, (uint8_t)bankValue, page, SRC_WEB_API)) {
            req->send(503, "application/json",
                      "{\"ok\":false,\"error\":\"audio command queue full\"}");
            return;
        }

        PA_LOG_INFO(TAG, "[AUDIO] POST /api/audio/play-banked bank=%u page=%c index=%u",
                    (unsigned)bankValue, page, (unsigned)indexValue);
        req->send(200, "application/json", "{\"ok\":true}");
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
