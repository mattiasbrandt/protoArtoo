// =============================================================================
// include/rc_mapping.h
//
// Pure RC mapping helpers shared by config persistence, runtime routing, and
// native tests. Bindings are persisted as compact NVS strings so receiver-mode
// routing can be remapped without rebuilding firmware.
// =============================================================================
#pragma once

#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static constexpr uint16_t RC_SBUS_DEFAULT_MIN = 172;
static constexpr uint16_t RC_SBUS_DEFAULT_CENTER = 992;
static constexpr uint16_t RC_SBUS_DEFAULT_MAX = 1811;
static constexpr float RC_SWITCH_TRIGGER = 0.4f;

enum RcBindingSource : uint8_t {
    RC_BINDING_NONE = 0,
    RC_BINDING_PWM,
    RC_BINDING_SBUS1,
    RC_BINDING_SBUS2,
};

struct RcBindingConfig {
    RcBindingSource source;
    uint8_t channel;
    uint16_t min;
    uint16_t center;
    uint16_t max;
    uint16_t deadband;
    bool reverse;
};

enum RcSwitchState : uint8_t {
    RC_SWITCH_LOW = 0,
    RC_SWITCH_MID,
    RC_SWITCH_HIGH,
    RC_SWITCH_INVALID,
};

// -----------------------------------------------------------------------------
// Tier 2 Trigger/Button Action Targets
// Defines what a trigger/button binding DOES (the action it triggers)
// -----------------------------------------------------------------------------
enum RobotActionId : uint8_t {
    ROBOT_ACTION_NONE = 0,        // Unbound / disabled slot
    DRIVE_ACTION_SPEED,     // Analog: forward/back movement
    DRIVE_ACTION_STEER,     // Analog: left/right steering
    DOME_ACTION_SPEED,      // Analog: dome rotation speed
    DRIVE_ACTION_SPEED_LIMIT,     // Analog: speed ceiling dial
    SYSTEM_ACTION_OP_MODE,  // Switch: Driving (LOW) / Stationary (HIGH)
    SERVO_ACTION_ARM1_TOGGLE,     // Button: ARM1 open/close toggle
    SERVO_ACTION_ARM2_TOGGLE,     // Button: ARM2 open/close toggle
    SERVO_ACTION_AUX1_TOGGLE,     // Button: AUX1 toggle
    SERVO_ACTION_AUX2_TOGGLE,     // Button: AUX2 toggle
    SERVO_ACTION_AUX3_TOGGLE,     // Button: AUX3 toggle
    DOME_ACTION_MARCDUINO_SEQ,   // Button: Body sequence SE30-SE36
    DOME_ACTION_MARCDUINO_CMD,   // Button: Arbitrary Marcduino command
    SYSTEM_ACTION_ESTOP,     // Button: Latch estop (guarded)
    SYSTEM_ACTION_SLEEP_TOGGLE,  // Button: toggle sleep/wake mode
    DOME_ACTION_SEQ,        // Button: Dome sequence SE10-SE16 (Phase 4)
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

inline RcBindingConfig makeRcBindingConfig(RcBindingSource source, uint8_t channel, uint16_t min,
                                           uint16_t center, uint16_t max, uint16_t deadband,
                                           bool reverse) {
    RcBindingConfig binding = {source, channel, min, center, max, deadband, reverse};
    return binding;
}

inline RcBindingConfig defaultPwmBinding(uint8_t channel) {
    return makeRcBindingConfig(RC_BINDING_PWM, channel, 1000, 1500, 2000, 0, false);
}

inline RcBindingConfig defaultSbusBinding(RcBindingSource source, uint8_t channel) {
    return makeRcBindingConfig(source, channel, RC_SBUS_DEFAULT_MIN, RC_SBUS_DEFAULT_CENTER,
                               RC_SBUS_DEFAULT_MAX, 0, false);
}

inline RcBindingConfig disabledRcBinding() {
    return makeRcBindingConfig(RC_BINDING_NONE, 0, 1000, 1500, 2000, 0, false);
}

inline const char* rcBindingSourceToString(RcBindingSource source) {
    switch (source) {
        case RC_BINDING_PWM:
            return "pwm";
        case RC_BINDING_SBUS1:
            return "sbus1";
        case RC_BINDING_SBUS2:
            return "sbus2";
        case RC_BINDING_NONE:
        default:
            return "none";
    }
}

inline bool parseRcBindingSource(const char* raw, RcBindingSource* out) {
    if (raw == nullptr || out == nullptr) {
        return false;
    }
    if (strcmp(raw, "none") == 0) {
        *out = RC_BINDING_NONE;
        return true;
    }
    if (strcmp(raw, "pwm") == 0) {
        *out = RC_BINDING_PWM;
        return true;
    }
    if (strcmp(raw, "sbus1") == 0) {
        *out = RC_BINDING_SBUS1;
        return true;
    }
    if (strcmp(raw, "sbus2") == 0) {
        *out = RC_BINDING_SBUS2;
        return true;
    }
    return false;
}

inline bool rcBindingChannelIsValid(RcBindingSource source, uint8_t channel) {
    switch (source) {
        case RC_BINDING_NONE:
            return channel == 0;
        case RC_BINDING_PWM:
            return channel >= 1 && channel <= 6;
        case RC_BINDING_SBUS1:
        case RC_BINDING_SBUS2:
            return channel >= 1 && channel <= 18;
        default:
            return false;
    }
}

inline bool rcBindingIsDigital(const RcBindingConfig& binding) {
    return (binding.source == RC_BINDING_SBUS1 || binding.source == RC_BINDING_SBUS2) &&
           (binding.channel == 17 || binding.channel == 18);
}

inline bool rcBindingSupportsAnalog(const RcBindingConfig& binding) {
    if (binding.source == RC_BINDING_PWM) {
        return binding.channel >= 1 && binding.channel <= 6;
    }
    if (binding.source == RC_BINDING_SBUS1 || binding.source == RC_BINDING_SBUS2) {
        return binding.channel >= 1 && binding.channel <= 16;
    }
    return false;
}

inline bool rcBindingIsValid(const RcBindingConfig& binding) {
    if (!rcBindingChannelIsValid(binding.source, binding.channel)) {
        return false;
    }
    if (binding.source == RC_BINDING_NONE) {
        return true;
    }
    if (!(binding.min < binding.center && binding.center < binding.max)) {
        return false;
    }
    return binding.deadband < (uint16_t)(binding.max - binding.min);
}

inline bool formatRcBindingConfig(char* buf, size_t bufSize, const RcBindingConfig& binding) {
    if (buf == nullptr || bufSize == 0 || !rcBindingIsValid(binding)) {
        return false;
    }
    int written = snprintf(
        buf, bufSize, "%s:%u:%u:%u:%u:%u:%u", rcBindingSourceToString(binding.source),
        (unsigned int)binding.channel, (unsigned int)binding.min, (unsigned int)binding.center,
        (unsigned int)binding.max, (unsigned int)binding.deadband, binding.reverse ? 1u : 0u);
    return written > 0 && (size_t)written < bufSize;
}

inline bool parseRcBindingConfig(const char* raw, RcBindingConfig* out) {
    if (raw == nullptr || out == nullptr || raw[0] == '\0') {
        return false;
    }

    char sourceBuf[8] = {};
    unsigned int channel = 0;
    unsigned int min = 0;
    unsigned int center = 0;
    unsigned int max = 0;
    unsigned int deadband = 0;
    unsigned int reverse = 0;

    if (sscanf(raw, "%7[^:]:%u:%u:%u:%u:%u:%u", sourceBuf, &channel, &min, &center, &max, &deadband,
               &reverse) != 7) {
        return false;
    }

    RcBindingSource source = RC_BINDING_NONE;
    if (!parseRcBindingSource(sourceBuf, &source)) {
        return false;
    }

    RcBindingConfig binding =
        makeRcBindingConfig(source, (uint8_t)channel, (uint16_t)min, (uint16_t)center,
                            (uint16_t)max, (uint16_t)deadband, reverse != 0);
    if (!rcBindingIsValid(binding)) {
        return false;
    }

    *out = binding;
    return true;
}

inline float applyRcAnalogCalibration(int raw, const RcBindingConfig& binding, bool* inDeadband) {
    if (!rcBindingSupportsAnalog(binding)) {
        if (inDeadband != nullptr) {
            *inDeadband = false;
        }
        return 0.0f;
    }

    int clamped = raw;
    if (clamped < (int)binding.min) {
        clamped = binding.min;
    }
    if (clamped > (int)binding.max) {
        clamped = binding.max;
    }

    int delta = clamped - (int)binding.center;
    if (inDeadband != nullptr) {
        *inDeadband = abs(delta) <= (int)binding.deadband;
    }
    if (abs(delta) <= (int)binding.deadband) {
        return 0.0f;
    }

    float mapped = 0.0f;
    if (delta > 0) {
        int span = (int)binding.max - (int)binding.center;
        mapped = span > 0 ? (float)delta / (float)span : 0.0f;
    } else {
        int span = (int)binding.center - (int)binding.min;
        mapped = span > 0 ? (float)delta / (float)span : 0.0f;
    }

    if (mapped < -1.0f) {
        mapped = -1.0f;
    }
    if (mapped > 1.0f) {
        mapped = 1.0f;
    }
    if (binding.reverse) {
        mapped = -mapped;
    }
    return mapped;
}

inline RcSwitchState rcAnalogToSwitchState(int raw, const RcBindingConfig& binding) {
    if (!rcBindingSupportsAnalog(binding)) {
        return RC_SWITCH_INVALID;
    }
    float value = applyRcAnalogCalibration(raw, binding, nullptr);
    if (value <= -RC_SWITCH_TRIGGER) {
        return RC_SWITCH_LOW;
    }
    if (value >= RC_SWITCH_TRIGGER) {
        return RC_SWITCH_HIGH;
    }
    return RC_SWITCH_MID;
}

// -----------------------------------------------------------------------------
// Tier 2 Trigger Binding Helpers
// -----------------------------------------------------------------------------

inline const char* robotActionIdToString(RobotActionId target) {
    switch (target) {
        case DRIVE_ACTION_SPEED:
            return "drive_speed";
        case DRIVE_ACTION_STEER:
            return "drive_steer";
        case DOME_ACTION_SPEED:
            return "dome_speed";
        case DRIVE_ACTION_SPEED_LIMIT:
            return "speed_limit";
        case SYSTEM_ACTION_OP_MODE:
            return "op_mode";
        case SERVO_ACTION_ARM1_TOGGLE:
            return "arm1_toggle";
        case SERVO_ACTION_ARM2_TOGGLE:
            return "arm2_toggle";
        case SERVO_ACTION_AUX1_TOGGLE:
            return "aux1_toggle";
        case SERVO_ACTION_AUX2_TOGGLE:
            return "aux2_toggle";
        case SERVO_ACTION_AUX3_TOGGLE:
            return "aux3_toggle";
        case DOME_ACTION_MARCDUINO_SEQ:
            return "seq";
        case DOME_ACTION_MARCDUINO_CMD:
            return "cmd";
        case SYSTEM_ACTION_ESTOP:
            return "estop";
        case SYSTEM_ACTION_SLEEP_TOGGLE:
            return "sleep_toggle";
        case DOME_ACTION_SEQ:
            return "dome_seq";
        case ROBOT_ACTION_NONE:
        default:
            return "none";
    }
}

inline bool parseRobotActionId(const char* raw, RobotActionId* out) {
    if (raw == nullptr || out == nullptr) {
        return false;
    }
    if (strcmp(raw, "none") == 0) {
        *out = ROBOT_ACTION_NONE;
        return true;
    }
    if (strcmp(raw, "drive_speed") == 0) {
        *out = DRIVE_ACTION_SPEED;
        return true;
    }
    if (strcmp(raw, "drive_steer") == 0) {
        *out = DRIVE_ACTION_STEER;
        return true;
    }
    if (strcmp(raw, "dome_speed") == 0) {
        *out = DOME_ACTION_SPEED;
        return true;
    }
    if (strcmp(raw, "speed_limit") == 0) {
        *out = DRIVE_ACTION_SPEED_LIMIT;
        return true;
    }
    if (strcmp(raw, "op_mode") == 0) {
        *out = SYSTEM_ACTION_OP_MODE;
        return true;
    }
    if (strcmp(raw, "arm1_toggle") == 0) {
        *out = SERVO_ACTION_ARM1_TOGGLE;
        return true;
    }
    if (strcmp(raw, "arm2_toggle") == 0) {
        *out = SERVO_ACTION_ARM2_TOGGLE;
        return true;
    }
    if (strcmp(raw, "aux1_toggle") == 0) {
        *out = SERVO_ACTION_AUX1_TOGGLE;
        return true;
    }
    if (strcmp(raw, "aux2_toggle") == 0) {
        *out = SERVO_ACTION_AUX2_TOGGLE;
        return true;
    }
    if (strcmp(raw, "aux3_toggle") == 0) {
        *out = SERVO_ACTION_AUX3_TOGGLE;
        return true;
    }
    if (strcmp(raw, "seq") == 0) {
        *out = DOME_ACTION_MARCDUINO_SEQ;
        return true;
    }
    if (strcmp(raw, "cmd") == 0) {
        *out = DOME_ACTION_MARCDUINO_CMD;
        return true;
    }
    if (strcmp(raw, "estop") == 0) {
        *out = SYSTEM_ACTION_ESTOP;
        return true;
    }
    if (strcmp(raw, "sleep_toggle") == 0) {
        *out = SYSTEM_ACTION_SLEEP_TOGGLE;
        return true;
    }
    if (strcmp(raw, "dome_seq") == 0) {
        *out = DOME_ACTION_SEQ;
        return true;
    }
    return false;
}

inline bool robotActionNeedsPayload(RobotActionId target) {
    return target == DOME_ACTION_MARCDUINO_SEQ || target == DOME_ACTION_MARCDUINO_CMD ||
           target == DOME_ACTION_SEQ;
}

inline bool robotActionIsAnalog(RobotActionId target) {
    return target == DRIVE_ACTION_SPEED || target == DRIVE_ACTION_STEER ||
           target == DOME_ACTION_SPEED || target == DRIVE_ACTION_SPEED_LIMIT;
}

// Tier 2 trigger bindings only support button/switch actions (not analog axes)
// Analog targets (drive_speed, drive_steer, dome_speed, speed_limit) are backbone-only
inline bool robotActionValidForTier2(RobotActionId target) {
    return target == ROBOT_ACTION_NONE || target == SYSTEM_ACTION_OP_MODE ||
           target == SERVO_ACTION_ARM1_TOGGLE || target == SERVO_ACTION_ARM2_TOGGLE ||
           target == SERVO_ACTION_AUX1_TOGGLE || target == SERVO_ACTION_AUX2_TOGGLE ||
           target == SERVO_ACTION_AUX3_TOGGLE || target == DOME_ACTION_MARCDUINO_SEQ ||
           target == DOME_ACTION_MARCDUINO_CMD || target == SYSTEM_ACTION_ESTOP ||
           target == SYSTEM_ACTION_SLEEP_TOGGLE || target == DOME_ACTION_SEQ;
}

// Validate Marcduino sequence payload for body sequences (SE30-SE36)
inline bool rcPayloadValidForBodySequence(const char* payload) {
    if (payload == nullptr || payload[0] == '\0') {
        return false;
    }
    // Format must be exactly 2 digits (30-36)
    size_t len = strlen(payload);
    if (len != 2) {
        return false;
    }
    if (!isdigit((unsigned char)payload[0]) || !isdigit((unsigned char)payload[1])) {
        return false;
    }
    int seqNum = (payload[0] - '0') * 10 + (payload[1] - '0');
    return seqNum >= 30 && seqNum <= 36;
}

// Validate Marcduino sequence payload for dome sequences (SE10-SE16)
inline bool rcPayloadValidForDomeSequence(const char* payload) {
    if (payload == nullptr || payload[0] == '\0') {
        return false;
    }
    size_t len = strlen(payload);
    if (len != 2) {
        return false;
    }
    if (!isdigit((unsigned char)payload[0]) || !isdigit((unsigned char)payload[1])) {
        return false;
    }
    int seqNum = (payload[0] - '0') * 10 + (payload[1] - '0');
    return seqNum >= 10 && seqNum <= 16;
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

inline bool robotActionIsButton(RobotActionId target) {
    return target == SERVO_ACTION_ARM1_TOGGLE || target == SERVO_ACTION_ARM2_TOGGLE ||
           target == SERVO_ACTION_AUX1_TOGGLE || target == SERVO_ACTION_AUX2_TOGGLE ||
           target == SERVO_ACTION_AUX3_TOGGLE || target == DOME_ACTION_MARCDUINO_SEQ ||
           target == DOME_ACTION_MARCDUINO_CMD || target == SYSTEM_ACTION_ESTOP ||
           target == SYSTEM_ACTION_SLEEP_TOGGLE || target == DOME_ACTION_SEQ;
}

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

inline bool rcTriggerBindingIsValid(const RcTriggerBinding& binding) {
    if (!rcBindingChannelIsValid(binding.source, binding.channel)) {
        return false;
    }
    if (binding.source == RC_BINDING_NONE) {
        return binding.target == ROBOT_ACTION_NONE;
    }
    // Tier 2 bindings cannot use analog action targets (those are backbone-only)
    if (!robotActionValidForTier2(binding.target)) {
        return false;
    }
    if (!(binding.min < binding.center && binding.center < binding.max)) {
        return false;
    }
    if (binding.deadband >= (uint16_t)(binding.max - binding.min)) {
        return false;
    }
    return true;
}

inline bool formatRcTriggerBinding(char* buf, size_t bufSize, const RcTriggerBinding& binding) {
    if (buf == nullptr || bufSize == 0 || !rcTriggerBindingIsValid(binding)) {
        return false;
    }
    buf[0] = '\0';
    const char* payload = binding.marcduinoPayload[0] != '\0' ? binding.marcduinoPayload : "";
    int written = snprintf(
        buf, bufSize, "%s:%u:%s:%s:%u:%u:%u:%u:%u", rcBindingSourceToString(binding.source),
        (unsigned int)binding.channel, robotActionIdToString(binding.target), payload,
        (unsigned int)binding.min, (unsigned int)binding.center, (unsigned int)binding.max,
        (unsigned int)binding.deadband, binding.reverse ? 1u : 0u);
    if (written > 0 && (size_t)written < bufSize) {
        return true;
    }
    buf[bufSize - 1] = '\0';
    return false;
}

inline bool parseRcTriggerBinding(const char* raw, RcTriggerBinding* out) {
    if (raw == nullptr || out == nullptr || raw[0] == '\0') {
        return false;
    }

    const char* p1 = strchr(raw, ':');
    if (p1 == nullptr) {
        return false;
    }
    const char* p2 = strchr(p1 + 1, ':');
    if (p2 == nullptr) {
        return false;
    }
    const char* p3 = strchr(p2 + 1, ':');
    if (p3 == nullptr) {
        return false;
    }

    const char* right = raw + strlen(raw);
    const char* sepReverse = nullptr;
    const char* sepDeadband = nullptr;
    const char* sepMax = nullptr;
    const char* sepCenter = nullptr;
    const char* sepMin = nullptr;

    const char* scan = right;
    sepReverse = strrchr(raw, ':');
    if (sepReverse == nullptr || sepReverse <= p3) {
        return false;
    }

    scan = sepReverse - 1;
    while (scan >= p3 && *scan != ':') {
        --scan;
    }
    if (scan <= p3) {
        return false;
    }
    sepDeadband = scan;

    scan = sepDeadband - 1;
    while (scan >= p3 && *scan != ':') {
        --scan;
    }
    if (scan <= p3) {
        return false;
    }
    sepMax = scan;

    scan = sepMax - 1;
    while (scan >= p3 && *scan != ':') {
        --scan;
    }
    if (scan <= p3) {
        return false;
    }
    sepCenter = scan;

    scan = sepCenter - 1;
    while (scan >= p3 && *scan != ':') {
        --scan;
    }
    if (scan <= p3) {
        return false;
    }
    sepMin = scan;

    char sourceBuf[8] = {};
    size_t sourceLen = (size_t)(p1 - raw);
    if (sourceLen == 0 || sourceLen >= sizeof(sourceBuf)) {
        return false;
    }
    memcpy(sourceBuf, raw, sourceLen);
    sourceBuf[sourceLen] = '\0';

    char channelBuf[8] = {};
    size_t channelLen = (size_t)(p2 - (p1 + 1));
    if (channelLen == 0 || channelLen >= sizeof(channelBuf)) {
        return false;
    }
    memcpy(channelBuf, p1 + 1, channelLen);
    channelBuf[channelLen] = '\0';

    char targetBuf[16] = {};
    size_t targetLen = (size_t)(p3 - (p2 + 1));
    if (targetLen == 0 || targetLen >= sizeof(targetBuf)) {
        return false;
    }
    memcpy(targetBuf, p2 + 1, targetLen);
    targetBuf[targetLen] = '\0';

    char payloadBuf[16] = {};
    size_t payloadLen = (size_t)(sepMin - (p3 + 1));
    if (payloadLen >= sizeof(payloadBuf)) {
        return false;
    }
    if (payloadLen > 0) {
        memcpy(payloadBuf, p3 + 1, payloadLen);
    }
    payloadBuf[payloadLen] = '\0';

    auto parseUnsignedField = [](const char* begin, size_t len, unsigned int* outValue,
                                 unsigned int maxValue) {
        if (begin == nullptr || outValue == nullptr || len == 0) {
            return false;
        }
        unsigned int value = 0;
        for (size_t i = 0; i < len; ++i) {
            char c = begin[i];
            if (!isdigit((unsigned char)c)) {
                return false;
            }
            unsigned int digit = (unsigned int)(c - '0');
            if (value > (UINT_MAX - digit) / 10u) {
                return false;
            }
            value = (value * 10u) + digit;
            if (value > maxValue) {
                return false;
            }
        }
        *outValue = value;
        return true;
    };

    unsigned int channel = 0;
    unsigned int min = 0;
    unsigned int center = 0;
    unsigned int max = 0;
    unsigned int deadband = 0;
    unsigned int reverse = 0;

    if (!parseUnsignedField(channelBuf, strlen(channelBuf), &channel, 255u) ||
        !parseUnsignedField(sepMin + 1, (size_t)(sepCenter - (sepMin + 1)), &min, 65535u) ||
        !parseUnsignedField(sepCenter + 1, (size_t)(sepMax - (sepCenter + 1)), &center, 65535u) ||
        !parseUnsignedField(sepMax + 1, (size_t)(sepDeadband - (sepMax + 1)), &max, 65535u) ||
        !parseUnsignedField(sepDeadband + 1, (size_t)(sepReverse - (sepDeadband + 1)), &deadband,
                            65535u) ||
        !parseUnsignedField(sepReverse + 1, (size_t)(right - (sepReverse + 1)), &reverse, 1u)) {
        return false;
    }

    RcBindingSource source = RC_BINDING_NONE;
    if (!parseRcBindingSource(sourceBuf, &source)) {
        return false;
    }

    RobotActionId target = ROBOT_ACTION_NONE;
    if (!parseRobotActionId(targetBuf, &target)) {
        return false;
    }

    RcTriggerBinding binding =
        makeRcTriggerBinding(source, (uint8_t)channel, target, payloadBuf, (uint16_t)min,
                             (uint16_t)center, (uint16_t)max, (uint16_t)deadband, reverse != 0);
    if (!rcTriggerBindingIsValid(binding)) {
        return false;
    }

    *out = binding;
    return true;
}

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
