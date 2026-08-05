// =============================================================================
// include/ledc_pwm.h
//
// LEDC PWM driver for servo and ESC control.
// Provides 50Hz RC PWM output for 6 channels:
//   - ARM1 (GPIO 23) — Utility arm servo #1 (Top/Left)
//   - ARM2 (GPIO 5)  — Utility arm servo #2 (Bottom/Right)
//   - AUX1 (GPIO 19) — Spare servo output (also labelled ARM3)
//   - AUX2 (GPIO 18) — Spare servo output (also labelled ARM4)
//   - AUX3 (GPIO 32) — Spare servo output (also labelled ARM5)
//   - DOME (GPIO 25) — Dome rotation ESC (brushless motor, not a servo)
//
// ESP32 LEDC Configuration:
//   - Timer: 50Hz (20ms period), 16-bit resolution
//   - Frequency: Standard RC servo/ESC frequency
//   - Pulse range: 500-2500µs (servos), 1000-2000µs (ESC ±500µs from neutral)
//
// Pure-math helpers (pulseUsToDuty, clampPulseWidth) are inline so they can
// be exercised by native unit tests without pulling in ESP32 LEDC headers.
//
// Thread safety: This driver is designed to be called from a single task
// (ServoTask). No internal locking — caller provides synchronization.
// =============================================================================
#pragma once

#include <stdint.h>

// Firmware-only: ESP32 LEDC hardware constants.
//
// The native build's Arduino stub defines ARDUINO_ARCH_ESP32 for an unrelated
// reason (a ServoComponentType guard), so that macro alone does not mean "an
// ESP-IDF sysroot is present". Host translation units that only want the pulse
// width constants below must not be handed an ESP-IDF driver header.
#if defined(ARDUINO_ARCH_ESP32) && !defined(PA_NATIVE_TEST_STUBS)
#include <driver/ledc.h>
#endif

// -----------------------------------------------------------------------------
// LEDC Configuration
// -----------------------------------------------------------------------------
#define LEDC_FREQUENCY_HZ 50
#define LEDC_RESOLUTION_BITS 16
#define LEDC_DUTY_MAX ((1U << LEDC_RESOLUTION_BITS) - 1U)  // 65535 on classic ESP32

// PWM period at 50Hz = 20,000µs
#define PWM_PERIOD_US 20000U

// Default pulse widths (microseconds)
#define SERVO_PULSE_MIN_US 500U
#define SERVO_PULSE_MAX_US 2500U
#define SERVO_PULSE_NEUTRAL_US 1500U

// ESC pulse range (±500µs from neutral)
#define ESC_PULSE_MIN_US 1000U
#define ESC_PULSE_MAX_US 2000U
#define ESC_PULSE_NEUTRAL_US 1500U

// ESP32 LEDC hardware configuration (firmware only)
#ifdef ARDUINO_ARCH_ESP32
#define LEDC_MODE LEDC_LOW_SPEED_MODE
#define LEDC_RESOLUTION LEDC_TIMER_16_BIT
#define LEDC_TIMER LEDC_TIMER_0
#endif

// -----------------------------------------------------------------------------
// LEDC Channel assignments
// ARM1=0, ARM2=1, DOME=2, AUX1=3, AUX2=4, AUX3=5
// Matches ESP32 LEDC channel numbers 0-5 on timer 0.
// -----------------------------------------------------------------------------
enum LedcChannel : uint8_t {
    LEDC_CH_ARM1 = 0,
    LEDC_CH_ARM2 = 1,
    LEDC_CH_DOME = 2,
    LEDC_CH_AUX1 = 3,
    LEDC_CH_AUX2 = 4,
    LEDC_CH_AUX3 = 5,
    LEDC_CH_MAX = 6
};

// Channel configuration: GPIO pin, min/max pulse widths
struct ChannelConfig {
    uint8_t gpio;
    uint8_t channel;
    uint16_t minUs;
    uint16_t maxUs;
};

