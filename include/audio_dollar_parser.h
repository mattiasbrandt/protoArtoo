// =============================================================================
// include/audio_dollar_parser.h
//
// Pure parser for MarcDuino '$' audio commands.
//
// No Arduino or FreeRTOS dependencies  --  this file is included in native unit
// tests as well as the firmware build. AudioTask calls parseAudioDollar() and
// dispatches the returned AudioAction to the active AudioDriver.
//
// Named track defaults follow the R2 community standard SD card numbering.
// They are compile-time defaults only; each is the default of its audio Setting,
// stored under that Setting's NVS key (src/config_settings.cpp).
//
// $ command reference (full set handled here):
//   $nnn   --  play track nnn (1-based integer)
//   $S     --  play scream
//   $F     --  play short circuit / faint
//   $L     --  play Leia message
//   $c     --  play short Cantina
//   $C     --  play long Cantina
//   $W     --  play Star Wars theme
//   $M     --  play Imperial March
//   $B     --  play startup / boot sound
//   $D     --  play disco (NVS key snd_disco, disabled when 0)
//   $R     --  enable random playback mode
//   $O     --  disable random mode (does not stop current sound)
//   $s     --  stop playback and disable random mode
//   $+     --  volume up by 1
//   $-     --  volume down by 1
//   $m     --  volume to mid (15)
//   $f     --  volume to max (30)
//   $p     --  volume to min (0)
// =============================================================================
#pragma once

#include <stdint.h>

// -----------------------------------------------------------------------------
// Default track indices for named $ commands.
// Based on R2 community standard SD card layout (sequential file numbering).
// NOTE: verify against the installed SD card layout during hardware validation.
// -----------------------------------------------------------------------------
constexpr uint16_t AUDIO_TRACK_SCREAM    = 126;  // $S  --  scream bank start
constexpr uint16_t AUDIO_TRACK_FAINT     = 128;  // $F  --  short circuit / faint
constexpr uint16_t AUDIO_TRACK_LEIA      = 151;  // $L  --  Leia message
constexpr uint16_t AUDIO_TRACK_CANTINA_S = 176;  // $c  --  short Cantina
constexpr uint16_t AUDIO_TRACK_SW_THEME  = 177;  // $W  --  Star Wars theme
constexpr uint16_t AUDIO_TRACK_IMP_MARCH = 178;  // $M  --  Imperial March
constexpr uint16_t AUDIO_TRACK_CANTINA_L = 180;  // $C  --  long Cantina
constexpr uint16_t AUDIO_TRACK_STARTUP   = 255;  // $B  --  startup / boot sound
constexpr uint16_t AUDIO_TRACK_DISCO     = 0;    // $D  --  disco (NVS snd_disco, 0=disabled)
constexpr uint16_t AUDIO_TRACK_HAPPY     = 3;    // $H  --  happy/greeting clip (R2 community track 3 default)

// Random playback pool defaults (NVS-configurable)
constexpr uint16_t AUDIO_RAND_TRACK_MIN   = 1;
constexpr uint16_t AUDIO_RAND_TRACK_MAX   = 100;
// Per-mood random playback intervals in seconds - NVS-configurable.
// AudioTask derives the active interval from robotState.activeMood + cfg_snd_int_*.
// Mood 0 (unset) falls back to AUDIO_RAND_INT_FULL. An interval of 0 suppresses
// random playback for that mood.
constexpr uint16_t AUDIO_RAND_INT_QUIET = 0;   // SE10 Quiet      --  silent
constexpr uint16_t AUDIO_RAND_INT_MID   = 30;  // SE13 Mid-Awake  --  sparse
constexpr uint16_t AUDIO_RAND_INT_FULL  = 20;  // SE11 Full-Awake  --  normal
constexpr uint16_t AUDIO_RAND_INT_AWAKE = 10;  // SE14 Awake+     --  frequent

