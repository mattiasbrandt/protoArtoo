// =============================================================================
// include/mood.h
//
// Mood preset system — dual-path execution for R2-D2 idle behaviour.
//
// Each mood (`:SE10`/`:SE11`/`:SE13`/`:SE14`) has two independent execution
// paths that run in parallel:
//
//   Audio path (body-local, always):
//     Dispatches `$s` (stop chatter) or `$R` (resume chatter) directly to
//     AudioTask. Body is the sole audio source and does not wait for the dome.
//
//   Dome visual path (requires active dome link):
//     Enqueues `:SE1x\r` to domeTxQueue → DomeLinkTask → UART2 → AstroPixelsPlus.
//     If the dome link is not active, this step is silently skipped.
//
// Valid mood IDs: 10 (Quiet), 11 (Full-Awake), 13 (Mid-Awake), 14 (Awake+).
// ID 0 means "unset" (no mood applied this session).
//
// moodAudioCommand() is a pure inline — no Arduino/FreeRTOS deps, natively testable.
// applyMood() has Arduino/FreeRTOS dependencies; tested on-device.
// =============================================================================
#pragma once

#include <stdint.h>

// -----------------------------------------------------------------------------
// moodIdFromSeCommand()
// Extract the mood SE index from a Marcduino ':SE' line.
// Returns the mood ID (10, 11, 13, or 14) if the line is a valid mood command,
// or 0 if it is not a mood command or not a valid mood index.
//
// Examples:
//   ":SE10"  → 10
//   ":SE14"  → 14
//   ":SE30"  → 0  (valid body sequence, but not a mood)
//   ":OP01"  → 0  (not a :SE command)
//   nullptr  → 0
//
// Pure function — no Arduino/FreeRTOS deps, safe to call from native tests.
// Used in dome_link.cpp (dome RX intercept) and api_drive.cpp (manual-command).
// -----------------------------------------------------------------------------
inline uint8_t moodIdFromSeCommand(const char* line) {
    if (!line || line[0] != ':' || line[1] != 'S' || line[2] != 'E') {
        return 0;
    }
    int id = 0;
    // atoi is safe here: returns 0 on non-numeric, which we treat as invalid
    id = 0;
    const char* p = line + 3;
    while (*p >= '0' && *p <= '9') {
        id = id * 10 + (*p - '0');
        p++;
    }
    if (id == 10 || id == 11 || id == 13 || id == 14) {
        return (uint8_t)id;
    }
    return 0;
}

// -----------------------------------------------------------------------------
// moodLabel()
// Returns the human-readable name for a mood ID, or "unknown".
// Pure function — safe to call from native tests.
// -----------------------------------------------------------------------------
inline const char* moodLabel(uint8_t moodId) {
    switch (moodId) {
        case 10: return "Quiet";
        case 11: return "Full-Awake";
        case 13: return "Mid-Awake";
        case 14: return "Awake+";
        default: return "unknown";
    }
}

// -----------------------------------------------------------------------------
// moodAudioCommand()
// Returns the $ audio command for a given SE1x mood index, or nullptr if the
// index is not a valid mood. Pure function — safe to call from native tests.
//
// Mapping:
//   SE10 (Quiet)      → "$s"  — stop chatter
//   SE11 (Full-Awake) → "$R"  — resume random chatter
//   SE13 (Mid-Awake)  → "$R"
//   SE14 (Awake+)     → "$R"
// -----------------------------------------------------------------------------
inline const char* moodAudioCommand(uint8_t moodId) {
    switch (moodId) {
        case 10:
            return "$s";
        case 11:
        case 13:
        case 14:
            return "$R";
        default:
            return nullptr;
    }
}

// -----------------------------------------------------------------------------
// applyMood()
// Execute a mood preset via the dual audio + dome TX path.
//
// moodId    : SE1x index — must be 10, 11, 13, or 14. Invalid values are
//             silently ignored.
// fromDome  : set true when called from the dome RX parser to suppress
//             echoing the command back to the dome (avoid command loop).
//
// Behaviour:
//   1. Dispatches moodAudioCommand(moodId) to AudioTask queue (non-blocking).
//   2. If dome link is active AND !fromDome: enqueues ":SE1x" to domeTxQueue.
//   3. Updates robotState.activeMood under portMUX.
//   4. Persists moodId to NVS key "last_mood".
// -----------------------------------------------------------------------------
void applyMood(uint8_t moodId, bool fromDome = false);
