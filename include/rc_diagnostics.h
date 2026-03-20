#pragma once

#include "rc_diagnostics_snapshot.h"

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
