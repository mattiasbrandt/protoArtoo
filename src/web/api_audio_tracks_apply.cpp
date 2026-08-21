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

void setError(AudioTracksApplyResult* result, const char* message) {
    result->error.hasError = true;
    snprintf(result->error.message, sizeof(result->error.message), "%s", message);
}

void setNotFoundError(AudioTracksApplyResult* result, const char* message) {
    setError(result, message);
    result->error.notFound = true;
}

}  // namespace

void audioTracksApply(const ConfigParamSource& params, bool catalogSupported, ConfigSnapshot* working,
                       AudioTracksApplyResult* result) {
    *result = AudioTracksApplyResult{};

    const char* key = configParamGet(params, "key");
    const char* trackRaw = configParamGet(params, "track");
    if (key == nullptr || trackRaw == nullptr) {
        setError(result, "requires key and track parameters");
        return;
    }
    snprintf(result->key, sizeof(result->key), "%s", key);

    const bool isInterval = (strncmp(key, "snd_int_", 8) == 0);
    const bool isCategoryRangeKey = (strncmp(key, "snd_cat_", 8) == 0);
    const bool isZeroAllowedTrackKey =
        isCategoryRangeKey || strcmp(key, "doodoo") == 0 || strcmp(key, "failure") == 0 ||
        strcmp(key, "disco") == 0 || strcmp(key, "mahna") == 0 || strcmp(key, "inlove") == 0 ||
        strcmp(key, "macho") == 0 || strcmp(key, "gangnam") == 0 || strcmp(key, "uptown") == 0 ||
        strcmp(key, "celebr") == 0 || strcmp(key, "stayin") == 0 || strcmp(key, "harlem") == 0 ||
        strcmp(key, "pbjtime") == 0 || strcmp(key, "sys_boot") == 0 ||
        strcmp(key, "sys_mode_n") == 0 || strcmp(key, "sys_mode_s") == 0 ||
        strcmp(key, "sys_mode_t") == 0 || strcmp(key, "sys_drv_on") == 0 ||
        strcmp(key, "sys_dome_on") == 0;

    const char* bankRaw = configParamGet(params, "bank");
    const char* pageRaw = configParamGet(params, "page");
    const bool hasBankedParams = (bankRaw != nullptr) || (pageRaw != nullptr);

    uint8_t bank = 0;
    char page = 'A';
    bool useBanked = false;
    const char* chirpBindingKey = chirpBindingNvsKey(key);

    if (hasBankedParams) {
        if (!(bankRaw && pageRaw)) {
            setError(result, "bank and page must be provided together");
            return;
        }
        if (!catalogSupported) {
            setNotFoundError(result, "catalog unsupported by active backend");
            return;
        }
        if (chirpBindingKey == nullptr || isInterval) {
            setError(result, "key does not support CHIRP binding");
            return;
        }

        uint32_t bankValue = 0;
        if (!parseUint32Value(bankRaw, &bankValue) || bankValue < 1 || bankValue > 6) {
            setError(result, "bank must be 1-6");
            return;
        }
        if (!parseChirpPage(pageRaw, &page)) {
            setError(result, "page must be a single letter A-Z");
            return;
        }
        bank = (uint8_t)bankValue;
        useBanked = true;
    }

    uint32_t track = 0;
    if (!parseUint32Value(trackRaw, &track)) {
        setError(result, "track must be a non-negative integer");
        return;
    }

    if (isInterval) {
        if (track > 3600U) {
            setError(result, "interval must be 0-3600 s");
            return;
        }
    } else if (useBanked) {
        if (track < 1U || track > 65535U) {
            setError(result, "banked index must be 1-65535");
            return;
        }
    } else {
        if (track > 999U) {
            setError(result, "track must be 0-999");
            return;
        }
        if (track == 0U && !isZeroAllowedTrackKey) {
            setError(result, "track must be 1-999");
            return;
        }
    }

    const uint16_t t = (uint16_t)track;
    uint16_t oldTrack = 0;

    if (!configAudioGetTrackByKey(working->audio, key, &oldTrack) ||
        !configAudioSetTrackByKey(&working->audio, key, t)) {
        setError(result, "unknown key");
        return;
    }

    result->track = t;
    result->oldTrack = oldTrack;
    result->useBanked = useBanked;
    result->bank = bank;
    result->page = page;
    if (chirpBindingKey != nullptr) {
        snprintf(result->chirpBindingKey, sizeof(result->chirpBindingKey), "%s", chirpBindingKey);
    }
}
