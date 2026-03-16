#pragma once

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "rc_mapping.h"

static constexpr size_t RC_DIAGNOSTICS_SOURCE_CAPACITY = 3;
static constexpr size_t RC_DIAGNOSTICS_CHANNEL_CAPACITY = 7;

struct RcDiagnosticsSourceSnapshot {
    const char* key;
    bool enabled;
    bool linked;
    uint32_t ageMs;
    uint32_t lostFrames;
    bool failsafe;
};

struct RcDiagnosticsAnalogChannel {
    uint8_t id;
    const char* name;
    const char* activeSource;
    uint8_t bindingChannel;
    int raw;
    uint16_t rawUs;
    float normalized;
    float mapped;
    bool inDeadband;
    bool reverse;
};

struct RcDiagnosticsDigitalChannel {
    const char* name;
    const char* activeSource;
    uint8_t bindingChannel;
    bool pressed;
};

struct RcDiagnosticsMappingChannel {
    const char* name;
    RcBindingConfig binding;
};

struct RcDiagnosticsSnapshot {
    const char* mode;
    uint32_t updatedMs;
    RcDiagnosticsSourceSnapshot sources[RC_DIAGNOSTICS_SOURCE_CAPACITY];
    size_t sourceCount;
    RcDiagnosticsAnalogChannel analogChannels[RC_DIAGNOSTICS_CHANNEL_CAPACITY];
    size_t analogCount;
    RcDiagnosticsDigitalChannel digitalChannels[RC_DIAGNOSTICS_CHANNEL_CAPACITY];
    size_t digitalCount;
    RcDiagnosticsMappingChannel mappingChannels[RC_DIAGNOSTICS_CHANNEL_CAPACITY];
    size_t mappingCount;
};

