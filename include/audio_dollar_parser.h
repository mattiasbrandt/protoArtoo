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
//   $8nn   --  bank 8, sound nn: ShadowMD's $Bnn, played where the fitted
//              sound module has a bank 8 and refused where it has not
//              (audioDollarBankForm() below)
//   $nnn   --  play track nnn (1-based integer), every other number
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
    AUDIO_ACTION_PLAY_BANKED,  // sound AudioAction.track in bank AudioAction.bank
};

// -----------------------------------------------------------------------------
// AudioAction  --  result of parsing a single $ command.
// -----------------------------------------------------------------------------
struct AudioAction {
    AudioActionType type = AUDIO_ACTION_NONE;
    uint16_t track       = 0;  // PLAY_TRACK: the track; PLAY_BANKED: the sound in the bank
    uint8_t volume       = 0;  // valid when type == AUDIO_ACTION_VOLUME_SET
    uint8_t bank         = 0;  // valid when type == AUDIO_ACTION_PLAY_BANKED
};

// -----------------------------------------------------------------------------
// audioDollarBankForm()
// Whether cmd is ShadowMD's bank form of '$', and which bank and sound it names.
//
// ShadowMD writes $Bnn for "bank B, sound nn" - its template sends $803 and
// $809-$825 - where '$' here has always meant a raw track number, so $803 used
// to play track 803: accepted, never an error, and the wrong file (#321). The
// operator's answer on #449 (2026-09-30): $8nn is bank 8, sound nn, and every
// other number stays a raw track. Bank 8 alone, because raw $1nn-$7nn are the
// R2 community's own track numbers ($126 is the scream default above) and
// reading those as banks would move files that play correctly today.
//
// Exactly four characters, '$', '8' and two digits. $800 is the bank form
// naming sound 0, which no bank has; parseAudioDollar() answers NONE for it.
// -----------------------------------------------------------------------------
constexpr uint8_t AUDIO_DOLLAR_BANK = 8;

inline bool audioDollarBankForm(const char* cmd, uint8_t* bankOut, uint16_t* soundOut) {
    if (cmd == nullptr || cmd[0] != '$' || cmd[1] != (char)('0' + AUDIO_DOLLAR_BANK) ||
        cmd[2] < '0' || cmd[2] > '9' || cmd[3] < '0' || cmd[3] > '9' || cmd[4] != '\0') {
        return false;
    }
    if (bankOut != nullptr) {
        *bankOut = AUDIO_DOLLAR_BANK;
    }
    if (soundOut != nullptr) {
        *soundOut = (uint16_t)(((cmd[2] - '0') * 10) + (cmd[3] - '0'));
    }
    return true;
}

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
