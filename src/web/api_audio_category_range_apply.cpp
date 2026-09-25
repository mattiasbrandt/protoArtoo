// =============================================================================
// src/web/api_audio_category_range_apply.cpp
//
// Apply Core for POST /api/audio/category-range (ADR 0011 audio wave). See
// api_audio_category_range_apply.h.
// =============================================================================

#include "api_audio_category_range_apply.h"

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
void setError(AudioCategoryRangeApplyResult* result, const char* message,
              ApplyRefusalReason reason, const char* field, const char* accepts = nullptr) {
    result->error.hasError = true;
    snprintf(result->error.message, sizeof(result->error.message), "%s", message);
    applyRefusalSet(&result->error.refusal, reason, field, accepts);
}

void setRangeError(AudioCategoryRangeApplyResult* result, const char* message, const char* field,
                   long lo, long hi) {
    result->error.hasError = true;
    snprintf(result->error.message, sizeof(result->error.message), "%s", message);
    applyRefusalSetRange(&result->error.refusal, field, lo, hi);
}

// The fitted module has no catalog to bind into; the shell answers 404.
void setNotFoundError(AudioCategoryRangeApplyResult* result, const char* message,
                      const char* field) {
    setError(result, message, ApplyRefusalReason::NotInThisBuild, field);
    result->error.notFound = true;
}

}  // namespace

void audioCategoryRangeApply(const ConfigParamSource& params, bool catalogSupported,
                              ConfigSnapshot* working, AudioCategoryRangeApplyResult* result) {
    *result = AudioCategoryRangeApplyResult{};

    const char* loKey = configParamGet(params, "lo_key");
    const char* hiKey = configParamGet(params, "hi_key");
    const char* loRaw = configParamGet(params, "lo");
    const char* hiRaw = configParamGet(params, "hi");
    if (loKey == nullptr || hiKey == nullptr || loRaw == nullptr || hiRaw == nullptr) {
        setError(result, "requires lo_key, hi_key, lo, hi parameters",
                 ApplyRefusalReason::MissingArgument,
                 loKey == nullptr ? "lo_key" : hiKey == nullptr ? "hi_key" : loRaw == nullptr ? "lo" : "hi");
        return;
    }
    snprintf(result->loKey, sizeof(result->loKey), "%s", loKey);
    snprintf(result->hiKey, sizeof(result->hiKey), "%s", hiKey);

    const char* loCompanion = configAudioCategoryCompanionKey(loKey);
    const char* hiCompanion = configAudioCategoryCompanionKey(hiKey);
    const ChirpCategoryBindingMapEntry* categoryBindingEntry =
        chirpCategoryBindingEntryForRangeKeys(loKey, hiKey);
    if (loCompanion == nullptr || hiCompanion == nullptr || strcmp(loCompanion, hiKey) != 0 ||
        strcmp(hiCompanion, loKey) != 0 || categoryBindingEntry == nullptr) {
        // A lo_key that names a category is answered on hi_key, with the one
        // hi_key that pairs with it; anything else is the lo_key's fault.
        if (loCompanion != nullptr && strcmp(loCompanion, hiKey) != 0 &&
            chirpCategoryBindingEntryForRangeKeys(loKey, loCompanion) != nullptr) {
            setError(result, "invalid category key pair", ApplyRefusalReason::OutOfRange, "hi_key",
                     loCompanion);
        } else {
            setError(result, "invalid category key pair", ApplyRefusalReason::OutOfRange, "lo_key");
        }
        return;
    }
    snprintf(result->categoryNvsKey, sizeof(result->categoryNvsKey), "%s", categoryBindingEntry->nvsKey);

    const char* bankRaw = configParamGet(params, "bank");
    const char* pageRaw = configParamGet(params, "page");
    const char* clearBindingRaw = configParamGet(params, "clear_binding");
    const bool hasBankedParams = (bankRaw != nullptr) || (pageRaw != nullptr);
    bool clearBinding = false;
    if (clearBindingRaw != nullptr && !parseBoolValue(clearBindingRaw, &clearBinding)) {
        setError(result, "clear_binding must be true/false/1/0", ApplyRefusalReason::OutOfRange,
                 "clear_binding", "true,false,1,0");
        return;
    }
    if (hasBankedParams && clearBinding) {
        setError(result, "clear_binding cannot be combined with bank/page",
                 ApplyRefusalReason::Conflict, "clear_binding");
        return;
    }

    uint8_t categoryBank = 0;
    char categoryPage = 'A';
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
        uint32_t bankValue = 0;
        if (!parseUint32Value(bankRaw, &bankValue) || bankValue < 1 || bankValue > 6) {
            setRangeError(result, "bank must be 1-6", "bank", 1, 6);
            return;
        }
        if (!parseChirpPage(pageRaw, &categoryPage)) {
            setError(result, "page must be a single letter A-Z", ApplyRefusalReason::OutOfRange,
                     "page", "A..Z");
            return;
        }
        categoryBank = (uint8_t)bankValue;
    } else if (clearBinding && !catalogSupported) {
        setNotFoundError(result, "catalog unsupported by active backend", "clear_binding");
        return;
    }

    uint32_t loTrack = 0;
    uint32_t hiTrack = 0;
    const bool loParsed = parseUint32Value(loRaw, &loTrack);
    if (!loParsed || !parseUint32Value(hiRaw, &hiTrack)) {
        setRangeError(result, "range values must be non-negative integers", loParsed ? "hi" : "lo",
                      0, 999);
        return;
    }
    if (loTrack > 999U || hiTrack > 999U) {
        setRangeError(result, "range values must be 0-999", loTrack > 999U ? "lo" : "hi", 0, 999);
        return;
    }
    // Each bound is fine on its own here; the pair clashes (lo above hi, or one
    // end off while the other is on), so it is a conflict named on `lo`.
    if (!((loTrack == 0U && hiTrack == 0U) ||
          (loTrack >= 1U && hiTrack >= 1U && loTrack <= hiTrack))) {
        setError(result, "range must be 0/0 or 1-999 with lo <= hi", ApplyRefusalReason::Conflict,
                 "lo");
        return;
    }

    const uint16_t loValue = (uint16_t)loTrack;
    const uint16_t hiValue = (uint16_t)hiTrack;
    uint16_t oldLo = 0;
    uint16_t oldHi = 0;

    if (!configAudioGetTrackByKey(working->audio, loKey, &oldLo) ||
        !configAudioGetTrackByKey(working->audio, hiKey, &oldHi) ||
        !configAudioSetTrackByKey(&working->audio, loKey, loValue) ||
        !configAudioSetTrackByKey(&working->audio, hiKey, hiValue)) {
        setError(result, "unknown category key", ApplyRefusalReason::OutOfRange, "lo_key");
        return;
    }

    result->loValue = loValue;
    result->hiValue = hiValue;
    result->oldLo = oldLo;
    result->oldHi = oldHi;
    result->hasBankedParams = hasBankedParams;
    result->clearBinding = clearBinding;
    result->categoryBank = categoryBank;
    result->categoryPage = categoryPage;
}
