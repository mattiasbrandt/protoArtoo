// =============================================================================
// src/rc_map_store.cpp
//
// The RC Map Store's placement: where a save puts each binding of an RC Map
// (ADR 0070, amended 2026-10-10, #490). See include/rc_map_store.h.
//
// Pure: no FreeRTOS call, no config cache, no NVS. The save's Apply Core
// (rcMapApply(), src/web/api_rc_map_apply.cpp) calls it on its Working
// Snapshot, after the RC Map's rules have taken the map.
// =============================================================================

#include "rc_map_store.h"

#include <stdio.h>
#include <string.h>

namespace {

bool triggerSlotIsFree(const RcTriggerBinding& binding) {
    return binding.source == RC_BINDING_NONE || binding.target == ROBOT_ACTION_NONE;
}

// Whether a stored trigger binding is the one a save is placing: the same
// control firing the same thing. Calibration and a Reaction's numbers may
// differ; it is still that binding. The store's own placement rule, not an
// identity a reader keeps state by.
bool rcMapSameTrigger(const RcTriggerBinding& stored, const RcTriggerBinding& placing) {
    return !triggerSlotIsFree(stored) && stored.source == placing.source &&
           stored.channel == placing.channel && stored.target == placing.target &&
           strncmp(stored.marcduinoPayload, placing.marcduinoPayload,
                   sizeof(stored.marcduinoPayload)) == 0;
}

// The calibration the droid already holds for this RC Channel, wherever it
// is bound: an axis, a legacy arm or sound binding, or a trigger place. The
// first match wins, in the order PWM group then SBUS group (each group's axes,
// then its legacy arm1, arm2 and sound), then the trigger places.
bool rcMapTryReuseCalibration(const SystemConfig& held, RcBindingSource source, uint8_t channel,
                              uint16_t* min, uint16_t* center, uint16_t* max,
                              uint16_t* deadband, bool* reverse) {
    if (min == nullptr || center == nullptr || max == nullptr || deadband == nullptr ||
        reverse == nullptr) {
        return false;
    }

    // By pointer, never a copy: this runs on the HTTP server task under two
    // ConfigSnapshots already.
    const RcBindingConfig* const legacy[][3] = {
        {&held.rc_pwm_arm1, &held.rc_pwm_arm2, &held.rc_pwm_audio},
        {&held.rc_sbus_arm1, &held.rc_sbus_arm2, &held.rc_sbus_audio},
    };
    const RcMapAxisGroup groups[] = {RcMapAxisGroup::Pwm, RcMapAxisGroup::Sbus};
    const RcMapAxis axes[] = {RcMapAxis::DriveSpeed, RcMapAxis::DriveSteer, RcMapAxis::DomeSpeed};
    static_assert(sizeof(legacy) / sizeof(legacy[0]) == sizeof(groups) / sizeof(groups[0]),
                  "one row of legacy bindings per axis group");
    for (size_t g = 0; g < sizeof(groups) / sizeof(groups[0]); ++g) {
        const RcBindingConfig* candidates[RC_MAP_AXES_PER_GROUP + 3];
        size_t n = 0;
        for (RcMapAxis axis : axes) {
            candidates[n++] = &rcMapAxisAt(held, groups[g], axis);
        }
        for (const RcBindingConfig* binding : legacy[g]) {
            candidates[n++] = binding;
        }
        for (size_t i = 0; i < n; ++i) {
            const RcBindingConfig& binding = *candidates[i];
            if (binding.source == source && binding.channel == channel &&
                rcBindingChannelIsValid(binding.source, binding.channel)) {
                *min = binding.min;
                *center = binding.center;
                *max = binding.max;
                *deadband = binding.deadband;
                *reverse = binding.reverse;
                return true;
            }
        }
    }

    for (const RcTriggerPlace& place : RC_MAP_TRIGGER_PLACES) {
        const RcTriggerBinding& binding = held.*place.place;
        if (binding.source == source && binding.channel == channel &&
            rcBindingChannelIsValid(binding.source, binding.channel)) {
            *min = binding.min;
            *center = binding.center;
            *max = binding.max;
            *deadband = binding.deadband;
            *reverse = binding.reverse;
            return true;
        }
    }

    return false;
}

bool rcMapBuildBackboneBinding(RobotActionId axis, RcBindingSource source, uint8_t channel,
                               const SystemConfig& held, RcBindingConfig* out) {
    if (out == nullptr || !rcBindingChannelIsValid(source, channel)) {
        return false;
    }

    // An axis is a radio channel's and nothing else's. A droid condition has a
    // legal channel of its own, so it is refused here by what it is.
    if (rcBindingSourceIsDroidCondition(source)) {
        return false;
    }

    RcBindingConfig binding =
        (source == RC_BINDING_PWM) ? defaultPwmBinding(channel) : defaultSbusBinding(source, channel);

    uint16_t min = binding.min;
    uint16_t center = binding.center;
    uint16_t max = binding.max;
    uint16_t deadband = binding.deadband;
    bool reverse = binding.reverse;
    if (rcMapTryReuseCalibration(held, source, channel, &min, &center, &max, &deadband,
                                  &reverse)) {
        RcBindingConfig reused =
            makeRcBindingConfig(source, channel, min, center, max, deadband, reverse);
        // Only a calibration the RC Map's rules take is carried over: one they
        // refuse would refuse the whole save, so mapping the axis again could
        // never mend it (ADR 0070). It starts from the defaults instead.
        if (rcBindingIsValid(reused) && rcRuleAxisCalibration(axis, reused).ok()) {
            binding = reused;
        }
    }

    *out = binding;
    return true;
}

bool rcMapBuildTriggerBinding(const RcMapEntry& entry, const SystemConfig& held,
                              RcTriggerBinding* out) {
    if (out == nullptr || !rcBindingChannelIsValid(entry.source, entry.channel)) {
        return false;
    }

    // A Reaction: its threshold and quiet period as the request gave them,
    // else as the stored Reaction on this condition holds them, else the
    // defaults. No calibration to reuse - a droid condition has none.
    if (rcBindingSourceIsDroidCondition(entry.source)) {
        uint16_t threshold = rcReactionThresholdDefault(entry.source);
        uint16_t quietS = RC_REACTION_QUIET_DEFAULT_S;
        // By place, never a copy of the slots: this runs on the HTTP server
        // task under two ConfigSnapshots already.
        for (const RcTriggerPlace& place : RC_MAP_TRIGGER_PLACES) {
            const RcTriggerBinding& slot = held.*place.place;
            if (slot.source == entry.source && slot.channel == entry.channel) {
                threshold = rcReactionThreshold(slot);
                quietS = rcReactionQuietS(slot);
                break;
            }
        }
        if (entry.threshold != kRcMapEntryKeep) threshold = entry.threshold;
        if (entry.quietS != kRcMapEntryKeep) quietS = entry.quietS;
        *out = makeRcReactionBinding(entry.source, entry.channel, entry.action, entry.payload,
                                     threshold, quietS);
        return rcTriggerBindingIsValid(*out);
    }

    const RcBindingConfig defaults = (entry.source == RC_BINDING_PWM)
                                         ? defaultPwmBinding(entry.channel)
                                         : defaultSbusBinding(entry.source, entry.channel);
    uint16_t min = defaults.min;
    uint16_t center = defaults.center;
    uint16_t max = defaults.max;
    uint16_t deadband = 0;
    bool reverse = rcTriggerDefaultReverse(entry.source, entry.channel);
    rcMapTryReuseCalibration(held, entry.source, entry.channel, &min, &center, &max, &deadband,
                              &reverse);

    *out = makeRcTriggerBinding(entry.source, entry.channel, entry.action, entry.payload, min,
                                center, max, deadband, reverse);
    return rcTriggerBindingIsValid(*out);
}

// The first free place of `sys`, in the table's order, among the places a
// toggle owns (`owned`) or among the open ones.
RcTriggerBinding* firstFreePlace(SystemConfig* sys, bool owned) {
    for (const RcTriggerPlace& place : RC_MAP_TRIGGER_PLACES) {
        RcTriggerBinding& slot = sys->*place.place;
        if ((place.ownAction != ROBOT_ACTION_NONE) == owned && triggerSlotIsFree(slot)) {
            return &slot;
        }
    }
    return nullptr;
}

}  // namespace

