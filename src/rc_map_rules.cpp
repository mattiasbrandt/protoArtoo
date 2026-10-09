// =============================================================================
// src/rc_map_rules.cpp
//
// The RC Map's rules (ADR 0070). See include/rc_map_rules.h.
// =============================================================================

#include "rc_map_rules.h"

#include <stdio.h>
#include <string.h>

#include "rc_puppet.h"    // rcPuppetChannelCanMove()
#include "robot_state.h"  // RcInputMode

namespace {

constexpr RcRuleVerdict kHolds = {nullptr, nullptr, nullptr, 0, 0, ROBOT_ACTION_NONE,
                                  RcRuleReason::kHolds, false};

// The request fields a refusal names (docs/api.md "POST /api/rc/map").
constexpr const char* kFieldMap = "map";
constexpr const char* kFieldSource = "map.source";
constexpr const char* kFieldChannel = "map.channel";
constexpr const char* kFieldAction = "map.action";
constexpr const char* kFieldPayload = "map.payload";
constexpr const char* kFieldThreshold = "map.threshold";
constexpr const char* kFieldQuietS = "map.quietS";

// A refusal. `aboutEntry` echoes the entry; the accepts are a range lo..hi
// (hi 0 for none) or the words given.
RcRuleVerdict refusal(const char* sentence, bool aboutEntry, const char* field, RcRuleReason reason,
                      uint16_t lo = 0, uint16_t hi = 0, const char* words = nullptr) {
    return {sentence, field, words, lo, hi, ROBOT_ACTION_NONE, reason, aboutEntry};
}

RcRuleVerdict onAxis(RcRuleVerdict verdict, RobotActionId axis) {
    verdict.axis = axis;
    verdict.aboutEntry = true;
    return verdict;
}

// The RC Channels a source has, as a range: what a channel refusal accepts.
RcRuleVerdict channelOutOfRange(RcBindingSource source) {
    uint16_t lo = 1;
    uint16_t hi = 0;
    switch (source) {
        case RC_BINDING_PWM:
            hi = 6;
            break;
        case RC_BINDING_SBUS1:
        case RC_BINDING_SBUS2:
            hi = 18;
            break;
        case RC_BINDING_DROID_WHEEL_SPEED:
        case RC_BINDING_DROID_WHEEL_AMPS:
            hi = RC_REACTION_WHEEL_RIGHT;
            break;
        default:
            lo = RC_REACTION_CHANNEL;
            hi = RC_REACTION_CHANNEL;
            break;
    }
    return refusal("channel out of range", false, kFieldChannel, RcRuleReason::kOutOfRange, lo, hi);
}

// What a droid condition may fire, and the numbers it carries (#450). A number
// the request left out (kRcMapEntryKeep) is not judged: it keeps the stored
// one or takes its default.
RcRuleVerdict reactionRule(const RcMapEntry& entry) {
    if (robotActionIsAnalog(entry.action)) {
        return refusal("a droid condition cannot drive an axis", true, kFieldAction,
                       RcRuleReason::kOutOfRange);
    }
    if (!robotActionValidForReaction(entry.action)) {
        return refusal("action not allowed on a droid condition", true, kFieldAction,
                       RcRuleReason::kOutOfRange);
    }
    if (entry.threshold != kRcMapEntryKeep) {
        const uint16_t thresholdMax = rcReactionThresholdMax(entry.source);
        if (thresholdMax == 0 ? entry.threshold != 0
                              : (entry.threshold < 1 || entry.threshold > thresholdMax)) {
            // A condition with no threshold takes 0 alone: "0..0".
            const uint16_t lo = thresholdMax == 0 ? 0 : 1;
            RcRuleVerdict verdict = refusal("threshold out of range", true, kFieldThreshold,
                                            RcRuleReason::kOutOfRange, lo, thresholdMax);
            if (thresholdMax == 0) {
                verdict.acceptsWords = "0";
            }
            return verdict;
        }
    }
    if (entry.quietS != kRcMapEntryKeep &&
        (entry.quietS < RC_REACTION_QUIET_MIN_S || entry.quietS > RC_REACTION_QUIET_MAX_S)) {
        return refusal("quiet period out of range", true, kFieldQuietS, RcRuleReason::kOutOfRange,
                       RC_REACTION_QUIET_MIN_S, RC_REACTION_QUIET_MAX_S);
    }
    return kHolds;
}

bool isAxis(RobotActionId action) {
    return action == DRIVE_ACTION_SPEED || action == DRIVE_ACTION_STEER ||
           action == DOME_ACTION_SPEED;
}

// Whether the receiver type reads a source at all, whatever its RC channel
// enables say: an enable is a wire, and a map is not refused for one that is
// off today.
bool receiverTypeReads(RcBindingSource source, RcInputMode type) {
    RcReceiverSetup setup = {};
    setup.mode = type;
    for (bool& enabled : setup.enableRc) {
        enabled = true;
    }
    return rcReceiverReads(source, setup);
}

// The receivers a type reads, as a source refusal's accepts; null for none.
const char* receiversOfType(RcInputMode type) {
    switch (type) {
        case RC_INPUT_STANDARD_PWM:
            return "pwm";
        case RC_INPUT_SINGLE_SBUS:
            return "sbus1";
        case RC_INPUT_DUAL_SBUS:
            return "sbus1,sbus2";
        default:
            return nullptr;
    }
}

// The rules one entry holds to on its own, against the receiver type the map
// is for.
RcRuleVerdict entryRule(const RcMapEntry& entry, RcInputMode type) {
    if (!rcBindingChannelIsValid(entry.source, entry.channel)) {
        return channelOutOfRange(entry.source);
    }
    if (rcBindingSourceIsDroidCondition(entry.source)) {
        const RcRuleVerdict reaction = reactionRule(entry);
        if (!reaction.ok()) {
            return reaction;
        }
    } else if (!receiverTypeReads(entry.source, type)) {
        // single_sbus reads SBUS1 only, standard_pwm only PWM, ELRS and
        // not-fitted nothing: a binding elsewhere is never read (ADR 0070).
        return refusal("the RC Receiver type does not read this source", true, kFieldSource,
                       RcRuleReason::kOutOfRange, 0, 0, receiversOfType(type));
    }
    // A PWM receiver carries the drive and dome axes only: the RC Map's cue
    // slots are not read on PWM (operator, 2026-10-09 on #486).
    if (entry.source == RC_BINDING_PWM && !isAxis(entry.action)) {
        return refusal("PWM carries only the drive and dome axes", true, kFieldAction,
                       RcRuleReason::kOutOfRange, 0, 0, "drive_speed,drive_steer,dome_speed");
    }
    // An axis reads a stick, never CH17/CH18, which are on/off.
    if (isAxis(entry.action) && !rcBindingSourceIsDroidCondition(entry.source)) {
        const RcBindingConfig channel =
            makeRcBindingConfig(entry.source, entry.channel, 0, 0, 0, 0, false);
        if (!rcBindingSupportsAnalog(channel)) {
            return refusal("an axis needs a stick channel", true, kFieldChannel,
                           RcRuleReason::kOutOfRange, 1, 16);
        }
    }
    if (entry.action == DOME_ACTION_MARCDUINO_CMD && strncmp(entry.payload, ":SM", 3) == 0) {
        return refusal(":SM is diagnostic only and cannot be saved as an RC binding", true,
                       kFieldPayload, RcRuleReason::kOutOfRange);
    }
    // A puppet string moves a Part in proportion to a stick (#442): an SBUS
    // stick channel, since CH17/CH18 are on/off.
    if (entry.action == SERVO_ACTION_PUPPET_PART &&
        !rcPuppetChannelCanMove(entry.source, entry.channel)) {
        return refusal("a puppet string needs an SBUS stick channel (CH1-CH16)", true, kFieldChannel,
                       RcRuleReason::kOutOfRange, 1, 16);
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
            return refusal("conflict: a Part on two puppet strings", true, kFieldPayload,
                           RcRuleReason::kConflict);
        }
    }
    // One control, one job: a channel is a drive axis, a cue or a puppet
    // string, never two of them (#442).
    for (size_t i = 0; i < count; ++i) {
        if (prior[i].source == next.source && prior[i].channel == next.channel) {
            return refusal("conflict: source+channel mapped more than once", true, kFieldChannel,
                           RcRuleReason::kConflict);
        }
    }
    if (const char* conflict = axisConflict(next.action)) {
        for (size_t i = 0; i < count; ++i) {
            if (prior[i].action == next.action) {
                return refusal(conflict, true, kFieldAction, RcRuleReason::kConflict);
            }
        }
    }
    return kHolds;
}

}  // namespace

