// =============================================================================
// src/rc_map_rules.cpp
//
// The RC Map's rules (ADR 0070). See include/rc_map_rules.h.
// =============================================================================

#include "rc_map_rules.h"

#include <stdio.h>
#include <string.h>

#include "rc_puppet.h"       // rcPuppetChannelCanMove()
#include "rc_pwm_helpers.h"  // RC_PWM_VALID_MIN_US / MAX_US
#include "robot_state.h"     // RcInputMode

namespace {

constexpr RcRuleVerdict kHolds = {nullptr, nullptr, nullptr, 0, 0, ROBOT_ACTION_NONE,
                                  ApplyRefusalReason::None, false};

// The request fields a refusal names (docs/api.md "POST /api/rc/map").
constexpr const char* kFieldMap = "map";
constexpr const char* kFieldSource = "map.source";
constexpr const char* kFieldChannel = "map.channel";
constexpr const char* kFieldAction = "map.action";
constexpr const char* kFieldPayload = "map.payload";
constexpr const char* kFieldThreshold = "map.threshold";
constexpr const char* kFieldQuietS = "map.quietS";
constexpr const char* kFieldDrive = "map.drive";

// A refusal. `aboutEntry` echoes the entry; the accepts are a range lo..hi
// (hi 0 for none) or the words given.
RcRuleVerdict refusal(const char* sentence, bool aboutEntry, const char* field, ApplyRefusalReason reason,
                      uint16_t lo = 0, uint16_t hi = 0, const char* words = nullptr) {
    return {sentence, field, words, lo, hi, ROBOT_ACTION_NONE, reason, aboutEntry};
}

RcRuleVerdict onAxis(RcRuleVerdict verdict, RobotActionId axis) {
    verdict.axis = axis;
    verdict.aboutEntry = true;
    return verdict;
}

// The RC Channels a source has, as a range: what a channel refusal accepts
// (rcBindingChannelRange(), the one home of the numbers).
RcRuleVerdict channelOutOfRange(RcBindingSource source) {
    uint8_t lo = 0;
    uint8_t hi = 0;
    rcBindingChannelRange(source, &lo, &hi);
    return refusal("channel out of range", false, kFieldChannel, ApplyRefusalReason::OutOfRange, lo, hi);
}

// What a droid condition may fire, and the numbers it carries (#450). A number
// the request left out (kRcMapEntryKeep) is not judged: it keeps the stored
// one or takes its default.
RcRuleVerdict reactionRule(const RcMapEntry& entry) {
    if (robotActionIsAnalog(entry.action)) {
        return refusal("a droid condition cannot drive an axis", true, kFieldAction,
                       ApplyRefusalReason::OutOfRange);
    }
    if (!robotActionValidForReaction(entry.action)) {
        return refusal("action not allowed on a droid condition", true, kFieldAction,
                       ApplyRefusalReason::OutOfRange);
    }
    if (entry.threshold != kRcMapEntryKeep) {
        const uint16_t thresholdMax = rcReactionThresholdMax(entry.source);
        if (thresholdMax == 0 ? entry.threshold != 0
                              : (entry.threshold < 1 || entry.threshold > thresholdMax)) {
            // A condition with no threshold takes 0 alone: "0..0".
            const uint16_t lo = thresholdMax == 0 ? 0 : 1;
            RcRuleVerdict verdict = refusal("threshold out of range", true, kFieldThreshold,
                                            ApplyRefusalReason::OutOfRange, lo, thresholdMax);
            if (thresholdMax == 0) {
                verdict.acceptsWords = "0";
            }
            return verdict;
        }
    }
    if (entry.quietS != kRcMapEntryKeep &&
        (entry.quietS < RC_REACTION_QUIET_MIN_S || entry.quietS > RC_REACTION_QUIET_MAX_S)) {
        return refusal("quiet period out of range", true, kFieldQuietS, ApplyRefusalReason::OutOfRange,
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
                       ApplyRefusalReason::OutOfRange, 0, 0, receiversOfType(type));
    }
    // A PWM receiver carries the drive and dome axes only: the RC Map's cue
    // slots are not read on PWM (operator, 2026-10-09 on #486).
    if (entry.source == RC_BINDING_PWM && !isAxis(entry.action)) {
        return refusal("PWM carries only the drive and dome axes", true, kFieldAction,
                       ApplyRefusalReason::OutOfRange, 0, 0, "drive_speed,drive_steer,dome_speed");
    }
    // An axis reads a stick, never CH17/CH18, which are on/off.
    if (isAxis(entry.action) && !rcBindingSourceIsDroidCondition(entry.source)) {
        const RcBindingConfig channel =
            makeRcBindingConfig(entry.source, entry.channel, 0, 0, 0, 0, false);
        if (!rcBindingSupportsAnalog(channel)) {
            return refusal("an axis needs a stick channel", true, kFieldChannel,
                           ApplyRefusalReason::OutOfRange, 1,
                           entry.source == RC_BINDING_PWM ? RC_PWM_CHANNELS : RC_SBUS_STICK_CHANNELS);
        }
    }
    // A payload the dispatcher would not send fires nothing, so it is not
    // stored: a body sequence is SE30-SE36 by its two digits, a Marcduino
    // command starts :, $ or # (rc_action_dispatcher.cpp checks the same).
    if (entry.action == DOME_ACTION_MARCDUINO_SEQ && !rcPayloadValidForBodySequence(entry.payload)) {
        return refusal("invalid body sequence payload (expected 30-36)", true, kFieldPayload,
                       ApplyRefusalReason::OutOfRange, 30, 36);
    }
    if (entry.action == DOME_ACTION_MARCDUINO_CMD && !rcPayloadValidForMarcduinoCommand(entry.payload)) {
        return refusal("invalid Marcduino command payload (expected :, $ or #)", true, kFieldPayload,
                       ApplyRefusalReason::OutOfRange);
    }
    if (entry.action == DOME_ACTION_MARCDUINO_CMD && strncmp(entry.payload, ":SM", 3) == 0) {
        return refusal(":SM is diagnostic only and cannot be saved as an RC binding", true,
                       kFieldPayload, ApplyRefusalReason::OutOfRange);
    }
    // A puppet string moves a Part in proportion to a stick (#442): an SBUS
    // stick channel, since CH17/CH18 are on/off.
    if (entry.action == SERVO_ACTION_PUPPET_PART &&
        !rcPuppetChannelCanMove(entry.source, entry.channel)) {
        return refusal("a puppet string needs an SBUS stick channel (CH1-CH16)", true, kFieldChannel,
                       ApplyRefusalReason::OutOfRange, 1, RC_SBUS_STICK_CHANNELS);
    }
    return kHolds;
}

// An action the RC Map binds once, and the refusal of a second: an axis, and
// on a radio an arm or aux toggle or the op mode, since two switches toggling
// one arm fight each other (ADR 0070, amended 2026-10-10). A Reaction is not
// held to it. Null for any other action, which may sit on several RC Channels.
const char* onceOnlyConflict(RobotActionId action, RcBindingSource source) {
    switch (action) {
        case DRIVE_ACTION_SPEED:
            return "conflict: drive_speed mapped more than once";
        case DRIVE_ACTION_STEER:
            return "conflict: drive_steer mapped more than once";
        case DOME_ACTION_SPEED:
            return "conflict: dome_speed mapped more than once";
        default:
            break;
    }
    if (rcBindingSourceIsDroidCondition(source)) {
        return nullptr;
    }
    switch (action) {
        case SERVO_ACTION_ARM1_TOGGLE:
            return "conflict: arm1_toggle mapped more than once";
        case SERVO_ACTION_ARM2_TOGGLE:
            return "conflict: arm2_toggle mapped more than once";
        case SERVO_ACTION_AUX1_TOGGLE:
            return "conflict: aux1_toggle mapped more than once";
        case SERVO_ACTION_AUX2_TOGGLE:
            return "conflict: aux2_toggle mapped more than once";
        case SERVO_ACTION_AUX3_TOGGLE:
            return "conflict: aux3_toggle mapped more than once";
        case SYSTEM_ACTION_OP_MODE:
            return "conflict: op_mode mapped more than once";
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
                           ApplyRefusalReason::Conflict);
        }
    }
    // One control, one job: a channel is a drive axis, a cue or a puppet
    // string, never two of them (#442).
    for (size_t i = 0; i < count; ++i) {
        if (prior[i].source == next.source && prior[i].channel == next.channel) {
            return refusal("conflict: source+channel mapped more than once", true, kFieldChannel,
                           ApplyRefusalReason::Conflict);
        }
    }
    if (const char* conflict = onceOnlyConflict(next.action, next.source)) {
        for (size_t i = 0; i < count; ++i) {
            if (prior[i].action == next.action && onceOnlyConflict(prior[i].action, prior[i].source)) {
                return refusal(conflict, true, kFieldAction, ApplyRefusalReason::Conflict);
            }
        }
    }
    return kHolds;
}

}  // namespace

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
    // Eleven trigger bindings, whatever they fire; the three axes have places
    // of their own, and binding one twice is a conflict below.
    size_t triggers = 0;
    for (size_t i = 0; i < count; ++i) {
        triggers += isAxis(prior[i].action) ? 0 : 1;
    }
    if (count >= kRcMapMaxEntries || (!isAxis(next.action) && triggers >= kRcMapMaxTriggers)) {
        return refusal("conflict: map exceeds capacity", false, kFieldMap, ApplyRefusalReason::Conflict);
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
                              ApplyRefusalReason::Conflict),
                      DRIVE_ACTION_STEER);
    }
    const RcRuleVerdict sbus2 = refusal("drive reads SBUS1, the drive receiver", true, kFieldSource,
                                        ApplyRefusalReason::OutOfRange, 0, 0, "sbus1");
    if (speedBound && speed.source == RC_BINDING_SBUS2) {
        return onAxis(sbus2, DRIVE_ACTION_SPEED);
    }
    if (steerBound && steer.source == RC_BINDING_SBUS2) {
        return onAxis(sbus2, DRIVE_ACTION_STEER);
    }
    return kHolds;
}

