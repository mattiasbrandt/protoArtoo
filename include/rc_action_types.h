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
    // Stick: a puppet string (#442, include/rc_puppet.h). Moves the Part its
    // payload names in proportion to the stick, so it is neither a button nor
    // a backbone axis: it lives in a trigger slot, is never debounced or
    // dispatched as a press, and reads no drive or dome-speed binding.
    // Appended, so every value above keeps its number.
    SERVO_ACTION_PUPPET_PART,
    // Button: the sound after, or before, the one the droid last played: within
    // its bank and page on a module that numbers sounds that way, within its
    // category range on one that does not (#447, ADR 0054). AudioTask knows
    // which sound that was, so the press carries only the direction.
    SOUND_ACTION_NEXT,
    SOUND_ACTION_PREVIOUS,
};

// -----------------------------------------------------------------------------
// Tier 2 Trigger/Button Binding
// Extends backbone binding with action target and optional Marcduino payload
//
// A Reaction (ADR 0053, #450) is this same binding with a droid condition as
// its source, and it is stored in these same slots: one binding editor, one
// stored form, one scan when a Sequence is deleted. A droid condition has no
// calibration, so on a Reaction the calibration fields carry what it does
// have, and are read only through rcReactionThreshold() / rcReactionQuietS()
// below:
//   min      the threshold, in the source's own unit (rcReactionThresholdMax())
//   max      how long it stays quiet after firing, in seconds
//   center, deadband, reverse   0, 0, false - one spelling per Reaction
// Slots of its own were the other choice and were not taken: each would add 30
// bytes to ConfigSnapshot, which every seam that crosses it pays for in stack
// (include/config_store.h, the 916-byte assertion).
// -----------------------------------------------------------------------------
struct RcTriggerBinding {
    RcBindingSource source;     // PWM, SBUS1, SBUS2, a droid condition, or NONE
    uint8_t channel;            // Channel number (1-6 for PWM, 1-18 for SBUS); the
                                // wheel for a per-wheel droid condition, else 1
    RobotActionId target;      // What action this binding triggers
    char marcduinoPayload[16];  // Payload for SEQ/CMD targets (e.g., "SE30", ":OP01")
    uint16_t min;               // Calibration: minimum raw value (Reaction: threshold)
    uint16_t center;            // Calibration: center raw value
    uint16_t max;               // Calibration: maximum raw value (Reaction: quiet seconds)
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

// A stick, not a press: the three backbone axes, and a puppet string. Nothing
// fires one once, so neither the REST action test nor the Console runs one, and
// a droid condition - which has no stick - may not be bound to one. Which slot
// an analog action is stored in is not this predicate's: the backbone axes have
// their own fields and a puppet string a trigger slot (assignRcMapEntryToSnapshot()).
inline bool robotActionIsAnalog(RobotActionId target) {
    return target == DRIVE_ACTION_SPEED || target == DRIVE_ACTION_STEER ||
           target == DOME_ACTION_SPEED || target == SERVO_ACTION_PUPPET_PART;
}

// Validate Marcduino command payload - must start with safe prefix
inline bool rcPayloadValidForMarcduinoCommand(const char* payload) {
    if (payload == nullptr || payload[0] == '\0') {
        return false;
    }
    // Allowed: : (panels, sequences), $ (sound), # (config). A ':' or '#'
    // line is routed by Command Ownership once it fires - the body runs what
    // it owns and forwards the rest to the dome (include/marcduino_router.h).
    // Rejected: * (holo), @ (logic), % (pass-through), ! (alt), & (I2C) - the
    // light families on a binding are a binding-surface question (#320)
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

// =============================================================================
// Reactions - a trigger binding whose source is a droid condition
// =============================================================================

inline uint16_t rcReactionThreshold(const RcTriggerBinding& binding) { return binding.min; }
inline uint16_t rcReactionQuietS(const RcTriggerBinding& binding) { return binding.max; }

// The quiet period a Reaction may store: a second to an hour. Never zero: that
// would leave only the droid-wide floor, 300 ms, between two firings of one
// Reaction, which is the car alarm ADR 0053 rejected.
static constexpr uint16_t RC_REACTION_QUIET_MIN_S = 1;
static constexpr uint16_t RC_REACTION_QUIET_MAX_S = 3600;
// What a Reaction nobody set a quiet period on holds. A starting value, not a
// measurement: long enough that a droid creeping across a threshold does not
// chatter, short enough that the second hard stop of a demo still answers.
static constexpr uint16_t RC_REACTION_QUIET_DEFAULT_S = 5;

// The largest threshold each droid condition accepts, in its own unit; the
// smallest is 1 everywhere a threshold exists, and a source with no threshold
// answers 0 and stores 0.
//   speed, hstop   drive units, the resolved output's own (-1000..1000)
//   rest           tenths of a second the drive has been at rest
//   wspeed         RPM, as the drive reports it
//   wamps          A x 100, as the drive reports it
uint16_t rcReactionThresholdMax(RcBindingSource source);

// What a Reaction nobody set a threshold on holds. Every one is a stated
// starting value: nobody has measured a droid to find them, and each is the
// builder's to change in the binding editor.
uint16_t rcReactionThresholdDefault(RcBindingSource source);

// What a Reaction may do. Everything a radio trigger may, less three: the
// estop (the guard every caller of the action door makes before it,
// evaluateActionTestGuard() in include/api_actions.h), and the two that act on
// the drive path - the drive lock and the speed preset - because the droid's
// own motion must never be what changes how it drives.
bool robotActionValidForReaction(RobotActionId target);

inline RcTriggerBinding makeRcReactionBinding(RcBindingSource source, uint8_t channel,
                                              RobotActionId target, const char* payload,
                                              uint16_t threshold, uint16_t quietS) {
    return makeRcTriggerBinding(source, channel, target, payload, threshold, 0, quietS, 0, false);
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

// Binding validation and serialization
bool rcTriggerBindingIsValid(const RcTriggerBinding& binding);
bool formatRcTriggerBinding(char* buf, size_t bufSize, const RcTriggerBinding& binding);
bool parseRcTriggerBinding(const char* raw, RcTriggerBinding* out);
