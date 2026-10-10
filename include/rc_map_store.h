// =============================================================================
// include/rc_map_store.h
//
// The RC Map Store (GLOSSARY.md, ADR 0070 amended 2026-10-10, #490): where the
// RC Map is kept. One table of its 11 trigger places and one of its six drive
// and dome axes, each place with its NVS key (unchanged, ADR 0068), the
// SystemConfig field it is held in and its default. The defaults, the load,
// the save and the single-SBUS label carry loop over these tables, so a place
// is named here and in SystemConfig's own fields and nowhere else.
//
// The legacy PWM and SBUS arm and sound bindings (rc_pwm_arm1/arm2/audio,
// rc_sbus_arm1/arm2/audio) are not RC Map places and stay outside it.
//
// The tables are constant-initialised (`inline constexpr`), so they sit in
// flash and cost no stack frame on any task's chain. Their defaults are kept
// as the few numbers that pick one, not as a built binding: a built
// RcTriggerBinding is made with strncpy, which would move the table to RAM and
// build it at boot.
//
// It sits above include/config_store.h (it needs SystemConfig whole to name its
// fields), and the RC Map's rules (include/rc_map_rules.h) sit below it.
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "config_store.h"     // SystemConfig, RC_TRIGGER_SLOT_COUNT
#include "rc_action_types.h"  // RcTriggerBinding, makeRcTriggerBinding()
#include "rc_binding_types.h"  // RcBindingConfig, defaultPwmBinding(), defaultSbusBinding()

// -----------------------------------------------------------------------------
// Trigger places
// -----------------------------------------------------------------------------

// One stored trigger place. A default whose source is RC_BINDING_NONE is the
// disabled binding (disabledRcTriggerBinding(), with its own PWM-range
// calibration); any other is bound on that channel with the SBUS calibration
// and the receiver's idle polarity (rcTriggerDefaultReverse()).
struct RcTriggerPlace {
    const char* key;  // NVS key
    RcTriggerBinding SystemConfig::*place;
    RcBindingSource defaultSource;
    uint8_t defaultChannel;
    RobotActionId defaultAction;
};

// In tier-2 dispatch order. The RC input task, ReactionTask, the RC snapshot,
// the RC Map's save and GET, the single-SBUS carry and the sequence
// dangling-binding scan index per-slot state by this order, so a place is
// added at the end. Note rc_audio's key is "rc_aud": it was carried from
// "rc_sound" by migrateSchema2To3() (src/config_store.cpp) and is what is stored.
inline constexpr RcTriggerPlace RC_MAP_TRIGGER_PLACES[] = {
    {"rc_arm1", &SystemConfig::rc_arm1, RC_BINDING_SBUS1, 4, SERVO_ACTION_ARM1_TOGGLE},
    {"rc_arm2", &SystemConfig::rc_arm2, RC_BINDING_SBUS1, 5, SERVO_ACTION_ARM2_TOGGLE},
    {"rc_aux1", &SystemConfig::rc_aux1, RC_BINDING_NONE, 0, ROBOT_ACTION_NONE},
    {"rc_aux2", &SystemConfig::rc_aux2, RC_BINDING_NONE, 0, ROBOT_ACTION_NONE},
    {"rc_aux3", &SystemConfig::rc_aux3, RC_BINDING_NONE, 0, ROBOT_ACTION_NONE},
    {"rc_aud", &SystemConfig::rc_audio, RC_BINDING_NONE, 0, ROBOT_ACTION_NONE},
    {"rc_opmode", &SystemConfig::rc_opmode, RC_BINDING_NONE, 0, ROBOT_ACTION_NONE},
    {"rc_free0", &SystemConfig::rc_free0, RC_BINDING_NONE, 0, ROBOT_ACTION_NONE},
    {"rc_free1", &SystemConfig::rc_free1, RC_BINDING_NONE, 0, ROBOT_ACTION_NONE},
    {"rc_free2", &SystemConfig::rc_free2, RC_BINDING_NONE, 0, ROBOT_ACTION_NONE},
    {"rc_free3", &SystemConfig::rc_free3, RC_BINDING_NONE, 0, ROBOT_ACTION_NONE},
};

