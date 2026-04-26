// =============================================================================
// src/drivers/dome_cue_handler.cpp
//
// Named body cue dispatcher for BD:<CUE> messages from the dome link.
//
// Each cue plays audio keyed to a dome sequence. Category ranges are preferred
// for variety; named slots are used for specific long tracks (VADER, LEIA,
// CANTINA) and as fallbacks where a category range may not yet be configured.
// Unknown cues are logged at DEBUG level and silently ignored.
// =============================================================================

#include "dome_cue_handler.h"

#include <Arduino.h>
#include <esp_system.h>
#include <string.h>

#include "audio_task.h"
#include "logging.h"
#include "rc_mapping.h"
#include "robot_state.h"

static const char* TAG = "DomeCue";

// Play one random track from [lo, hi]. Returns true if a track was queued.
static bool playCategory(uint16_t lo, uint16_t hi) {
    uint16_t track = 0;
    if (!selectRandomTrackInRange(lo, hi, esp_random(), &track) || track == 0) {
        return false;
    }
    return audioQueuePlayTrack(track, SRC_INTERNAL);
}

void handleDomeCue(const char* cue) {
    if (strcmp(cue, "SCREAM") == 0) {
        uint16_t lo, hi;
        taskENTER_CRITICAL(&robotStateMux);
        lo = robotState.cfg_snd_cat_scrm_lo;
        hi = robotState.cfg_snd_cat_scrm_hi;
        taskEXIT_CRITICAL(&robotStateMux);
        if (!playCategory(lo, hi)) {
            audioQueuePlaySlot(AUDIO_SLOT_NAMED_SCREAM, SRC_INTERNAL);
        }
    } else if (strcmp(cue, "HAPPY") == 0) {
        uint16_t lo, hi;
        taskENTER_CRITICAL(&robotStateMux);
        lo = robotState.cfg_snd_cat_hap_lo;
        hi = robotState.cfg_snd_cat_hap_hi;
        taskEXIT_CRITICAL(&robotStateMux);
        playCategory(lo, hi);
    } else if (strcmp(cue, "OVERLOAD") == 0) {
        uint16_t lo, hi;
        taskENTER_CRITICAL(&robotStateMux);
        lo = robotState.cfg_snd_cat_sad_lo;
        hi = robotState.cfg_snd_cat_sad_hi;
        taskEXIT_CRITICAL(&robotStateMux);
        if (!playCategory(lo, hi)) {
            audioQueuePlaySlot(AUDIO_SLOT_NAMED_FAINT, SRC_INTERNAL);
        }
    } else if (strcmp(cue, "ALARM") == 0) {
        uint16_t lo, hi;
        taskENTER_CRITICAL(&robotStateMux);
        lo = robotState.cfg_snd_cat_alrm_lo;
        hi = robotState.cfg_snd_cat_alrm_hi;
        taskEXIT_CRITICAL(&robotStateMux);
        playCategory(lo, hi);
    } else if (strcmp(cue, "VADER") == 0) {
        audioQueuePlaySlot(AUDIO_SLOT_NAMED_IMP_MARCH, SRC_INTERNAL);
    } else if (strcmp(cue, "ROCKMARCH") == 0) {
        audioQueuePlaySlot(AUDIO_SLOT_NAMED_IMP_MARCH, SRC_INTERNAL);
    } else if (strcmp(cue, "LEIA") == 0) {
        audioQueuePlaySlot(AUDIO_SLOT_NAMED_LEIA, SRC_INTERNAL);
    } else if (strcmp(cue, "CANTINA") == 0) {
        audioQueuePlaySlot(AUDIO_SLOT_NAMED_CANTINA_L, SRC_INTERNAL);
    } else if (strcmp(cue, "HEART") == 0) {
        uint16_t lo, hi;
        taskENTER_CRITICAL(&robotStateMux);
        lo = robotState.cfg_snd_cat_sent_lo;
        hi = robotState.cfg_snd_cat_sent_hi;
        taskEXIT_CRITICAL(&robotStateMux);
        playCategory(lo, hi);
    } else if (strcmp(cue, "HELLO") == 0) {
        uint16_t lo, hi;
        taskENTER_CRITICAL(&robotStateMux);
        lo = robotState.cfg_snd_cat_gen_lo;
        hi = robotState.cfg_snd_cat_gen_hi;
        taskEXIT_CRITICAL(&robotStateMux);
        playCategory(lo, hi);
    } else if (strcmp(cue, "RESET") == 0) {
        audioQueueStop(SRC_INTERNAL);
    } else {
        PA_LOG_DEBUG(TAG, "BD:%s -- unknown cue ignored", cue);
        return;
    }
    PA_LOG_INFO(TAG, "dome cue: BD:%s", cue);
}