bool rcMapAxisOfAction(RobotActionId action, RcMapAxis* axis) {
    RcMapAxis found;
    switch (action) {
        case DRIVE_ACTION_SPEED: found = RcMapAxis::DriveSpeed; break;
        case DRIVE_ACTION_STEER: found = RcMapAxis::DriveSteer; break;
        case DOME_ACTION_SPEED: found = RcMapAxis::DomeSpeed; break;
        default: return false;
    }
    if (axis != nullptr) {
        *axis = found;
    }
    return true;
}

void rcMapStoreClear(SystemConfig* sys) {
    if (sys == nullptr) {
        return;
    }
    for (const RcAxisPlace& place : RC_MAP_AXIS_PLACES) {
        sys->*place.place = disabledRcBinding();
    }
    for (const RcTriggerPlace& place : RC_MAP_TRIGGER_PLACES) {
        sys->*place.place = disabledRcTriggerBinding();
    }
}

void rcMapStorePlaceAxis(SystemConfig* sys, RcMapAxis axis, const RcBindingConfig& binding) {
    if (sys == nullptr) {
        return;
    }
    for (const RcAxisPlace& place : RC_MAP_AXIS_PLACES) {
        if (place.axis == axis) {
            sys->*place.place = binding;
        }
    }
}

const RcBindingConfig& rcMapStorePlacedAxis(const SystemConfig& sys, RcMapAxis axis) {
    return rcMapAxisAt(sys, RcMapAxisGroup::Sbus, axis);
}

