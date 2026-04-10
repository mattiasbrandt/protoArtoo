// =============================================================================
// include/audio_task.h
//
// AudioTask — the sole writer to the audio serial GPIO.
//
// All audio commands (from RC, web API, dome serial '$' RX, mood presets)
// are enqueued via the helpers below and processed by audioTask() on Core 0.
//
// Queue design:
//   - AUDIO_CMD_DOLLAR      : raw '$' command string, parsed inside AudioTask.
//   - AUDIO_CMD_PLAY_TRACK  : direct play-by-track-number (e.g. from web API).
//   - AUDIO_CMD_STOP        : direct stop.
//   - AUDIO_CMD_SET_VOLUME  : direct absolute volume set.
//   - AUDIO_CMD_QUERY_STATUS: on-demand module status query (web UI poll button).
//                             Used for manual DY-SV5W poll and modules without
//                             AUDIO_CAP_QUERY_SAFE_PLAYING only.
//
// Queue sends from real-time tasks MUST use the audioQueue* helpers which
// use timeout 0 (non-blocking). Never call xQueueSend directly on audioCmdQueue
// from a Core 1 task.
// =============================================================================
#pragma once

#include <stdint.h>

#include "audio_chirp.h"
#include "robot_state.h"

// -----------------------------------------------------------------------------
// AudioCommandType — discriminant for messages placed on audioCmdQueue.
//
// This is the queue-level enum: it describes what the AudioTask should execute.
// It is intentionally coarser than AudioActionType (see audio_dollar_parser.h),
// which operates at the dollar-command parsing layer and carries more variants
// (RANDOM_ON/OFF, VOLUME_UP/DOWN) that the parser resolves before enqueueing.
// -----------------------------------------------------------------------------
enum AudioCommandType : uint8_t {
    AUDIO_CMD_DOLLAR = 0,    // raw '$' command string — parsed in AudioTask
    AUDIO_CMD_PLAY_TRACK,    // play specific track number directly
    AUDIO_CMD_PLAY_TRACK_BANKED,  // play CHIRP bank/page/index tuple
    AUDIO_CMD_PLAY_SLOT,  // play named/system slot with backend-aware resolution
    AUDIO_CMD_STOP,          // stop playback
    AUDIO_CMD_SET_VOLUME,    // set absolute volume 0–30
    AUDIO_CMD_QUERY_STATUS,  // on-demand status query (manual/fallback poll path)
    AUDIO_CMD_REFRESH_CATALOG,  // refresh CHIRP catalog cache
    AUDIO_CMD_REFRESH_BINDINGS,  // refresh cached CHIRP slot/category bindings from NVS
};

enum AudioPlaybackSlot : uint8_t {
    AUDIO_SLOT_NONE = 0,
    AUDIO_SLOT_NAMED_SCREAM,
    AUDIO_SLOT_NAMED_FAINT,
    AUDIO_SLOT_NAMED_LEIA,
    AUDIO_SLOT_NAMED_CANTINA_S,
    AUDIO_SLOT_NAMED_SW_THEME,
    AUDIO_SLOT_NAMED_IMP_MARCH,
    AUDIO_SLOT_NAMED_CANTINA_L,
    AUDIO_SLOT_NAMED_STARTUP,
    AUDIO_SLOT_NAMED_DISCO,
    AUDIO_SLOT_SYS_BOOT,
    AUDIO_SLOT_SYS_MODE_NORMAL,
    AUDIO_SLOT_SYS_MODE_SLOW,
    AUDIO_SLOT_SYS_MODE_TURBO,
    AUDIO_SLOT_SYS_DRIVE_ON,
    AUDIO_SLOT_SYS_DOME_ON,
};

// -----------------------------------------------------------------------------
// AudioCommand — message placed on audioCmdQueue.
// Sized conservatively: dollar string covers all $ shortcuts ($S, $001 etc.).
// -----------------------------------------------------------------------------
struct AudioCommand {
    AudioCommandType type;
    CommandSource source;
    union {
        char dollar[10];  // AUDIO_CMD_DOLLAR: '$'-prefixed, null-terminated
        uint16_t track;   // AUDIO_CMD_PLAY_TRACK
        uint8_t volume;   // AUDIO_CMD_SET_VOLUME
        AudioPlaybackSlot slot;  // AUDIO_CMD_PLAY_SLOT
        struct {          // AUDIO_CMD_PLAY_TRACK_BANKED
            uint16_t index;
            uint8_t bank;
            char page;
        } banked;
    };
};

// Union must be large enough to hold the dollar string (largest member).
// If this fires, increase dollar[] or check for accidental struct changes.
static_assert(sizeof(AudioCommand) >= 10 + 2,
              "AudioCommand too small — dollar[] union member may be truncated");

// -----------------------------------------------------------------------------
// audioTask() — FreeRTOS task entry point.
// Pinned to Core 0 (non-RT side). Software bit-bang TX blocks for up to ~6 ms
// per audio command; Core 0 keeps this away from DriveTask / ServoTask.
// Priority: 3 (below web server; above idle).
// Stack: 3072 bytes.
// -----------------------------------------------------------------------------
void audioTask(void* pvParameters);

// -----------------------------------------------------------------------------
// Non-blocking queue-send helpers.
// Return true if the command was enqueued, false if the queue was full.
// Use these from any task; they always use timeout 0.
// -----------------------------------------------------------------------------

// Enqueue a raw '$' command (e.g. "$R", "$001", "$S").
// cmd must include the '$' prefix. Strings longer than 9 chars are truncated.
bool audioQueueDollar(const char* cmd, CommandSource src);

// Enqueue a direct play-by-track command. track must be > 0.
bool audioQueuePlayTrack(uint16_t track, CommandSource src);

// Enqueue a banked play-by-index command (CHIRP).
bool audioQueuePlayTrackBanked(uint16_t index, uint8_t bank, char page, CommandSource src);

// Enqueue backend-aware playback for a named/system slot.
// Uses CHIRP bank/page/index binding when available; otherwise falls back to numeric snd_* track.
bool audioQueuePlaySlot(AudioPlaybackSlot slot, CommandSource src);

// Enqueue a stop command.
bool audioQueueStop(CommandSource src);

// Enqueue an absolute volume set (clamped to 0–30 before enqueue).
bool audioQueueSetVolume(uint8_t vol, CommandSource src);

// Enqueue an on-demand module status query. AudioTask runs queryModuleState()
// and updates RobotState. Used by the web UI Poll button for manual DY-SV5W
// polling and as a fallback for modules without AUDIO_CAP_QUERY_SAFE_PLAYING.
// The caller should GET /api/audio after ~1.5 s to read the result. Do not call
// from real-time tasks or in any loop.
bool audioQueueQueryStatus(CommandSource src);

// Enqueue an asynchronous CHIRP catalog refresh.
bool audioQueueRefreshCatalog(CommandSource src);

// Enqueue CHIRP slot/category binding cache refresh from NVS.
bool audioQueueRefreshBindings(CommandSource src);
// Returns the short name of the active audio driver (e.g. "DY-SV5W", "CHIRP").
// Safe to call from any task or web handler after AudioTask has been created.
const char* audioGetDriverName();
// Returns the capabilities bitmask of the compiled-in audio driver.
// Safe to call from any context after AudioTask has been created.
uint8_t audioGetCapabilities();

// CHIRP catalog accessors (Core 0 only). Non-CHIRP builds return empty values.
const ChirpCatalogEntry* audioGetCatalogEntries(uint16_t* count);
const ChirpCatalogBank* audioGetCatalogBanks(uint8_t* count);
bool audioIsCatalogReady();

