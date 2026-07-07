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

void setError(AudioCategoryRangeApplyResult* result, const char* message) {
    result->error.hasError = true;
    snprintf(result->error.message, sizeof(result->error.message), "%s", message);
}

void setNotFoundError(AudioCategoryRangeApplyResult* result, const char* message) {
    setError(result, message);
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
        setError(result, "requires lo_key, hi_key, lo, hi parameters");
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
        setError(result, "invalid category key pair");
        return;
    }
    snprintf(result->categoryNvsKey, sizeof(result->categoryNvsKey), "%s", categoryBindingEntry->nvsKey);

    const char* bankRaw = configParamGet(params, "bank");
    const char* pageRaw = configParamGet(params, "page");
    const char* clearBindingRaw = configParamGet(params, "clear_binding");
    const bool hasBankedParams = (bankRaw != nullptr) || (pageRaw != nullptr);
    bool clearBinding = false;
    if (clearBindingRaw != nullptr && !parseBoolValue(clearBindingRaw, &clearBinding)) {
        setError(result, "clear_binding must be true/false/1/0");
        return;
    }
    if (hasBankedParams && clearBinding) {
        setError(result, "clear_binding cannot be combined with bank/page");
        return;
    }

    uint8_t categoryBank = 0;
    char categoryPage = 'A';
    if (hasBankedParams) {
        if (!(bankRaw && pageRaw)) {
            setError(result, "bank and page must be provided together");
            return;
        }
        if (!catalogSupported) {
            setNotFoundError(result, "catalog unsupported by active backend");
            return;
        }
        uint32_t bankValue = 0;
        if (!parseUint32Value(bankRaw, &bankValue) || bankValue < 1 || bankValue > 6) {
            setError(result, "bank must be 1-6");
            return;
        }
        if (!parseChirpPage(pageRaw, &categoryPage)) {
            setError(result, "page must be a single letter A-Z");
            return;
        }
        categoryBank = (uint8_t)bankValue;
    } else if (clearBinding && !catalogSupported) {
        setNotFoundError(result, "catalog unsupported by active backend");
        return;
    }

    uint32_t loTrack = 0;
    uint32_t hiTrack = 0;
    if (!parseUint32Value(loRaw, &loTrack) || !parseUint32Value(hiRaw, &hiTrack)) {
        setError(result, "range values must be non-negative integers");
        return;
    }
    if (loTrack > 999U || hiTrack > 999U) {
        setError(result, "range values must be 0–999");
        return;
    }
    if (!((loTrack == 0U && hiTrack == 0U) ||
          (loTrack >= 1U && hiTrack >= 1U && loTrack <= hiTrack))) {
        setError(result, "range must be 0/0 or 1–999 with lo <= hi");
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
        setError(result, "unknown category key");
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
