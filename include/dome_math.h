// =============================================================================
// include/dome_math.h
//
// Pure-logic dome ESC pulse mapping and random-move pause arithmetic  --  no
// hardware, no FreeRTOS.
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

// -----------------------------------------------------------------------------
// Random dome movement: the Mood sets how often (#452)
//
// The pause window the builder stores (dome_rnd_pause_min/max, whole seconds)
// is Full-Awake's pace. The other Moods scale both ends of it, by the same
// proportions the shipped chatter defaults use (30 : 20 : 10 s):
//
//   Quiet      (10)          no random move starts
//   Mid-Awake  (13)          window x 1.5
//   Full-Awake (11), unset 0 window as stored   (default:, as the chatter does)
//   Awake+     (14)          window x 0.5
//
// Both ends are floored at DOME_RND_MIN_PAUSE_MS before the draw, so a 1-2 s
// window in Awake+ draws from 1000..1000 ms rather than from 500..1000 ms and
// then clamping. Speed and move duration do not scale: this is how often, not
// how much.
// -----------------------------------------------------------------------------
static constexpr uint32_t DOME_RND_MIN_PAUSE_MS = 1000;

inline bool domeRndMoodStartsMoves(uint8_t mood) {
    return mood != 10;
}

// Next pause in ms, drawn from the Mood-scaled window with the caller's random
// value (esp_random() on the device; passed in so the endpoints are testable).
// Returns 0 when the Mood starts no move (Quiet): a real pause is never under
// DOME_RND_MIN_PAUSE_MS, so 0 cannot be mistaken for one. pauseMax <= pauseMin
// gives the scaled pauseMin every time, as the task always did.
inline uint32_t domeRndPauseMsForMood(uint8_t pauseMinS, uint8_t pauseMaxS, uint8_t mood,
                                      uint32_t randomValue) {
    if (!domeRndMoodStartsMoves(mood)) {
        return 0;
    }
    uint32_t num = 1;
    uint32_t den = 1;
    switch (mood) {
        case 13:
            num = 3;
            den = 2;
            break;
        case 14:
            num = 1;
            den = 2;
            break;
        default:
            break;
    }
    // Scale in ms, never in whole seconds: 7 s x 0.5 is 3500 ms, not 3000.
    uint32_t minMs = (uint32_t)pauseMinS * 1000UL * num / den;
    uint32_t maxMs = (uint32_t)pauseMaxS * 1000UL * num / den;
    if (minMs < DOME_RND_MIN_PAUSE_MS)
        minMs = DOME_RND_MIN_PAUSE_MS;
    if (maxMs < DOME_RND_MIN_PAUSE_MS)
        maxMs = DOME_RND_MIN_PAUSE_MS;
    const uint32_t rangeMs = (maxMs > minMs) ? (maxMs - minMs) : 0UL;
    return minMs + (rangeMs > 0 ? (randomValue % rangeMs) : 0UL);
}