static_assert(sizeof(RC_MAP_TRIGGER_PLACES) / sizeof(RC_MAP_TRIGGER_PLACES[0]) == RC_TRIGGER_SLOT_COUNT,
              "RC_TRIGGER_SLOT_COUNT counts the RC Map's trigger places");
// The dispatch order, pinned: readers index per-slot state by it.
static_assert(RC_MAP_TRIGGER_PLACES[0].place == &SystemConfig::rc_arm1 &&
                  RC_MAP_TRIGGER_PLACES[1].place == &SystemConfig::rc_arm2 &&
                  RC_MAP_TRIGGER_PLACES[2].place == &SystemConfig::rc_aux1 &&
                  RC_MAP_TRIGGER_PLACES[3].place == &SystemConfig::rc_aux2 &&
                  RC_MAP_TRIGGER_PLACES[4].place == &SystemConfig::rc_aux3 &&
                  RC_MAP_TRIGGER_PLACES[5].place == &SystemConfig::rc_audio &&
                  RC_MAP_TRIGGER_PLACES[6].place == &SystemConfig::rc_opmode &&
                  RC_MAP_TRIGGER_PLACES[7].place == &SystemConfig::rc_free0 &&
                  RC_MAP_TRIGGER_PLACES[8].place == &SystemConfig::rc_free1 &&
                  RC_MAP_TRIGGER_PLACES[9].place == &SystemConfig::rc_free2 &&
                  RC_MAP_TRIGGER_PLACES[10].place == &SystemConfig::rc_free3,
              "the RC Map's trigger places are in tier-2 dispatch order");

inline RcTriggerBinding rcTriggerPlaceDefault(const RcTriggerPlace& place) {
    if (place.defaultSource == RC_BINDING_NONE) {
        return disabledRcTriggerBinding();
    }
    return makeRcTriggerBinding(place.defaultSource, place.defaultChannel, place.defaultAction,
                                nullptr, RC_SBUS_DEFAULT_MIN, RC_SBUS_DEFAULT_CENTER,
                                RC_SBUS_DEFAULT_MAX, 0,
                                rcTriggerDefaultReverse(place.defaultSource, place.defaultChannel));
}

// The trigger places of `sys`, in the table's order. A place of a SystemConfig
// that is not const may be written through its pointer.
inline void rcTriggerSlotPlaces(const SystemConfig& sys, const RcTriggerBinding* out[RC_TRIGGER_SLOT_COUNT]) {
    for (size_t i = 0; i < RC_TRIGGER_SLOT_COUNT; ++i) {
        out[i] = &(sys.*RC_MAP_TRIGGER_PLACES[i].place);
    }
}

inline size_t rcTriggerSlotsCopy(const SystemConfig& sys, RcTriggerBinding* out, size_t cap) {
    const size_t n = (cap < RC_TRIGGER_SLOT_COUNT) ? cap : RC_TRIGGER_SLOT_COUNT;
    for (size_t i = 0; i < n; ++i) {
        out[i] = sys.*RC_MAP_TRIGGER_PLACES[i].place;
    }
    return n;
}

// -----------------------------------------------------------------------------
// Axis places
// -----------------------------------------------------------------------------

// Which receiver type's group an axis place belongs to: the PWM group is what
// a standard PWM receiver reads, the SBUS group what every SBUS receiver reads.
enum class RcMapAxisGroup : uint8_t {
    Pwm,
    Sbus,
};

enum class RcMapAxis : uint8_t {
    DriveSpeed,
    DriveSteer,
    DomeSpeed,
};

