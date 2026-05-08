// =============================================================================
// src/tasks/aux_led.cpp
//
// AuxLedTask - single WS2812B strip driver on selectable AUX header.
// Runs on Core 0 (non real-time path) and never blocks Core 1 control loops.
// =============================================================================

#include "aux_led.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <string.h>

#include "config.h"
#include "config_store.h"
#include "logging.h"
#include "robot_state.h"
#include "web_server.h"

#ifdef ARDUINO
#include <Adafruit_NeoPixel.h>
#endif

namespace {

static const char* TAG = "AuxLedTask";

enum AuxLedCommandType : uint8_t {
    AUX_LED_CMD_SET_COLOR = 0,
    AUX_LED_CMD_SET_EFFECT,
};

struct AuxLedCommand {
    AuxLedCommandType type;
    CommandSource source;
    uint8_t r;
    uint8_t g;
    uint8_t b;
    AuxLedEffect effect;
};

static constexpr uint8_t AUX_LED_QUEUE_LEN = 8;
static constexpr uint16_t AUX_LED_BLINK_PERIOD_MS = 1000;
static constexpr uint16_t AUX_LED_PULSE_PERIOD_MS = 1800;

static QueueHandle_t s_auxLedQueue = nullptr;

#ifdef ARDUINO
static Adafruit_NeoPixel* s_strip = nullptr;
#endif

static bool setAuxLedStateLocked(uint8_t pin, uint8_t r, uint8_t g, uint8_t b, AuxLedEffect effect,
                                 bool available) {
    bool changed = false;
    taskENTER_CRITICAL(&robotStateMux);
    if (robotState.auxLed.pin != pin || robotState.auxLed.r != r || robotState.auxLed.g != g ||
        robotState.auxLed.b != b || robotState.auxLed.effect != effect ||
        robotState.auxLed.available != available) {
        robotState.auxLed.pin = pin;
        robotState.auxLed.r = r;
        robotState.auxLed.g = g;
        robotState.auxLed.b = b;
        robotState.auxLed.effect = effect;
        robotState.auxLed.available = available;
        changed = true;
    }
    taskEXIT_CRITICAL(&robotStateMux);
    return changed;
}

static bool auxLedCommandsAccepted() {
    taskENTER_CRITICAL(&robotStateMux);
    const bool available = robotState.auxLed.available;
    const uint8_t pin = robotState.auxLed.pin;
    taskEXIT_CRITICAL(&robotStateMux);

    return available && pin != 0;
}

static uint8_t clampLedCount(uint8_t rawCount) {
    return constrain(rawCount, AUX_LED_COUNT_DEFAULT, AUX_LED_COUNT_MAX);
}

static uint8_t pulseLevel(uint32_t nowMs) {
    uint32_t phase = nowMs % AUX_LED_PULSE_PERIOD_MS;
    uint32_t half = AUX_LED_PULSE_PERIOD_MS / 2U;
    if (phase >= half) {
        phase = AUX_LED_PULSE_PERIOD_MS - phase;
    }
    return (uint8_t)((phase * 255U) / half);
}

static void resolveDisplayedColor(uint8_t baseR, uint8_t baseG, uint8_t baseB, AuxLedEffect effect,
                                  uint32_t nowMs, uint8_t* outR, uint8_t* outG, uint8_t* outB) {
    if (outR == nullptr || outG == nullptr || outB == nullptr) {
        return;
    }

    switch (effect) {
        case AUX_LED_EFFECT_OFF:
            *outR = 0;
            *outG = 0;
            *outB = 0;
            return;

        case AUX_LED_EFFECT_SOLID:
            *outR = baseR;
            *outG = baseG;
            *outB = baseB;
            return;

        case AUX_LED_EFFECT_BLINK: {
            bool on = (nowMs % AUX_LED_BLINK_PERIOD_MS) < (AUX_LED_BLINK_PERIOD_MS / 2U);
            *outR = on ? baseR : 0;
            *outG = on ? baseG : 0;
            *outB = on ? baseB : 0;
            return;
        }

        case AUX_LED_EFFECT_PULSE: {
            uint8_t level = pulseLevel(nowMs);
            *outR = (uint8_t)(((uint16_t)baseR * level) / 255U);
            *outG = (uint8_t)(((uint16_t)baseG * level) / 255U);
            *outB = (uint8_t)(((uint16_t)baseB * level) / 255U);
            return;
        }

        default:
            *outR = 0;
            *outG = 0;
            *outB = 0;
            return;
    }
}

#ifdef ARDUINO
static void renderStrip(uint8_t r, uint8_t g, uint8_t b, uint8_t count) {
    if (s_strip == nullptr) {
        return;
    }

    uint32_t color = s_strip->Color(r, g, b);
    for (uint8_t i = 0; i < count; ++i) {
        s_strip->setPixelColor(i, color);
    }
    s_strip->show();
}
#endif

}  // namespace

