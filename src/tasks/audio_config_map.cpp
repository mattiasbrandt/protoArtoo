// =============================================================================
// src/tasks/audio_config_map.cpp
//
// audio_config_map (ADR 0013). See audio_config_map.h.
// =============================================================================

#include "audio_config_map.h"

#include <ctype.h>

namespace {
constexpr uint8_t kChirpSlotBindingCount = AUDIO_SLOT_COUNT;
constexpr uint8_t kChirpCategoryBindingCount = AUDIO_CATEGORY_COUNT;
}  // namespace

void audioConfigMapBuild(const ConfigSnapshot& cfg, AudioPlaybackConfig* out) {
    if (out == nullptr) {
        return;
    }
    *out = {};
    out->slotTracks[AUDIO_SLOT_NAMED_SCREAM] = cfg.audio.snd_scream;
    out->slotTracks[AUDIO_SLOT_NAMED_FAINT] = cfg.audio.snd_faint;
    out->slotTracks[AUDIO_SLOT_NAMED_LEIA] = cfg.audio.snd_leia;
    out->slotTracks[AUDIO_SLOT_NAMED_CANTINA_S] = cfg.audio.snd_cantina_s;
    out->slotTracks[AUDIO_SLOT_NAMED_SW_THEME] = cfg.audio.snd_sw_theme;
    out->slotTracks[AUDIO_SLOT_NAMED_IMP_MARCH] = cfg.audio.snd_imp_march;
    out->slotTracks[AUDIO_SLOT_NAMED_CANTINA_L] = cfg.audio.snd_cantina_l;
    out->slotTracks[AUDIO_SLOT_NAMED_STARTUP] = cfg.audio.snd_startup;
    out->slotTracks[AUDIO_SLOT_NAMED_DISCO] = cfg.audio.snd_disco;
    out->slotTracks[AUDIO_SLOT_NAMED_HAPPY] = cfg.audio.snd_happy;
    out->slotTracks[AUDIO_SLOT_SYS_BOOT] = cfg.audio.snd_sys_boot;
    out->slotTracks[AUDIO_SLOT_SYS_MODE_NORMAL] = cfg.audio.snd_sys_mode_n;
    out->slotTracks[AUDIO_SLOT_SYS_MODE_SLOW] = cfg.audio.snd_sys_mode_s;
    out->slotTracks[AUDIO_SLOT_SYS_MODE_TURBO] = cfg.audio.snd_sys_mode_t;
    out->slotTracks[AUDIO_SLOT_SYS_DRIVE_ON] = cfg.audio.snd_sys_drv_on;
    out->slotTracks[AUDIO_SLOT_SYS_DOME_ON] = cfg.audio.snd_sys_dome_on;
    out->slotTracks[AUDIO_SLOT_SYS_NET_DOWN] = cfg.audio.snd_sys_net_down;
    out->randMin = cfg.audio.snd_rand_min;
    out->randMax = cfg.audio.snd_rand_max;
    out->intervalQuietS = cfg.audio.snd_int_quiet;
    out->intervalMidS = cfg.audio.snd_int_mid;
    out->intervalFullS = cfg.audio.snd_int_full;
    out->intervalAwakeS = cfg.audio.snd_int_awake;
    out->moodMasks = {cfg.audio.snd_moodcat_quiet, cfg.audio.snd_moodcat_mid,
                      cfg.audio.snd_moodcat_full, cfg.audio.snd_moodcat_awakeplus};
    out->categoryRanges[AUDIO_CATEGORY_GENERAL] = {cfg.audio.snd_cat_gen_lo, cfg.audio.snd_cat_gen_hi};
    out->categoryRanges[AUDIO_CATEGORY_CHATTY] = {cfg.audio.snd_cat_chat_lo, cfg.audio.snd_cat_chat_hi};
    out->categoryRanges[AUDIO_CATEGORY_HAPPY] = {cfg.audio.snd_cat_hap_lo, cfg.audio.snd_cat_hap_hi};
    out->categoryRanges[AUDIO_CATEGORY_PROCESSING] = {cfg.audio.snd_cat_proc_lo, cfg.audio.snd_cat_proc_hi};
    out->categoryRanges[AUDIO_CATEGORY_SAD] = {cfg.audio.snd_cat_sad_lo, cfg.audio.snd_cat_sad_hi};
    out->categoryRanges[AUDIO_CATEGORY_SENTIMENTAL] = {cfg.audio.snd_cat_sent_lo, cfg.audio.snd_cat_sent_hi};
    out->categoryRanges[AUDIO_CATEGORY_HUMMING] = {cfg.audio.snd_cat_hum_lo, cfg.audio.snd_cat_hum_hi};
    out->categoryRanges[AUDIO_CATEGORY_SCREAM] = {cfg.audio.snd_cat_scrm_lo, cfg.audio.snd_cat_scrm_hi};
    out->categoryRanges[AUDIO_CATEGORY_SURPRISED] = {cfg.audio.snd_cat_ooh_lo, cfg.audio.snd_cat_ooh_hi};
    out->categoryRanges[AUDIO_CATEGORY_ALERT] = {cfg.audio.snd_cat_alrm_lo, cfg.audio.snd_cat_alrm_hi};
    out->categoryRanges[AUDIO_CATEGORY_SNARKY] = {cfg.audio.snd_cat_snarky_lo, cfg.audio.snd_cat_snarky_hi};
    out->categoryRanges[AUDIO_CATEGORY_WHISTLE] = {cfg.audio.snd_cat_whis_lo, cfg.audio.snd_cat_whis_hi};
}

