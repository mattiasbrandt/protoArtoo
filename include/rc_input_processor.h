// =============================================================================
// include/rc_input_processor.h
//
// RcInputProcessor  --  pure RC input orchestration.
// Owns debounce state, dome filter, sound edge detection.
// Input: channel snapshot + injected config. Output: backbone intent + trigger results.
// No FreeRTOS, no robotState, no hardware I/O  --  safe for native unit tests.
// =============================================================================
#pragma once

#include <stdint.h>

#include "dome_input_filter.h"
#include "drive_speed_preset.h"
#include "rc_action_dispatcher.h"
#include "rc_channel_mapper.h"
#include "rc_mapping.h"
#include "rc_puppet.h"
#include "trigger_debounce.h"

static constexpr size_t RC_TRIGGER_MAX = 11;

struct RcProcessorConfig {
    RcMappingConfig mapping;
    RcTriggerBinding triggers[RC_TRIGGER_MAX];
    size_t triggerCount;
    RcAudioCategorySnapshot categories;
    bool estopActive;
    bool currentSleepMode;
    SpeedPresetId currentSpeedPreset;
};

struct RcProcessorInput {
    RcChannelSnapshot channels;
    RcProcessorConfig config;
    uint32_t nowMs;
    uint32_t randomSeed;
    RcBindingSource sourceFilter;  // RC_BINDING_NONE = process all triggers
    // The SBUS watchdog's timeout as configured: how long a puppet string may
    // see no frame before it lets go of its Part (include/rc_puppet.h).
    uint32_t puppetGapMs;
};

struct RcProcessorOutput {
    RcControlIntent backbone;
    bool domeFiltered;
    int domeRawFiltered;  // raw SBUS value after filter (calibrate before dispatch)
    RcActionResult triggerResults[RC_TRIGGER_MAX];
    // Per trigger slot, whether its cue was pressed this frame: the press
    // edge alone, never a release, whatever the action did with it. A take
    // records a cue press here as a step to place (#442, include/take.h).
    bool triggerPressed[RC_TRIGGER_MAX];
    // Per trigger slot, what its puppet string asks for this frame (#442,
    // include/rc_puppet.h); `send` false for a slot that holds a cue, holds
    // nothing, or whose string has nothing new. The caller resolves the Part
    // to its Output and records an accepted target with rcPuppetSent().
    RcPuppetAsk puppet[RC_TRIGGER_MAX];
    bool stationaryLockedByTrigger;
    // Whether this frame sends a drive command. False when the drive bindings
    // read another receiver that is enabled for the mode: in dual_sbus a dome
    // receiver frame then leaves the drive alone instead of sending a zero
    // between the drive receiver's frames (#389).
    bool submitDrive;
    // The boot hold: the drive sticks have not yet been at centre since the
    // RC task started, so the drive output is zero (operator, 2026-10-09 on
    // #389). A HotRC trigger resting at an endpoint keeps it set.
    bool driveAwaitingCentre;
};

// How far from centre, in permille of full travel, both drive sticks may sit
// for the boot hold to release.
static constexpr int16_t RC_DRIVE_CENTRE_TOLERANCE_PERMILLE = 100;

struct RcInputProcessor {
    TriggerDebounceState triggerStates[RC_TRIGGER_MAX];
    RcPuppetState puppetStates[RC_TRIGGER_MAX];
    DomeInputFilter domeInputFilter;
    bool lastSoundPressed;
    bool stationaryLocked;
    // Set once both drive sticks have been at centre since init (the boot hold).
    bool driveCentreSeen;
};

void rcInputProcessorInit(RcInputProcessor* proc);
void rcInputProcessorTick(RcInputProcessor* proc, const RcProcessorInput& input,
                          RcProcessorOutput* out);
