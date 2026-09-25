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

// The sentence, and what it says as data (#425): the reason is a parameter,
// so no error write can leave it unset.
void setError(AudioMoodMapApplyResult* result, const char* message, ApplyRefusalReason reason,
              const char* field) {
    result->error.hasError = true;
    snprintf(result->error.message, sizeof(result->error.message), "%s", message);
    applyRefusalSet(&result->error.refusal, reason, field);
}

// A refusal about one mask, `fmt` naming it by `key`. A mask that is there
// and wrong is refused against the range every mask takes.
void setFieldError(AudioMoodMapApplyResult* result, const char* fmt, const char* key,
                   ApplyRefusalReason reason) {
    result->error.hasError = true;
    snprintf(result->error.message, sizeof(result->error.message), fmt, key);
    if (reason == ApplyRefusalReason::OutOfRange) {
        applyRefusalSetRange(&result->error.refusal, key, 0, MOOD_CATEGORY_MASK_MAX);
    } else {
        applyRefusalSet(&result->error.refusal, reason, key);
    }
}

bool parseMaskText(const char* raw, const char* key, uint16_t* out, AudioMoodMapApplyResult* result) {
    uint32_t value = 0;
    if (!parseUint32Value(raw, &value)) {
        setFieldError(result, "%s must be a non-negative integer", key, ApplyRefusalReason::OutOfRange);
        return false;
    }
    if (!isValidMoodCategoryMaskValue(value)) {
        setFieldError(result, "%s must be 0..4095", key, ApplyRefusalReason::OutOfRange);
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
            setError(result, "requires quiet, mid, full, awakeplus", ApplyRefusalReason::MissingArgument,
                     quietRaw == nullptr ? "quiet"
                     : midRaw == nullptr ? "mid"
                     : fullRaw == nullptr ? "full"
                                          : "awakeplus");
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
        setError(result, "requires form fields or json body", ApplyRefusalReason::MissingArgument,
                 nullptr);
        return;
    }

    JsonDocument bodyDoc;
    if (deserializeJson(bodyDoc, plainRaw)) {
        setError(result, "invalid json body", ApplyRefusalReason::MalformedArgument, "plain");
        return;
    }

    auto parseMaskJson = [&](const char* key, uint16_t* out) -> bool {
        JsonVariantConst value = bodyDoc[key];
        if (value.isNull()) {
            setFieldError(result, "missing %s", key, ApplyRefusalReason::MissingArgument);
            return false;
        }

        if (value.is<uint32_t>()) {
            uint32_t parsed = value.as<uint32_t>();
            if (!isValidMoodCategoryMaskValue(parsed)) {
                setFieldError(result, "%s must be 0..4095", key, ApplyRefusalReason::OutOfRange);
                return false;
            }
            *out = (uint16_t)parsed;
            return true;
        }

        if (value.is<int32_t>()) {
            int32_t parsed = value.as<int32_t>();
            if (parsed < 0 || !isValidMoodCategoryMaskValue((uint32_t)parsed)) {
                setFieldError(result, "%s must be 0..4095", key, ApplyRefusalReason::OutOfRange);
                return false;
            }
            *out = (uint16_t)parsed;
            return true;
        }

        if (value.is<const char*>()) {
            return parseMaskText(value.as<const char*>(), key, out, result);
        }

        setFieldError(result, "%s must be integer", key, ApplyRefusalReason::OutOfRange);
        return false;
    };

    if (!parseMaskJson("quiet", &result->quiet) || !parseMaskJson("mid", &result->mid) ||
        !parseMaskJson("full", &result->full) || !parseMaskJson("awakeplus", &result->awakeplus)) {
        return;
    }
}
