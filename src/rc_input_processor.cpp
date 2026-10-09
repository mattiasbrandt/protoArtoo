// =============================================================================
// src/rc_input_processor.cpp
//
// RcInputProcessor implementation  --  see include/rc_input_processor.h.
// =============================================================================

#include "rc_input_processor.h"

#include "rc_map_rules.h"  // rcRuleStoredDrive(), rcRuleStoredAxis(), rcRuleStoredCue()

static constexpr uint32_t kOneShotEdgeDebounceMs = 120;
static constexpr uint8_t kSwitchEdgeConfirmFrames = 2;

void rcInputProcessorInit(RcInputProcessor* proc) {
    if (proc == nullptr) {
        return;
    }
    for (size_t i = 0; i < RC_TRIGGER_MAX; ++i) {
        proc->triggerStates[i] = {};
        proc->puppetStates[i] = {};
    }
    proc->domeInputFilter = {};
    proc->lastSoundPressed = false;
    proc->stationaryLocked = false;
    proc->driveCentreSeen = false;
}

// Whether a binding reads a receiver other than this frame's, one the mode
// enables: the frame then has nothing to say about it.
static bool readsAnotherReceiver(const RcBindingConfig& binding, const RcChannelSnapshot& snap,
                                 const RcMappingConfig& mapping) {
    if (snap.source == RC_BINDING_NONE || binding.source == snap.source) {
        return false;
    }
    RcChannelSnapshot other = snap;
    other.source = binding.source;
    return rcMapBindingReadsSnapshot(binding, other, mapping);
}

// Whether both drive bindings, on one receiver, read some frame this mode
// takes - this one or the drive receiver's.
static bool driveCanBeRead(const RcChannelSnapshot& snap, const RcMappingConfig& mapping) {
    if (mapping.driveSpeed.source == RC_BINDING_NONE ||
        !rcRuleStoredDrive(mapping.driveSpeed, mapping.driveSteer, snap.mode).ok()) {
        return false;
    }
    RcChannelSnapshot probe = snap;
    probe.source = mapping.driveSpeed.source;
    return rcMapBindingReadsSnapshot(mapping.driveSpeed, probe, mapping) &&
           rcMapBindingReadsSnapshot(mapping.driveSteer, probe, mapping);
}

// The one-receiver test here is the frame-routing question, not the RC Map's
// drive rule (rcRuleDrive()): a pair the rule refuses still reads another
// receiver's frames as "not mine", so this frame leaves the drive alone.
static bool driveReadsAnotherReceiver(const RcChannelSnapshot& snap, const RcMappingConfig& mapping) {
    return mapping.driveSteer.source == mapping.driveSpeed.source &&
           readsAnotherReceiver(mapping.driveSpeed, snap, mapping) &&
           readsAnotherReceiver(mapping.driveSteer, snap, mapping);
}

