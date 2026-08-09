// =============================================================================
// include/api_audio_tracks_apply.h
//
// Apply Core for POST /api/audio/tracks (ADR 0011 audio wave, family 3).
//
// audioTracksApply(): pure function — no FreeRTOS, no request object,
//   no NVS, no logging. Reads key/track and the optional bank/page through
//   a ConfigParamSource, classifies the key (interval / banked / plain
//   named-or-category track, with the zero-allowed exception list), and
//   mutates `working` in place. Byte-identical error messages to the legacy
//   handler.
//
// `catalogSupported` is a live input the shell must snapshot before calling
// (audioCatalogSupported() queries the live AudioDriver), same reasoning as
// the category-range core.
//
// The core does not persist to NVS: the legacy handler's two-phase persist
// (config NVS, then a separate chirp-binding NVS key) with rollback on
// partial failure stays in the shell verbatim, per ADR 0011's "the shell
// keeps every side effect" principle — same shape as the category-range
// core (family 2).
//
// Defined in src/web/api_audio_tracks_apply.cpp.
// =============================================================================
#pragma once

#include <stdint.h>

#include "api_param_source.h"
#include "config_cache.h"

struct AudioTracksApplyError {
    bool hasError = false;
    bool notFound = false;  // true -> shell responds 404 instead of 400
    char message[128] = {0};
};

struct AudioTracksApplyResult {
    AudioTracksApplyError error;
    char key[32] = {0};
    uint16_t track = 0;
    uint16_t oldTrack = 0;
    bool useBanked = false;
    uint8_t bank = 0;
    char page = 'A';
    // NVS key for the key's chirp binding (e.g. "chr_scream"), empty if this
    // key has no chirp binding (interval keys and unrecognized keys).
    char chirpBindingKey[16] = {0};
};

// `working` must already hold the current cached snapshot (shell reads it
// via configCacheRead before calling); mutated in place on success.
// `catalogSupported` is audioCatalogSupported() snapshotted by the shell.
void audioTracksApply(const ConfigParamSource& params, bool catalogSupported, ConfigSnapshot* working,
                       AudioTracksApplyResult* result);
