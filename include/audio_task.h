// =============================================================================
// include/audio_task.h
//
// AudioTask  --  the sole writer to the audio serial GPIO.
//
// All audio commands (from RC, web API, dome serial '$' RX, mood presets)
// are enqueued via the helpers below and processed by audioTask() on Core 0.
//
// Queue design:
//   - AUDIO_CMD_DOLLAR      : raw '$' command string, parsed inside AudioTask.
//   - AUDIO_CMD_PLAY_TRACK  : direct play-by-track-number (e.g. from web API).
//   - AUDIO_CMD_SET_VOLUME  : direct absolute volume set.
//   - AUDIO_CMD_QUERY_STATUS: on-demand module status query (web UI poll button).
//                             Used for manual DY-SV5W poll and modules without
//                             AUDIO_CAP_QUERY_SAFE_PLAYING only.
//   - AUDIO_CMD_TRACK_STOP  : Track Stop -- the vocals, never a Sound Bed.
//   - AUDIO_CMD_BED_START / AUDIO_CMD_BED_STOP: the Sound Bed (ADR 0054).
//
// Queue sends from real-time tasks MUST use the audioQueue* helpers which
// use timeout 0 (non-blocking). Never call xQueueSend directly on audioCmdQueue
// from a Core 1 task.
// =============================================================================
#pragma once

#include <stdint.h>

#include "audio_playback_policy.h"
#include "audio_driver.h"
#include "robot_state.h"

// -----------------------------------------------------------------------------
// AudioCommandType  --  discriminant for messages placed on audioCmdQueue.
//
// This is the queue-level enum: it describes what the AudioTask should execute.
// It is intentionally coarser than AudioActionType (see audio_dollar_parser.h),
// which operates at the dollar-command parsing layer and carries more variants
// (RANDOM_ON/OFF, VOLUME_UP/DOWN) that the parser resolves before enqueueing.
// -----------------------------------------------------------------------------
enum AudioCommandType : uint8_t {
    AUDIO_CMD_DOLLAR = 0,    // raw '$' command string  --  parsed in AudioTask
    AUDIO_CMD_PLAY_TRACK,    // play specific track number directly
    AUDIO_CMD_PLAY_TRACK_BANKED,  // play CHIRP bank/page/index tuple
    AUDIO_CMD_PLAY_SLOT,  // play named/system slot with backend-aware resolution
    AUDIO_CMD_PLAY_CATEGORY,  // play random category track with optional fallback slot
    AUDIO_CMD_TRACK_STOP,    // Track Stop (ADR 0010): stop current playback only,
                             // preserve random/idle mood
    AUDIO_CMD_SET_VOLUME,    // set absolute volume 0-30
    AUDIO_CMD_QUERY_STATUS,  // on-demand status query (manual/fallback poll path)
    AUDIO_CMD_REFRESH_CATALOG,  // refresh CHIRP catalog cache
    AUDIO_CMD_REFRESH_BINDINGS,  // refresh cached CHIRP slot/category bindings from NVS
    AUDIO_CMD_BED_START,     // start a Sound Bed (ADR 0054): banked tuple at its own volume
    AUDIO_CMD_BED_STOP,      // stop the Sound Bed and nothing else
};

// -----------------------------------------------------------------------------
// AudioCommand  --  message placed on audioCmdQueue.
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
        struct {          // AUDIO_CMD_PLAY_CATEGORY
            AudioPlaybackCategory category;
            AudioPlaybackSlot fallbackSlot;
        } category;
        struct {          // AUDIO_CMD_PLAY_TRACK_BANKED
            uint16_t index;
            uint8_t bank;
            char page;
        } banked;
        struct {          // AUDIO_CMD_BED_START
            uint16_t index;
            uint8_t bank;
            char page;
            uint8_t volume;  // 0-30, clamped before enqueue
        } bed;
    };
};

// Union must be large enough to hold the dollar string (largest member).
// If this fires, increase dollar[] or check for accidental struct changes.
static_assert(sizeof(AudioCommand) >= 10 + 2,
              "AudioCommand too small - dollar[] union member may be truncated");

const char* audioRxStatusToken(AudioRxStatus status);
const char* audioRxStatusDetail(AudioRxStatus status);

