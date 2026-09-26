// =============================================================================
// src/web/api_audio_tracks_apply.cpp
//
// Apply Core for POST /api/audio/tracks (ADR 0011 audio wave). See
// api_audio_tracks_apply.h.
// =============================================================================

#include "api_audio_tracks_apply.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "api_helpers.h"
#include "chirp_binding_keys.h"
#include "config_settings.h"  // each audio Setting's check - a track, an interval, a category bound

namespace {

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

// The sentence, and what it says as data (#425): the reason is a parameter,
// so no error write can leave it unset.
void setError(AudioTracksApplyResult* result, const char* message, ApplyRefusalReason reason,
              const char* field, const char* accepts = nullptr) {
    result->error.hasError = true;
    snprintf(result->error.message, sizeof(result->error.message), "%s", message);
    applyRefusalSet(&result->error.refusal, reason, field, accepts);
}

void setRangeError(AudioTracksApplyResult* result, const char* message, const char* field, long lo,
                   long hi) {
    result->error.hasError = true;
    snprintf(result->error.message, sizeof(result->error.message), "%s", message);
    applyRefusalSetRange(&result->error.refusal, field, lo, hi);
}

// The fitted module has no catalog to bind into; the shell answers 404.
void setNotFoundError(AudioTracksApplyResult* result, const char* message, const char* field) {
    setError(result, message, ApplyRefusalReason::NotInThisBuild, field);
    result->error.notFound = true;
}

}  // namespace

void audioTracksApply(const ConfigParamSource& params, bool catalogSupported, ConfigSnapshot* working,
                       AudioTracksApplyResult* result) {
    *result = AudioTracksApplyResult{};

    const char* key = configParamGet(params, "key");
    const char* trackRaw = configParamGet(params, "track");
    if (key == nullptr || trackRaw == nullptr) {
        setError(result, "requires key and track parameters", ApplyRefusalReason::MissingArgument,
                 key == nullptr ? "key" : "track");
        return;
    }
    snprintf(result->key, sizeof(result->key), "%s", key);

    // The Setting this key names (include/config_settings.h): a sound action's
    // track, the random track range, a random-chatter interval or a category
    // bound. Its declaration holds what it takes, and its refusal names it.
    const ConfigSetting* setting = audioSettingByName(key, SettingDoor::AudioTracks);
    if (setting == nullptr) {
        setError(result, "unknown key", ApplyRefusalReason::OutOfRange, "key");
        return;
    }

    const char* bankRaw = configParamGet(params, "bank");
    const char* pageRaw = configParamGet(params, "page");
    const bool hasBankedParams = (bankRaw != nullptr) || (pageRaw != nullptr);

    uint8_t bank = 0;
    char page = 'A';
    bool useBanked = false;
    const char* chirpBindingKey = chirpBindingNvsKey(key);

    if (hasBankedParams) {
        if (!(bankRaw && pageRaw)) {
            setError(result, "bank and page must be provided together",
                     ApplyRefusalReason::MissingArgument, bankRaw == nullptr ? "bank" : "page");
            return;
        }
        if (!catalogSupported) {
            setNotFoundError(result, "catalog unsupported by active backend", "bank");
            return;
        }
        // An interval, the random range and a category bound have no catalog
        // form; a sound action's track does.
        if (chirpBindingKey == nullptr) {
            setError(result, "key does not support CHIRP binding", ApplyRefusalReason::OutOfRange,
                     "key");
            return;
        }

        uint32_t bankValue = 0;
        if (!parseUint32Value(bankRaw, &bankValue) || bankValue < 1 || bankValue > 6) {
            setRangeError(result, "bank must be 1-6", "bank", 1, 6);
            return;
        }
        if (!parseChirpPage(pageRaw, &page)) {
            setError(result, "page must be a single letter A-Z", ApplyRefusalReason::OutOfRange,
                     "page", "A..Z");
            return;
        }
        bank = (uint8_t)bankValue;
        useBanked = true;
    }

    uint16_t oldTrack = 0;
    configAudioGetTrackByKey(working->audio, key, &oldTrack);

    if (useBanked) {
        // The catalog form of the binding: an index into the fitted module's
        // catalog, which reaches past the 999 a plain track takes. It is the
        // binding's own rule, spanning bank, page and index, so it is checked
        // here rather than by the track's declaration.
        uint32_t index = 0;
        if (!parseUint32Value(trackRaw, &index) || index < 1U || index > 65535U) {
            setRangeError(result, "banked index must be 1-65535", key, 1, 65535);
            return;
        }
        configAudioSetTrackByKey(&working->audio, key, (uint16_t)index);
    } else if (!configSettingApply(*setting, trackRaw, working, &result->error.refusal,
                                   result->error.message, sizeof(result->error.message))) {
        result->error.hasError = true;
        return;
    }

    uint16_t track = 0;
    configAudioGetTrackByKey(working->audio, key, &track);
    result->track = track;
    result->oldTrack = oldTrack;
    result->useBanked = useBanked;
    result->bank = bank;
    result->page = page;
    if (chirpBindingKey != nullptr) {
        snprintf(result->chirpBindingKey, sizeof(result->chirpBindingKey), "%s", chirpBindingKey);
    }
}