const char* rcRuleReasonToken(RcRuleReason reason) {
    switch (reason) {
        case RcRuleReason::kOutOfRange:
            return "out-of-range";
        case RcRuleReason::kConflict:
            return "conflict";
        case RcRuleReason::kHolds:
        default:
            return nullptr;
    }
}

bool rcRuleFormatAccepts(const RcRuleVerdict& verdict, char* buf, size_t bufSize) {
    if (buf == nullptr || bufSize == 0) {
        return false;
    }
    buf[0] = '\0';
    int written = 0;
    if (verdict.acceptsWords != nullptr) {
        written = snprintf(buf, bufSize, "%s", verdict.acceptsWords);
    } else if (verdict.acceptsHi != 0) {
        written = snprintf(buf, bufSize, "%u..%u", (unsigned)verdict.acceptsLo,
                           (unsigned)verdict.acceptsHi);
    } else {
        return false;
    }
    if (written <= 0 || (size_t)written >= bufSize) {
        buf[0] = '\0';
        return false;
    }
    return true;
}

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

RcRuleVerdict rcRuleMapAdd(const RcMapEntry* prior, size_t count, const RcMapEntry& next,
                           RcInputMode type) {
    if (count >= kRcMapMaxEntries) {
        return refusal("conflict: map exceeds capacity", false, kFieldMap, RcRuleReason::kConflict);
    }
    const RcRuleVerdict own = entryRule(next, type);
    if (!own.ok()) {
        return own;
    }
    return conflictRule(prior, count, next);
}

