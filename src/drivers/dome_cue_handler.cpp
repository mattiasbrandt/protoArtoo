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
#include <string.h>

#include "audio_task.h"
#include "logging.h"

static const char* TAG = "DomeCue";

void handleDomeCue(const char* cue) {
    if (strcmp(cue, "SCREAM") == 0) {
        audioQueuePlayCategory(AUDIO_CATEGORY_SCREAM, AUDIO_SLOT_NAMED_SCREAM, SRC_INTERNAL);
    } else if (strcmp(cue, "HAPPY") == 0) {
        audioQueuePlayCategory(AUDIO_CATEGORY_HAPPY, AUDIO_SLOT_NONE, SRC_INTERNAL);
    } else if (strcmp(cue, "OVERLOAD") == 0) {
        audioQueuePlayCategory(AUDIO_CATEGORY_SAD, AUDIO_SLOT_NAMED_FAINT, SRC_INTERNAL);
    } else if (strcmp(cue, "ALARM") == 0) {
        audioQueuePlayCategory(AUDIO_CATEGORY_ALERT, AUDIO_SLOT_NONE, SRC_INTERNAL);
    } else if (strcmp(cue, "VADER") == 0) {
        audioQueuePlaySlot(AUDIO_SLOT_NAMED_IMP_MARCH, SRC_INTERNAL);
    } else if (strcmp(cue, "ROCKMARCH") == 0) {
        audioQueuePlaySlot(AUDIO_SLOT_NAMED_IMP_MARCH, SRC_INTERNAL);
    } else if (strcmp(cue, "LEIA") == 0) {
        audioQueuePlaySlot(AUDIO_SLOT_NAMED_LEIA, SRC_INTERNAL);
    } else if (strcmp(cue, "CANTINA") == 0) {
        audioQueuePlaySlot(AUDIO_SLOT_NAMED_CANTINA_L, SRC_INTERNAL);
    } else if (strcmp(cue, "HEART") == 0) {
        audioQueuePlayCategory(AUDIO_CATEGORY_SENTIMENTAL, AUDIO_SLOT_NONE, SRC_INTERNAL);
    } else if (strcmp(cue, "HELLO") == 0) {
        audioQueuePlayCategory(AUDIO_CATEGORY_GENERAL, AUDIO_SLOT_NONE, SRC_INTERNAL);
    } else if (strcmp(cue, "RESET") == 0) {
        audioQueueTrackStop(SRC_INTERNAL);
    } else {
        PA_LOG_DEBUG(TAG, "BD:%s -- unknown cue ignored", cue);
        return;
    }
    PA_LOG_INFO(TAG, "dome cue: BD:%s", cue);
}
