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
#include "config_settings.h"  // each mood mask's check

namespace {

// The sentence, and what it says as data (#425): the reason is a parameter,
// so no error write can leave it unset.
void setError(AudioMoodMapApplyResult* result, const char* message, ApplyRefusalReason reason,
              const char* field) {
    result->error.hasError = true;
    snprintf(result->error.message, sizeof(result->error.message), "%s", message);
    applyRefusalSet(&result->error.refusal, reason, field);
}

// One mask, by its own Setting (include/config_settings.h): its range and its
// refusal, named by the mask's key. `raw` is the text of the value, whichever
// shape the request sent it in.
bool parseMaskText(const char* raw, const char* key, uint16_t* out, AudioMoodMapApplyResult* result) {
    const ConfigSetting* setting = audioSettingByName(key, SettingDoor::AudioMoodMap);
    int32_t value = 0;
    if (setting == nullptr ||
        !configSettingCheck(*setting, raw, key, &value, &result->error.refusal,
                            result->error.message, sizeof(result->error.message))) {
        if (setting == nullptr) {
            setError(result, "unknown mood mask", ApplyRefusalReason::OutOfRange, key);
        }
        result->error.hasError = true;
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
            char message[48];
            snprintf(message, sizeof(message), "missing %s", key);
            setError(result, message, ApplyRefusalReason::MissingArgument, key);
            return false;
        }

        // A number is checked as the text JSON writes it, so 1.5 stays 1.5 and
        // is refused by the mask's own parse rather than truncated to 1.
        if (value.is<long>() || value.is<double>()) {
            char text[24] = {};
            const size_t length = serializeJson(value, text, sizeof(text));
            if (length == 0 || length >= sizeof(text)) {
                setError(result, "mask must be a number", ApplyRefusalReason::OutOfRange, key);
                return false;
            }
            return parseMaskText(text, key, out, result);
        }

        if (value.is<const char*>()) {
            return parseMaskText(value.as<const char*>(), key, out, result);
        }

        setError(result, "mask must be a number", ApplyRefusalReason::OutOfRange, key);
        return false;
    };

    if (!parseMaskJson("quiet", &result->quiet) || !parseMaskJson("mid", &result->mid) ||
        !parseMaskJson("full", &result->full) || !parseMaskJson("awakeplus", &result->awakeplus)) {
        return;
    }
}