RcRuleVerdict rcRuleDrive(const RcBindingConfig& speed, const RcBindingConfig& steer) {
    const bool speedBound = speed.source != RC_BINDING_NONE;
    const bool steerBound = steer.source != RC_BINDING_NONE;
    if (speedBound && steerBound && speed.source != steer.source) {
        return onAxis(refusal("drive speed and steer must be on the same receiver", true, kFieldSource,
                              RcRuleReason::kConflict),
                      DRIVE_ACTION_STEER);
    }
    const RcRuleVerdict sbus2 = refusal("drive reads SBUS1, the drive receiver", true, kFieldSource,
                                        RcRuleReason::kOutOfRange, 0, 0, "sbus1");
    if (speedBound && speed.source == RC_BINDING_SBUS2) {
        return onAxis(sbus2, DRIVE_ACTION_SPEED);
    }
    if (steerBound && steer.source == RC_BINDING_SBUS2) {
        return onAxis(sbus2, DRIVE_ACTION_STEER);
    }
    return kHolds;
}

RcRuleVerdict rcRuleAxisCalibration(RobotActionId axis, const RcBindingConfig& binding) {
    // The field within the request's `calibration` object, by axis and key.
    static const char* const kFields[3][2] = {
        {"calibration.drive_speed.center", "calibration.drive_speed.deadband"},
        {"calibration.drive_steer.center", "calibration.drive_steer.deadband"},
        {"calibration.dome_speed.center", "calibration.dome_speed.deadband"},
    };
    const size_t row = axis == DRIVE_ACTION_SPEED ? 0 : axis == DRIVE_ACTION_STEER ? 1 : 2;
    if (!(binding.min < binding.center && binding.center < binding.max)) {
        return refusal("calibration needs min < center < max", false, kFields[row][0],
                       RcRuleReason::kConflict);
    }
    // The dead zone must leave travel on both sides of the centre, or that
    // side of the stick maps to nothing (Codex review, #389).
    if (binding.deadband >= (uint16_t)(binding.center - binding.min) ||
        binding.deadband >= (uint16_t)(binding.max - binding.center)) {
        return refusal("calibration leaves no travel past the deadband", false, kFields[row][1],
                       RcRuleReason::kConflict);
    }
    return kHolds;
}

RcRuleVerdict rcRuleStoredAxis(RobotActionId axis, const RcBindingConfig& binding,
                               RcInputMode type) {
    RcMapEntry entry = {};
    entry.source = binding.source;
    entry.channel = binding.channel;
    entry.action = axis;
    entry.threshold = kRcMapEntryKeep;
    entry.quietS = kRcMapEntryKeep;
    const RcRuleVerdict own = entryRule(entry, type);
    if (!own.ok()) {
        return own;
    }
    return rcRuleAxisCalibration(axis, binding);
}

RcRuleVerdict rcRuleStoredCue(const RcTriggerBinding& binding, RcInputMode type) {
    if (binding.source == RC_BINDING_NONE || binding.target == ROBOT_ACTION_NONE) {
        return kHolds;  // an empty slot binds nothing to judge
    }
    RcMapEntry entry = {};
    entry.source = binding.source;
    entry.channel = binding.channel;
    entry.action = binding.target;
    static_assert(sizeof(entry.payload) == sizeof(binding.marcduinoPayload),
                  "a stored payload is an entry's payload");
    memcpy(entry.payload, binding.marcduinoPayload, sizeof(entry.payload));
    entry.payload[sizeof(entry.payload) - 1] = '\0';
    // A stored Reaction carries its numbers in the calibration fields
    // (include/rc_action_types.h); a radio cue carries none.
    const bool reaction = rcBindingSourceIsDroidCondition(binding.source);
    entry.threshold = reaction ? rcReactionThreshold(binding) : kRcMapEntryKeep;
    entry.quietS = reaction ? rcReactionQuietS(binding) : kRcMapEntryKeep;
    return entryRule(entry, type);
}

RcRuleVerdict rcRuleStoredDrive(const RcBindingConfig& speed, const RcBindingConfig& steer,
                                RcInputMode type) {
    const RobotActionId axes[] = {DRIVE_ACTION_SPEED, DRIVE_ACTION_STEER};
    const RcBindingConfig* const bindings[] = {&speed, &steer};
    for (size_t i = 0; i < 2; ++i) {
        if (bindings[i]->source == RC_BINDING_NONE) {
            continue;
        }
        const RcRuleVerdict own = rcRuleStoredAxis(axes[i], *bindings[i], type);
        if (!own.ok()) {
            return onAxis(own, axes[i]);
        }
    }
    return rcRuleDrive(speed, steer);
}
