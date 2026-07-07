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

#include "api_audio_category_range_apply.h"
#include "api_audio_mood_map_apply.h"
#include "api_audio_tracks_apply.h"
#include "api_helpers.h"
#include "chirp_binding_keys.h"
#include "audio_dollar_parser.h"
#include "audio_task.h"
#include "config.h"
#include "config_store.h"
#include "logging.h"
#include "mood.h"
#include "mood_sound_mapping.h"
#include "robot_state.h"
#include "web_server.h"

static const char* TAG = "WebServer";

static bool isSleepModeActive() {
    taskENTER_CRITICAL(&robotStateMux);
    bool sleeping = robotState.sleepMode;
    taskEXIT_CRITICAL(&robotStateMux);
    return sleeping;
}

static bool audioCatalogSupported() {
    return (audioGetCapabilities() & AudioDriver::AUDIO_CAP_CATALOG) != 0;
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
        ConfigSnapshot snap = {};
        configCacheRead(&snap);
        masks.quiet = (uint16_t)(snap.audio.snd_moodcat_quiet & MOOD_CATEGORY_MASK_MAX);
        masks.mid = (uint16_t)(snap.audio.snd_moodcat_mid & MOOD_CATEGORY_MASK_MAX);
        masks.full = (uint16_t)(snap.audio.snd_moodcat_full & MOOD_CATEGORY_MASK_MAX);
        masks.awakeplus =
            (uint16_t)(snap.audio.snd_moodcat_awakeplus & MOOD_CATEGORY_MASK_MAX);

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
        ConfigParamSource params;
        params.ctx = req;
        params.get = [](void* ctx, const char* name) -> const char* {
            auto* r = static_cast<AsyncWebServerRequest*>(ctx);
            if (!r->hasParam(name, true)) {
                return nullptr;
            }
            return r->getParam(name, true)->value().c_str();
        };

        static AudioMoodMapApplyResult result;
        audioMoodMapApply(params, &result);
        if (result.error.hasError) {
            char err[192];
            snprintf(err, sizeof(err), "{\"ok\":false,\"error\":\"%s\"}", result.error.message);
            req->send(400, "application/json", err);
            return;
        }

        Preferences prefs;
        bool ok = false;
        if (prefs.begin(NVS_NAMESPACE, false)) {
            ok = configUpdateAudioMoodMasks(prefs, result.quiet, result.mid, result.full,
                                            result.awakeplus);
            prefs.end();
        }

        if (!ok) {
            req->send(500, "application/json", "{\"ok\":false,\"error\":\"NVS write failed\"}");
            return;
        }

        PA_LOG_INFO(TAG, "[AUDIO] POST /api/audio/mood-map q=%u m=%u f=%u a=%u",
                    (unsigned)result.quiet, (unsigned)result.mid, (unsigned)result.full,
                    (unsigned)result.awakeplus);
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
        ConfigSnapshot snap;
        configCacheRead(&snap);
        const AudioConfig& audio = snap.audio;
        scream = audio.snd_scream;
        faint = audio.snd_faint;
        leia = audio.snd_leia;
        cantinaS = audio.snd_cantina_s;
        swTheme = audio.snd_sw_theme;
        impMarch = audio.snd_imp_march;
        cantinaL = audio.snd_cantina_l;
        startup = audio.snd_startup;
        doodoo = audio.snd_doodoo;
        failure = audio.snd_failure;
        disco = audio.snd_disco;
        mahna = audio.snd_mahna;
        inlove = audio.snd_inlove;
        macho = audio.snd_macho;
        gangnam = audio.snd_gangnam;
        uptown = audio.snd_uptown;
        celebr = audio.snd_celebr;
        stayin = audio.snd_stayin;
        harlem = audio.snd_harlem;
        pbjtime = audio.snd_pbjtime;
        sysBoot = audio.snd_sys_boot;
        sysModeN = audio.snd_sys_mode_n;
        sysModeS = audio.snd_sys_mode_s;
        sysModeT = audio.snd_sys_mode_t;
        sysDrvOn = audio.snd_sys_drv_on;
        sysDomeOn = audio.snd_sys_dome_on;
        catGenLo = audio.snd_cat_gen_lo;
        catGenHi = audio.snd_cat_gen_hi;
        catChatLo = audio.snd_cat_chat_lo;
        catChatHi = audio.snd_cat_chat_hi;
        catHapLo = audio.snd_cat_hap_lo;
        catHapHi = audio.snd_cat_hap_hi;
        catProcLo = audio.snd_cat_proc_lo;
        catProcHi = audio.snd_cat_proc_hi;
        catSadLo = audio.snd_cat_sad_lo;
        catSadHi = audio.snd_cat_sad_hi;
        catSentLo = audio.snd_cat_sent_lo;
        catSentHi = audio.snd_cat_sent_hi;
        catHumLo = audio.snd_cat_hum_lo;
        catHumHi = audio.snd_cat_hum_hi;
        catScrmLo = audio.snd_cat_scrm_lo;
        catScrmHi = audio.snd_cat_scrm_hi;
        catOohLo = audio.snd_cat_ooh_lo;
        catOohHi = audio.snd_cat_ooh_hi;
        catAlrmLo = audio.snd_cat_alrm_lo;
        catAlrmHi = audio.snd_cat_alrm_hi;
        catSnarkyLo = audio.snd_cat_snarky_lo;
        catSnarkyHi = audio.snd_cat_snarky_hi;
        catWhisLo = audio.snd_cat_whis_lo;
        catWhisHi = audio.snd_cat_whis_hi;
        randMin = audio.snd_rand_min;
        randMax = audio.snd_rand_max;
        volume = audio.audioVolume;
        intQuiet = audio.snd_int_quiet;
        intMid = audio.snd_int_mid;
        intFull = audio.snd_int_full;
        intAwake = audio.snd_int_awake;

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
        ConfigParamSource params;
        params.ctx = req;
        params.get = [](void* ctx, const char* name) -> const char* {
            auto* r = static_cast<AsyncWebServerRequest*>(ctx);
            if (!r->hasParam(name, true)) {
                return nullptr;
            }
            return r->getParam(name, true)->value().c_str();
        };

        ConfigSnapshot snap;
        configCacheRead(&snap);

        static AudioCategoryRangeApplyResult result;
        audioCategoryRangeApply(params, audioCatalogSupported(), &snap, &result);
        if (result.error.hasError) {
            char err[192];
            snprintf(err, sizeof(err), "{\"ok\":false,\"error\":\"%s\"}", result.error.message);
            req->send(result.error.notFound ? 404 : 400, "application/json", err);
            return;
        }

        const char* loKey = result.loKey;
        const char* hiKey = result.hiKey;
        const uint16_t loValue = result.loValue;
        const uint16_t hiValue = result.hiValue;
        const uint16_t oldLo = result.oldLo;
        const uint16_t oldHi = result.oldHi;
        const bool hasBankedParams = result.hasBankedParams;
        const bool clearBinding = result.clearBinding;
        const uint8_t categoryBank = result.categoryBank;
        const char categoryPage = result.categoryPage;
        const char* categoryNvsKey = result.categoryNvsKey;

        configCacheApply(snap);

        Preferences prefs;
        bool ok = false;
        if (prefs.begin(NVS_NAMESPACE, false)) {
            bool wroteConfig = configSaveAudio(prefs, snap.audio);
            bool wroteBinding = true;
            if (wroteConfig && hasBankedParams) {
                uint32_t packedBinding = packChirpCategoryBinding(categoryBank, categoryPage);
                wroteBinding = prefs.putUInt(categoryNvsKey, packedBinding) > 0;
            } else if (wroteConfig && clearBinding) {
                wroteBinding = prefs.putUInt(categoryNvsKey, 0) > 0;
            }
            if (wroteConfig && !wroteBinding) {
                // CHIRP write failed: restore robotState and re-save old config.
                ConfigSnapshot oldSnap;
                configCacheRead(&oldSnap);
                configAudioSetTrackByKey(&oldSnap.audio, loKey, oldLo);
                configAudioSetTrackByKey(&oldSnap.audio, hiKey, oldHi);
                configCacheApply(oldSnap);
                configCacheRead(&oldSnap);
                configSaveAudio(prefs, oldSnap.audio);
            } else if (!wroteConfig) {
                ConfigSnapshot oldSnap;
                configCacheRead(&oldSnap);
                configAudioSetTrackByKey(&oldSnap.audio, loKey, oldLo);
                configAudioSetTrackByKey(&oldSnap.audio, hiKey, oldHi);
                configCacheApply(oldSnap);
            }
            ok = wroteConfig && wroteBinding;
            prefs.end();
        } else {
            ConfigSnapshot oldSnap;
            configCacheRead(&oldSnap);
            configAudioSetTrackByKey(&oldSnap.audio, loKey, oldLo);
            configAudioSetTrackByKey(&oldSnap.audio, hiKey, oldHi);
            configCacheApply(oldSnap);
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
        ConfigParamSource params;
        params.ctx = req;
        params.get = [](void* ctx, const char* name) -> const char* {
            auto* r = static_cast<AsyncWebServerRequest*>(ctx);
            if (!r->hasParam(name, true)) {
                return nullptr;
            }
            return r->getParam(name, true)->value().c_str();
        };

        ConfigSnapshot snap;
        configCacheRead(&snap);

        static AudioTracksApplyResult result;
        audioTracksApply(params, audioCatalogSupported(), &snap, &result);
        if (result.error.hasError) {
            char err[192];
            snprintf(err, sizeof(err), "{\"ok\":false,\"error\":\"%s\"}", result.error.message);
            req->send(result.error.notFound ? 404 : 400, "application/json", err);
            return;
        }

        const char* key = result.key;
        const uint16_t t = result.track;
        const uint16_t oldTrack = result.oldTrack;
        const bool useBanked = result.useBanked;
        const uint8_t bank = result.bank;
        const char page = result.page;
        const char* chirpBindingKey = result.chirpBindingKey[0] != '\0' ? result.chirpBindingKey : nullptr;

        configCacheApply(snap);

        Preferences prefs;
        bool ok = false;
        if (prefs.begin(NVS_NAMESPACE, false)) {
            bool wroteTrack = configSaveAudio(prefs, snap.audio);
            bool wroteChirp = true;

            if (wroteTrack && chirpBindingKey != nullptr) {
                uint32_t chirpPacked = useBanked ? packChirpBinding(t, bank, page) : 0;
                wroteChirp = prefs.putUInt(chirpBindingKey, chirpPacked) > 0;
            }

            if (wroteTrack && !wroteChirp) {
                // CHIRP write failed: restore old value and re-save to roll back NVS.
                ConfigSnapshot oldSnap;
                configCacheRead(&oldSnap);
                configAudioSetTrackByKey(&oldSnap.audio, key, oldTrack);
                configCacheApply(oldSnap);
                configCacheRead(&oldSnap);
                configSaveAudio(prefs, oldSnap.audio);
            }

            ok = wroteTrack && wroteChirp;
            prefs.end();
        }

        if (!ok) {
            // Ensure robotState reflects the rollback.
            ConfigSnapshot oldSnap;
            configCacheRead(&oldSnap);
            configAudioSetTrackByKey(&oldSnap.audio, key, oldTrack);
            configCacheApply(oldSnap);
        }

        if (useBanked) {
            PA_LOG_INFO(TAG,
                        "[AUDIO] POST /api/audio/tracks key=%s index=%u bank=%u page=%c", key,
                        (unsigned)t, (unsigned)bank, page);
        } else {
            PA_LOG_INFO(TAG, "[AUDIO] POST /api/audio/tracks key=%s track=%u", key, (unsigned)t);
        }

        if (ok) {
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
        requestStatusBroadcastNow();
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
        const AudioCatalogBank* banks = audioGetCatalogBanks(&bankCount);
        const AudioCatalogEntry* entries = audioGetCatalogEntries(&entryCount);

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
        AudioRxStatus rxStatus;
        taskENTER_CRITICAL(&robotStateMux);
        linkOk = robotState.audio_module_link_ok;
        playState = robotState.audio_module_play_state;
        device = robotState.audio_module_device;
        totalTracks = robotState.audio_module_total_tracks;
        currentTrack = robotState.audio_module_current_track;
        active = robotState.audioActive;
        rxStatus = robotState.audio_module_rx_status;
        taskEXIT_CRITICAL(&robotStateMux);

        uint8_t caps = audioGetCapabilities();
        char body[256];
        formatAudioStatusJson(body, sizeof(body), audioGetDriverName(), caps, linkOk, active,
                              playState, device, totalTracks, currentTrack,
                              audioRxStatusToken(rxStatus), audioRxStatusDetail(rxStatus));
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
            if (!audioQueueTrackStop(SRC_WEB_API)) {
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
            ConfigSnapshot snap = {};
            configCacheRead(&snap);
            snap.audio.audioVolume = (uint8_t)level;
            configCacheApply(snap);
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