void audioConfigMapNamedTracks(const AudioPlaybackConfig& playback, AudioNamedTracks* out) {
    if (out == nullptr) {
        return;
    }
    out->scream = playback.slotTracks[AUDIO_SLOT_NAMED_SCREAM];
    out->faint = playback.slotTracks[AUDIO_SLOT_NAMED_FAINT];
    out->leia = playback.slotTracks[AUDIO_SLOT_NAMED_LEIA];
    out->cantina_s = playback.slotTracks[AUDIO_SLOT_NAMED_CANTINA_S];
    out->sw_theme = playback.slotTracks[AUDIO_SLOT_NAMED_SW_THEME];
    out->imp_march = playback.slotTracks[AUDIO_SLOT_NAMED_IMP_MARCH];
    out->cantina_l = playback.slotTracks[AUDIO_SLOT_NAMED_CANTINA_L];
    out->startup = playback.slotTracks[AUDIO_SLOT_NAMED_STARTUP];
    out->disco = playback.slotTracks[AUDIO_SLOT_NAMED_DISCO];
    out->happy = playback.slotTracks[AUDIO_SLOT_NAMED_HAPPY];
}

const char* audioChirpKeyForSlot(AudioPlaybackSlot slot) {
    switch (slot) {
        case AUDIO_SLOT_NAMED_SCREAM: return "chr_scream";
        case AUDIO_SLOT_NAMED_FAINT: return "chr_faint";
        case AUDIO_SLOT_NAMED_LEIA: return "chr_leia";
        case AUDIO_SLOT_NAMED_CANTINA_S: return "chr_cantina_s";
        case AUDIO_SLOT_NAMED_SW_THEME: return "chr_sw_theme";
        case AUDIO_SLOT_NAMED_IMP_MARCH: return "chr_imp_march";
        case AUDIO_SLOT_NAMED_CANTINA_L: return "chr_cantina_l";
        case AUDIO_SLOT_NAMED_STARTUP: return "chr_startup";
        case AUDIO_SLOT_NAMED_DISCO: return "chr_disco";
        case AUDIO_SLOT_NAMED_HAPPY: return "chr_happy";
        case AUDIO_SLOT_SYS_BOOT: return "chr_sys_boot";
        case AUDIO_SLOT_SYS_MODE_NORMAL: return "chr_sys_mode_n";
        case AUDIO_SLOT_SYS_MODE_SLOW: return "chr_sys_mode_s";
        case AUDIO_SLOT_SYS_MODE_TURBO: return "chr_sys_mode_t";
        case AUDIO_SLOT_SYS_DRIVE_ON: return "chr_sys_drv_on";
        case AUDIO_SLOT_SYS_DOME_ON: return "chr_sys_dome_on";
        case AUDIO_SLOT_SYS_NET_DOWN: return "chr_sys_netdown";
        case AUDIO_SLOT_NONE:
        default:
            return nullptr;
    }
}

