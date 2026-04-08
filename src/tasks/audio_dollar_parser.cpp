// =============================================================================
// src/tasks/audio_dollar_parser.cpp
//
// Implementation of parseAudioDollar() — pure logic, no Arduino/FreeRTOS deps.
// Included in both the firmware build and the native unit test build.
//
// See include/audio_dollar_parser.h for the full $ command reference.
// =============================================================================

#include "audio_dollar_parser.h"

#include <stdlib.h>  // atoi
#include <string.h>  // strlen

AudioAction parseAudioDollar(const char* cmd, const AudioNamedTracks& named) {
    AudioAction action{};

    if (!cmd || cmd[0] != '$') {
        return action;  // NONE
    }

    const char* arg = cmd + 1;  // character(s) after '$'

    if (*arg == '\0') {
        return action;  // bare '$' — NONE
    }

    // Numeric argument: $nnn — play track by number
    if (*arg >= '0' && *arg <= '9') {
        int track = atoi(arg);
        if (track > 0 && track <= 65535) {
            action.type  = AUDIO_ACTION_PLAY_TRACK;
            action.track = (uint16_t)track;
        }
        return action;
    }

    // Single-character commands
    switch (*arg) {
        // ---- Named sound shortcuts ----
        case 'S': action.type = AUDIO_ACTION_PLAY_TRACK; action.track = named.scream;    break;
        case 'F': action.type = AUDIO_ACTION_PLAY_TRACK; action.track = named.faint;     break;
        case 'L': action.type = AUDIO_ACTION_PLAY_TRACK; action.track = named.leia;      break;
        case 'c': action.type = AUDIO_ACTION_PLAY_TRACK; action.track = named.cantina_s; break;
        case 'C': action.type = AUDIO_ACTION_PLAY_TRACK; action.track = named.cantina_l; break;
        case 'W': action.type = AUDIO_ACTION_PLAY_TRACK; action.track = named.sw_theme;  break;
        case 'M': action.type = AUDIO_ACTION_PLAY_TRACK; action.track = named.imp_march; break;
        case 'B': action.type = AUDIO_ACTION_PLAY_TRACK; action.track = named.startup;   break;
        case 'D':
            if (named.disco > 0) {
                action.type = AUDIO_ACTION_PLAY_TRACK;
                action.track = named.disco;
            }
            break;

        // ---- Playback control ----
        case 'R': action.type = AUDIO_ACTION_RANDOM_ON;  break;
        case 'O': action.type = AUDIO_ACTION_RANDOM_OFF; break;
        case 's': action.type = AUDIO_ACTION_STOP;       break;

        // ---- Volume control ----
        case '+': action.type = AUDIO_ACTION_VOLUME_UP;   break;
        case '-': action.type = AUDIO_ACTION_VOLUME_DOWN; break;
        case 'm': action.type = AUDIO_ACTION_VOLUME_SET; action.volume = AUDIO_VOLUME_MID; break;
        case 'f': action.type = AUDIO_ACTION_VOLUME_SET; action.volume = AUDIO_VOLUME_MAX; break;
        case 'p': action.type = AUDIO_ACTION_VOLUME_SET; action.volume = AUDIO_VOLUME_MIN; break;

        default:
            break;  // unrecognised — NONE
    }

    return action;
}
