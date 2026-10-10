// =============================================================================
// include/rc_map_rules.h
//
// The RC Map's rules, in one place (ADR 0070): what an RC Map may hold, and
// which RC Receivers a receiver type reads. The save (rcMapApply()) asks here
// of what it is given; every reader of a stored RC Map - the input processor,
// ReactionTask, the RC snapshot and GET /api/rc/map - asks here which of its
// bindings the droid reads (rcStoredMapKeepRead(), rcStoredMapWhy()). None of
// them keeps a copy of a rule.
//
// Pure: no FreeRTOS call, no RobotState read, no config cache (the .cpp
// includes robot_state.h only for RcInputMode's values). A rule that needs live
// state - whether a Part or a Learned Sequence exists - stays with the caller
// that holds it (rcMapApply()), so nothing below the mapper reads runtime
// state.
//
// The stored form's own validators (rcBindingIsValid(), rcTriggerBindingIsValid())
// are not these rules and must not grow into them: the NVS load replaces a
// binding they refuse with its default (loadRcBinding(), config_serializer.cpp),
// and a default drive binding can move the droid. A rule is applied where a
// binding is saved or read, never where it is parsed.
//
// Sits directly on the binding and action types (ADR 0001): it includes
// rc_action_types.h and nothing that includes it back.
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "api_apply_refusal.h"  // ApplyRefusalReason: the reasons every refusal states
#include "rc_action_types.h"

enum RcInputMode : uint8_t;  // include/robot_state.h

// One entry of an RC Map as POST /api/rc/map takes it: an RC Channel (or a
// droid condition) and what it fires.
struct RcMapEntry {
    RcBindingSource source;
    uint8_t channel;
    RobotActionId action;
    char payload[16];
    // A Reaction's two numbers (a droid-condition source; include/
    // rc_action_types.h). One a request leaves out is kRcMapEntryKeep: it
    // keeps what the stored Reaction on that condition holds, or takes its
    // default, the way a radio binding keeps its calibration. A sentinel
    // rather than a flag each, because rcMapApply() holds fourteen of these on
    // the HTTP server task's stack.
    uint16_t threshold;
    uint16_t quietS;
};

// The most entries one RC Map holds: the three axes and the eleven trigger
// bindings. Any trigger binding may take any of the eleven stored places
// (ADR 0070, amended 2026-10-10), so the twelfth is what a save refuses.
static constexpr size_t kRcMapMaxTriggers = 11;
static constexpr size_t kRcMapMaxEntries = kRcMapMaxTriggers + 3;

// Past every threshold and quiet period a Reaction accepts.
static constexpr uint16_t kRcMapEntryKeep = 0xFFFF;

// The receivers a droid reads from: its receiver type, and the RC channel
// enables that gate each receiver (Component Toggles rcCh1..rcCh6).
struct RcReceiverSetup {
    RcInputMode mode;
    bool enableRc[6];
    // single_sbus only: the receiver is wired to the CH2 header (the
    // sbusRecvCh2 Setting). It is still SBUS1 (operator, 2026-10-09 on #389);
    // the flag picks which header's enable gates it.
    bool useCh2;
};

// Whether the receiver type reads this source at all. single_sbus reads SBUS1
// whichever header it is on, and never SBUS2; dual_sbus reads each SBUS header
// it enables; standard_pwm reads PWM when any RC channel is enabled. A droid
// condition is no receiver and is never read here.
bool rcReceiverReads(RcBindingSource source, const RcReceiverSetup& setup);

// A rule's answer, as a refusal names it on the wire (docs/api.md "Refusals
// from a settings write", include/api_apply_refusal.h): `field` is the request
// field it is about ("map.channel", "map.source", ...), `reason` why, and what
// the field would have taken - a range `acceptsLo..acceptsHi` when
// `acceptsWords` is null and acceptsHi is not 0, or the comma-separated words
// in `acceptsWords`.
// rcRuleFormatAccepts() writes it. `sentence` is the refusal as POST
// /api/rc/map has always said it; null when the rule holds. `aboutEntry`: the
// refusal names the entry it is about (the answer echoes it). `axis`: which
// drive axis a drive-pair refusal is about, ROBOT_ACTION_NONE otherwise.
struct RcRuleVerdict {
    const char* sentence;
    const char* field;
    const char* acceptsWords;
    uint16_t acceptsLo;
    uint16_t acceptsHi;
    RobotActionId axis;
    ApplyRefusalReason reason;
    bool aboutEntry;
    bool ok() const { return sentence == nullptr; }
};

// Every request field a refusal of POST /api/rc/map names, and an entry of
// GET /api/rc/map the droid does not read. A calibration field is the key
// alone; the refusal echoes the axis's entry to say which axis.
// tools/check_setting_words.py fails when one has no words in the browser's
// words table (data/web_api.js), so none reaches a page as its wire name.
constexpr const char* kRcMapRefusalFields[] = {
    "map",
    "map.source",
    "map.channel",
    "map.action",
    "map.payload",
    "map.threshold",
    "map.quietS",
    "map.drive",
    "calibration",
    "calibration.min",
    "calibration.center",
    "calibration.max",
    "calibration.reverse",
    "calibration.deadband",
};

