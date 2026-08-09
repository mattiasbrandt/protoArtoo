// =============================================================================
// include/chirp_binding_keys.h
//
// Canonical NVS-key lookup tables for CHIRP catalog bindings, shared by the
// api_audio.cpp GET handlers and the api_audio_tracks_apply /
// api_audio_category_range_apply write-path Apply Cores (ADR 0011 audio
// wave). Single source of truth  --  a code-review pass on the campaign
// flagged these as duplicated across three files with drift risk if a new
// slot or category is ever added; consolidated here.
// =============================================================================
#pragma once

#include <stddef.h>
#include <string.h>

struct ChirpBindingKeyMapEntry {
    const char* key;
    const char* nvsKey;
};

constexpr ChirpBindingKeyMapEntry CHIRP_BINDING_KEYS[] = {
    {"scream", "chr_scream"},       {"faint", "chr_faint"},
    {"leia", "chr_leia"},           {"cantina_s", "chr_cantina_s"},
    {"sw_theme", "chr_sw_theme"},   {"imp_march", "chr_imp_march"},
    {"cantina_l", "chr_cantina_l"}, {"startup", "chr_startup"},
    {"doodoo", "chr_doodoo"},       {"failure", "chr_failure"},
    {"disco", "chr_disco"},         {"mahna", "chr_mahna"},
    {"inlove", "chr_inlove"},       {"macho", "chr_macho"},
    {"gangnam", "chr_gangnam"},     {"uptown", "chr_uptown"},
    {"celebr", "chr_celebr"},       {"stayin", "chr_stayin"},
    {"harlem", "chr_harlem"},       {"pbjtime", "chr_pbjtime"},
    {"sys_boot", "chr_sys_boot"},   {"sys_mode_n", "chr_sys_mode_n"},
    {"sys_mode_s", "chr_sys_mode_s"},
    {"sys_mode_t", "chr_sys_mode_t"},
    {"sys_drv_on", "chr_sys_drv_on"},
    {"sys_dome_on", "chr_sys_dome_on"},
};

inline const char* chirpBindingNvsKey(const char* key) {
    if (key == nullptr) {
        return nullptr;
    }
    for (size_t i = 0; i < (sizeof(CHIRP_BINDING_KEYS) / sizeof(CHIRP_BINDING_KEYS[0])); ++i) {
        if (strcmp(CHIRP_BINDING_KEYS[i].key, key) == 0) {
            return CHIRP_BINDING_KEYS[i].nvsKey;
        }
    }
    return nullptr;
}

struct ChirpCategoryBindingMapEntry {
    const char* loKey;
    const char* hiKey;
    const char* nvsKey;
};

constexpr ChirpCategoryBindingMapEntry CHIRP_CATEGORY_BINDING_KEYS[] = {
    {"snd_cat_gen_lo", "snd_cat_gen_hi", "chr_cat_gen"},
    {"snd_cat_chat_lo", "snd_cat_chat_hi", "chr_cat_chat"},
    {"snd_cat_hap_lo", "snd_cat_hap_hi", "chr_cat_hap"},
    {"snd_cat_proc_lo", "snd_cat_proc_hi", "chr_cat_proc"},
    {"snd_cat_sad_lo", "snd_cat_sad_hi", "chr_cat_sad"},
    {"snd_cat_sent_lo", "snd_cat_sent_hi", "chr_cat_sent"},
    {"snd_cat_hum_lo", "snd_cat_hum_hi", "chr_cat_hum"},
    {"snd_cat_scrm_lo", "snd_cat_scrm_hi", "chr_cat_scrm"},
    {"snd_cat_ooh_lo", "snd_cat_ooh_hi", "chr_cat_ooh"},
    {"snd_cat_alrm_lo", "snd_cat_alrm_hi", "chr_cat_alrm"},
    {"snd_cat_snrk_lo", "snd_cat_snrk_hi", "chr_cat_snrk"},
    {"snd_cat_whis_lo", "snd_cat_whis_hi", "chr_cat_whis"},
};

inline const ChirpCategoryBindingMapEntry* chirpCategoryBindingEntryForRangeKeys(const char* loKey,
                                                                                  const char* hiKey) {
    if (loKey == nullptr || hiKey == nullptr) {
        return nullptr;
    }
    for (size_t i = 0; i < (sizeof(CHIRP_CATEGORY_BINDING_KEYS) / sizeof(CHIRP_CATEGORY_BINDING_KEYS[0]));
         ++i) {
        const ChirpCategoryBindingMapEntry& entry = CHIRP_CATEGORY_BINDING_KEYS[i];
        if (strcmp(entry.loKey, loKey) == 0 && strcmp(entry.hiKey, hiKey) == 0) {
            return &entry;
        }
    }
    return nullptr;
}