// Volume presets (normalised 0-30 interface range)
constexpr uint8_t AUDIO_VOLUME_MID = 15;
constexpr uint8_t AUDIO_VOLUME_MAX = 30;
constexpr uint8_t AUDIO_VOLUME_MIN = 0;

// Clamp volume to the valid range [0, AUDIO_VOLUME_MAX].
// Pure function, no side effects, safe to use in unit tests.
inline uint8_t audioClampVolume(uint8_t vol) {
    return (vol > AUDIO_VOLUME_MAX) ? AUDIO_VOLUME_MAX : vol;
}

// -----------------------------------------------------------------------------
// AudioNamedTracks  --  passed into parseAudioDollar() so callers can substitute
// NVS-configured values without changing the parser itself.
// Default-constructed to the constexpr defaults above.
// -----------------------------------------------------------------------------
struct AudioNamedTracks {
    uint16_t scream    = AUDIO_TRACK_SCREAM;
    uint16_t faint     = AUDIO_TRACK_FAINT;
    uint16_t leia      = AUDIO_TRACK_LEIA;
    uint16_t cantina_s = AUDIO_TRACK_CANTINA_S;
    uint16_t sw_theme  = AUDIO_TRACK_SW_THEME;
    uint16_t imp_march = AUDIO_TRACK_IMP_MARCH;
    uint16_t cantina_l = AUDIO_TRACK_CANTINA_L;
    uint16_t startup   = AUDIO_TRACK_STARTUP;
    uint16_t disco     = AUDIO_TRACK_DISCO;
    uint16_t happy     = AUDIO_TRACK_HAPPY;
};

// -----------------------------------------------------------------------------
// AudioActionType  --  output of the dollar-command parser (audio_dollar_parser.h).
//
// This enum operates at the parsing layer, one level above the queue. It
// carries variants the queue enum (AudioCommandType) does not need: RANDOM_ON,
// RANDOM_OFF, VOLUME_UP, VOLUME_DOWN. The parser resolves these to concrete
// AudioCommandType values (or NVS config updates) before placing a message on
// audioCmdQueue. Do not conflate the two enums.
// -----------------------------------------------------------------------------
enum AudioActionType : uint8_t {
    AUDIO_ACTION_NONE = 0,
    AUDIO_ACTION_PLAY_TRACK,   // play a specific track number (see AudioAction.track)
    AUDIO_ACTION_STOP,         // stop playback and disable random mode
    AUDIO_ACTION_RANDOM_ON,    // enable random playback mode
    AUDIO_ACTION_RANDOM_OFF,   // disable random mode without stopping current sound
    AUDIO_ACTION_VOLUME_SET,   // set absolute volume 0-30 (see AudioAction.volume)
    AUDIO_ACTION_VOLUME_UP,    // increment volume by 1 (AudioTask applies clamp)
    AUDIO_ACTION_VOLUME_DOWN,  // decrement volume by 1 (AudioTask applies clamp)
};

// -----------------------------------------------------------------------------
// AudioAction  --  result of parsing a single $ command.
// -----------------------------------------------------------------------------
struct AudioAction {
    AudioActionType type = AUDIO_ACTION_NONE;
    uint16_t track       = 0;  // valid when type == AUDIO_ACTION_PLAY_TRACK
    uint8_t volume       = 0;  // valid when type == AUDIO_ACTION_VOLUME_SET
};

// -----------------------------------------------------------------------------
// parseAudioDollar()
// Parse a MarcDuino $ command string into an AudioAction.
//
// cmd must start with '$'. Returns AUDIO_ACTION_NONE for null, empty, or
// unrecognised input  --  callers may safely ignore NONE actions.
//
// named provides track numbers for named shortcuts; default-construct it to
// use the constexpr defaults, or populate from NVS for configurable mapping.
//
// Pure function  --  no Arduino, FreeRTOS, or global state dependencies.
// -----------------------------------------------------------------------------
AudioAction parseAudioDollar(const char* cmd,
                              const AudioNamedTracks& named = AudioNamedTracks{});
