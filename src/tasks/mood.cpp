// =============================================================================
// src/tasks/mood.cpp
//
// applyMood() — dual-path mood preset execution.
// See include/mood.h for the full design contract.
// =============================================================================

#include "mood.h"

#include <Preferences.h>

#include "audio_task.h"
#include "commanded_modes.h"
#include "config.h"
#include "dome_link.h"
#include "logging.h"
#include "robot_state.h"

static const char* TAG = "Mood";

void applyMood(uint8_t moodId, bool fromDome) {
    const char* audioCmd = moodAudioCommand(moodId);
    if (!audioCmd) {
        PA_LOG_WARN(TAG, "applyMood: invalid mood id %u — ignored", (unsigned)moodId);
        return;
    }

    // --- Path 1: audio (body-local, always) ---
    // Body is the sole audio source. Apply regardless of dome link state.
    audioQueueDollar(audioCmd, SRC_INTERNAL);
    PA_LOG_INFO(TAG, "mood %u audio: %s", (unsigned)moodId, audioCmd);

    // --- Path 2: dome visual (requires active link, no echo from dome RX) ---
    if (!fromDome && domeConnected()) {
        char domeTxBuf[8];
        snprintf(domeTxBuf, sizeof(domeTxBuf), ":SE%02u", (unsigned)moodId);
        domeQueueTx(domeTxBuf);
        PA_LOG_INFO(TAG, "mood %u dome TX: %s", (unsigned)moodId, domeTxBuf);
    } else if (fromDome) {
        PA_LOG_DEBUG(TAG, "mood %u — dome TX suppressed (called from dome RX)", (unsigned)moodId);
    } else {
        PA_LOG_DEBUG(TAG, "mood %u — dome TX skipped (dome not connected)", (unsigned)moodId);
    }

    // --- Update shared state ---
    commandedSetActiveMood(moodId, fromDome ? SRC_INTERNAL : SRC_WEB_API);

    // --- Persist to NVS ---
    // Dedicated mini-write — avoids saving entire config for a mood change.
    // NOTE: applyMood() may be called from DomeLinkTask (Core 1) when a mood
    // command arrives from dome serial. Flash writes take a few ms and will
    // briefly stall DomeLinkTask. This is acceptable because mood changes are
    // rare (user-initiated) and DomeLinkTask is not safety-critical real-time.
    Preferences prefs;
    if (prefs.begin(NVS_NAMESPACE, false)) {
        prefs.putUChar("last_mood", moodId);
        prefs.end();
    }
}