// -----------------------------------------------------------------------------
// pulseUsToDuty() — pure math, inline for native testability
// Convert pulse width in microseconds to LEDC duty cycle.
// Formula: duty = (pulseUs / 20000) * LEDC_DUTY_MAX
// Uses 64-bit intermediate to avoid overflow.
// Precision: ±1 count at the configured resolution (~0.3µs at 50Hz/16-bit).
// -----------------------------------------------------------------------------
inline uint32_t pulseUsToDuty(uint16_t pulseUs) {
    return (uint32_t)(((uint64_t)pulseUs * LEDC_DUTY_MAX) / PWM_PERIOD_US);
}

// -----------------------------------------------------------------------------
// clampPulseWidth() — pure math, inline for native testability
// Clamp pulse width to valid range for the given channel type.
//   - Servo channels (ARM1, ARM2, AUX1-3): 500-2500µs
//   - ESC channel (DOME): 1000-2000µs
// Returns SERVO_PULSE_NEUTRAL_US if channel index is out of range.
// -----------------------------------------------------------------------------
inline uint16_t clampPulseWidth(uint8_t channel, uint16_t pulseUs) {
    // Per-channel min/max table (mirrors kChannelConfig in ledc_pwm.cpp)
    static const uint16_t kMinUs[LEDC_CH_MAX] = {
        SERVO_PULSE_MIN_US,  // ARM1
        SERVO_PULSE_MIN_US,  // ARM2
        ESC_PULSE_MIN_US,    // DOME
        SERVO_PULSE_MIN_US,  // AUX1
        SERVO_PULSE_MIN_US,  // AUX2
        SERVO_PULSE_MIN_US,  // AUX3
    };
    static const uint16_t kMaxUs[LEDC_CH_MAX] = {
        SERVO_PULSE_MAX_US,  // ARM1
        SERVO_PULSE_MAX_US,  // ARM2
        ESC_PULSE_MAX_US,    // DOME
        SERVO_PULSE_MAX_US,  // AUX1
        SERVO_PULSE_MAX_US,  // AUX2
        SERVO_PULSE_MAX_US,  // AUX3
    };

    if (channel >= LEDC_CH_MAX) {
        return SERVO_PULSE_NEUTRAL_US;
    }
    if (pulseUs < kMinUs[channel]) {
        return kMinUs[channel];
    }
    if (pulseUs > kMaxUs[channel]) {
        return kMaxUs[channel];
    }
    return pulseUs;
}

// -----------------------------------------------------------------------------
// Hardware-dependent functions — implemented in ledc_pwm.cpp (ESP32 only)
// -----------------------------------------------------------------------------

// Initialize LEDC timer and configure PWM channels.
// skipChannel allows leaving one channel detached (LEDC_CH_MAX = no skip).
// Must be called once before using any PWM outputs.
// Returns true on success, false if LEDC setup fails.
bool ledcPwmInit(uint8_t skipChannel = LEDC_CH_MAX);

// Set pulse width for a specific channel in microseconds.
// Pulse width is automatically clamped to safe range for the channel type.
// Returns true on success, false if channel invalid or LEDC write fails.
bool ledcPwmSetPulseWidth(uint8_t channel, uint16_t pulseUs);

// Set pulse width as a percentage of range (0.0-1.0).
// 0.0 = minimum pulse, 1.0 = maximum pulse.
// Useful for mapping normalized SBUS commands to servo positions.
bool ledcPwmSetPercent(uint8_t channel, float percent);

// Set channel to neutral position (1500µs).
bool ledcPwmSetNeutral(uint8_t channel);

// Get the GPIO pin associated with a channel. Returns 0 if channel invalid.
uint8_t getChannelGpio(uint8_t channel);

// Initialize all outputs to neutral position.
// Call after ledcPwmInit() to ensure servos/ESC start in a known state.
void ledcPwmInitNeutralPositions();

// Emergency stop — set all channels to neutral immediately.
// Safe to call from any context; does not log.
void ledcPwmEmergencyStop();
