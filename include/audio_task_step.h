// =============================================================================
// include/audio_task_step.h
//
// Audio Step Core (ADR 0014)  --  the audio task's pure per-tick decision module.
//
// Second instance of the Step Core pattern (ADR 0005): the audioTask() loop
// gathers inputs, calls the phase functions below in loop order, and executes
// the returned plain-data actions. No driver, FreeRTOS, RobotState, NVS, or
// logging dependencies; compiles in the native test environment.
//
// Calling contract (one loop iteration):
//   1. audioStepTick()        --  disable/init/sleep-entry transitions.
//        drainQueue  -> receive+discard one command, end iteration.
//        initDriver  -> run driver->begin(state.currentVol), then step 2.
//   2. audioStepInitResult()  --  retry-ceiling policy for driver begin().
//        skipRestOfTick -> delay AUDIO_STEP_INIT_RETRY_DELAY_MS, end iteration.
//   3. audioStepCommand()     --  once per command received this iteration.
//   4. audioStepIdle()        --  random playback tick + auto status-query gate.
//
// The core owns the intent's state effects (randomMode, currentVol, cadence
// timestamps); the adapter executes intents on the driver and performs all
// RobotState writes (ADR 0012 zone ownership).
// =============================================================================
#pragma once

#include <stdint.h>

#include "audio_dollar_parser.h"    // AudioNamedTracks
#include "audio_playback_policy.h"  // AudioPlaybackIntent, config, bindings
#include "audio_task.h"             // AudioCommand

// Cadence for the periodic module status poll (AUDIO_CAP_QUERY_SAFE_PLAYING only).
static constexpr uint32_t AUDIO_STEP_AUTO_QUERY_INTERVAL_MS = 10000;
// Delay between driver begin() retries, and the give-up ceiling (~5 s total).
static constexpr uint32_t AUDIO_STEP_INIT_RETRY_DELAY_MS = 250;
static constexpr uint8_t AUDIO_STEP_INIT_MAX_RETRIES = 20;

// -----------------------------------------------------------------------------
// AudioStepState  --  the loop's cross-iteration state, owned by the core.
// Default initialization is the boot state.
// -----------------------------------------------------------------------------
struct AudioStepState {
    bool driverInitialized = false;  // also true after give-up (driver inoperative)
    uint8_t beginRetryCount = 0;
    bool lastAudioEnabled = true;    // previous iteration's enabled state
    bool randomMode = false;
    bool wasSleeping = false;
    uint8_t currentVol = 20;         // replaced by config volume on first init
    // The sound the droid last played, which next and previous step from: a
    // vocal sent to the driver, never a Background Track. Bank and page are
    // CHIRP's (a flat track is AUDIO_FLAT_BANK / AUDIO_FLAT_PAGE there, where
    // it plays); on a flat module the index is the track. currentIndex 0 is
    // nothing played since boot. Bank and page fill the padding before the
    // words below; currentIndex does not fit there and grows the struct from
    // 20 to 24 B. It is a local in audioTask(), whose stack chain is recorded
    // to the byte (tools/task_stack_recipes.json).
    uint8_t currentBank = 0;
    char currentPage = 'A';
    uint32_t lastRandMs = 0;
    uint32_t lastPlayMs = 0;         // anti-spam: last play timestamp
    uint32_t lastAutoQueryMs = 0;
    uint16_t currentIndex = 0;
};

enum AudioStepStopReason : uint8_t {
    AUDIO_STEP_STOP_NONE = 0,
    AUDIO_STEP_STOP_DISABLED,     // audio toggled off with playback possibly active
    AUDIO_STEP_STOP_SLEEP_ENTRY,  // sleep mode entered  --  suppress playback
};

enum AudioStepIgnoreReason : uint8_t {
    AUDIO_STEP_IGNORE_NONE = 0,
    AUDIO_STEP_IGNORE_SLEEP,                // play-type command while sleeping
    AUDIO_STEP_IGNORE_UNSUPPORTED_BACKEND,  // catalog command on non-catalog driver
    AUDIO_STEP_IGNORE_BANK_NOT_FITTED,      // $8nn, and the module has no bank 8
    AUDIO_STEP_IGNORE_CANNOT_MIX,           // Background Track on a module without AUDIO_CAP_MIXES
    AUDIO_STEP_IGNORE_NOT_A_SOUND,          // Background Track whose '$' plays nothing ($s, $R, $+)
};

// -----------------------------------------------------------------------------
// Phase 1  --  audioStepTick()
// -----------------------------------------------------------------------------
struct AudioStepTickInputs {
    bool audioEnabled = false;  // cfg.system.enable_audio
    bool sleepMode = false;
    uint8_t configVolume = 0;   // cfg.audio.audioVolume, applied on initDriver
};

