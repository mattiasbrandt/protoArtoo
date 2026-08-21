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
#include "trigger_debounce.h"

static constexpr size_t RC_TRIGGER_MAX = 11;

struct RcProcessorConfig {
    RcMappingConfig mapping;
    RcTriggerBinding triggers[RC_TRIGGER_MAX];
    size_t triggerCount;
    RcSoundCategorySnapshot categories;
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
};

struct RcProcessorOutput {
    RcControlIntent backbone;
    bool domeFiltered;
    int domeRawFiltered;  // raw SBUS value after filter (calibrate before dispatch)
    RcActionResult triggerResults[RC_TRIGGER_MAX];
    bool stationaryLockedByTrigger;
};

struct RcInputProcessor {
    TriggerDebounceState triggerStates[RC_TRIGGER_MAX];
    DomeInputFilter domeInputFilter;
    bool lastSoundPressed;
    bool stationaryLocked;
};

void rcInputProcessorInit(RcInputProcessor* proc);
void rcInputProcessorTick(RcInputProcessor* proc, const RcProcessorInput& input,
                          RcProcessorOutput* out);
