// =============================================================================
// src/web/api_audio.cpp
//
// Audio REST API - see include/api_audio.h for the route list.
//
// Written against the project-owned WebRequest seam (ADR 0021) and bound by the
// seam route table. No vendor request type is named here.
//
// The three write paths keep their decision logic in the ADR 0011 apply cores
// (audioTracksApply, audioCategoryRangeApply, audioMoodMapApply), which are
// reused unchanged: this file reads their result, persists it, and answers.
// The canonical field schema stays in the ADR 0013 audio config map behind its
// ConfigReader seam, reached through configAudioSetTrackByKey/configSaveAudio.
//
// The two large read payloads - the track assignments and the CHIRP catalog -
// are produced slice by slice through WebRequest::sendChunked() rather than
// assembled whole. The catalog alone can carry 300 entries, and this is the
// heaviest read surface the dashboard has. Chunked bodies are driven by a plain
// function pointer with no capture, so what each producer needs is snapshotted
// into file-scope state before the send starts (the pattern api_dome.cpp
// documents: handlers serialize on one server task under both backends, so
// exactly one of these reads can be in flight).
//
// POST /api/audio params:
//   action=play   &track=N      - play track N (1-based)
//   action=stop                 - stop playback
//   action=volume &level=N      - set absolute volume (0-30)
//   action=dollar &cmd=$R       - raw $ command (any from the $ command set)
//
// POST /api/audio/tracks params:
//   key=<name>   &track=N       - set named/category/system track (1-999, or 0-999 where allowed)
//   key=rand_min &track=N       - set random pool minimum
//   key=rand_max &track=N       - set random pool maximum
//
// Valid key names: scream faint leia cantina_s sw_theme imp_march cantina_l
//                  startup doodoo failure disco mahna inlove macho gangnam
//                  uptown celebr stayin harlem pbjtime
//                  sys_boot sys_mode_n sys_mode_s sys_mode_t sys_drv_on sys_dome_on sys_net_down
//                  snd_cat_*_lo snd_cat_*_hi, rand_min rand_max
// =============================================================================

#include "api_audio.h"

#include <Arduino.h>
#include <Preferences.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "api_audio_category_range_apply.h"
#include "api_audio_mood_map_apply.h"
#include "config_settings.h"  // the volume Setting's check
#include "api_audio_tracks_apply.h"
#include "api_helpers.h"
#include "api_json_response.h"
#include "audio_catalog_gate.h"
#include "audio_sound_member.h"
#include "audio_task.h"
#include "chirp_binding_keys.h"
#include "config.h"
#include "config_store.h"
#include "config_cache.h"
#include "config_write_lock.h"  // the four audio Write Windows live here
#include "logging.h"
#include "mood.h"
#include "mood_sound_mapping.h"
#include "robot_state.h"
#include "web_json_slice_writer.h"
#include "web_param_source.h"
#include "web_request_scratch.h"
#include "web_server.h"

static const char* TAG = "WebServer";