// What the refused field accepts, written into `buf` ("1..16", "sbus1,sbus2").
// False, with `buf` empty, when the verdict states none (every conflict, and
// a refusal no value would cure).
bool rcRuleFormatAccepts(const RcRuleVerdict& verdict, char* buf, size_t bufSize);
// Whether `next` may join the `count` entries already in an RC Map for the
// receiver type `type`: its RC Channel, that the type reads its receiver, that
// a cue is not on PWM and an axis sits on a stick, what a droid condition may
// fire and the numbers it carries, a puppet string's stick, the eleven trigger
// bindings, and the conflicts with the entries before it: one RC Channel, one
// job; one Part, one puppet string; and an axis, or an arm or aux toggle or the
// op mode on a radio, bound once. A payload that must name something that
// exists (a Part, a dome sequence) is the caller's to check.
RcRuleVerdict rcRuleMapAdd(const RcMapEntry* prior, size_t count, const RcMapEntry& next,
                           RcInputMode type);

// The drive pair. Speed and Steer are read together, from one frame of one
// receiver, so they sit on one RC Receiver; and that receiver is not SBUS2,
// because only SBUS1, the drive receiver, carries the drive watchdog and the
// hardware-failsafe stop (operator, 2026-10-09 on #389). An unbound axis
// (RC_BINDING_NONE) breaks neither rule.
RcRuleVerdict rcRuleDrive(const RcBindingConfig& speed, const RcBindingConfig& steer);

// An axis's calibration (`axis` names which, and the verdict's `axis` says it
// again): each end and the centre within what its receiver reports (PWM
// RC_PWM_VALID_MIN_US..RC_PWM_VALID_MAX_US, SBUS 0..2047), end, centre and end
// in order, and a dead zone that leaves stick travel on both sides of the
// centre.
RcRuleVerdict rcRuleAxisCalibration(RobotActionId axis, const RcBindingConfig& binding);

// What a receiver may carry, for rcMapReceivers().
enum class RcMapReceiverUse : uint8_t {
    Read,   // anything: the receiver type reads it
    Drive,  // the drive pair
    Cue,    // a cue or a puppet string (a trigger slot); PWM carries none
};

// The RC Receivers a map for the receiver type `type` may bind for `use` (ADR
// 0070), judged by the rules a save applies. Written to `out` in the order
// pwm, sbus1, sbus2; returns how many. GET /api/rc/map answers them, so the RC
// page offers exactly what a save takes.
size_t rcMapReceivers(RcInputMode type, RcMapReceiverUse use, RcBindingSource* out, size_t cap);

// ---------------------------------------------------------------------------
// The same rules on read (ADR 0070): a stored binding a save would refuse is
// not read, so it stays still. `read` means what the droid reads now (amended
// 2026-10-10): each binding is judged on its own against the receiver type the
// droid runs, then against the others - NVS commits each key on its own, so a
// save cut short by a power loss can leave two bindings on one control that no
// save would have taken together - and the drive pair last, since the drive
// moves only on both sticks.
// ---------------------------------------------------------------------------

// The stored map as a reader holds it: the three axes, and the trigger slots
// in whatever order the reader keeps them. An unbound axis is
// disabledRcBinding().
struct RcStoredMap {
    RcBindingConfig driveSpeed;
    RcBindingConfig driveSteer;
    RcBindingConfig domeSpeed;
    RcTriggerBinding* cues;
    size_t cueCount;
};

// Leaves still every binding of `map` the droid does not read: a slot becomes
// disabledRcTriggerBinding(), an axis disabledRcBinding(). What is left is what
// the droid reads, so a reader that takes it keeps no check of its own. A
// binding is not read when:
//   - it breaks a rule on its own (rcRuleMapAdd()'s entry rules, an axis's
//     calibration included);
//   - it holds its own rules and conflicts with another that does - one RC
//     Channel, one job; one Part, one puppet string; a toggle or the op mode
//     bound twice on a radio. Both stay still, since neither is known to be the
//     one the operator meant;
//   - it is a drive axis whose partner is not read, or is unbound, or the pair
//     breaks rcRuleDrive(): the drive moves only on both sticks.
// No allocation and no copy of a slot: the input processor asks once a frame
// on core 1.
void rcStoredMapKeepRead(RcStoredMap* map, RcInputMode type);

// Why rcStoredMapKeepRead() would leave one binding still, as a save would
// word it, the binding named by its bit: trigger slot i is rcStoredCueBit(i),
// the axes the three bits below. Holds for a binding the droid reads and for
// an unbound one. A drive axis left still only for its partner says so
// ("map.drive"), never with the partner's own refusal: the RC page acts on a
// refusal's field. For GET /api/rc/map.
static constexpr size_t kRcStoredCueBitMax = 29;
static constexpr uint32_t kRcStoredDriveSpeedBit = 1u << 29;
static constexpr uint32_t kRcStoredDriveSteerBit = 1u << 30;
static constexpr uint32_t kRcStoredDomeSpeedBit = 1u << 31;
inline uint32_t rcStoredCueBit(size_t slot) {
    return slot < kRcStoredCueBitMax ? (1u << slot) : 0u;
}
RcRuleVerdict rcStoredMapWhy(const RcStoredMap& map, RcInputMode type, uint32_t bit);
