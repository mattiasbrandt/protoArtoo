// =============================================================================
// include/rc_action_types.h
//
// RC action tokens, trigger bindings, and action classification helpers.
// Split from rc_mapping.h; rc_mapping.h re-exports both halves for compatibility.
//
// This header defines types and declarations only. Function bodies are in
// src/rc_action_types.cpp to reduce header bloat (formerly ~700 lines of inlines).
// =============================================================================
#pragma once

#include <cstdlib>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include "rc_binding_types.h"

// -----------------------------------------------------------------------------
// Tier 2 Trigger/Button Action Targets
// Defines what a trigger/button binding DOES (the action it triggers)
// -----------------------------------------------------------------------------
enum RobotActionId : uint8_t {
    ROBOT_ACTION_NONE = 0,        // Unbound / disabled slot
    DRIVE_ACTION_SPEED,           // Analog: forward/back movement
    DRIVE_ACTION_STEER,           // Analog: left/right steering
    DOME_ACTION_SPEED,            // Analog: dome rotation speed
    SYSTEM_ACTION_OP_MODE,        // Switch: Driving (LOW) / Stationary (HIGH)
    SERVO_ACTION_ARM1_TOGGLE,     // Button: ARM1 open/close toggle
    SERVO_ACTION_ARM2_TOGGLE,     // Button: ARM2 open/close toggle
    SERVO_ACTION_AUX1_TOGGLE,     // Button: AUX1 toggle
    SERVO_ACTION_AUX2_TOGGLE,     // Button: AUX2 toggle
    SERVO_ACTION_AUX3_TOGGLE,     // Button: AUX3 toggle
    DOME_ACTION_MARCDUINO_SEQ,    // Button: Body sequence SE30-SE36
    DOME_ACTION_MARCDUINO_CMD,    // Button: Arbitrary Marcduino command
    SOUND_ACTION_RANDOM_GENERAL,
    SOUND_ACTION_RANDOM_CHATTY,
    SOUND_ACTION_RANDOM_HAPPY,
    SOUND_ACTION_RANDOM_PROCESSING,
    SOUND_ACTION_RANDOM_SAD,
    SOUND_ACTION_RANDOM_SENTIMENTAL,
    SOUND_ACTION_RANDOM_HUMMING,
    SOUND_ACTION_RANDOM_SCREAM,
    SOUND_ACTION_RANDOM_SURPRISED,
    SOUND_ACTION_RANDOM_ALERT,
    SOUND_ACTION_RANDOM_SNARKY,
    SOUND_ACTION_RANDOM_WHISTLE,
    SYSTEM_ACTION_ESTOP,          // Button: Latch estop (guarded)
    SYSTEM_ACTION_SLEEP_TOGGLE,   // Button: toggle sleep/wake mode
    DOME_ACTION_SEQ,              // Button: Forward DM:<NAME> sequence to dome (DM:VADER, DM:LEIA, etc.)
    DROID_SEQ_SCREAM,           // Button: SE01 scream + body + dome forward
    DROID_SEQ_WAVE,             // Button: SE02 wave sequence
    DROID_SEQ_FAST_WAVE,        // Button: SE03 fast wave sequence
    DROID_SEQ_OPEN_WAVE,        // Button: SE04 open wave sequence
    DROID_SEQ_BEEP_CANTINA,     // Button: SE05 beep cantina sequence
    DROID_SEQ_FAINT,            // Button: SE06 faint sequence
    DROID_SEQ_CANTINA,          // Button: SE07 cantina dance sequence
    DROID_SEQ_LEIA,             // Button: SE08 leia sequence
    DROID_SEQ_DISCO,            // Button: SE09 disco sequence
    DROID_SEQ_SCREAMS,          // Button: SE15 screams (audio-only body side)
    DROID_SEQ_WIGGLE,           // Button: SE16 panel wiggle sequence
    DRIVE_ACTION_SPEED_PRESET_CYCLE,  // Button: cycle Slow/Normal/Turbo speed presets
};

// -----------------------------------------------------------------------------
// Tier 2 Trigger/Button Binding
// Extends backbone binding with action target and optional Marcduino payload
// -----------------------------------------------------------------------------
struct RcTriggerBinding {
    RcBindingSource source;     // PWM, SBUS1, SBUS2, or NONE
    uint8_t channel;            // Channel number (1-6 for PWM, 1-18 for SBUS)
    RobotActionId target;      // What action this binding triggers
    char marcduinoPayload[16];  // Payload for SEQ/CMD targets (e.g., "SE30", ":OP01")
    uint16_t min;               // Calibration: minimum raw value
    uint16_t center;            // Calibration: center raw value
    uint16_t max;               // Calibration: maximum raw value
    uint16_t deadband;          // Calibration: deadband around center
    bool reverse;               // Calibration: reverse direction
};

// =============================================================================
// Trivial Inline Accessors
// =============================================================================

// Resolve a random track from an inclusive [lo, hi] category range.
// Returns false when the range is inactive (lo==0 or lo>hi) or outTrack is null.
inline bool selectRandomTrackInRange(uint16_t lo, uint16_t hi, uint32_t randomValue,
                                     uint16_t* outTrack) {
    if (outTrack == nullptr || lo == 0 || lo > hi) {
        return false;
    }
    const uint32_t span = (uint32_t)hi - (uint32_t)lo + 1U;
    *outTrack = (uint16_t)((uint32_t)lo + (randomValue % span));
    return true;
}

