// =============================================================================
// include/api_audio_mood_map_apply.h
//
// Apply Core for POST /api/audio/mood-map (ADR 0011 audio wave, family 1 —
// issue #18 finding 1).
//
// audioMoodMapApply(): pure function — no FreeRTOS, no AsyncWebServerRequest,
//   no NVS, no logging. Reads the four mood-category masks (quiet, mid,
//   full, awakeplus) through a ConfigParamSource, accepting either the
//   form-field shape or a "plain" JSON body (matching the legacy handler's
//   precedence: form fields first, JSON body second, neither -> error), and
//   validates each against MOOD_CATEGORY_MASK_MAX. Byte-identical error
//   messages to the legacy handler.
//
// This core does not touch NVS or the config cache — the existing
// configUpdateAudioMoodMasks(Preferences&, ...) domain function
// (config_store.h) already bundles validate+apply+persist for this single
// atomic write, and stays a shell-only call. The core's job is purely the
// HTTP-shaped input resolution/validation that function doesn't do.
//
// Defined in src/web/api_audio_mood_map_apply.cpp.
// =============================================================================
#pragma once

#include <stdint.h>

#include "api_param_source.h"

struct AudioMoodMapApplyError {
    bool hasError = false;
    char message[128] = {0};
};

struct AudioMoodMapApplyResult {
    AudioMoodMapApplyError error;
    uint16_t quiet = 0;
    uint16_t mid = 0;
    uint16_t full = 0;
    uint16_t awakeplus = 0;
};

void audioMoodMapApply(const ConfigParamSource& params, AudioMoodMapApplyResult* result);