struct AudioStepTickActions {
    bool stopDriver = false;
    AudioStepStopReason stopReason = AUDIO_STEP_STOP_NONE;
    bool clearAudioActive = false;  // write robotState.audioActive = false
    bool drainQueue = false;        // disabled: receive+discard, end iteration
    bool initDriver = false;        // call driver->begin(state.currentVol)
};

AudioStepTickActions audioStepTick(AudioStepState& state, const AudioStepTickInputs& in);

// -----------------------------------------------------------------------------
// Phase 2  --  audioStepInitResult()
// -----------------------------------------------------------------------------
struct AudioStepInitResultActions {
    bool skipRestOfTick = false;  // failure (retry or give-up): delay, end iteration
    bool giveUp = false;          // ceiling hit: log + rx status NO_RESPONSE
    bool refreshBindings = false;  // success on a catalog-capable driver
    bool seedModuleState = false;  // success: getCachedState() -> RobotState
};

AudioStepInitResultActions audioStepInitResult(AudioStepState& state, bool beginOk,
                                               bool catalogCapable);

// -----------------------------------------------------------------------------
// Phase 3  --  audioStepCommand()
// -----------------------------------------------------------------------------
struct AudioStepCommandInputs {
    uint32_t nowMs = 0;
    bool sleepMode = false;
    bool catalogCapable = false;
    // The page of the bank a $8nn line names (AUDIO_DOLLAR_BANK), as the
    // fitted module reported it (audioCatalogBankPage(), include/
    // audio_driver.h), or '\0' where it reported no such bank. The answer
    // rather than the bank table: one byte that sits in this struct's padding,
    // because AudioTask's stack chain is recorded to the byte
    // (tools/task_stack_recipes.json) and a pointer and a count cost 16 B.
    char dollarBankPage = '\0';
    // AUDIO_CAP_MIXES. Also in the padding before the pointers, for the same reason.
    bool mixCapable = false;
    const AudioPlaybackConfig* playback = nullptr;
    const AudioNamedTracks* named = nullptr;
    const AudioBindingCache* bindings = nullptr;
    uint32_t randomValue = 0;  // category-play selection entropy
    // The driver holds a Background Track as playing (AudioDriver::
    // backgroundTrackHeld()), so a Track Stop leaves the droid's sound playing.
    bool backgroundTrackHeld = false;
    // The driver may still be playing a vocal (AudioDriver::vocalHeld()), so
    // a Background Track stop leaves the droid's sound playing.
    bool vocalHeld = false;
    // AUDIO_CMD_STEP_SOUND only: how many sounds the module reported on the
    // current sound's bank and page (audioCatalogPageCount(), include/
    // audio_driver.h), 0 where it reported none. The answer rather than the
    // bank table, for the reason dollarBankPage gives; it sits in this struct's
    // tail padding.
    uint16_t currentPageCount = 0;
};

struct AudioStepCommandActions {
    bool hasIntent = false;
    AudioPlaybackIntent intent{};   // execute on the driver; state effects applied
    bool queryStatus = false;       // run the on-demand module status query
    bool refreshCatalog = false;    // run the CHIRP catalog refresh
    bool refreshBindings = false;   // reload the CHIRP binding cache from NVS
    AudioStepIgnoreReason ignored = AUDIO_STEP_IGNORE_NONE;
};

AudioStepCommandActions audioStepCommand(AudioStepState& state,
                                         const AudioStepCommandInputs& in,
                                         const AudioCommand& cmd);

// -----------------------------------------------------------------------------
// Phase 4  --  audioStepIdle()
// -----------------------------------------------------------------------------
struct AudioStepIdleInputs {
    uint32_t nowMs = 0;
    bool sleepMode = false;
    bool catalogCapable = false;
    bool querySafePlayingCapable = false;  // AUDIO_CAP_QUERY_SAFE_PLAYING
    bool webOtaActive = false;
    uint8_t activeMood = 0;
    bool domeSeqActive = false;
    bool driving = false;  // the droid is driving (include/drive_motion.h)
    uint32_t randomValue = 0;
    const AudioPlaybackConfig* playback = nullptr;
    const AudioBindingCache* bindings = nullptr;
};

struct AudioStepIdleActions {
    bool hasIntent = false;
    AudioPlaybackIntent intent{};  // random-tick playback
    bool autoQuery = false;        // run the periodic module status poll
};

AudioStepIdleActions audioStepIdle(AudioStepState& state, const AudioStepIdleInputs& in);
