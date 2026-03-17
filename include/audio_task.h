// =============================================================================
// include/audio_task.h
//
// AudioTask — the sole writer to the audio serial GPIO.
//
// All audio commands (from RC, web API, dome serial '$' RX, mood presets)
// are enqueued via the helpers below and processed by audioTask() on Core 0.
//
// Queue design:
//   - AUDIO_CMD_DOLLAR   : raw '$' command string, parsed inside AudioTask.
//   - AUDIO_CMD_PLAY_TRACK: direct play-by-track-number (e.g. from web API).
//   - AUDIO_CMD_STOP     : direct stop.
//   - AUDIO_CMD_SET_VOLUME: direct absolute volume set.
//
// Queue sends from real-time tasks MUST use the audioQueue* helpers which
// use timeout 0 (non-blocking). Never call xQueueSend directly on audioCmdQueue
// from a Core 1 task.
// =============================================================================
#pragma once

#include <stdint.h>

#include "robot_state.h"

// -----------------------------------------------------------------------------
// AudioCommandType
// -----------------------------------------------------------------------------
enum AudioCommandType : uint8_t {
    AUDIO_CMD_DOLLAR    = 0,  // raw '$' command string — parsed in AudioTask
    AUDIO_CMD_PLAY_TRACK,     // play specific track number directly
    AUDIO_CMD_STOP,           // stop playback
    AUDIO_CMD_SET_VOLUME,     // set absolute volume 0–30
};

// -----------------------------------------------------------------------------
// AudioCommand — message placed on audioCmdQueue.
// Sized conservatively: dollar string covers all $ shortcuts ($S, $001 etc.).
// -----------------------------------------------------------------------------
struct AudioCommand {
    AudioCommandType type;
    CommandSource source;
    union {
        char     dollar[10];  // AUDIO_CMD_DOLLAR: '$'-prefixed, null-terminated
        uint16_t track;       // AUDIO_CMD_PLAY_TRACK
        uint8_t  volume;      // AUDIO_CMD_SET_VOLUME
    };
};

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

// Enqueue a stop command.
bool audioQueueStop(CommandSource src);

// Enqueue an absolute volume set (clamped to 0–30 before enqueue).
bool audioQueueSetVolume(uint8_t vol, CommandSource src);
