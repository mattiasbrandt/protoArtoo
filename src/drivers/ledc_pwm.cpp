// =============================================================================
// src/drivers/ledc_pwm.cpp
//
// LEDC PWM driver implementation for servo and ESC control.
// Configures ESP32 LEDC timer 0 at 50Hz/16-bit and drives 6 channels:
//   ARM1, ARM2 (servos), DOME (ESC), AUX1-3 (spare servos).
//
// Pure-math helpers (pulseUsToDuty, clampPulseWidth) are inline in ledc_pwm.h
// so native unit tests can exercise them without pulling in ESP32 LEDC headers.
// =============================================================================

#include "ledc_pwm.h"

#include <Arduino.h>
#include <driver/ledc.h>
#include <esp_log.h>

#include "config.h"

static const char* TAG = "ledc_pwm";

// Bit mask of configured LEDC channels (1 = configured, 0 = skipped).
// Set at init time; channels outside this mask fail silently (return false, no log).
static uint8_t s_configuredMask = 0;

// ESP32 LEDC peripheral constants  --  kept here, not in the header, so the
// header stays free of ESP32-specific types and is includable on native.
#define PA_LEDC_TIMER LEDC_TIMER_0
#define PA_LEDC_MODE LEDC_LOW_SPEED_MODE
#define PA_LEDC_RESOLUTION LEDC_RESOLUTION  // 16-bit on classic ESP32 (Artoo PCB target)

// GPIO pin for each channel  --  indexed by LedcChannel enum value (0-5).
static const uint8_t kChannelGpio[LEDC_CH_MAX] = {
    PIN_ARM1_SERVO,  // LEDC_CH_ARM1 = 0
    PIN_ARM2_SERVO,  // LEDC_CH_ARM2 = 1
    PIN_DOME_ESC,    // LEDC_CH_DOME = 2
    PIN_ARM3_SERVO,  // LEDC_CH_AUX1 = 3
    PIN_ARM4_SERVO,  // LEDC_CH_AUX2 = 4
    PIN_ARM5_SERVO,  // LEDC_CH_AUX3 = 5
};

// -----------------------------------------------------------------------------
// getChannelGpio()
// Returns the GPIO pin for a channel, or 0 if the index is out of range.
// -----------------------------------------------------------------------------
uint8_t getChannelGpio(uint8_t channel) {
    if (channel >= LEDC_CH_MAX) {
        return 0;
    }
    return kChannelGpio[channel];
}

// -----------------------------------------------------------------------------
// ledcPwmInit()
// Configure LEDC timer 0 at 50Hz/16-bit and attach channels.
// enabledMask: bitmask where bit N = 1 includes LedcChannel N, 0 excludes it.
// Channels outside the mask are left unconfigured; PWM writes to them fail silently.
// Returns false and logs on any LEDC API error (timer config only; skipped
// channels do not fail init).
// Stores the mask in s_configuredMask for use by write functions.
// Returns true if timer config succeeds, even if enabledMask is 0 (no channels).
// Configured channels start at neutral (1500us).
// -----------------------------------------------------------------------------
bool ledcPwmInit(uint8_t enabledMask) {
    s_configuredMask = enabledMask;

    // If no channels are enabled, skip timer config entirely.
    if (enabledMask == 0) {
        return true;
    }

    ledc_timer_config_t timerConfig = {};
    timerConfig.speed_mode = PA_LEDC_MODE;
    timerConfig.duty_resolution = PA_LEDC_RESOLUTION;
    timerConfig.timer_num = PA_LEDC_TIMER;
    timerConfig.freq_hz = LEDC_FREQUENCY_HZ;
    timerConfig.clk_cfg = LEDC_AUTO_CLK;

    esp_err_t err = ledc_timer_config(&timerConfig);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Timer config failed: %d", err);
        return false;
    }

    uint8_t configuredCount = 0;
    for (int i = 0; i < LEDC_CH_MAX; i++) {
        if (!(enabledMask & (1 << i))) {
            continue;
        }

        ledc_channel_config_t channelConfig = {};
        channelConfig.gpio_num = kChannelGpio[i];
        channelConfig.speed_mode = PA_LEDC_MODE;
        channelConfig.channel = (ledc_channel_t)i;
        channelConfig.intr_type = LEDC_INTR_DISABLE;
        channelConfig.timer_sel = PA_LEDC_TIMER;
        channelConfig.duty = pulseUsToDuty(SERVO_PULSE_NEUTRAL_US);
        channelConfig.hpoint = 0;

        err = ledc_channel_config(&channelConfig);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Channel %d config failed: %d", i, err);
            return false;
        }
        configuredCount++;
    }

    ESP_LOGI(TAG, "LEDC PWM initialized: %d channels @ %dHz", (int)configuredCount,
             LEDC_FREQUENCY_HZ);
    return true;
}

