// =============================================================================
// include/dome_math.h
//
// Pure-logic dome ESC pulse mapping  --  no hardware, no FreeRTOS.
// Extracted for testability. Used by DomeTask and native tests.
//
// ESC PWM semantics (standard RC PWM, 50 Hz):
//   1000us = full reverse / max brake
//   1500us = neutral / stop
//   2000us = full forward
// =============================================================================
#pragma once
#include <stdint.h>

// -----------------------------------------------------------------------------
// domePulsesInOrder()
// The one rule a dome ESC pulse set must keep: min <= neutral <= max.
//
// Out of order, the mapping below cannot put neutral out for a stop. With min
// 1800, neutral 1500 and max 1200, speed 0 computes 1500, is raised to min
// (1800) and then lowered to max: every speed, 0 included, drives 1200 us -
// about 60% reverse on an ESC in a forward/reverse mode, with nobody
// commanding it (#417). So the config door refuses such a set, the loader
// replaces a stored one with the defaults, and the mapping falls back to
// neutral rather than trust it. data/dome.js applies the same rule before it
// sends.
// -----------------------------------------------------------------------------
inline bool domePulsesInOrder(uint16_t minPulseUs, uint16_t neutralUs, uint16_t maxPulseUs) {
    return minPulseUs <= neutralUs && neutralUs <= maxPulseUs;
}

// -----------------------------------------------------------------------------
// domeSpeedToPulseUs()
// Map normalized speed (-1.0..1.0) to ESC PWM pulse width (us).
//
// The speed limit percentage scales the usable pulse range symmetrically around
// neutral. Asymmetric neutral trimming (neutral != midpoint of min..max) is
// handled correctly: forward and reverse half-ranges are computed independently.
//
// Returns pulse width clamped to [minPulseUs, maxPulseUs] - or neutralUs for
// every speed when the set is out of order (domePulsesInOrder()), since then
// there is no range to clamp into and a stop must still be a stop. A safety
// invariant, not a nicety: the door and the loader keep such a set out, and
// this is what holds if one gets past them anyway.
// -----------------------------------------------------------------------------
inline uint16_t domeSpeedToPulseUs(float speed, uint16_t neutralUs, uint16_t minPulseUs,
                                   uint16_t maxPulseUs, uint8_t speedLimitPct) {
    if (!domePulsesInOrder(minPulseUs, neutralUs, maxPulseUs)) {
        return neutralUs;
    }
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
