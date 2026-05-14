// =============================================================================
// include/rc_binding_types.h
//
// RC binding wire types, calibration helpers, and format/parse utilities.
// Split from rc_mapping.h; rc_mapping.h re-exports both halves for compatibility.
// =============================================================================
#pragma once

#include <cstdlib>
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

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

// DS-650 button channels (SBUS CH3-CH6) idle high and press low.
// Default trigger polarity therefore needs reverse=true on those channels
// so idle decodes as "not pressed" and press decodes as "pressed".
inline bool rcTriggerDefaultReverse(RcBindingSource source, uint8_t channel) {
    return (source == RC_BINDING_SBUS1 || source == RC_BINDING_SBUS2) &&
           channel >= 3 && channel <= 6;
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

// Display label for logs — uppercase. Use rcBindingSourceToString() for serialization
// (parsing expects lowercase; changing that function would break config round-trips).
inline const char* rcBindingSourceToLabel(RcBindingSource source) {
    switch (source) {
        case RC_BINDING_PWM:   return "PWM";
        case RC_BINDING_SBUS1: return "SBUS1";
        case RC_BINDING_SBUS2: return "SBUS2";
        case RC_BINDING_NONE:
        default:               return "NONE";
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