// -----------------------------------------------------------------------------
// ledcPwmSetPulseWidth()
// Set pulse width for a specific channel in microseconds.
// Clamps to channel-appropriate range before writing to hardware.
// Returns false silently (no log) if channel is not in the configured mask.
// Returns false with error log if LEDC write fails.
// -----------------------------------------------------------------------------
bool ledcPwmSetPulseWidth(uint8_t channel, uint16_t pulseUs) {
    if (channel >= LEDC_CH_MAX) {
        return false;
    }

    if (!(s_configuredMask & (1 << channel))) {
        return false;
    }

    uint32_t duty = pulseUsToDuty(clampPulseWidth(channel, pulseUs));

    esp_err_t err = ledc_set_duty(PA_LEDC_MODE, (ledc_channel_t)channel, duty);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Set duty failed for channel %d: %d", channel, err);
        return false;
    }

    err = ledc_update_duty(PA_LEDC_MODE, (ledc_channel_t)channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Update duty failed for channel %d: %d", channel, err);
        return false;
    }

    return true;
}

// -----------------------------------------------------------------------------
// ledcPwmSetPercent()
// Set pulse width as a fraction of the channel's full range (0.0-1.0).
// Derives min/max from clampPulseWidth to stay consistent with clamping logic.
// Returns false silently (no log) if channel is not in the configured mask.
// -----------------------------------------------------------------------------
bool ledcPwmSetPercent(uint8_t channel, float percent) {
    if (channel >= LEDC_CH_MAX) {
        return false;
    }

    if (!(s_configuredMask & (1 << channel))) {
        return false;
    }

    if (percent < 0.0f)
        percent = 0.0f;
    if (percent > 1.0f)
        percent = 1.0f;

    // Probe channel bounds via clampPulseWidth  --  single source of truth for limits.
    uint16_t minUs = clampPulseWidth(channel, 0);
    uint16_t maxUs = clampPulseWidth(channel, 65535U);
    uint16_t pulseUs = minUs + (uint16_t)(percent * (float)(maxUs - minUs));

    return ledcPwmSetPulseWidth(channel, pulseUs);
}

// -----------------------------------------------------------------------------
// ledcPwmSetNeutral()
// -----------------------------------------------------------------------------
bool ledcPwmSetNeutral(uint8_t channel) {
    return ledcPwmSetPulseWidth(channel, SERVO_PULSE_NEUTRAL_US);
}

// -----------------------------------------------------------------------------
// ledcPwmInitNeutralPositions()
// Set only configured channels to neutral.
// Skips channels outside the enabled mask.
// -----------------------------------------------------------------------------
void ledcPwmInitNeutralPositions() {
    for (int i = 0; i < LEDC_CH_MAX; i++) {
        if (s_configuredMask & (1 << i)) {
            ledcPwmSetNeutral(i);
        }
    }
    ESP_LOGI(TAG, "Configured channels set to neutral");
}

// -----------------------------------------------------------------------------
// ledcPwmEmergencyStop()
// Bypasses clamp/log path  --  writes neutral duty directly for minimum latency.
// Sets only configured channels to neutral; skips channels outside the mask.
// -----------------------------------------------------------------------------
void ledcPwmEmergencyStop() {
    uint32_t duty = pulseUsToDuty(SERVO_PULSE_NEUTRAL_US);
    for (int i = 0; i < LEDC_CH_MAX; i++) {
        if (s_configuredMask & (1 << i)) {
            ledc_set_duty(PA_LEDC_MODE, (ledc_channel_t)i, duty);
            ledc_update_duty(PA_LEDC_MODE, (ledc_channel_t)i);
        }
    }
    ESP_LOGW(TAG, "EMERGENCY STOP - all configured channels neutral");
}