// Whether one drive stick sits within the tolerance of its calibrated centre,
// judged on the raw position against the binding's own travel on that side:
// before the deadband, the reverse and the speed limit, so none of them can
// widen the hold or release it on a deflected stick (#389).
static bool axisAtCentre(const RcBindingConfig& binding, const RcChannelSnapshot& snap) {
    if (binding.channel < 1 || binding.channel > 18) {
        return false;
    }
    const int32_t delta = (int32_t)snap.channels[binding.channel - 1] - (int32_t)binding.center;
    const int32_t half = delta > 0 ? (int32_t)binding.max - (int32_t)binding.center
                                   : (int32_t)binding.center - (int32_t)binding.min;
    if (half <= 0) {
        return false;
    }
    const int32_t distance = delta < 0 ? -delta : delta;
    return distance * 1000 <= half * RC_DRIVE_CENTRE_TOLERANCE_PERMILLE;
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

    // Two stored bindings on one control (a save cut short by a power loss
    // can leave them) are both left still, as a save would refuse them (ADR
    // 0070): an axis among them reads as unbound, so the drive sends zero.
    const RcStoredMap stored = {localMapping.driveSpeed, localMapping.driveSteer,
                                localMapping.domeSpeed, input.config.triggers,
                                input.config.triggerCount};
    const uint32_t conflicts = rcStoredMapConflicts(stored, input.channels.mode);
    if ((conflicts & kRcStoredDriveSpeedBit) != 0) {
        localMapping.driveSpeed = disabledRcBinding();
    }
    if ((conflicts & kRcStoredDriveSteerBit) != 0) {
        localMapping.driveSteer = disabledRcBinding();
    }
    if ((conflicts & kRcStoredDomeSpeedBit) != 0) {
        localMapping.domeSpeed = disabledRcBinding();
    }

    // Map channel snapshot to control intent (pure function)
    RcControlIntent intent = rcMapChannels(input.channels, localMapping);

    // Update sound state for next iteration, from the sound binding's own
    // receiver only: another receiver's frame would read as a release and the
    // next own frame as a fresh press, re-firing at the frame rate (#389).
    if (intent.soundActive || !readsAnotherReceiver(localMapping.sound, input.channels, localMapping)) {
        proc->lastSoundPressed = intent.soundPressed;
    }

    // The boot hold: until both drive sticks have been at centre once, the
    // drive output is zero. A trigger resting at an endpoint (the HotRC
    // DS-650's CH2 at factory stroke) never releases it.
    if (intent.driveActive && !proc->driveCentreSeen) {
        if (axisAtCentre(localMapping.driveSpeed, input.channels) &&
            axisAtCentre(localMapping.driveSteer, input.channels)) {
            proc->driveCentreSeen = true;
        } else {
            intent.driveSpeed = 0;
            intent.driveSteer = 0;
        }
    }
    // Said only for a drive the mode can read at all: a droid with no drive
    // bound (or bound where nothing arrives) has no sticks to centre.
    output.driveAwaitingCentre = !proc->driveCentreSeen && driveCanBeRead(input.channels, localMapping);
    output.submitDrive = intent.driveActive || !driveReadsAnotherReceiver(input.channels, localMapping);

    // Copy backbone intent to output
    output.backbone = intent;
    output.stationaryLockedByTrigger = proc->stationaryLocked;

    // Process dome filter on raw SBUS channel value, from the dome binding's
    // own receiver only: on another receiver's frame (or an SBUS2 binding in
    // single_sbus) the filter keeps its state and nothing is sent (#389).
    RcBindingConfig domeBinding = localMapping.domeSpeed;
    if (localMapping.enableDome &&
        rcRuleStoredAxis(DOME_ACTION_SPEED, domeBinding, input.channels.mode).ok() &&
        rcMapBindingReadsSnapshot(domeBinding, input.channels, localMapping)) {
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

        // A Reaction is never evaluated on an RC frame, whatever the filter
        // says: its condition is the droid's own state, read by ReactionTask
        // whether or not a radio is fitted (#450).
        if (rcBindingSourceIsDroidCondition(binding.source)) {
            continue;
        }

        // Filter triggers by source (SBUS1, SBUS2, PWM)
        if (input.sourceFilter != RC_BINDING_NONE && binding.source != input.sourceFilter) {
            continue;
        }

        // A stored binding the RC Map's rules refuse, on its own or in a
        // conflict, is not read (ADR 0070): a save would not take it, so it
        // fires nothing.
        if (!rcRuleStoredCue(binding, input.channels.mode).ok() ||
            (conflicts & rcStoredCueBit(i)) != 0) {
            continue;
        }

        // Get raw channel value (0-indexed)
        int raw = input.channels.channels[binding.channel - 1];

        // A puppet string is a stick, not a press: it is never debounced and
        // never dispatched as an action, so it cannot fire a cue, and it reads
        // only its own slot - no drive or dome-speed binding (#442). A latched
        // estop takes its leave to move, so it lets go of its Part and picks
        // it up afresh once the estop clears and the stick moves.
        if (binding.target == SERVO_ACTION_PUPPET_PART) {
            output.puppet[i] = rcPuppetStep(&proc->puppetStates[i], binding,
                                            rcPuppetPermille(raw, binding), input.nowMs,
                                            input.puppetGapMs, !input.config.estopActive);
            continue;
        }

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
            output.triggerPressed[i] = dr.pressed;

            // Update stationary lock if action requested it
            if (output.triggerResults[i].setStationary) {
                proc->stationaryLocked = output.triggerResults[i].newStationaryMode;
                output.stationaryLockedByTrigger = proc->stationaryLocked;
            }
        }
    }
}