// -----------------------------------------------------------------------------
// audioTask()  --  FreeRTOS task entry point.
// Pinned to Core 0 (non-RT side). Driver init and queries block for hundreds of
// ms, and without PA_CAP_DEDICATED_AUDIO_UART the software bit-bang TX
// additionally holds a critical section for ~1.04 ms per byte, once per byte of
// a command that is 2 bytes on an MP3 Trigger, 4 to 6 on a DY-SV5W and 15 on a
// CHIRP "PLAY:12,2,C,66" (22 when a Sound Bed is held and a "STOP:1" goes
// first); Core 0 keeps all of it away from DriveTask / ServoTask.
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

// Enqueue category playback with optional named/system fallback slot.
bool audioQueuePlayCategory(AudioPlaybackCategory category, AudioPlaybackSlot fallbackSlot,
                            CommandSource src);

// Enqueue a Track Stop (ADR 0010): stops current playback only, preserves
// random/idle mood, and bumps the anti-spam cadence so idle chatter resumes after
// a natural beat. Use this everywhere except the mood system's Quiet path.
// A Sound Bed playing under the vocals keeps playing (ADR 0054): stop it with
// audioQueueBedStop().
bool audioQueueTrackStop(CommandSource src);

// -----------------------------------------------------------------------------
// Sound Bed (ADR 0054) -- the seam the Sound Bed step plugs into.
//
// audioQueueBedStart() starts a bed: music playing UNDER the routine at its own
// volume, which vocals fire over without stopping. audioQueueBedStop() stops it
// and nothing else. Non-blocking like every helper here; false only when the
// queue is full or the tuple is malformed (index 0 or bank 0).
//
// Target form: bank/page/index, the CHIRP address. CHIRP is the only module
// that mixes (AUDIO_CAP_MIXES, include/component_registry.inc), its music lives
// on banks 2-6 by the module's own convention, and a Named Track on CHIRP is
// already that tuple (AudioChirpSlotBinding), so a step naming a Named Track
// resolves to the same three values. There is no flat-track form: a module
// with one stream cannot play a bed at all.
//
// What the caller gets on a module without AUDIO_CAP_MIXES: the command is
// accepted, AudioTask does not play it and logs why (the one audio seam,
// AUDIO_STEP_IGNORE_CANNOT_MIX). The run-time report and the Rehearsal Warning
// are the caller's to compose from audioGetCapabilities(); this seam does not
// answer back. Ignored in Sleep Mode like any play.
//
// Stops: Quiet, Sleep Mode entry and Sound switched off stop the bed with
// everything else. A Track Stop does NOT -- so a Sequence that started a bed
// owns stopping it: its teardown calls audioQueueBedStop() as well as
// audioQueueTrackStop() when the bed is bounded (the default) or the end is
// abnormal (estop included).
// -----------------------------------------------------------------------------
bool audioQueueBedStart(uint16_t index, uint8_t bank, char page, uint8_t vol, CommandSource src);
bool audioQueueBedStop(CommandSource src);

// Enqueue an absolute volume set (clamped to 0-30 before enqueue).
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
// Returns the operator-visible name of the active audio driver's module
// (e.g. "DY-SV5W", "CHIRP Audio Trigger").
// Safe to call from any task or web handler after AudioTask has been created.
const char* audioGetDriverName();
// Returns the capabilities bitmask of the compiled-in audio driver.
// Safe to call from any context after AudioTask has been created.
uint8_t audioGetCapabilities();

// Audio catalog accessors (Core 0 only). Non-catalog builds return empty values.
const AudioCatalogEntry* audioGetCatalogEntries(uint16_t* count);
const AudioCatalogBank* audioGetCatalogBanks(uint8_t* count);
bool audioIsCatalogReady();
// Whether the fitted sound module reported a bank numbered `bank` (Core 0
// only, like the accessors above). What a $8nn line asks before it is taken:
// the answer AudioTask gives the same line (audioStepCommand()), asked
// early so the sender hears it.
//
// The bank table is read as a catalog reader (include/audio_catalog_gate.h):
// a refresh can be replacing it on AudioTask while this runs on the web or
// Console task. While one holds the gate the answer is CatalogBusy - never a
// "not fitted" read from storage being rewritten (#449).
enum class AudioBankFit : uint8_t { Fitted, NotFitted, CatalogBusy };
AudioBankFit audioBankFitted(uint8_t bank);