RcRuleVerdict rcRuleAxisCalibration(RobotActionId axis, const RcBindingConfig& binding) {
    // Each value within what the receiver reports: a pulse width on PWM, an
    // SBUS channel's 11 bits otherwise.
    const bool pwm = binding.source == RC_BINDING_PWM;
    const uint16_t lo = pwm ? RC_PWM_VALID_MIN_US : 0;
    const uint16_t hi = pwm ? RC_PWM_VALID_MAX_US : 2047;
    const struct {
        uint16_t value;
        const char* field;
    } values[] = {{binding.min, "calibration.min"},
                  {binding.center, "calibration.center"},
                  {binding.max, "calibration.max"}};
    for (const auto& each : values) {
        if (each.value < lo || each.value > hi) {
            return onAxis(refusal("calibration out of range", true, each.field, ApplyRefusalReason::OutOfRange,
                                  lo, hi),
                          axis);
        }
    }
    if (!(binding.min < binding.center && binding.center < binding.max)) {
        return onAxis(refusal("calibration needs min < center < max", true, "calibration.center",
                              ApplyRefusalReason::Conflict),
                      axis);
    }
    // The dead zone must leave travel on both sides of the centre, or that
    // side of the stick maps to nothing (Codex review, #389).
    if (binding.deadband >= (uint16_t)(binding.center - binding.min) ||
        binding.deadband >= (uint16_t)(binding.max - binding.center)) {
        return onAxis(refusal("calibration leaves no travel past the deadband", true,
                              "calibration.deadband", ApplyRefusalReason::Conflict),
                      axis);
    }
    return kHolds;
}