const char* auxLedEffectToString(AuxLedEffect effect) {
    switch (effect) {
        case AUX_LED_EFFECT_OFF:
            return "off";
        case AUX_LED_EFFECT_SOLID:
            return "solid";
        case AUX_LED_EFFECT_BLINK:
            return "blink";
        case AUX_LED_EFFECT_PULSE:
            return "pulse";
        default:
            return "off";
    }
}

bool parseAuxLedEffect(const char* raw, AuxLedEffect* out) {
    if (raw == nullptr || out == nullptr) {
        return false;
    }

    if (strcmp(raw, "off") == 0) {
        *out = AUX_LED_EFFECT_OFF;
        return true;
    }
    if (strcmp(raw, "solid") == 0) {
        *out = AUX_LED_EFFECT_SOLID;
        return true;
    }
    if (strcmp(raw, "blink") == 0) {
        *out = AUX_LED_EFFECT_BLINK;
        return true;
    }
    if (strcmp(raw, "pulse") == 0) {
        *out = AUX_LED_EFFECT_PULSE;
        return true;
    }

    return false;
}

bool auxLedTaskInit() {
    if (s_auxLedQueue != nullptr) {
        return true;
    }

    s_auxLedQueue = xQueueCreate(AUX_LED_QUEUE_LEN, sizeof(AuxLedCommand));
    if (s_auxLedQueue == nullptr) {
        PA_LOG_ERROR(TAG, "failed to create aux LED command queue");
        setAuxLedStateLocked(0, 0, 0, 0, AUX_LED_EFFECT_OFF, false);
        return false;
    }

    uint8_t selection = AUX_LED_PIN_DISABLED;
    uint8_t count = AUX_LED_COUNT_DEFAULT;
    ConfigSnapshot cfg = {};
    configCacheRead(&cfg);
    selection = cfg.servo.aux_led_pin;
    count = cfg.servo.aux_led_count;

    const uint8_t gpio = auxLedSelectionToGpio(selection);
    const bool enabled = gpio != 0;
    (void)clampLedCount(count);

    setAuxLedStateLocked(enabled ? gpio : 0, 0, 0, 0, AUX_LED_EFFECT_OFF, enabled);
    return true;
}

bool auxLedQueueSetColor(uint8_t r, uint8_t g, uint8_t b, CommandSource source) {
    if (s_auxLedQueue == nullptr || !auxLedCommandsAccepted()) {
        return false;
    }

    AuxLedCommand cmd{};
    cmd.type = AUX_LED_CMD_SET_COLOR;
    cmd.source = source;
    cmd.r = r;
    cmd.g = g;
    cmd.b = b;

    if (xQueueSend(s_auxLedQueue, &cmd, 0) != pdTRUE) {
        taskENTER_CRITICAL(&robotStateMux);
        robotState.queueOverflowCount++;
        taskEXIT_CRITICAL(&robotStateMux);
        return false;
    }

    return true;
}

