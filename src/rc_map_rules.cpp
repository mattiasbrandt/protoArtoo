// =============================================================================
// src/rc_map_rules.cpp
//
// The RC Map's rules (ADR 0070). See include/rc_map_rules.h.
// =============================================================================

#include "rc_map_rules.h"

#include <string.h>

#include "rc_puppet.h"    // rcPuppetChannelCanMove()
#include "robot_state.h"  // RcInputMode

namespace {

constexpr RcRuleVerdict kHolds = {nullptr, false, ROBOT_ACTION_NONE};

RcRuleVerdict refuse(const char* sentence) {
    return {sentence, false, ROBOT_ACTION_NONE};
}

RcRuleVerdict refuseEntry(const char* sentence) {
    return {sentence, true, ROBOT_ACTION_NONE};
}

RcRuleVerdict refuseAxis(const char* sentence, RobotActionId axis) {
    return {sentence, true, axis};
}

// What a droid condition may fire, and the numbers it carries (#450). A number
// the request left out (kRcMapEntryKeep) is not judged: it keeps the stored
// one or takes its default.
RcRuleVerdict reactionRule(const RcMapEntry& entry) {
    if (robotActionIsAnalog(entry.action)) {
        return refuseEntry("a droid condition cannot drive an axis");
    }
    if (!robotActionValidForReaction(entry.action)) {
        return refuseEntry("action not allowed on a droid condition");
    }
    if (entry.threshold != kRcMapEntryKeep) {
        const uint16_t thresholdMax = rcReactionThresholdMax(entry.source);
        if (thresholdMax == 0 ? entry.threshold != 0
                              : (entry.threshold < 1 || entry.threshold > thresholdMax)) {
            return refuseEntry("threshold out of range");
        }
    }
    if (entry.quietS != kRcMapEntryKeep &&
        (entry.quietS < RC_REACTION_QUIET_MIN_S || entry.quietS > RC_REACTION_QUIET_MAX_S)) {
        return refuseEntry("quiet period out of range");
    }
    return kHolds;
}

// The rules one entry holds to on its own.
RcRuleVerdict entryRule(const RcMapEntry& entry) {
    if (!rcBindingChannelIsValid(entry.source, entry.channel)) {
        return refuse("channel out of range");
    }
    if (rcBindingSourceIsDroidCondition(entry.source)) {
        const RcRuleVerdict reaction = reactionRule(entry);
        if (!reaction.ok()) {
            return reaction;
        }
    }
    if (entry.action == DOME_ACTION_MARCDUINO_CMD && strncmp(entry.payload, ":SM", 3) == 0) {
        return refuseEntry(":SM is diagnostic only and cannot be saved as an RC binding");
    }
    // A puppet string moves a Part in proportion to a stick (#442): an SBUS
    // stick channel, since PWM input runs no string and CH17/CH18 are on/off.
    if (entry.action == SERVO_ACTION_PUPPET_PART &&
        !rcPuppetChannelCanMove(entry.source, entry.channel)) {
        return refuseEntry("a puppet string needs an SBUS stick channel (CH1-CH16)");
    }
    return kHolds;
}

const char* axisConflict(RobotActionId action) {
    switch (action) {
        case DRIVE_ACTION_SPEED:
            return "conflict: drive_speed mapped more than once";
        case DRIVE_ACTION_STEER:
            return "conflict: drive_steer mapped more than once";
        case DOME_ACTION_SPEED:
            return "conflict: dome_speed mapped more than once";
        default:
            return nullptr;
    }
}

// The rules between `next` and the entries before it.
RcRuleVerdict conflictRule(const RcMapEntry* prior, size_t count, const RcMapEntry& next) {
    for (size_t i = 0; i < count; ++i) {
        // One Part has one string: two sticks on one Part would fight over it.
        if (next.action == SERVO_ACTION_PUPPET_PART && prior[i].action == SERVO_ACTION_PUPPET_PART &&
            strcmp(prior[i].payload, next.payload) == 0) {
            return refuseEntry("conflict: a Part on two puppet strings");
        }
    }
    // One control, one job: a channel is a drive axis, a cue or a puppet
    // string, never two of them (#442).
    for (size_t i = 0; i < count; ++i) {
        if (prior[i].source == next.source && prior[i].channel == next.channel) {
            return refuseEntry("conflict: source+channel mapped more than once");
        }
    }
    if (const char* conflict = axisConflict(next.action)) {
        for (size_t i = 0; i < count; ++i) {
            if (prior[i].action == next.action) {
                return refuseEntry(conflict);
            }
        }
    }
    return kHolds;
}

}  // namespace

bool rcReceiverReads(RcBindingSource source, const RcReceiverSetup& setup) {
    switch (source) {
        case RC_BINDING_PWM:
            if (setup.mode != RC_INPUT_STANDARD_PWM) {
                return false;
            }
            for (bool enabled : setup.enableRc) {
                if (enabled) {
                    return true;
                }
            }
            return false;
        case RC_BINDING_SBUS1:
            // single_sbus: one receiver, SBUS1 whichever header it is on
            // (operator, 2026-10-09 on #389); useCh2 picks the header's enable.
            if (setup.mode == RC_INPUT_SINGLE_SBUS) {
                return setup.useCh2 ? setup.enableRc[1] : setup.enableRc[0];
            }
            return setup.mode == RC_INPUT_DUAL_SBUS && setup.enableRc[0];
        case RC_BINDING_SBUS2:
            return setup.mode == RC_INPUT_DUAL_SBUS && setup.enableRc[1];
        default:
            return false;
    }
}

RcRuleVerdict rcRuleMapAdd(const RcMapEntry* prior, size_t count, const RcMapEntry& next) {
    if (count >= kRcMapMaxEntries) {
        return refuse("conflict: map exceeds capacity");
    }
    const RcRuleVerdict own = entryRule(next);
    if (!own.ok()) {
        return own;
    }
    return conflictRule(prior, count, next);
}

RcRuleVerdict rcRuleDrive(const RcBindingConfig& speed, const RcBindingConfig& steer) {
    const bool speedBound = speed.source != RC_BINDING_NONE;
    const bool steerBound = steer.source != RC_BINDING_NONE;
    if (speedBound && steerBound && speed.source != steer.source) {
        return refuseAxis("drive speed and steer must be on the same receiver", DRIVE_ACTION_STEER);
    }
    static const char* const kDriveSbus2 = "drive reads SBUS1, the drive receiver";
    if (speedBound && speed.source == RC_BINDING_SBUS2) {
        return refuseAxis(kDriveSbus2, DRIVE_ACTION_SPEED);
    }
    if (steerBound && steer.source == RC_BINDING_SBUS2) {
        return refuseAxis(kDriveSbus2, DRIVE_ACTION_STEER);
    }
    return kHolds;
}

RcRuleVerdict rcRuleAxisCalibration(const RcBindingConfig& binding) {
    if (!(binding.min < binding.center && binding.center < binding.max)) {
        return refuse("calibration needs min < center < max");
    }
    // The dead zone must leave travel on both sides of the centre, or that
    // side of the stick maps to nothing (Codex review, #389).
    if (binding.deadband >= (uint16_t)(binding.center - binding.min) ||
        binding.deadband >= (uint16_t)(binding.max - binding.center)) {
        return refuse("calibration leaves no travel past the deadband");
    }
    return kHolds;
}