size_t rcMapReceivers(RcInputMode type, RcMapReceiverUse use, RcBindingSource* out, size_t cap) {
    static constexpr RcBindingSource kReceivers[] = {RC_BINDING_PWM, RC_BINDING_SBUS1, RC_BINDING_SBUS2};
    size_t count = 0;
    for (RcBindingSource source : kReceivers) {
        // A use is judged as the save judges one entry of it, on the first RC
        // Channel: the drive as Speed (and the drive pair rule), a cue as a
        // press, anything else by whether the type reads the receiver.
        RcMapEntry entry = {};
        entry.source = source;
        entry.channel = 1;
        entry.action = use == RcMapReceiverUse::Drive ? DRIVE_ACTION_SPEED
                       : use == RcMapReceiverUse::Cue ? SOUND_ACTION_NEXT
                                                      : DOME_ACTION_SPEED;
        entry.threshold = kRcMapEntryKeep;
        entry.quietS = kRcMapEntryKeep;
        if (!entryRule(entry, type).ok()) {
            continue;
        }
        if (use == RcMapReceiverUse::Drive &&
            !rcRuleDrive(makeRcBindingConfig(source, 1, 0, 0, 0, 0, false), disabledRcBinding()).ok()) {
            continue;
        }
        if (out != nullptr && count < cap) {
            out[count] = source;
        }
        ++count;
    }
    return count;
}