bool auxLedQueueSetEffect(AuxLedEffect effect, CommandSource source) {
    if (s_auxLedQueue == nullptr || !auxLedCommandsAccepted()) {
        return false;
    }

    AuxLedCommand cmd{};
    cmd.type = AUX_LED_CMD_SET_EFFECT;
    cmd.source = source;
    cmd.effect = effect;

    if (xQueueSend(s_auxLedQueue, &cmd, 0) != pdTRUE) {
        taskENTER_CRITICAL(&robotStateMux);
        robotState.queueOverflowCount++;
        taskEXIT_CRITICAL(&robotStateMux);
        return false;
    }

    return true;
}

void auxLedTask(void* pvParameters) {
    (void)pvParameters;

    uint8_t selection = AUX_LED_PIN_DISABLED;
    uint8_t ledCount = AUX_LED_COUNT_DEFAULT;
    ConfigSnapshot cfg = {};
    configCacheRead(&cfg);
    selection = cfg.servo.aux_led_pin;
    ledCount = cfg.servo.aux_led_count;

    ledCount = clampLedCount(ledCount);

    const uint8_t gpio = auxLedSelectionToGpio(selection);
    if (gpio == 0) {
        if (setAuxLedStateLocked(0, 0, 0, 0, AUX_LED_EFFECT_OFF, false)) {
            requestStatusBroadcastNow();
        }
        PA_LOG_DEBUG(TAG, "AUX LED disabled (aux_led_pin=0)");
        for (;;) {
            vTaskDelay(pdMS_TO_TICKS(250));
        }
    }

#ifdef ARDUINO
    s_strip = new Adafruit_NeoPixel(ledCount, gpio, NEO_GRB + NEO_KHZ800);
    if (s_strip == nullptr) {
        PA_LOG_ERROR(TAG, "NeoPixel allocation failed for GPIO %u count %u", (unsigned)gpio,
                     (unsigned)ledCount);
        if (setAuxLedStateLocked(gpio, 0, 0, 0, AUX_LED_EFFECT_OFF, false)) {
            requestStatusBroadcastNow();
        }
        for (;;) {
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }

    if (!s_strip->begin()) {
        PA_LOG_WARN(TAG, "NeoPixel begin failed on GPIO %u (RMT channel unavailable?)",
                    (unsigned)gpio);
        if (setAuxLedStateLocked(gpio, 0, 0, 0, AUX_LED_EFFECT_OFF, false)) {
            requestStatusBroadcastNow();
        }
        for (;;) {
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }

    s_strip->clear();
    s_strip->show();
#endif

    uint8_t baseR = 0;
    uint8_t baseG = 0;
    uint8_t baseB = 0;
    AuxLedEffect effect = AUX_LED_EFFECT_OFF;

    if (setAuxLedStateLocked(gpio, baseR, baseG, baseB, effect, true)) {
        requestStatusBroadcastNow();
    }

    AuxLedCommand cmd{};
    uint8_t lastOutR = 255;
    uint8_t lastOutG = 255;
    uint8_t lastOutB = 255;

    PA_LOG_INFO(TAG, "AUX LED task ready on GPIO %u, %u pixel(s)", (unsigned)gpio, (unsigned)ledCount);

    for (;;) {
        bool stateChanged = false;

        while (xQueueReceive(s_auxLedQueue, &cmd, 0) == pdTRUE) {
            if (cmd.type == AUX_LED_CMD_SET_COLOR) {
                baseR = cmd.r;
                baseG = cmd.g;
                baseB = cmd.b;
                stateChanged = true;
            } else if (cmd.type == AUX_LED_CMD_SET_EFFECT) {
                effect = cmd.effect;
                stateChanged = true;
            }
        }

        if (stateChanged && setAuxLedStateLocked(gpio, baseR, baseG, baseB, effect, true)) {
            requestStatusBroadcastNow();
        }

        uint8_t outR = 0;
        uint8_t outG = 0;
        uint8_t outB = 0;
        resolveDisplayedColor(baseR, baseG, baseB, effect, millis(), &outR, &outG, &outB);

        if (outR != lastOutR || outG != lastOutG || outB != lastOutB) {
#ifdef ARDUINO
            renderStrip(outR, outG, outB, ledCount);
#endif
            lastOutR = outR;
            lastOutG = outG;
            lastOutB = outB;
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
