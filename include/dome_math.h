// =============================================================================
// include/dome_math.h
//
// Pure-logic dome ESC pulse mapping — no hardware, no FreeRTOS.
// Extracted for testability. Used by DomeTask and native tests.
//
// ESC PWM semantics (standard RC PWM, 50 Hz):
//   1000µs = full reverse / max brake
//   1500µs = neutral / stop
//   2000µs = full forward
// =============================================================================
#pragma once
#include <stdint.h>

// -----------------------------------------------------------------------------
// domeSpeedToPulseUs()
// Map normalized speed (-1.0..1.0) to ESC PWM pulse width (µs).
//
// The speed limit percentage scales the usable pulse range symmetrically around
// neutral. Asymmetric neutral trimming (neutral != midpoint of min..max) is
// handled correctly: forward and reverse half-ranges are computed independently.
//
// Returns pulse width clamped to [minPulseUs, maxPulseUs].
// -----------------------------------------------------------------------------
inline uint16_t domeSpeedToPulseUs(float speed, uint16_t neutralUs, uint16_t minPulseUs,
                                   uint16_t maxPulseUs, uint8_t speedLimitPct) {
    if (speed < -1.0f)
        speed = -1.0f;
    if (speed > 1.0f)
        speed = 1.0f;

    float limitScale = (float)speedLimitPct / 100.0f;

    float reverseRange = (float)(neutralUs - minPulseUs) * limitScale;
    float forwardRange = (float)(maxPulseUs - neutralUs) * limitScale;

    int16_t pulseUs;
    if (speed >= 0.0f) {
        pulseUs = (int16_t)((float)neutralUs + speed * forwardRange);
    } else {
        pulseUs = (int16_t)((float)neutralUs + speed * reverseRange);
    }

    if (pulseUs < (int16_t)minPulseUs)
        pulseUs = (int16_t)minPulseUs;
    if (pulseUs > (int16_t)maxPulseUs)
        pulseUs = (int16_t)maxPulseUs;

    return (uint16_t)pulseUs;
}