namespace {

constexpr size_t kChirpBindingCount =
    sizeof(CHIRP_BINDING_KEYS) / sizeof(CHIRP_BINDING_KEYS[0]);
constexpr size_t kChirpCategoryBindingCount =
    sizeof(CHIRP_CATEGORY_BINDING_KEYS) / sizeof(CHIRP_CATEGORY_BINDING_KEYS[0]);

bool isSleepModeActive() {
    taskENTER_CRITICAL(&robotStateMux);
    bool sleeping = robotState.sleepMode;
    taskEXIT_CRITICAL(&robotStateMux);
    return sleeping;
}

bool audioCatalogSupported() {
    return (audioGetCapabilities() & AudioDriver::AUDIO_CAP_CATALOG) != 0;
}

// With audio output off at boot nothing drains the audio queue, so a play or
// sound command is refused with the reason rather than answered "ok" onto a
// queue nobody reads (#370, include/audio_sound_member.h). Checked last, just
// before the enqueue, so a malformed request still gets its own 400.
bool refusedWhileSoundOff(WebRequest& req) {
    if (audioSoundOn()) {
        return false;
    }
    webSendJsonError(req, 409, AUDIO_SOUND_OFF_REASON);
    return true;
}

uint32_t packChirpBinding(uint16_t index, uint8_t bank, char page) {
    const uint8_t pageByte = (uint8_t)toupper((unsigned char)page);
    return ((uint32_t)bank << 24) | ((uint32_t)pageByte << 16) | (uint32_t)index;
}

bool unpackChirpBinding(uint32_t packed, uint8_t* bankOut, char* pageOut, uint16_t* indexOut) {
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

// The builder just saved an assignment against the card as it reads today, so
// today's sound list becomes the baseline the change warning is measured from,
// and whatever the warning was about is now the state they chose.
//
// Nothing observed means nothing to baseline: the previous baseline and any
// warning already up both stand, because a manifest that could not be read is
// not evidence that the card is unchanged (#397 D4). Returns false in that
// case and on a failed write; neither fails the save, which is the builder's
// assignment and landed either way.
bool saveSoundListBaseline(Preferences& prefs) {
    AudioCatalogObservation observation{};
    audioCatalogObservationRead(&observation);
    if (!observation.identity.observed) {
        return false;
    }
    if (prefs.putUInt(CHIRP_SOUND_LIST_CHECKSUM_KEY, observation.identity.checksum) == 0) {
        return false;
    }
    audioBindingWarningClear();
    return true;
}

uint32_t packChirpCategoryBinding(uint8_t bank, char page) {
    const uint8_t pageByte = (uint8_t)toupper((unsigned char)page);
    return ((uint32_t)bank << 8) | (uint32_t)pageByte;
}

bool unpackChirpCategoryBinding(uint32_t packed, uint8_t* bankOut, char* pageOut) {
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

bool parseChirpPage(const char* raw, char* pageOut) {
    if (raw == nullptr || pageOut == nullptr || strlen(raw) != 1) {
        return false;
    }
    char page = (char)toupper((unsigned char)raw[0]);
    if (page < 'A' || page > 'Z') {
        return false;
    }
    *pageOut = page;
    return true;
}

// Answer an apply core's rejection in the shape every write path in this file
// shares: 400, or 404 when the core reports the fitted module has no catalog
// to bind into. The three cores carry their own error structs, so the fields
// that matter are passed rather than the struct; the refusal's field, reason
// and accepts ride beside the sentence (webSendApplyRefusal()).
void sendApplyError(WebRequest& req, const char* message, const ApplyRefusal& refusal,
                    bool notFound) {
    webSendApplyRefusal(req, notFound ? 404 : 400, message, refusal);
}

// -----------------------------------------------------------------------------
// GET /api/audio/tracks producer state.
//
// Snapshotted by the handler before the send: the audio config as one struct
// (~124 bytes) and the CHIRP bindings as the packed NVS words they are stored
// as (~152 bytes), so the producer re-walks RAM rather than re-opening NVS on
// every chunk. Small on purpose - permanent BSS is the scarcest budget on this
// target (include/api_json_response.h records what overran it).
// -----------------------------------------------------------------------------
AudioConfig s_tracksAudio = {};
bool s_tracksIncludeBindings = false;
uint32_t s_tracksBindings[kChirpBindingCount] = {};
uint32_t s_tracksCategoryBindings[kChirpCategoryBindingCount] = {};

struct AudioTrackField {
    const char* name;
    uint16_t value;
};

size_t fillTracksResponse(uint8_t* out, size_t capacity, size_t offset) {
    JsonSliceWriter writer(out, capacity, offset);
    const AudioConfig& a = s_tracksAudio;

    // Field order and spelling are the payload contract data/sound.js reads and
    // Backup and Restore (data/maintenance.js) carries. Note snd_cat_snrk_* : the wire name is the short form
    // even though the config member is snd_cat_snarky_*.
    const AudioTrackField fields[] = {
        {"scream", a.snd_scream},
        {"faint", a.snd_faint},
        {"leia", a.snd_leia},
        {"cantina_s", a.snd_cantina_s},
        {"sw_theme", a.snd_sw_theme},
        {"imp_march", a.snd_imp_march},
        {"cantina_l", a.snd_cantina_l},
        {"startup", a.snd_startup},
        {"doodoo", a.snd_doodoo},
        {"failure", a.snd_failure},
        {"disco", a.snd_disco},
        {"mahna", a.snd_mahna},
        {"inlove", a.snd_inlove},
        {"macho", a.snd_macho},
        {"gangnam", a.snd_gangnam},
        {"uptown", a.snd_uptown},
        {"celebr", a.snd_celebr},
        {"stayin", a.snd_stayin},
        {"harlem", a.snd_harlem},
        {"pbjtime", a.snd_pbjtime},
        {"sys_boot", a.snd_sys_boot},
        {"sys_mode_n", a.snd_sys_mode_n},
        {"sys_mode_s", a.snd_sys_mode_s},
        {"sys_mode_t", a.snd_sys_mode_t},
        {"sys_drv_on", a.snd_sys_drv_on},
        {"sys_dome_on", a.snd_sys_dome_on},
        {"sys_net_down", a.snd_sys_net_down},
        {"snd_cat_gen_lo", a.snd_cat_gen_lo},
        {"snd_cat_gen_hi", a.snd_cat_gen_hi},
        {"snd_cat_chat_lo", a.snd_cat_chat_lo},
        {"snd_cat_chat_hi", a.snd_cat_chat_hi},
        {"snd_cat_hap_lo", a.snd_cat_hap_lo},
        {"snd_cat_hap_hi", a.snd_cat_hap_hi},
        {"snd_cat_proc_lo", a.snd_cat_proc_lo},
        {"snd_cat_proc_hi", a.snd_cat_proc_hi},
        {"snd_cat_sad_lo", a.snd_cat_sad_lo},
        {"snd_cat_sad_hi", a.snd_cat_sad_hi},
        {"snd_cat_sent_lo", a.snd_cat_sent_lo},
        {"snd_cat_sent_hi", a.snd_cat_sent_hi},
        {"snd_cat_hum_lo", a.snd_cat_hum_lo},
        {"snd_cat_hum_hi", a.snd_cat_hum_hi},
        {"snd_cat_scrm_lo", a.snd_cat_scrm_lo},
        {"snd_cat_scrm_hi", a.snd_cat_scrm_hi},
        {"snd_cat_ooh_lo", a.snd_cat_ooh_lo},
        {"snd_cat_ooh_hi", a.snd_cat_ooh_hi},
        {"snd_cat_alrm_lo", a.snd_cat_alrm_lo},
        {"snd_cat_alrm_hi", a.snd_cat_alrm_hi},
        {"snd_cat_snrk_lo", a.snd_cat_snarky_lo},
        {"snd_cat_snrk_hi", a.snd_cat_snarky_hi},
        {"snd_cat_whis_lo", a.snd_cat_whis_lo},
        {"snd_cat_whis_hi", a.snd_cat_whis_hi},
        {"rand_min", a.snd_rand_min},
        {"rand_max", a.snd_rand_max},
        {"volume", a.audioVolume},
        {"snd_int_quiet", a.snd_int_quiet},
        {"snd_int_mid", a.snd_int_mid},
        {"snd_int_full", a.snd_int_full},
        {"snd_int_awake", a.snd_int_awake},
    };

    writer.append('{');
    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) {
        if (i > 0) {
            writer.append(',');
        }
        writer.appendJsonString(fields[i].name);
        writer.append(':');
        writer.appendUint(fields[i].value);
    }

    if (s_tracksIncludeBindings) {
        writer.append(",\"chirp_bindings\":{");
        bool first = true;
        for (size_t i = 0; i < kChirpBindingCount; ++i) {
            uint8_t bank = 0;
            char page = 'A';
            uint16_t index = 0;
            if (!unpackChirpBinding(s_tracksBindings[i], &bank, &page, &index)) {
                continue;
            }
            if (!first) {
                writer.append(',');
            }
            first = false;
            writer.appendJsonString(CHIRP_BINDING_KEYS[i].key);
            writer.append(":{\"bank\":");
            writer.appendUint(bank);
            writer.append(",\"page\":\"");
            writer.append(page);
            writer.append("\",\"index\":");
            writer.appendUint(index);
            writer.append('}');
        }

        writer.append("},\"chirp_category_bindings\":{");
        first = true;
        for (size_t i = 0; i < kChirpCategoryBindingCount; ++i) {
            uint8_t bank = 0;
            char page = 'A';
            if (!unpackChirpCategoryBinding(s_tracksCategoryBindings[i], &bank, &page)) {
                continue;
            }
            if (!first) {
                writer.append(',');
            }
            first = false;
            writer.appendJsonString(CHIRP_CATEGORY_BINDING_KEYS[i].loKey);
            writer.append(":{\"bank\":");
            writer.appendUint(bank);
            writer.append(",\"page\":\"");
            writer.append(page);
            writer.append("\"}");
        }
        writer.append('}');
    }

    writer.append('}');
    return writer.written();
}

// -----------------------------------------------------------------------------
// GET /api/audio/catalog producer state.
//
// The bank/entry arrays are borrowed from the driver's cache and re-walked once
// per HTTP chunk while the body goes out, so their lifetime has to outlast the
// send. It is the catalog gate that makes that true (include/audio_catalog_gate.h):
// the handler holds a reader lease across sendChunked(), and a refresh cannot
// replace the allocation while one is out. The lease is a counter taken and
// released under a short critical section of its own -- no lock is held while
// the body is on the wire.
// -----------------------------------------------------------------------------
uint8_t s_catalogBankFilter = 0;
bool s_catalogReady = false;
bool s_catalogBusy = false;
const AudioCatalogBank* s_catalogBanks = nullptr;
uint8_t s_catalogBankCount = 0;
const AudioCatalogEntry* s_catalogEntries = nullptr;
uint16_t s_catalogEntryCount = 0;
AudioCatalogObservation s_catalogObservation{};
AudioCatalogRefreshLedger s_catalogLedger{};
AudioBindingWarning s_catalogBindingWarning{};

const char* catalogRefreshStateToken(AudioCatalogRefreshState state) {
    switch (state) {
        case AudioCatalogRefreshState::Queued:      return "queued";
        case AudioCatalogRefreshState::Running:     return "running";
        case AudioCatalogRefreshState::Completed:   return "completed";
        case AudioCatalogRefreshState::Blocked:     return "blocked";
        case AudioCatalogRefreshState::Failed:      return "failed";
        case AudioCatalogRefreshState::Interrupted: return "interrupted";
        case AudioCatalogRefreshState::None:
        default:                                    return "none";
    }
}

size_t fillCatalogResponse(uint8_t* out, size_t capacity, size_t offset) {
    JsonSliceWriter writer(out, capacity, offset);

    writer.append("{\"ready\":");
    writer.append(s_catalogReady ? "true" : "false");

    // Busy is not "no catalog": a reader refused while a refresh holds the gate
    // has learned nothing about the catalog, and a page that overwrote its rows
    // on this answer would blank a listing that is still perfectly good.
    writer.append(",\"busy\":");
    writer.append(s_catalogBusy ? "true" : "false");

    // What the last discovery could not see. A ready catalog is usable; these
    // say whether it is also whole.
    const AudioCatalogCompleteness& limits = s_catalogObservation.completeness;
    const bool complete = s_catalogReady && limits.manifestComplete &&
                          limits.missingNameCount == 0 && !limits.entryCapReached;
    writer.append(",\"complete\":");
    writer.append(complete ? "true" : "false");
    writer.append(",\"limits\":{\"manifest_incomplete\":");
    writer.append(limits.manifestComplete ? "false" : "true");
    writer.append(",\"missing_names\":");
    writer.appendUint(limits.missingNameCount);
    writer.append(",\"entry_cap_reached\":");
    writer.append(limits.entryCapReached ? "true" : "false");
    writer.append('}');

    // Which refresh the caller is watching, and how it ended. Queue acceptance
    // and refresh completion are different events (#397 work item 10).
    writer.append(",\"refresh\":{\"request\":");
    writer.appendUint(s_catalogLedger.requestId);
    writer.append(",\"active\":");
    writer.appendUint(s_catalogLedger.activeId);
    writer.append(",\"settled\":");
    writer.appendUint(s_catalogLedger.settledId);
    writer.append(",\"state\":");
    writer.appendJsonString(catalogRefreshStateToken(s_catalogLedger.settledState));
    writer.append('}');

    writer.append(",\"bindings\":{\"sound_list_changed\":");
    writer.append(s_catalogBindingWarning.soundListChanged ? "true" : "false");
    writer.append(",\"sound_list_checked\":");
    writer.append(s_catalogBindingWarning.soundListChecked ? "true" : "false");
    writer.append('}');

    writer.append(",\"banks\":[");
    if (s_catalogReady && s_catalogBanks != nullptr) {
        bool first = true;
        for (uint8_t i = 0; i < s_catalogBankCount; ++i) {
            if (s_catalogBankFilter != 0 && s_catalogBanks[i].bank != s_catalogBankFilter) {
                continue;
            }
            if (!first) {
                writer.append(',');
            }
            first = false;
            writer.append("{\"bank\":");
            writer.appendUint(s_catalogBanks[i].bank);
            writer.append(",\"page\":\"");
            writer.append(s_catalogBanks[i].page);
            writer.append("\",\"dir\":");
            writer.appendJsonString(s_catalogBanks[i].dirName);
            writer.append(",\"count\":");
            writer.appendUint(s_catalogBanks[i].count);
            writer.append('}');
        }
    }
    writer.append(']');

    writer.append(",\"entries\":[");
    if (s_catalogReady && s_catalogEntries != nullptr) {
        bool first = true;
        for (uint16_t i = 0; i < s_catalogEntryCount; ++i) {
            if (s_catalogBankFilter != 0 && s_catalogEntries[i].bank != s_catalogBankFilter) {
                continue;
            }
            if (!first) {
                writer.append(',');
            }
            first = false;
            writer.append("{\"bank\":");
            writer.appendUint(s_catalogEntries[i].bank);
            writer.append(",\"page\":\"");
            writer.append(s_catalogEntries[i].page);
            writer.append("\",\"index\":");
            writer.appendUint(s_catalogEntries[i].index);
            writer.append(",\"name\":");
            writer.appendJsonString(s_catalogEntries[i].name);
            writer.append('}');
        }
    }
    writer.append("]}");
    return writer.written();
}

}  // namespace

// -----------------------------------------------------------------------------
// Mood category mask map
// -----------------------------------------------------------------------------

void handleAudioMoodMapGet(WebRequest& req) {
    MoodCategoryMaskConfig masks{};
    ConfigSnapshot snap = {};
    configCacheRead(&snap);
    masks.quiet = (uint16_t)(snap.audio.snd_moodcat_quiet & MOOD_CATEGORY_MASK_MAX);
    masks.mid = (uint16_t)(snap.audio.snd_moodcat_mid & MOOD_CATEGORY_MASK_MAX);
    masks.full = (uint16_t)(snap.audio.snd_moodcat_full & MOOD_CATEGORY_MASK_MAX);
    masks.awakeplus = (uint16_t)(snap.audio.snd_moodcat_awakeplus & MOOD_CATEGORY_MASK_MAX);

    char body[128];
    size_t n = formatMoodCategoryMapJson(body, sizeof(body), masks);
    if (n >= sizeof(body)) {
        webSendJsonError(req, 500, "mood-map response overflow");
        return;
    }
    req.send(200, "application/json", body);
}

// See include/api_audio.h for the full contract.
AudioMoodMapCommitOutcome audioMoodMapCommitApplied(const AudioMoodMapApplyResult& result) {
    AudioMoodMapCommitOutcome outcome;

    Preferences prefs;
    if (prefs.begin(NVS_NAMESPACE, false)) {
        outcome.ok = configUpdateAudioMoodMasks(prefs, result.quiet, result.mid, result.full,
                                                 result.awakeplus);
        prefs.end();
    }

    if (outcome.ok) {
        PA_LOG_INFO(TAG, "[AUDIO] POST /api/audio/mood-map q=%u m=%u f=%u a=%u",
                    (unsigned)result.quiet, (unsigned)result.mid, (unsigned)result.full,
                    (unsigned)result.awakeplus);
    }
    return outcome;
}

void handleAudioMoodMapPost(WebRequest& req) {
    ConfigParamSource params = webParamSource(req);

    // In the web request scratch rather than a static of its own (#428).
    WebRequestScratch<AudioMoodMapApplyResult> scratch;
    if (!scratch) {
        webSendJsonError(req, 500, "request scratch unavailable");
        return;
    }
    AudioMoodMapApplyResult& result = *scratch;
    audioMoodMapApply(params, &result);
    if (result.error.hasError) {
        // This core has no not-found case: an unknown field is a bad request.
        sendApplyError(req, result.error.message, result.error.refusal, false);
        return;
    }

    AudioMoodMapCommitOutcome commit;
    if (!audioMoodMapWriteWindow(result, &commit)) {
        webSendJsonError(req, 503, "config write busy");
        return;
    }
    if (!commit.ok) {
        webSendJsonError(req, 500, "NVS write failed");
        return;
    }

    req.send(200, "application/json", "{\"ok\":true}");
}

// -----------------------------------------------------------------------------
// Named track assignments
// -----------------------------------------------------------------------------

void handleAudioTracksGet(WebRequest& req) {
    ConfigSnapshot snap;
    configCacheRead(&snap);
    s_tracksAudio = snap.audio;

    // CHIRP bindings only exist on a catalog-capable backend, and only that
    // backend's payload carries them - matching what the async handler emitted.
    s_tracksIncludeBindings = audioCatalogSupported();
    memset(s_tracksBindings, 0, sizeof(s_tracksBindings));
    memset(s_tracksCategoryBindings, 0, sizeof(s_tracksCategoryBindings));
    if (s_tracksIncludeBindings) {
        Preferences prefs;
        // Read every packed word up front. The producer runs once per chunk, so
        // reading NVS from inside it would re-open and re-scan the namespace for
        // every kilobyte of body. An unavailable namespace leaves the words at
        // zero, which serializes as the two empty objects the async handler
        // emitted on the same failure.
        if (prefs.begin(NVS_NAMESPACE, true)) {
            for (size_t i = 0; i < kChirpBindingCount; ++i) {
                s_tracksBindings[i] = prefs.getUInt(CHIRP_BINDING_KEYS[i].nvsKey, 0);
            }
            for (size_t i = 0; i < kChirpCategoryBindingCount; ++i) {
                s_tracksCategoryBindings[i] =
                    prefs.getUInt(CHIRP_CATEGORY_BINDING_KEYS[i].nvsKey, 0);
            }
            prefs.end();
        }
    }

    if (!req.sendChunked("application/json", fillTracksResponse)) {
        webSendJsonError(req, 500, "response stream alloc failed");
        return;
    }
    PA_LOG_DEBUG(TAG, "[AUDIO] GET /api/audio/tracks bindings=%s",
                 s_tracksIncludeBindings ? "true" : "false");
}

// See include/api_audio.h for the full contract. `snap` must already carry
// audioTracksApply()'s in-place mutation (the shell reads the cache, calls
// the core, then this). audioQueueRefreshBindings() keeps SRC_WEB_API
// hardcoded, matching today's sole REST caller - the day a second caller
// (e.g. the Console) reaches this, that becomes a `source` parameter, not
// invented speculatively here.
AudioTracksCommitOutcome audioTracksCommitApplied(ConfigSnapshot* snap,
                                                   const AudioTracksApplyResult& result) {
    AudioTracksCommitOutcome outcome;

    const char* key = result.key;
    const uint16_t t = result.track;
    const uint16_t oldTrack = result.oldTrack;
    const bool useBanked = result.useBanked;
    const uint8_t bank = result.bank;
    const char page = result.page;
    const char* chirpBindingKey =
        result.chirpBindingKey[0] != '\0' ? result.chirpBindingKey : nullptr;

    configCacheApply(*snap);

    Preferences prefs;
    bool ok = false;
    if (prefs.begin(NVS_NAMESPACE, false)) {
        bool wroteTrack = configSaveAudio(prefs, snap->audio);
        bool wroteChirp = true;

        if (wroteTrack && chirpBindingKey != nullptr) {
            uint32_t chirpPacked = useBanked ? packChirpBinding(t, bank, page) : 0;
            wroteChirp = prefs.putUInt(chirpBindingKey, chirpPacked) > 0;
            if (wroteChirp && !saveSoundListBaseline(prefs)) {
                PA_LOG_DEBUG(TAG,
                             "[AUDIO] sound-list baseline not saved (nothing observed, or NVS refused)");
            }
        }

        if (wroteTrack && !wroteChirp) {
            // CHIRP write failed: restore old value and re-save to roll back NVS.
            ConfigSnapshot oldSnap;
            configCacheRead(&oldSnap);
            configAudioSetTrackByKey(&oldSnap.audio, key, oldTrack);
            configCacheApply(oldSnap);
            configCacheRead(&oldSnap);
            bool rollbackOk = configSaveAudio(prefs, oldSnap.audio);
            if (!rollbackOk) {
                PA_LOG_ERROR(TAG, "[AUDIO] rollback save failed after binding write error");
            }
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
        PA_LOG_INFO(TAG, "[AUDIO] POST /api/audio/tracks key=%s index=%u bank=%u page=%c", key,
                    (unsigned)t, (unsigned)bank, page);
    } else {
        PA_LOG_INFO(TAG, "[AUDIO] POST /api/audio/tracks key=%s track=%u", key, (unsigned)t);
    }

    if (ok && chirpBindingKey != nullptr && !audioQueueRefreshBindings(SRC_WEB_API)) {
        PA_LOG_WARN(TAG, "[AUDIO] binding cache refresh enqueue failed (queue full)");
    }

    outcome.ok = ok;
    return outcome;
}

void handleAudioTracksPost(WebRequest& req) {
    ConfigParamSource params = webParamSource(req);

    // In the web request scratch rather than a static of its own (#428).
    WebRequestScratch<AudioTracksApplyResult> scratch;
    if (!scratch) {
        webSendJsonError(req, 500, "request scratch unavailable");
        return;
    }
    AudioTracksApplyResult& result = *scratch;
    ConfigSnapshot snap;
    AudioTracksCommitOutcome commit;
    if (!audioTracksWriteWindow(params, audioCatalogSupported(), &snap, &result, &commit)) {
        webSendJsonError(req, 503, "config write busy");
        return;
    }
    if (result.error.hasError) {
        sendApplyError(req, result.error.message, result.error.refusal,
                       result.error.notFound);
        return;
    }
    if (!commit.ok) {
        webSendJsonError(req, 500, "NVS write failed");
        return;
    }

    req.send(200, "application/json", "{\"ok\":true}");
}

// See include/api_audio.h for the full contract. Same SRC_WEB_API note as
// audioTracksCommitApplied() above applies to the binding-refresh enqueue.
AudioCategoryRangeCommitOutcome audioCategoryRangeCommitApplied(
    ConfigSnapshot* snap, const AudioCategoryRangeApplyResult& result) {
    AudioCategoryRangeCommitOutcome outcome;

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

    configCacheApply(*snap);

    Preferences prefs;
    bool ok = false;
    if (prefs.begin(NVS_NAMESPACE, false)) {
        bool wroteConfig = configSaveAudio(prefs, snap->audio);
        bool wroteBinding = true;
        if (wroteConfig && hasBankedParams) {
            uint32_t packedBinding = packChirpCategoryBinding(categoryBank, categoryPage);
            wroteBinding = prefs.putUInt(categoryNvsKey, packedBinding) > 0;
        } else if (wroteConfig && clearBinding) {
            wroteBinding = prefs.putUInt(categoryNvsKey, 0) > 0;
        }
        if (wroteConfig && wroteBinding && (hasBankedParams || clearBinding) &&
            !saveSoundListBaseline(prefs)) {
            PA_LOG_DEBUG(TAG,
                         "[AUDIO] sound-list baseline not saved (nothing observed, or NVS refused)");
        }
        if (wroteConfig && !wroteBinding) {
            // CHIRP write failed: restore robotState and re-save old config.
            ConfigSnapshot oldSnap;
            configCacheRead(&oldSnap);
            configAudioSetTrackByKey(&oldSnap.audio, loKey, oldLo);
            configAudioSetTrackByKey(&oldSnap.audio, hiKey, oldHi);
            configCacheApply(oldSnap);
            configCacheRead(&oldSnap);
            bool rollbackOk = configSaveAudio(prefs, oldSnap.audio);
            if (!rollbackOk) {
                PA_LOG_ERROR(TAG, "[AUDIO] rollback save failed after category binding write error");
            }
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
        outcome.ok = false;
        return outcome;
    }

    if (hasBankedParams) {
        PA_LOG_INFO(TAG, "[AUDIO] POST /api/audio/category-range %s=%u %s=%u bank=%u page=%c",
                    loKey, (unsigned)loValue, hiKey, (unsigned)hiValue, (unsigned)categoryBank,
                    categoryPage);
    } else if (clearBinding) {
        PA_LOG_INFO(TAG,
                    "[AUDIO] POST /api/audio/category-range %s=%u %s=%u clear_binding=true", loKey,
                    (unsigned)loValue, hiKey, (unsigned)hiValue);
    } else {
        PA_LOG_INFO(TAG, "[AUDIO] POST /api/audio/category-range %s=%u %s=%u", loKey,
                    (unsigned)loValue, hiKey, (unsigned)hiValue);
    }
    if ((hasBankedParams || clearBinding) && !audioQueueRefreshBindings(SRC_WEB_API)) {
        PA_LOG_WARN(TAG, "[AUDIO] binding cache refresh enqueue failed (queue full)");
    }

    outcome.ok = true;
    return outcome;
}

// Atomic update for one category lo/hi pair to avoid partial two-request saves.
void handleAudioCategoryRangePost(WebRequest& req) {
    ConfigParamSource params = webParamSource(req);

    // In the web request scratch rather than a static of its own (#428).
    WebRequestScratch<AudioCategoryRangeApplyResult> scratch;
    if (!scratch) {
        webSendJsonError(req, 500, "request scratch unavailable");
        return;
    }
    AudioCategoryRangeApplyResult& result = *scratch;
    ConfigSnapshot snap;
    AudioCategoryRangeCommitOutcome commit;
    if (!audioCategoryRangeWriteWindow(params, audioCatalogSupported(), &snap, &result, &commit)) {
        webSendJsonError(req, 503, "config write busy");
        return;
    }
    if (result.error.hasError) {
        sendApplyError(req, result.error.message, result.error.refusal,
                       result.error.notFound);
        return;
    }
    if (!commit.ok) {
        webSendJsonError(req, 500, "NVS write failed");
        return;
    }

    req.send(200, "application/json", "{\"ok\":true}");
}

// -----------------------------------------------------------------------------
// Mood presets (dual-path: audio + dome TX)
// -----------------------------------------------------------------------------

void handleMoodPost(WebRequest& req) {
    if (isSleepModeActive()) {
        webSendJsonError(req, 423, "sleeping", "POST /api/wake");
        return;
    }

    // Wider than the longest valid value, so an over-long input reaches the
    // whitelist below as the invalid value it is rather than as a truncation
    // that happens to match (web_request.h).
    char moodRaw[16] = {};
    if (!req.param("mood", moodRaw, sizeof(moodRaw))) {
        webSendJsonError(req, 400, "missing mood parameter");
        return;
    }

    // Unparseable input lands on the same whitelist error a numerically invalid
    // mood gets, which is what this endpoint has always answered - the vendor's
    // toInt() read garbage as 0, and 0 is not a mood.
    uint32_t mood = 0;
    if (!parseUint32Value(moodRaw, &mood) || (mood != 10 && mood != 11 && mood != 13 &&
                                              mood != 14)) {
        webSendJsonError(req, 400, "mood must be 10, 11, 13, or 14");
        return;
    }

    applyMood((uint8_t)mood);
    requestStatusBroadcastNow();
    PA_LOG_INFO(TAG, "[MOOD] POST /api/mood mood=%d", (int)mood);
    req.send(200, "application/json", "{\"ok\":true}");
}

// See include/api_audio.h for the full contract.
AudioSetVolumeCommitOutcome audioSetVolumeCommitApplied(uint8_t level, CommandSource source) {
    AudioSetVolumeCommitOutcome outcome;
    if (!audioQueueSetVolume(level, source)) {
        return outcome;  // queued=false
    }
    outcome.queued = true;

    // Persist as the new default volume so it survives reboot.
    ConfigSnapshot snap = {};
    configCacheRead(&snap);
    snap.audio.audioVolume = level;
    configCacheApply(snap);
    outcome.saved = saveConfigToNvs();

    PA_LOG_INFO(TAG, "[AUDIO] volume level=%d saved=%s", (int)level,
                outcome.saved ? "true" : "false");
    return outcome;
}

// The four audio Write Windows. See include/api_audio.h for the contract.

bool audioTracksWriteWindow(const ConfigParamSource& params, bool catalogSupported,
                            ConfigSnapshot* working, AudioTracksApplyResult* result,
                            AudioTracksCommitOutcome* commit) {
    ConfigWriteLock lock;
    if (!lock.acquired()) {
        return false;
    }
    configCacheRead(working);
    audioTracksApply(params, catalogSupported, working, result);
    if (!result->error.hasError) {
        *commit = audioTracksCommitApplied(working, *result);
    }
    return true;
}

bool audioCategoryRangeWriteWindow(const ConfigParamSource& params, bool catalogSupported,
                                   ConfigSnapshot* working, AudioCategoryRangeApplyResult* result,
                                   AudioCategoryRangeCommitOutcome* commit) {
    ConfigWriteLock lock;
    if (!lock.acquired()) {
        return false;
    }
    configCacheRead(working);
    audioCategoryRangeApply(params, catalogSupported, working, result);
    if (!result->error.hasError) {
        *commit = audioCategoryRangeCommitApplied(working, *result);
    }
    return true;
}

bool audioMoodMapWriteWindow(const AudioMoodMapApplyResult& result,
                             AudioMoodMapCommitOutcome* commit) {
    ConfigWriteLock lock;
    if (!lock.acquired()) {
        return false;
    }
    *commit = audioMoodMapCommitApplied(result);
    return true;
}

bool audioSetVolumeWriteWindow(uint8_t level, CommandSource source,
                               AudioSetVolumeCommitOutcome* commit) {
    ConfigWriteLock lock;
    if (!lock.acquired()) {
        return false;
    }
    *commit = audioSetVolumeCommitApplied(level, source);
    return true;
}

// -----------------------------------------------------------------------------
// CHIRP catalog
// -----------------------------------------------------------------------------

// Enqueues AUDIO_CMD_QUERY_STATUS; AudioTask runs queryModuleState() and updates
// RobotState. The result is available via GET /api/audio after ~1.5 s
// (3 x 300 ms query timeout + queue latency).
void handleAudioQueryPost(WebRequest& req) {
    if (refusedWhileSoundOff(req)) {
        return;
    }
    if (!audioQueueQueryStatus(SRC_WEB_API)) {
        webSendJsonError(req, 503, "audio command queue full");
        return;
    }
    PA_LOG_INFO(TAG, "[AUDIO] POST /api/audio/query - status poll enqueued");
    req.send(200, "application/json", "{\"ok\":true}");
}

// Returns the cached CHIRP catalog from the most recent refresh.
void handleAudioCatalogGet(WebRequest& req) {
    if (!audioCatalogSupported()) {
        webSendJsonError(req, 404, "catalog unsupported by active backend");
        return;
    }

    uint8_t bankFilter = 0;
    char bankRaw[16] = {};
    if (req.param("bank", bankRaw, sizeof(bankRaw))) {
        uint32_t parsedBank = 0;
        if (!parseUint32Value(bankRaw, &parsedBank) || parsedBank < 1 || parsedBank > 6) {
            webSendJsonError(req, 400, "bank must be 1-6");
            return;
        }
        bankFilter = (uint8_t)parsedBank;
    }

    s_catalogBankFilter = bankFilter;
    s_catalogBankCount = 0;
    s_catalogEntryCount = 0;
    s_catalogBanks = nullptr;
    s_catalogEntries = nullptr;
    s_catalogReady = false;

    // The refresh ledger and the last observation are the gate's own state, not
    // the driver's storage, so they are readable whether or not this reader
    // gets in -- which is what lets a refused read still say "still running"
    // instead of looking like a catalog that vanished.
    audioCatalogObservationRead(&s_catalogObservation);
    audioCatalogRefreshLedgerRead(&s_catalogLedger);
    audioBindingWarningRead(&s_catalogBindingWarning);

    // Everything below borrows the driver's arrays and is walked once per chunk
    // as the body goes out, so the lease has to span the whole send.
    const bool leased = audioCatalogReaderAcquire();
    s_catalogBusy = !leased;
    if (leased) {
        s_catalogReady = audioIsCatalogReady();
        s_catalogBanks = audioGetCatalogBanks(&s_catalogBankCount);
        s_catalogEntries = audioGetCatalogEntries(&s_catalogEntryCount);
    }

    const bool sent = req.sendChunked("application/json", fillCatalogResponse);
    if (leased) {
        audioCatalogReaderRelease();
    }
    if (!sent) {
        webSendJsonError(req, 500, "response stream alloc failed");
        return;
    }
    PA_LOG_DEBUG(TAG, "[AUDIO] GET /api/audio/catalog ready=%s busy=%s entries=%u bank=%u",
                 s_catalogReady ? "true" : "false", s_catalogBusy ? "true" : "false",
                 (unsigned)s_catalogEntryCount, (unsigned)bankFilter);
}

// Accepting a refresh onto the audio command queue is not the same event as
// that refresh finishing, and this answer says only the first. The request
// number it returns is what GET /api/audio/catalog's "refresh" block is
// reporting on, so a caller can tell ITS refresh completing from an older
// catalog that merely happens to still be ready (#397 work item 10).
void handleAudioCatalogRefreshPost(WebRequest& req) {
    if (!audioCatalogSupported()) {
        webSendJsonError(req, 404, "catalog unsupported by active backend");
        return;
    }
    if (refusedWhileSoundOff(req)) {
        return;
    }
    const uint32_t requestId = audioCatalogRefreshRequested();
    if (!audioQueueRefreshCatalog(SRC_WEB_API)) {
        // Nothing will ever run this one, so settle it here rather than leaving
        // a caller polling for a completion that cannot arrive.
        audioCatalogRefreshSettled(requestId, AudioCatalogRefreshState::Blocked);
        webSendJsonError(req, 503, "audio command queue full");
        return;
    }
    char body[48];
    const int needed = snprintf(body, sizeof(body), "{\"ok\":true,\"request\":%lu}",
                                (unsigned long)requestId);
    if (needed < 0 || (size_t)needed >= sizeof(body)) {
        webSendJsonError(req, 500, "catalog refresh response overflow");
        return;
    }
    PA_LOG_INFO(TAG, "[AUDIO] POST /api/audio/catalog/refresh queued request=%lu",
                (unsigned long)requestId);
    req.send(200, "application/json", body);
}

// Play a CHIRP entry by bank/page/index for quick validation from Sound UI.
void handleAudioPlayBankedPost(WebRequest& req) {
    if (isSleepModeActive()) {
        webSendJsonError(req, 423, "sleeping", "POST /api/wake");
        return;
    }
    if (!audioCatalogSupported()) {
        webSendJsonError(req, 404, "catalog unsupported by active backend");
        return;
    }

    // All three buffers are wider than the longest valid value, so an over-long
    // input reaches its parser as an over-long string (web_request.h).
    char indexRaw[16] = {};
    char bankRaw[16] = {};
    char pageRaw[8] = {};
    if (!req.param("index", indexRaw, sizeof(indexRaw)) ||
        !req.param("bank", bankRaw, sizeof(bankRaw)) ||
        !req.param("page", pageRaw, sizeof(pageRaw))) {
        webSendJsonError(req, 400, "requires index, bank, page");
        return;
    }

    uint32_t indexValue = 0;
    uint32_t bankValue = 0;
    if (!parseUint32Value(indexRaw, &indexValue) || indexValue < 1 || indexValue > 65535) {
        webSendJsonError(req, 400, "index must be 1-65535");
        return;
    }
    if (!parseUint32Value(bankRaw, &bankValue) || bankValue < 1 || bankValue > 6) {
        webSendJsonError(req, 400, "bank must be 1-6");
        return;
    }

    char page = 'A';
    if (!parseChirpPage(pageRaw, &page)) {
        webSendJsonError(req, 400, "page must be a single letter A-Z");
        return;
    }

    if (refusedWhileSoundOff(req)) {
        return;
    }
    if (!audioQueuePlayTrackBanked((uint16_t)indexValue, (uint8_t)bankValue, page, SRC_WEB_API)) {
        webSendJsonError(req, 503, "audio command queue full");
        return;
    }

    PA_LOG_INFO(TAG, "[AUDIO] POST /api/audio/play-banked bank=%u page=%c index=%u",
                (unsigned)bankValue, page, (unsigned)indexValue);
    req.send(200, "application/json", "{\"ok\":true}");
}

// -----------------------------------------------------------------------------
// Module status and control
// -----------------------------------------------------------------------------

// Returns driver name, module link state, and last-known module status.
// Device and total-tracks are populated from begin()-time cached queries.
// Play state, current track, and link_ok are updated by status queries.
// Polling during playback may interfere with some module RX paths; see
// driver capabilities for safe-polling flag.
void handleAudioGet(WebRequest& req) {
    AudioStatusSnapshot snap = {};
    captureAudioStatusSnapshot(&snap);

    // With sound off this names the module the builder picked, never the
    // driver bound at boot (#370).
    const SoundStatusIdentity sound = audioSoundStatusIdentity();
    char body[AUDIO_STATUS_JSON_BUF_SIZE];
    const int needed = formatAudioStatusJson(
        body, sizeof(body), sound.driver, sound.on, sound.capabilities, snap.linkOk, snap.active,
        snap.playState,
        snap.device, snap.totalTracks, snap.currentTrack, snap.missingTrack,
        audioRxStatusToken(snap.rxStatus), audioRxStatusDetail(snap.rxStatus));
    // A truncated document is not an answer. The buffer above is sized for the
    // longest response every field can produce today, so this is the guard for
    // a field that grows later rather than an expected path: say so instead of
    // sending JSON that stops mid-string under HTTP 200.
    if (needed < 0 || (size_t)needed >= sizeof(body)) {
        PA_LOG_WARN(TAG, "[AUDIO] GET /api/audio needs %d bytes, buffer is %u",
                    needed, (unsigned)sizeof(body));
        webSendJsonError(req, 500, "audio status response overflow");
        return;
    }
    req.send(200, "application/json", body);
}

// The gather step behind handleAudioGet()'s /api/audio response, shared with
// the Console module's sound.status.current executor (ADR 0036) so both read
// RobotState through the same function.
void captureAudioStatusSnapshot(AudioStatusSnapshot* out) {
    if (out == nullptr) {
        return;
    }
    *out = AudioStatusSnapshot{};

    taskENTER_CRITICAL(&robotStateMux);
    out->linkOk = robotState.audio_module_link_ok;
    out->playState = robotState.audio_module_play_state;
    out->device = robotState.audio_module_device;
    out->totalTracks = robotState.audio_module_total_tracks;
    out->currentTrack = robotState.audio_module_current_track;
    out->missingTrack = robotState.audio_module_missing_track;
    out->active = robotState.audioActive;
    out->rxStatus = robotState.audio_module_rx_status;
    taskEXIT_CRITICAL(&robotStateMux);
}

void handleAudioPost(WebRequest& req) {
    // Wider than the longest valid action, so an over-long value reaches the
    // unknown-action answer below instead of truncating into a match.
    char action[16] = {};
    if (!req.param("action", action, sizeof(action))) {
        webSendJsonError(req, 400, "missing action parameter");
        return;
    }

    // ---- play ----
    if (strcmp(action, "play") == 0) {
        if (isSleepModeActive()) {
            webSendJsonError(req, 423, "sleeping", "POST /api/wake");
            return;
        }
        char trackRaw[16] = {};
        if (!req.param("track", trackRaw, sizeof(trackRaw))) {
            webSendJsonError(req, 400, "play requires track parameter");
            return;
        }
        uint32_t track = 0;
        if (!parseUint32Value(trackRaw, &track) || track < 1 || track > 65535) {
            webSendJsonError(req, 400, "track must be 1-65535");
            return;
        }
        if (refusedWhileSoundOff(req)) {
            return;
        }
        if (!audioQueuePlayTrack((uint16_t)track, SRC_WEB_API)) {
            webSendJsonError(req, 503, "audio command queue full");
            return;
        }
        PA_LOG_INFO(TAG, "[AUDIO] POST /api/audio play track=%d", (int)track);
        req.send(200, "application/json", "{\"ok\":true}");
        return;
    }

    // ---- stop ----
    if (strcmp(action, "stop") == 0) {
        if (refusedWhileSoundOff(req)) {
            return;
        }
        if (!audioQueueTrackStop(SRC_WEB_API)) {
            webSendJsonError(req, 503, "audio command queue full");
            return;
        }
        PA_LOG_INFO(TAG, "[AUDIO] POST /api/audio stop");
        req.send(200, "application/json", "{\"ok\":true}");
        return;
    }

    // ---- volume ----
    if (strcmp(action, "volume") == 0) {
        char levelRaw[16] = {};
        if (!req.param("level", levelRaw, sizeof(levelRaw))) {
            ApplyRefusal refusal;
            applyRefusalSet(&refusal, ApplyRefusalReason::MissingArgument, "volume");
            webSendApplyRefusal(req, 400, "volume requires level parameter", refusal);
            return;
        }
        // The volume Setting's own check (include/config_settings.h), refused
        // with its field, reason and range so the Sound page can word it.
        const ConfigSetting* volume = audioSettingByName("volume", SettingDoor::AudioVolume);
        int32_t level = 0;
        ApplyRefusal refusal;
        char sentence[CONFIG_SETTING_SENTENCE_MAX] = {};
        if (volume == nullptr ||
            !configSettingCheck(*volume, levelRaw, "volume", &level, &refusal, sentence,
                                sizeof(sentence))) {
            webSendApplyRefusal(req, 400, sentence, refusal);
            return;
        }

        // Commit Step (ADR 0036 criterion 1, include/api_audio.h): the same
        // apply-then-persist sequence the Console's sound.action.set-volume
        // executor now shares.
        if (refusedWhileSoundOff(req)) {
            return;
        }
        AudioSetVolumeCommitOutcome commit;
        if (!audioSetVolumeWriteWindow((uint8_t)level, SRC_WEB_API, &commit)) {
            webSendJsonError(req, 503, "config write busy");
            return;
        }
        if (!commit.queued) {
            webSendJsonError(req, 503, "audio command queue full");
            return;
        }
        if (!commit.saved) {
            webSendJsonError(req, 500, "volume applied but NVS save failed");
            return;
        }
        req.send(200, "application/json", "{\"ok\":true}");
        return;
    }

    // ---- dollar (raw $ command) ----
    if (strcmp(action, "dollar") == 0) {
        if (isSleepModeActive()) {
            webSendJsonError(req, 423, "sleeping", "POST /api/wake");
            return;
        }
        // Wider than the 9-character ceiling below, so an over-long command is
        // still over-long after the copy and gets the length answer rather than
        // being truncated into an accepted one.
        char cmd[24] = {};
        if (!req.param("cmd", cmd, sizeof(cmd)) || cmd[0] != '$') {
            webSendJsonError(req, 400, "dollar requires cmd starting with '$'");
            return;
        }
        // Limit cmd length to what audioCmdQueue dollar field can hold
        if (strlen(cmd) > 9) {
            webSendJsonError(req, 400, "cmd too long (max 9 chars)");
            return;
        }
        if (refusedWhileSoundOff(req)) {
            return;
        }
        if (!audioQueueDollar(cmd, SRC_WEB_API)) {
            webSendJsonError(req, 503, "audio command queue full");
            return;
        }
        PA_LOG_INFO(TAG, "[AUDIO] POST /api/audio dollar cmd=%s", cmd);
        req.send(200, "application/json", "{\"ok\":true}");
        return;
    }

    webSendJsonError(req, 400, "unknown action - use play/stop/volume/dollar");
}
