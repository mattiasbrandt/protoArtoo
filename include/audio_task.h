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
//   - AUDIO_CMD_TRACK_STOP  : Track Stop -- the vocals, never a Background
//     Track.
//   - AUDIO_CMD_BACKGROUND_TRACK_START / AUDIO_CMD_BACKGROUND_TRACK_STOP: the
//     Background Track (ADR 0054).
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
    AUDIO_CMD_BACKGROUND_TRACK_START,  // start a Background Track (ADR 0054): a '$'
                                       // sound at its own volume, resolved in AudioTask
    AUDIO_CMD_BACKGROUND_TRACK_STOP,   // stop the Background Track and nothing else
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
        struct {          // AUDIO_CMD_BACKGROUND_TRACK_START
            char dollar[9];  // '$' + up to 7 chars, null-terminated: the same
                             // 10 bytes as dollar[] above with the volume, so
                             // the union does not grow
            uint8_t volume;  // 0-30, clamped before enqueue
        } backgroundTrack;
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
// CHIRP "PLAY:12,2,C,66" (22 when a Background Track is held and a "STOP:1"
// goes first); Core 0 keeps all of it away from DriveTask / ServoTask.
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
// random/idle mood, and bumps the anti-spam cadence so idle chatter resumes
// after a natural beat. Use this everywhere except the mood system's Quiet
// path. A Background Track playing under the vocals keeps playing (ADR 0054):
// stop it with audioQueueBackgroundTrackStop().
bool audioQueueTrackStop(CommandSource src);

// -----------------------------------------------------------------------------
// Background Track (ADR 0054) -- the seam the Background Track step plugs into.
//
// audioQueueBackgroundTrackStart() starts a Background Track: music playing
// UNDER the routine at its own volume, which vocals fire over without stopping.
// audioQueueBackgroundTrackStop() stops it and nothing else. Non-blocking like
// every helper here; false only when the queue is full or `dollar` is not a
// '$' command that fits the queue entry.
//
// Target form: a '$' command, the address a Learned audio step already uses
// ("$W", "$212", "$805"), so a sequence names a Background Track the way it
// names a vocal. AudioTask resolves it where it resolves a vocal's, because
// the Named Track bindings live there: a Named Track to its CHIRP binding, or
// to its numbered track where it has none; $8nn to bank 8 on the page the
// module reported; any other number to that track. A numbered track plays at
// AUDIO_FLAT_BANK / AUDIO_FLAT_PAGE (include/audio_driver.h), where CHIRP
// plays it as a vocal. The volume is the interface's 0-30.
//
// What the caller gets on a module without AUDIO_CAP_MIXES: the command is
// accepted, AudioTask does not play it and logs why (the one audio seam,
// AUDIO_STEP_IGNORE_CANNOT_MIX). The run-time report and the Rehearsal Warning
// are the caller's to compose from audioGetCapabilities(); this seam does not
// answer back. Ignored in Sleep Mode like any play, and a '$' that names no
// sound (AUDIO_STEP_IGNORE_NOT_A_SOUND) or a Named Track set to nothing is
// logged and not played.
//
// Stops: Quiet, Sleep Mode entry and Sound switched off stop the Background
// Track with everything else. A Track Stop does NOT -- so a Sequence that
// started a Background Track owns stopping it: its teardown calls
// audioQueueBackgroundTrackStop() as well as audioQueueTrackStop() when the
// Background Track is bounded (the default) or the end is abnormal (estop
// included).
// -----------------------------------------------------------------------------
bool audioQueueBackgroundTrackStart(const char* dollar, uint8_t vol, CommandSource src);
bool audioQueueBackgroundTrackStop(CommandSource src);

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