inline const char* rcDiagnosticsSourceName(RcBindingSource source) {
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

inline float rcDiagnosticsNormalizeRaw(int raw, const RcBindingConfig& binding) {
    if (!rcBindingSupportsAnalog(binding)) {
        return 0.0f;
    }

    int clamped = raw;
    if (clamped < (int)binding.min) {
        clamped = binding.min;
    }
    if (clamped > (int)binding.max) {
        clamped = binding.max;
    }

    float normalized = 0.0f;
    int delta = clamped - (int)binding.center;
    if (delta > 0) {
        int span = (int)binding.max - (int)binding.center;
        normalized = span > 0 ? (float)delta / (float)span : 0.0f;
    } else if (delta < 0) {
        int span = (int)binding.center - (int)binding.min;
        normalized = span > 0 ? (float)delta / (float)span : 0.0f;
    }

    if (normalized < -1.0f) {
        normalized = -1.0f;
    }
    if (normalized > 1.0f) {
        normalized = 1.0f;
    }
    return normalized;
}

inline uint16_t rcDiagnosticsRawToPulseUs(int raw, const RcBindingConfig& binding) {
    if (!rcBindingSupportsAnalog(binding)) {
        return 1500;
    }
    if (binding.source == RC_BINDING_PWM) {
        if (raw < 1000) {
            return 1000;
        }
        if (raw > 2000) {
            return 2000;
        }
        return (uint16_t)raw;
    }

    int clamped = raw;
    if (clamped < (int)binding.min) {
        clamped = binding.min;
    }
    if (clamped > (int)binding.max) {
        clamped = binding.max;
    }

    if (clamped <= (int)binding.center) {
        int span = (int)binding.center - (int)binding.min;
        if (span <= 0) {
            return 1500;
        }
        float ratio = (float)(clamped - (int)binding.min) / (float)span;
        return (uint16_t)(1000.0f + ratio * 500.0f + 0.5f);
    }

    int span = (int)binding.max - (int)binding.center;
    if (span <= 0) {
        return 1500;
    }
    float ratio = (float)(clamped - (int)binding.center) / (float)span;
    return (uint16_t)(1500.0f + ratio * 500.0f + 0.5f);
}

inline bool rcDiagnosticsAppendf(char*& pos, size_t& remaining, const char* fmt, ...) {
    if (remaining == 0) {
        return false;
    }

    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(pos, remaining, fmt, args);
    va_end(args);

    if (written < 0 || (size_t)written >= remaining) {
        return false;
    }

    pos += written;
    remaining -= (size_t)written;
    return true;
}

inline bool formatRcDiagnosticsJson(char* buf, size_t bufSize,
                                    const RcDiagnosticsSnapshot& snapshot) {
    if (buf == nullptr || bufSize == 0 || snapshot.mode == nullptr) {
        return false;
    }

    char* pos = buf;
    size_t remaining = bufSize;

    if (!rcDiagnosticsAppendf(pos, remaining,
                              "{\"mode\":\"%s\",\"updatedMs\":%lu,"
                              "\"sources\":{",
                              snapshot.mode, (unsigned long)snapshot.updatedMs)) {
        return false;
    }

    for (size_t i = 0; i < snapshot.sourceCount; ++i) {
        const RcDiagnosticsSourceSnapshot& source = snapshot.sources[i];
        if (!rcDiagnosticsAppendf(pos, remaining,
                                  "%s\"%s\":{\"enabled\":%s,\"linked\":%s,"
                                  "\"ageMs\":%lu,\"lostFrames\":%lu,\"failsafe\":%s}",
                                  i == 0 ? "" : ",", source.key, source.enabled ? "true" : "false",
                                  source.linked ? "true" : "false", (unsigned long)source.ageMs,
                                  (unsigned long)source.lostFrames,
                                  source.failsafe ? "true" : "false")) {
            return false;
        }
    }

    if (!rcDiagnosticsAppendf(pos, remaining, "},\"channels\":[")) {
        return false;
    }

    for (size_t i = 0; i < snapshot.analogCount; ++i) {
        const RcDiagnosticsAnalogChannel& channel = snapshot.analogChannels[i];
        if (!rcDiagnosticsAppendf(
                pos, remaining,
                "%s{\"id\":%u,\"name\":\"%s\",\"type\":"
                "\"analog\",\"activeSource\":\"%s\","
                "\"bindingChannel\":%u,\"raw\":%d,\"rawUs\":%u,"
                "\"normalized\":%.3f,\"mapped\":%.3f,"
                "\"inDeadband\":%s,\"reverse\":%s}",
                i == 0 ? "" : ",", (unsigned int)channel.id, channel.name, channel.activeSource,
                (unsigned int)channel.bindingChannel, channel.raw, (unsigned int)channel.rawUs,
                (double)channel.normalized, (double)channel.mapped,
                channel.inDeadband ? "true" : "false", channel.reverse ? "true" : "false")) {
            return false;
        }
    }

    if (!rcDiagnosticsAppendf(pos, remaining, "],\"digital\":{")) {
        return false;
    }

    for (size_t i = 0; i < snapshot.digitalCount; ++i) {
        const RcDiagnosticsDigitalChannel& channel = snapshot.digitalChannels[i];
        if (!rcDiagnosticsAppendf(pos, remaining,
                                  "%s\"%s\":{\"activeSource\":\"%s\","
                                  "\"bindingChannel\":%u,\"pressed\":%s}",
                                  i == 0 ? "" : ",", channel.name, channel.activeSource,
                                  (unsigned int)channel.bindingChannel,
                                  channel.pressed ? "true" : "false")) {
            return false;
        }
    }

    if (!rcDiagnosticsAppendf(pos, remaining,
                              "},\"mappingProfile\":{\"version\":1,\"channels\":{")) {
        return false;
    }

    for (size_t i = 0; i < snapshot.mappingCount; ++i) {
        const RcDiagnosticsMappingChannel& channel = snapshot.mappingChannels[i];
        if (!rcDiagnosticsAppendf(
                pos, remaining,
                "%s\"%s\":{\"source\":\"%s\",\"channel\":%u,"
                "\"min\":%u,\"center\":%u,\"max\":%u,"
                "\"deadband\":%u,\"reverse\":%s}",
                i == 0 ? "" : ",", channel.name, rcDiagnosticsSourceName(channel.binding.source),
                (unsigned int)channel.binding.channel, (unsigned int)channel.binding.min,
                (unsigned int)channel.binding.center, (unsigned int)channel.binding.max,
                (unsigned int)channel.binding.deadband,
                channel.binding.reverse ? "true" : "false")) {
            return false;
        }
    }

    return rcDiagnosticsAppendf(pos, remaining, "}}}");
}
