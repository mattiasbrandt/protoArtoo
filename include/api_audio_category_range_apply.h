// =============================================================================
// include/api_audio_category_range_apply.h
//
// Apply Core for POST /api/audio/category-range (ADR 0011 audio wave,
// family 2 — issue #18 finding 1).
//
// audioCategoryRangeApply(): pure function — no FreeRTOS, no
//   AsyncWebServerRequest, no NVS, no logging. Reads lo_key/hi_key/lo/hi
//   and the optional bank/page/clear_binding params through a
//   ConfigParamSource, validates the category key pair and range, and
//   mutates `working` in place (mirrors ADR 0011 slice 1/2 house style).
//   Byte-identical error messages to the legacy handler.
//
// `catalogSupported` is a live input the shell must snapshot before
// calling (audioCatalogSupported() queries the live AudioDriver — an
// impure runtime query, same reasoning as ADR 0011's
// "snapshot live inputs before calling the core").
//
// The core does NOT persist to NVS or touch the config cache: the legacy
// handler's two-phase persist (config NVS, then a separate chirp-binding
// NVS key) with rollback-on-partial-failure is side-effect orchestration
// that stays in the shell verbatim, per ADR 0011's "the shell keeps every
// side effect" principle — this is not a "simple apply-once, persist-once"
// core like slices 1/2, so the result carries everything the shell needs
// to replay that exact two-phase dance: the old field values (for
// rollback), the resolved binding NVS key, and the packed bank/page
// binding.
//
// Defined in src/web/api_audio_category_range_apply.cpp.
// =============================================================================
#pragma once

#include <stdint.h>

#include "api_param_source.h"
#include "config_store.h"

struct AudioCategoryRangeApplyError {
    bool hasError = false;
    bool notFound = false;  // true -> shell responds 404 instead of 400
    char message[128] = {0};
};

struct AudioCategoryRangeApplyResult {
    AudioCategoryRangeApplyError error;
    char loKey[32] = {0};
    char hiKey[32] = {0};
    uint16_t loValue = 0;
    uint16_t hiValue = 0;
    uint16_t oldLo = 0;
    uint16_t oldHi = 0;
    bool hasBankedParams = false;
    bool clearBinding = false;
    uint8_t categoryBank = 0;
    char categoryPage = 'A';
    // NVS key for the category's chirp binding (e.g. "chr_cat_gen"), resolved
    // from the lo_key/hi_key pair. Empty only if hasError.
    char categoryNvsKey[16] = {0};
};

// `working` must already hold the current cached snapshot (shell reads it
// via configCacheRead before calling); mutated in place on success.
// `catalogSupported` is audioCatalogSupported() snapshotted by the shell.
void audioCategoryRangeApply(const ConfigParamSource& params, bool catalogSupported,
                              ConfigSnapshot* working, AudioCategoryRangeApplyResult* result);
