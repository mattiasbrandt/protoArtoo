// =============================================================================
// src/rc_input_processor.cpp
//
// RcInputProcessor implementation — see include/rc_input_processor.h.
// =============================================================================

#include "rc_input_processor.h"

static constexpr uint32_t kOneShotEdgeDebounceMs = 120;
static constexpr uint8_t kSwitchEdgeConfirmFrames = 2;

void rcInputProcessorInit(RcInputProcessor* proc) {
    if (proc == nullptr) {
        return;
    }
    for (size_t i = 0; i < RC_TRIGGER_MAX; ++i) {
        proc->triggerStates[i] = {};
    }
    proc->domeInputFilter = {};
    proc->lastSoundPressed = false;
    proc->stationaryLocked = false;
}

void rcInputProcessorTick(RcInputProcessor* proc, const RcProcessorInput& input,
                          RcProcessorOutput* out) {
    *out = {};

    if (proc == nullptr) {
        return;
    }

    RcProcessorOutput& output = *out;

    // Copy mapping config and preserve sound edge state
    RcMappingConfig localMapping = input.config.mapping;
    bool prevSoundPressed = proc->lastSoundPressed;
    localMapping.prevSoundPressed = prevSoundPressed;

    // Map channel snapshot to control intent (pure function)
    RcControlIntent intent = rcMapChannels(input.channels, localMapping);

    // Update sound state for next iteration
    proc->lastSoundPressed = intent.soundPressed;

    // Copy backbone intent to output
    output.backbone = intent;
    output.stationaryLockedByTrigger = proc->stationaryLocked;

    // Process dome filter on raw SBUS channel value
    RcBindingConfig domeBinding = input.config.mapping.domeSpeed;
    if (input.config.mapping.enableDome && domeBinding.source != RC_BINDING_NONE) {
        int raw = input.channels.channels[domeBinding.channel - 1];
        DomeInputFilterResult filterResult =
            domeInputFilterUpdate(&proc->domeInputFilter, raw, (int)domeBinding.center, 140, 90,
                                  kSwitchEdgeConfirmFrames);
        output.domeFiltered = filterResult.accepted;
        output.domeRawFiltered = raw;
    } else {
        output.domeFiltered = false;
        output.domeRawFiltered = 0;
    }

    // Process Tier 2 trigger bindings
    for (size_t i = 0; i < input.config.triggerCount && i < RC_TRIGGER_MAX; ++i) {
        RcTriggerBinding binding = input.config.triggers[i];

        // Initialize result to no-action state
        output.triggerResults[i] = {};
        output.triggerResults[i].servoIndex = -1;

        if (binding.target == ROBOT_ACTION_NONE || binding.source == RC_BINDING_NONE) {
            continue;
        }

        // Filter triggers by source (SBUS1, SBUS2, PWM)
        if (input.sourceFilter != RC_BINDING_NONE && binding.source != input.sourceFilter) {
            continue;
        }

        // Get raw channel value (0-indexed)
        int raw = input.channels.channels[binding.channel - 1];

        // Build backbone binding config from trigger binding
        RcBindingConfig backbone = makeRcBindingConfig(
            binding.source, binding.channel, binding.min, binding.center, binding.max,
            binding.deadband, binding.reverse);

        TriggerDebounceResult dr = {};

        if (rcBindingIsDigital(backbone)) {
            bool pressed = (raw >= 992);
            dr = triggerDebounceDigital(&proc->triggerStates[i], pressed);
        } else {
            dr = triggerDebounceAnalog(&proc->triggerStates[i], binding, raw, input.nowMs,
                                       kSwitchEdgeConfirmFrames, kOneShotEdgeDebounceMs);
        }

        if (dr.fired) {
            // Build action payload
            RcActionPayload ap = {};
            ap.target = binding.target;
            ap.bindingPayload = binding.marcduinoPayload;
            ap.pressed = dr.pressed;
            ap.randomSeed = input.randomSeed;
            ap.categories = input.config.categories;
            ap.estopActive = input.config.estopActive;
            ap.currentSleepMode = input.config.currentSleepMode;
            ap.currentSpeedPreset = input.config.currentSpeedPreset;

            // Dispatch action
            output.triggerResults[i] = rcDispatchAction(ap);

            // Update stationary lock if action requested it
            if (output.triggerResults[i].setStationary) {
                proc->stationaryLocked = output.triggerResults[i].newStationaryMode;
                output.stationaryLockedByTrigger = proc->stationaryLocked;
            }
        }
    }
}