namespace {

// A stored drive or dome axis (`axis` names which), its calibration included.
RcRuleVerdict storedAxisRule(RobotActionId axis, const RcBindingConfig& binding, RcInputMode type) {
    RcMapEntry entry = {};
    entry.source = binding.source;
    entry.channel = binding.channel;
    entry.action = axis;
    entry.threshold = kRcMapEntryKeep;
    entry.quietS = kRcMapEntryKeep;
    const RcRuleVerdict own = entryRule(entry, type);
    if (!own.ok()) {
        return onAxis(own, axis);
    }
    return rcRuleAxisCalibration(axis, binding);
}

// A stored trigger slot: a cue, a puppet string or a Reaction. The stored
// form's own check (rcTriggerBindingIsValid()) does not hold a Reaction's
// payload to the RC Map's rules, so a Marcduino :SM line passes it; this does.
RcRuleVerdict storedCueRule(const RcTriggerBinding& binding, RcInputMode type) {
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

// One stored binding of an RcStoredMap, by index: the trigger slots first,
// then the three axes. Points into the map; copies no payload.
struct StoredRef {
    RcBindingSource source;
    uint8_t channel;
    RobotActionId action;
    const char* payload;
    uint32_t bit;
};

constexpr size_t kAxisCount = 3;

size_t storedCueCount(const RcStoredMap& map) {
    if (map.cues == nullptr) {
        return 0;
    }
    return map.cueCount < kRcStoredCueBitMax ? map.cueCount : kRcStoredCueBitMax;
}

const RcBindingConfig& storedAxis(const RcStoredMap& map, size_t axis) {
    const RcBindingConfig* const axes[] = {&map.driveSpeed, &map.driveSteer, &map.domeSpeed};
    return *axes[axis];
}

StoredRef storedRef(const RcStoredMap& map, size_t index) {
    const size_t cues = storedCueCount(map);
    if (index < cues) {
        const RcTriggerBinding& cue = map.cues[index];
        return {cue.source, cue.channel, cue.target, cue.marcduinoPayload, rcStoredCueBit(index)};
    }
    static constexpr RobotActionId kAxes[] = {DRIVE_ACTION_SPEED, DRIVE_ACTION_STEER, DOME_ACTION_SPEED};
    static constexpr uint32_t kBits[] = {kRcStoredDriveSpeedBit, kRcStoredDriveSteerBit,
                                         kRcStoredDomeSpeedBit};
    const size_t axis = index - cues;
    const RcBindingConfig& binding = storedAxis(map, axis);
    return {binding.source, binding.channel, kAxes[axis], "", kBits[axis]};
}

// Whether a stored binding binds anything: an empty slot or an unbound axis
// has nothing to read.
bool storedBound(const StoredRef& ref) {
    return ref.source != RC_BINDING_NONE && ref.action != ROBOT_ACTION_NONE;
}

// A bound stored binding's own rules.
RcRuleVerdict storedOwnRule(const RcStoredMap& map, size_t index, RcInputMode type) {
    const size_t cues = storedCueCount(map);
    if (index < cues) {
        return storedCueRule(map.cues[index], type);
    }
    const StoredRef ref = storedRef(map, index);
    return storedAxisRule(ref.action, storedAxis(map, index - cues), type);
}

// The save's conflicts between two entries (conflictRule()), on stored ones.
RcRuleVerdict storedPairConflict(const StoredRef& a, const StoredRef& b) {
    if (a.action == SERVO_ACTION_PUPPET_PART && b.action == SERVO_ACTION_PUPPET_PART &&
        strncmp(a.payload, b.payload, sizeof(RcMapEntry::payload)) == 0) {
        return refusal("conflict: a Part on two puppet strings", true, kFieldPayload,
                       ApplyRefusalReason::Conflict);
    }
    if (a.source == b.source && a.channel == b.channel) {
        return refusal("conflict: source+channel mapped more than once", true, kFieldChannel,
                       ApplyRefusalReason::Conflict);
    }
    if (a.action == b.action && onceOnlyConflict(b.action, b.source) != nullptr) {
        if (const char* conflict = onceOnlyConflict(a.action, a.source)) {
            return refusal(conflict, true, kFieldAction, ApplyRefusalReason::Conflict);
        }
    }
    return kHolds;
}

// The drive axis left still only for its partner: the drive moves on both
// sticks or on neither.
RcRuleVerdict drivePairWaits(RobotActionId axis) {
    return onAxis(refusal("drive waits on its other stick", true, kFieldDrive, ApplyRefusalReason::Conflict),
                  axis);
}

// Every stored binding's verdict, as bits: `own` the bound ones that break a
// rule on their own, `conflicts` the ones that hold their own rules and
// conflict with another that does, and `drive` both drive axes when the pair
// does not read. Their union is what the droid does not read.
struct StoredVerdicts {
    uint32_t own;
    uint32_t conflicts;
    uint32_t drive;
    uint32_t unread() const { return own | conflicts | drive; }
};

StoredVerdicts judgeStored(const RcStoredMap& map, RcInputMode type) {
    StoredVerdicts verdicts = {0, 0, 0};
    const size_t count = storedCueCount(map) + kAxisCount;
    uint32_t holds = 0;
    for (size_t i = 0; i < count; ++i) {
        const StoredRef ref = storedRef(map, i);
        if (!storedBound(ref)) {
            continue;
        }
        if (storedOwnRule(map, i, type).ok()) {
            holds |= ref.bit;
        } else {
            verdicts.own |= ref.bit;
        }
    }
    // Only bindings that hold their own rules are judged against each other:
    // one that is not read takes no control from another.
    for (size_t i = 0; i < count; ++i) {
        const StoredRef a = storedRef(map, i);
        if ((holds & a.bit) == 0) {
            continue;
        }
        for (size_t j = i + 1; j < count; ++j) {
            const StoredRef b = storedRef(map, j);
            if ((holds & b.bit) != 0 && !storedPairConflict(a, b).ok()) {
                verdicts.conflicts |= a.bit | b.bit;
            }
        }
    }
    const uint32_t pair = kRcStoredDriveSpeedBit | kRcStoredDriveSteerBit;
    const uint32_t bound = (map.driveSpeed.source != RC_BINDING_NONE ? kRcStoredDriveSpeedBit : 0u) |
                           (map.driveSteer.source != RC_BINDING_NONE ? kRcStoredDriveSteerBit : 0u);
    const uint32_t pairReads = holds & ~verdicts.conflicts & pair;
    if (bound != 0 && (pairReads != pair || !rcRuleDrive(map.driveSpeed, map.driveSteer).ok())) {
        verdicts.drive = bound;
    }
    return verdicts;
}

}  // namespace

void rcStoredMapKeepRead(RcStoredMap* map, RcInputMode type) {
    if (map == nullptr) {
        return;
    }
    // Judged on the map as stored, then left still: a binding blanked first
    // would free a control for another that conflicts with it.
    const uint32_t unread = judgeStored(*map, type).unread();
    const size_t cues = storedCueCount(*map);
    for (size_t i = 0; i < cues; ++i) {
        if ((unread & rcStoredCueBit(i)) != 0) {
            map->cues[i] = disabledRcTriggerBinding();
        }
    }
    RcBindingConfig* const axes[] = {&map->driveSpeed, &map->driveSteer, &map->domeSpeed};
    const uint32_t bits[] = {kRcStoredDriveSpeedBit, kRcStoredDriveSteerBit, kRcStoredDomeSpeedBit};
    for (size_t axis = 0; axis < kAxisCount; ++axis) {
        if ((unread & bits[axis]) != 0) {
            *axes[axis] = disabledRcBinding();
        }
    }
}

RcRuleVerdict rcStoredMapWhy(const RcStoredMap& map, RcInputMode type, uint32_t bit) {
    const StoredVerdicts verdicts = judgeStored(map, type);
    if (bit == 0 || (verdicts.unread() & bit) == 0) {
        return kHolds;
    }
    const size_t count = storedCueCount(map) + kAxisCount;
    size_t self = count;
    for (size_t i = 0; i < count; ++i) {
        if (storedRef(map, i).bit == bit) {
            self = i;
            break;
        }
    }
    if (self == count) {
        return kHolds;
    }
    if ((verdicts.own & bit) != 0) {
        return storedOwnRule(map, self, type);
    }
    if ((verdicts.conflicts & bit) != 0) {
        const StoredRef a = storedRef(map, self);
        for (size_t j = 0; j < count; ++j) {
            const StoredRef b = storedRef(map, j);
            if (j == self || !storedBound(b) || (verdicts.own & b.bit) != 0) {
                continue;
            }
            const RcRuleVerdict verdict = storedPairConflict(a, b);
            if (!verdict.ok()) {
                return verdict;
            }
        }
    }
    // The drive pair. The axis the pair rule names carries its refusal; the
    // other, and an axis left still because its partner is unbound or not
    // read, waits on its partner.
    const RobotActionId axis = bit == kRcStoredDriveSpeedBit ? DRIVE_ACTION_SPEED : DRIVE_ACTION_STEER;
    const RcRuleVerdict pair = rcRuleDrive(map.driveSpeed, map.driveSteer);
    if (!pair.ok() && pair.axis == axis) {
        return pair;
    }
    return drivePairWaits(axis);
}
