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

// A rule's answer. `sentence` is the refusal as POST /api/rc/map says it; null
// when the rule holds. `aboutEntry`: the refusal names the entry it is about
// (the answer echoes it), rather than the map or a field of an entry. `axis`:
// which drive axis a drive-pair refusal is about, ROBOT_ACTION_NONE otherwise.
struct RcRuleVerdict {
    const char* sentence;
    bool aboutEntry;
    RobotActionId axis;
    bool ok() const { return sentence == nullptr; }
};

// Whether `next` may join the `count` entries already in an RC Map: its RC
// Channel, what a droid condition may fire and the numbers it carries, a
// puppet string's stick, and the conflicts with the entries before it. A
// payload that must name something that exists (a Part, a dome sequence) is
// the caller's to check.
RcRuleVerdict rcRuleMapAdd(const RcMapEntry* prior, size_t count, const RcMapEntry& next);

// The drive pair. Speed and Steer are read together, from one frame of one
// receiver, so they sit on one RC Receiver; and that receiver is not SBUS2,
// because only SBUS1, the drive receiver, carries the drive watchdog and the
// hardware-failsafe stop (operator, 2026-10-09 on #389). An unbound axis
// (RC_BINDING_NONE) breaks neither rule.
RcRuleVerdict rcRuleDrive(const RcBindingConfig& speed, const RcBindingConfig& steer);

// An axis's calibration: end, centre and end in order, and a dead zone that
// leaves stick travel on both sides of the centre.
RcRuleVerdict rcRuleAxisCalibration(const RcBindingConfig& binding);
