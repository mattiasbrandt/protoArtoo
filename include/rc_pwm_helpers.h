#pragma once

#include <stdint.h>

#include "config.h"

static constexpr uint16_t RC_PWM_MIN_US = 1000;
static constexpr uint16_t RC_PWM_MAX_US = 2000;
static constexpr uint16_t RC_PWM_CENTER_US = 1500;
static constexpr uint16_t RC_PWM_VALID_MIN_US = 900;
static constexpr uint16_t RC_PWM_VALID_MAX_US = 2100;
static constexpr uint16_t RC_PWM_SWITCH_LOW_US = 1300;
static constexpr uint16_t RC_PWM_SWITCH_HIGH_US = 1700;

enum RcPwmSwitchState : uint8_t {
    RC_PWM_SWITCH_LOW = 0,
    RC_PWM_SWITCH_MID,
    RC_PWM_SWITCH_HIGH,
    RC_PWM_SWITCH_INVALID,
};

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

inline int16_t rcPwmPulseToDrive(uint32_t pulseUs, int16_t maxOut) {
    float normalized = rcPwmPulseToNormalized(pulseUs);
    return (int16_t)(normalized * maxOut);
}

inline RcPwmSwitchState rcPwmPulseToSwitchState(uint32_t pulseUs) {
    if (!rcPwmPulseIsValid(pulseUs))
        return RC_PWM_SWITCH_INVALID;
    if (pulseUs <= RC_PWM_SWITCH_LOW_US)
        return RC_PWM_SWITCH_LOW;
    if (pulseUs >= RC_PWM_SWITCH_HIGH_US)
        return RC_PWM_SWITCH_HIGH;
    return RC_PWM_SWITCH_MID;
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