// One stored axis place. Its default is bound: a PWM source takes the PWM
// calibration (defaultPwmBinding()), an SBUS one the SBUS calibration
// (defaultSbusBinding()).
struct RcAxisPlace {
    const char* key;  // NVS key
    RcBindingConfig SystemConfig::*place;
    RcMapAxisGroup group;
    RcMapAxis axis;
    RcBindingSource defaultSource;
    uint8_t defaultChannel;
};

inline constexpr RcAxisPlace RC_MAP_AXIS_PLACES[] = {
    {"rcp_drv", &SystemConfig::rc_pwm_drive_speed, RcMapAxisGroup::Pwm, RcMapAxis::DriveSpeed,
     RC_BINDING_PWM, 1},
    {"rcp_str", &SystemConfig::rc_pwm_drive_steer, RcMapAxisGroup::Pwm, RcMapAxis::DriveSteer,
     RC_BINDING_PWM, 2},
    {"rcp_dom", &SystemConfig::rc_pwm_dome_speed, RcMapAxisGroup::Pwm, RcMapAxis::DomeSpeed,
     RC_BINDING_PWM, 3},
    {"rcs_drv", &SystemConfig::rc_sbus_drive_speed, RcMapAxisGroup::Sbus, RcMapAxis::DriveSpeed,
     RC_BINDING_SBUS1, 1},
    {"rcs_str", &SystemConfig::rc_sbus_drive_steer, RcMapAxisGroup::Sbus, RcMapAxis::DriveSteer,
     RC_BINDING_SBUS1, 2},
    {"rcs_dom", &SystemConfig::rc_sbus_dome_speed, RcMapAxisGroup::Sbus, RcMapAxis::DomeSpeed,
     RC_BINDING_SBUS2, 1},
};

static constexpr size_t RC_MAP_AXIS_PLACE_COUNT = sizeof(RC_MAP_AXIS_PLACES) / sizeof(RC_MAP_AXIS_PLACES[0]);
static_assert(RC_MAP_AXIS_PLACE_COUNT == 6, "the RC Map has a drive speed, a drive steer and a dome "
                                            "speed axis in each of its two groups");

inline RcBindingConfig rcAxisPlaceDefault(const RcAxisPlace& place) {
    if (place.defaultSource == RC_BINDING_PWM) {
        return defaultPwmBinding(place.defaultChannel);
    }
    return defaultSbusBinding(place.defaultSource, place.defaultChannel);
}

// -----------------------------------------------------------------------------
// The single-SBUS label carry (#389)
// -----------------------------------------------------------------------------

// The NVS key that marks the single-SBUS trigger labels as already carried
// across #389, and the carry itself. Before #389 a single_sbus receiver on the
// CH2 header was read as SBUS2 by the trigger path, so its working triggers
// were stored `sbus2` and its `sbus1` ones were inert; since #389 that receiver
// is SBUS1. Swapping the two labels on every trigger place keeps exactly the
// triggers that fired firing (an estop switch among them) and the inert ones
// inert. Drive and dome bindings already read the receiver as SBUS1 and are
// left alone. Returns how many places changed. Only for single_sbus on CH2.
constexpr char CONFIG_RC_SINGLE_LABELS_KEY[] = "rc_single_lbl";
inline size_t rcCarrySingleSbusCh2TriggerLabels(SystemConfig* sys) {
    if (sys == nullptr || sys->rc_input_mode != RC_INPUT_SINGLE_SBUS || !sys->single_sbus_use_ch2) {
        return 0;
    }
    size_t changed = 0;
    for (const RcTriggerPlace& place : RC_MAP_TRIGGER_PLACES) {
        RcTriggerBinding& slot = sys->*place.place;
        if (slot.source == RC_BINDING_SBUS1) {
            slot.source = RC_BINDING_SBUS2;
            ++changed;
        } else if (slot.source == RC_BINDING_SBUS2) {
            slot.source = RC_BINDING_SBUS1;
            ++changed;
        }
    }
    return changed;
}