const char* audioChirpKeyForCategory(uint8_t categoryIndex) {
    switch (categoryIndex) {
        case 0: return "chr_cat_gen";
        case 1: return "chr_cat_chat";
        case 2: return "chr_cat_hap";
        case 3: return "chr_cat_proc";
        case 4: return "chr_cat_sad";
        case 5: return "chr_cat_sent";
        case 6: return "chr_cat_hum";
        case 7: return "chr_cat_scrm";
        case 8: return "chr_cat_ooh";
        case 9: return "chr_cat_alrm";
        case 10: return "chr_cat_snrk";
        case 11: return "chr_cat_whis";
        default:
            return nullptr;
    }
}

AudioPlaybackSlot audioSlotForDollar(const char* cmd) {
    if (cmd == nullptr || cmd[0] != '$') {
        return AUDIO_SLOT_NONE;
    }
    switch (cmd[1]) {
        case 'S': return AUDIO_SLOT_NAMED_SCREAM;
        case 'F': return AUDIO_SLOT_NAMED_FAINT;
        case 'L': return AUDIO_SLOT_NAMED_LEIA;
        case 'c': return AUDIO_SLOT_NAMED_CANTINA_S;
        case 'C': return AUDIO_SLOT_NAMED_CANTINA_L;
        case 'W': return AUDIO_SLOT_NAMED_SW_THEME;
        case 'M': return AUDIO_SLOT_NAMED_IMP_MARCH;
        case 'B': return AUDIO_SLOT_NAMED_STARTUP;
        case 'D': return AUDIO_SLOT_NAMED_DISCO;
        case 'H': return AUDIO_SLOT_NAMED_HAPPY;
        default:
            return AUDIO_SLOT_NONE;
    }
}

bool audioUnpackChirpBinding(uint32_t packed, uint8_t* bankOut, char* pageOut, uint16_t* indexOut) {
    if (bankOut == nullptr || pageOut == nullptr || indexOut == nullptr) {
        return false;
    }

    uint8_t bank = (uint8_t)((packed >> 24) & 0xFFu);
    char page = (char)((packed >> 16) & 0xFFu);
    uint16_t index = (uint16_t)(packed & 0xFFFFu);
    if (bank == 0 || index == 0) {
        return false;
    }

    page = (char)toupper((unsigned char)page);
    if (page < 'A' || page > 'Z') {
        return false;
    }

    *bankOut = bank;
    *pageOut = page;
    *indexOut = index;
    return true;
}

bool audioUnpackChirpCategoryBinding(uint32_t packed, uint8_t* bankOut, char* pageOut) {
    if (bankOut == nullptr || pageOut == nullptr) {
        return false;
    }
    uint8_t bank = (uint8_t)((packed >> 8) & 0xFFu);
    char page = (char)(packed & 0xFFu);
    if (bank == 0) {
        return false;
    }
    page = (char)toupper((unsigned char)page);
    if (page < 'A' || page > 'Z') {
        return false;
    }
    *bankOut = bank;
    *pageOut = page;
    return true;
}

bool audioBindingsRefresh(const ConfigReader& reader, bool catalogCapable, AudioBindingCache* out) {
    if (out == nullptr) {
        return false;
    }
    *out = AudioBindingCache{};

    if (!catalogCapable) {
        return false;
    }

    for (uint8_t slotIdx = 1; slotIdx < kChirpSlotBindingCount; ++slotIdx) {
        const char* nvsKey = audioChirpKeyForSlot((AudioPlaybackSlot)slotIdx);
        if (nvsKey == nullptr) {
            continue;
        }

        uint8_t bank = 0;
        char page = 'A';
        uint16_t index = 0;
        uint32_t packed = reader.readU32(nvsKey, 0);
        if (audioUnpackChirpBinding(packed, &bank, &page, &index)) {
            AudioChirpSlotBinding& entry = out->slots[slotIdx];
            entry.valid = true;
            entry.bank = bank;
            entry.page = page;
            entry.index = index;
        }
    }

    for (uint8_t categoryIdx = 0; categoryIdx < kChirpCategoryBindingCount; ++categoryIdx) {
        const char* nvsKey = audioChirpKeyForCategory(categoryIdx);
        if (nvsKey == nullptr) {
            continue;
        }

        uint8_t bank = 0;
        char page = 'A';
        uint32_t packed = reader.readU32(nvsKey, 0);
        if (audioUnpackChirpCategoryBinding(packed, &bank, &page)) {
            AudioChirpCategoryBinding& entry = out->categories[categoryIdx];
            entry.valid = true;
            entry.bank = bank;
            entry.page = page;
        }
    }

    return true;
}
