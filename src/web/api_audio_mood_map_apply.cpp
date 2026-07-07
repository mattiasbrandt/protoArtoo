// =============================================================================
// src/web/api_audio_mood_map_apply.cpp
//
// Apply Core for POST /api/audio/mood-map (ADR 0011 audio wave). See
// api_audio_mood_map_apply.h.
// =============================================================================

#include "api_audio_mood_map_apply.h"

#include <ArduinoJson.h>
#include <stdio.h>

#include "api_helpers.h"
#include "mood_sound_mapping.h"

namespace {

void setError(AudioMoodMapApplyResult* result, const char* message) {
    result->error.hasError = true;
    snprintf(result->error.message, sizeof(result->error.message), "%s", message);
}

void setFieldError(AudioMoodMapApplyResult* result, const char* fmt, const char* key) {
    result->error.hasError = true;
    snprintf(result->error.message, sizeof(result->error.message), fmt, key);
}

bool parseMaskText(const char* raw, const char* key, uint16_t* out, AudioMoodMapApplyResult* result) {
    uint32_t value = 0;
    if (!parseUint32Value(raw, &value)) {
        setFieldError(result, "%s must be a non-negative integer", key);
        return false;
    }
    if (!isValidMoodCategoryMaskValue(value)) {
        setFieldError(result, "%s must be 0..4095", key);
        return false;
    }
    *out = (uint16_t)value;
    return true;
}

}  // namespace

void audioMoodMapApply(const ConfigParamSource& params, AudioMoodMapApplyResult* result) {
    *result = AudioMoodMapApplyResult{};

    const char* quietRaw = configParamGet(params, "quiet");
    const char* midRaw = configParamGet(params, "mid");
    const char* fullRaw = configParamGet(params, "full");
    const char* awakeplusRaw = configParamGet(params, "awakeplus");
    const bool hasAnyForm = quietRaw != nullptr || midRaw != nullptr || fullRaw != nullptr ||
                             awakeplusRaw != nullptr;

    if (hasAnyForm) {
        if (!(quietRaw && midRaw && fullRaw && awakeplusRaw)) {
            setError(result, "requires quiet, mid, full, awakeplus");
            return;
        }
        if (!parseMaskText(quietRaw, "quiet", &result->quiet, result) ||
            !parseMaskText(midRaw, "mid", &result->mid, result) ||
            !parseMaskText(fullRaw, "full", &result->full, result) ||
            !parseMaskText(awakeplusRaw, "awakeplus", &result->awakeplus, result)) {
            return;
        }
        return;
    }

    const char* plainRaw = configParamGet(params, "plain");
    if (plainRaw == nullptr) {
        setError(result, "requires form fields or json body");
        return;
    }

    JsonDocument bodyDoc;
    if (deserializeJson(bodyDoc, plainRaw)) {
        setError(result, "invalid json body");
        return;
    }

    auto parseMaskJson = [&](const char* key, uint16_t* out) -> bool {
        JsonVariantConst value = bodyDoc[key];
        if (value.isNull()) {
            setFieldError(result, "missing %s", key);
            return false;
        }

        if (value.is<uint32_t>()) {
            uint32_t parsed = value.as<uint32_t>();
            if (!isValidMoodCategoryMaskValue(parsed)) {
                setFieldError(result, "%s must be 0..4095", key);
                return false;
            }
            *out = (uint16_t)parsed;
            return true;
        }

        if (value.is<int32_t>()) {
            int32_t parsed = value.as<int32_t>();
            if (parsed < 0 || !isValidMoodCategoryMaskValue((uint32_t)parsed)) {
                setFieldError(result, "%s must be 0..4095", key);
                return false;
            }
            *out = (uint16_t)parsed;
            return true;
        }

        if (value.is<const char*>()) {
            return parseMaskText(value.as<const char*>(), key, out, result);
        }

        setFieldError(result, "%s must be integer", key);
        return false;
    };

    if (!parseMaskJson("quiet", &result->quiet) || !parseMaskJson("mid", &result->mid) ||
        !parseMaskJson("full", &result->full) || !parseMaskJson("awakeplus", &result->awakeplus)) {
        return;
    }
}
