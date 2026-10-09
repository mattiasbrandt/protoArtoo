// =============================================================================
// include/rc_map_rules.h
//
// The RC Map's rules, in one place (ADR 0070): what an RC Map may hold, and
// which RC Receivers a receiver type reads. The save (rcMapApply()), the
// channel mapper, the input processor and the RC and validation snapshots ask
// here; none of them keeps a copy of a rule.
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

// The most entries one RC Map holds: the three axes and the eleven trigger slots.
static constexpr size_t kRcMapMaxEntries = 14;

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
// fire and the numbers it carries, a puppet string's stick, and the conflicts
// with the entries before it. A payload that must name something that exists
// (a Part, a dome sequence) is the caller's to check.
RcRuleVerdict rcRuleMapAdd(const RcMapEntry* prior, size_t count, const RcMapEntry& next,
                           RcInputMode type);

// The drive pair. Speed and Steer are read together, from one frame of one
// receiver, so they sit on one RC Receiver; and that receiver is not SBUS2,
// because only SBUS1, the drive receiver, carries the drive watchdog and the
// hardware-failsafe stop (operator, 2026-10-09 on #389). An unbound axis
// (RC_BINDING_NONE) breaks neither rule.
RcRuleVerdict rcRuleDrive(const RcBindingConfig& speed, const RcBindingConfig& steer);

// An axis's calibration (`axis` names which, and the verdict's `axis` says it
// again): end, centre and end in order, and a dead zone that leaves stick
// travel on both sides of the centre.
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
// not read, so it stays still. Each is judged on its own against the receiver
// type the droid runs. Conflicts between entries are left to the save, since
// every stored map came through one.
// ---------------------------------------------------------------------------

// A stored drive or dome axis (`axis` names which), its calibration included.
RcRuleVerdict rcRuleStoredAxis(RobotActionId axis, const RcBindingConfig& binding,
                               RcInputMode type);

// A stored trigger slot: a cue, a puppet string or a Reaction. An empty slot
// holds. The input processor reads radio cues through it; a Reaction is held
// to its rules by the stored form already (rcTriggerBindingIsValid()), and is
// judged here for the RC Map's own answer (populateRcMapJson()).
RcRuleVerdict rcRuleStoredCue(const RcTriggerBinding& binding, RcInputMode type);

// The stored drive pair: each bound axis, then the pair (rcRuleDrive()). The
// verdict's `axis` says which axis a refusal is about.
RcRuleVerdict rcRuleStoredDrive(const RcBindingConfig& speed, const RcBindingConfig& steer,
                                RcInputMode type);