bool rcMapStorePlace(const RcMapEntry& entry, const SystemConfig& held, SystemConfig* sys,
                     char* error, size_t errorSize, ApplyRefusal* refusal) {
    // Each refusal below states its field and reason here too, so a caller
    // never reads them back out of the sentence (include/api_apply_refusal.h).
    ApplyRefusal unused;
    ApplyRefusal* said = refusal != nullptr ? refusal : &unused;
    if (sys == nullptr || error == nullptr || errorSize == 0) {
        return false;
    }

    RcMapAxis axis;
    if (rcMapAxisOfAction(entry.action, &axis)) {
        RcBindingConfig backbone = disabledRcBinding();
        if (!rcMapBuildBackboneBinding(entry.action, entry.source, entry.channel, held, &backbone)) {
            snprintf(error, errorSize, "invalid backbone binding");
            applyRefusalSet(said, ApplyRefusalReason::OutOfRange, "map.action");
            return false;
        }
        rcMapStorePlaceAxis(sys, axis, backbone);
        return true;
    }

    // A trigger binding takes any free place of the eleven (ADR 0070, amended
    // 2026-10-10): every reader goes by the binding's own target, never by the
    // place it sits in. The RC Map's rules hold the map to eleven and each
    // toggle to one RC Channel (rcRuleMapAdd()), so a place is always free here.
    RcTriggerBinding trigger = disabledRcTriggerBinding();
    if (!rcMapBuildTriggerBinding(entry, held, &trigger)) {
        snprintf(error, errorSize, "invalid trigger binding");
        applyRefusalSet(said, ApplyRefusalReason::OutOfRange, "map.action");
        return false;
    }

    // A binding the droid already held stays in its place: every reader
    // keeps a place's state while its change stamp stays put, so an
    // unchanged binding kept in its place keeps its state, where one moved
    // would start afresh (Codex review, #488; #490).
    for (const RcTriggerPlace& place : RC_MAP_TRIGGER_PLACES) {
        RcTriggerBinding& now = sys->*place.place;
        if (triggerSlotIsFree(now) && rcMapSameTrigger(held.*place.place, trigger)) {
            now = trigger;
            return true;
        }
    }

    // A toggle on a radio takes the place it owns when that is free, so a map
    // that fits the older layout is stored as it always was.
    if (entry.action != ROBOT_ACTION_NONE && !rcBindingSourceIsDroidCondition(entry.source)) {
        for (const RcTriggerPlace& place : RC_MAP_TRIGGER_PLACES) {
            RcTriggerBinding& slot = sys->*place.place;
            if (place.ownAction == entry.action && triggerSlotIsFree(slot)) {
                slot = trigger;
                return true;
            }
        }
    }

    // Anything else takes the open places first (rc_aud, rc_free0..3, in the
    // table's order), then a place a toggle owns that nothing has claimed
    // (rc_arm1, rc_arm2, rc_aux1..3, rc_opmode).
    RcTriggerBinding* spare = firstFreePlace(sys, false);
    if (spare == nullptr) {
        spare = firstFreePlace(sys, true);
    }
    if (spare != nullptr) {
        *spare = trigger;
        return true;
    }

    // Past what rcRuleMapAdd() lets through: the same refusal it gives.
    snprintf(error, errorSize, "conflict: map exceeds capacity");
    applyRefusalSet(said, ApplyRefusalReason::Conflict, "map");
    return false;
}
