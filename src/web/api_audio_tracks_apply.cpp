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
        strcmp(key, "sys_dome_on") == 0 || strcmp(key, "sys_net_down") == 0;

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
        if (chirpBindingKey == nullptr || isInterval) {
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

    // What this key takes, which is also what a track that is not a number is
    // refused against: an interval in seconds, a banked index, or a track.
    const uint32_t trackMin = (useBanked || (!isInterval && !isZeroAllowedTrackKey)) ? 1U : 0U;
    const uint32_t trackMax = isInterval ? 3600U : useBanked ? 65535U : 999U;

    uint32_t track = 0;
    if (!parseUint32Value(trackRaw, &track)) {
        setRangeError(result, "track must be a non-negative integer", "track", trackMin, trackMax);
        return;
    }

    if (isInterval) {
        if (track > 3600U) {
            setRangeError(result, "interval must be 0-3600 s", "track", trackMin, trackMax);
            return;
        }
    } else if (useBanked) {
        if (track < 1U || track > 65535U) {
            setRangeError(result, "banked index must be 1-65535", "track", trackMin, trackMax);
            return;
        }
    } else {
        if (track > 999U) {
            setRangeError(result, "track must be 0-999", "track", trackMin, trackMax);
            return;
        }
        if (track == 0U && !isZeroAllowedTrackKey) {
            setRangeError(result, "track must be 1-999", "track", trackMin, trackMax);
            return;
        }
    }

    const uint16_t t = (uint16_t)track;
    uint16_t oldTrack = 0;

    if (!configAudioGetTrackByKey(working->audio, key, &oldTrack) ||
        !configAudioSetTrackByKey(&working->audio, key, t)) {
        setError(result, "unknown key", ApplyRefusalReason::OutOfRange, "key");
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