// Human-readable category labels for random sound trigger actions.
// Returns nullptr for non-random actions.
inline const char* randomSoundCategoryLabel(RobotActionId target) {
    switch (target) {
        case SOUND_ACTION_RANDOM_GENERAL:
            return "general";
        case SOUND_ACTION_RANDOM_CHATTY:
            return "chatty";
        case SOUND_ACTION_RANDOM_HAPPY:
            return "happy";
        case SOUND_ACTION_RANDOM_PROCESSING:
            return "processing";
        case SOUND_ACTION_RANDOM_SAD:
            return "sad";
        case SOUND_ACTION_RANDOM_SENTIMENTAL:
            return "sentimental";
        case SOUND_ACTION_RANDOM_HUMMING:
            return "humming";
        case SOUND_ACTION_RANDOM_SCREAM:
            return "scream";
        case SOUND_ACTION_RANDOM_SURPRISED:
            return "surprised";
        case SOUND_ACTION_RANDOM_ALERT:
            return "alert";
        case SOUND_ACTION_RANDOM_SNARKY:
            return "snarky";
        case SOUND_ACTION_RANDOM_WHISTLE:
            return "whistle";
        default:
            return nullptr;
    }
}

// Inline action classification predicates - remain in header for use in routing logic.
inline bool robotActionNeedsPayload(RobotActionId target) {
    return target == DOME_ACTION_MARCDUINO_SEQ || target == DOME_ACTION_MARCDUINO_CMD ||
           target == DOME_ACTION_SEQ;
}

inline bool robotActionIsAnalog(RobotActionId target) {
    return target == DRIVE_ACTION_SPEED || target == DRIVE_ACTION_STEER ||
           target == DOME_ACTION_SPEED;
}

// Validate Marcduino command payload - must start with safe prefix
inline bool rcPayloadValidForMarcduinoCommand(const char* payload) {
    if (payload == nullptr || payload[0] == '\0') {
        return false;
    }
    // Accept only body-owned prefixes per topology contract
    // Allowed: : (panel), $ (sound), # (config)
    // Rejected: * (holo), @ (logic), % (pass-through), ! (alt), & (I2C)
    char prefix = payload[0];
    return prefix == ':' || prefix == '$' || prefix == '#';
}

// Struct builder (trivial assignment sequence, worth staying inline)
inline RcTriggerBinding makeRcTriggerBinding(RcBindingSource source, uint8_t channel,
                                             RobotActionId target, const char* payload,
                                             uint16_t min, uint16_t center, uint16_t max,
                                             uint16_t deadband, bool reverse) {
    RcTriggerBinding binding = {};
    binding.source = source;
    binding.channel = channel;
    binding.target = target;
    if (payload != nullptr) {
        strncpy(binding.marcduinoPayload, payload, sizeof(binding.marcduinoPayload) - 1);
        binding.marcduinoPayload[sizeof(binding.marcduinoPayload) - 1] = '\0';
    }
    binding.min = min;
    binding.center = center;
    binding.max = max;
    binding.deadband = deadband;
    binding.reverse = reverse;
    return binding;
}

inline RcTriggerBinding disabledRcTriggerBinding() {
    return makeRcTriggerBinding(RC_BINDING_NONE, 0, ROBOT_ACTION_NONE, nullptr, 1000, 1500, 2000, 0,
                                false);
}

// Calibration wrappers (thin delegation to backbone functions, worth staying inline)
inline float applyRcTriggerCalibration(int raw, const RcTriggerBinding& binding, bool* inDeadband) {
    RcBindingConfig backbone =
        makeRcBindingConfig(binding.source, binding.channel, binding.min, binding.center,
                            binding.max, binding.deadband, binding.reverse);
    return applyRcAnalogCalibration(raw, backbone, inDeadband);
}

inline RcSwitchState rcTriggerToSwitchState(int raw, const RcTriggerBinding& binding) {
    RcBindingConfig backbone =
        makeRcBindingConfig(binding.source, binding.channel, binding.min, binding.center,
                            binding.max, binding.deadband, binding.reverse);
    return rcAnalogToSwitchState(raw, backbone);
}

// =============================================================================
// Function Declarations (bodies in src/rc_action_types.cpp)
// =============================================================================

// String conversion: RobotActionId <-> string token
const char* robotActionIdToString(RobotActionId target);
bool parseRobotActionId(const char* raw, RobotActionId* out);

// Droid sequence ID mapping
int robotActionIdToDroidSeqId(RobotActionId target);

// Action classification predicates
bool robotActionValidForTier2(RobotActionId target);
bool robotActionIsButton(RobotActionId target);
bool robotActionIsOneShotButton(RobotActionId target);

// Payload validation
bool rcPayloadValidForBodySequence(const char* payload);
bool rcPayloadValidForDomeSequence(const char* payload);

// Binding validation and serialization
bool rcTriggerBindingIsValid(const RcTriggerBinding& binding);
bool formatRcTriggerBinding(char* buf, size_t bufSize, const RcTriggerBinding& binding);
bool parseRcTriggerBinding(const char* raw, RcTriggerBinding* out);
