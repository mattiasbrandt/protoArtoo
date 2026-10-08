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
};

struct RcInputProcessor {
    TriggerDebounceState triggerStates[RC_TRIGGER_MAX];
    RcPuppetState puppetStates[RC_TRIGGER_MAX];
    DomeInputFilter domeInputFilter;
    bool lastSoundPressed;
    bool stationaryLocked;
};

void rcInputProcessorInit(RcInputProcessor* proc);
void rcInputProcessorTick(RcInputProcessor* proc, const RcProcessorInput& input,
                          RcProcessorOutput* out);
