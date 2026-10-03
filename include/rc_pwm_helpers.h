#pragma once

#include <stdint.h>

#include "config.h"

static constexpr uint16_t RC_PWM_MIN_US = 1000;
static constexpr uint16_t RC_PWM_MAX_US = 2000;
static constexpr uint16_t RC_PWM_CENTER_US = 1500;
static constexpr uint16_t RC_PWM_VALID_MIN_US = 900;
static constexpr uint16_t RC_PWM_VALID_MAX_US = 2100;

inline bool rcPwmPulseIsValid(uint32_t pulseUs) {
    return pulseUs >= RC_PWM_VALID_MIN_US && pulseUs <= RC_PWM_VALID_MAX_US;
}

inline float rcPwmPulseToNormalized(uint32_t pulseUs) {
    if (pulseUs <= RC_PWM_MIN_US)
        return -1.0f;
    if (pulseUs >= RC_PWM_MAX_US)
        return 1.0f;
    return ((float)pulseUs - (float)RC_PWM_CENTER_US) / 500.0f;
}

// -----------------------------------------------------------------------------
// pwmSignalLostCheck()
// Pure function to check if PWM signal has been lost based on last valid
// timestamp and current time. Used by dispatchStandardPwmInputs() for failsafe.
//
// params: lastPwmMs       - timestamp of last valid PWM pulse (0 = never)
//         currentMs       - current timestamp (millis())
//         timeoutMs       - timeout threshold for signal loss
// returns: true if signal lost (timeout exceeded or never received)
// -----------------------------------------------------------------------------
inline bool pwmSignalLostCheck(uint32_t lastPwmMs, uint32_t currentMs, uint32_t timeoutMs) {
    if (lastPwmMs == 0) {
        return true;  // Never received valid PWM
    }
    // Unsigned subtraction handles millis() overflow correctly
    return (currentMs - lastPwmMs) > timeoutMs;
}
