// =============================================================================
// src/tasks/aux_led.cpp
//
// AuxLedTask - the WS2812B strips on the droid's lit wires. One driver per wire
// that carries a Light Type (ADR 0067, #413), where there used to be one strip
// on one selectable header.
//
// Runs on Core 0 (non real-time path) and never blocks Core 1 control loops.
// Which wires are lit is read ONCE at start, like every other Component Toggle
// (ADR 0027): a builder changing what a wire carries is told it bites at the
// next restart, and the alternative is re-creating an RMT driver underneath a
// running strip.
// =============================================================================

#include "aux_led.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <string.h>

#include "board_output_enabled.h"
#include "board_outputs.h"
#include "config.h"
#include "config_cache.h"
#include "ledc_pwm.h"
#include "logging.h"
#include "queue_drop_tracker.h"
#include "robot_state.h"
#include "servo_output_row.h"
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
    uint8_t target;  // a BOARD_OUTPUTS index, or AUX_LED_TARGET_ALL
    uint8_t r;
    uint8_t g;
    uint8_t b;
    AuxLedEffect effect;
};

// Eight deep per wire: a builder dragging the brightness slider on one plate
// must not push another wire's pending command off the queue.
static constexpr uint8_t AUX_LED_QUEUE_LEN = 8 * BOARD_OUTPUT_COUNT;
static constexpr uint16_t AUX_LED_BLINK_PERIOD_MS = 1000;
static constexpr uint16_t AUX_LED_PULSE_PERIOD_MS = 1800;

static QueueHandle_t s_auxLedQueue = nullptr;

// One lit wire, as the task holds it. Indexed by its Output's place in
// BOARD_OUTPUTS, which is also robotState.auxLed's index, so neither end keeps
// a list of its own.
struct LitWire {
    bool lit;        // the builder wired a light here
    bool available;  // and its driver started - see AuxLedState's own note
    uint8_t gpio;
    uint8_t ledCount;
    uint8_t baseR;
    uint8_t baseG;
    uint8_t baseB;
    AuxLedEffect effect;
    // What was last rendered, so a frame that resolves to the same color does
    // not re-drive the strip. 255/255/255 is deliberately not a color the
    // strip starts at, so the first frame always renders.
    uint8_t lastR;
    uint8_t lastG;
    uint8_t lastB;
#ifdef ARDUINO
    Adafruit_NeoPixel* strip;
#endif
};

static LitWire s_wires[BOARD_OUTPUT_COUNT] = {};

// Whether this Output carries a light, as the stored config says: ticked as
// wired AND with a Light Type on its Servo Output row. Both halves matter - a
// wire nobody has plugged in is not lit, and neither is one carrying a servo.
static bool outputIsLit(const ConfigSnapshot& cfg, size_t index) {
    if (!BOARD_OUTPUTS[index].lightCapable || !boardOutputIsWired(cfg.system, index)) {
        return false;
    }
    return configCacheReadServoOutputComponent(SERVO_DRIVER_LEDC, BOARD_OUTPUTS[index].channel) ==
           SERVO_COMP_RGB;
}

// Read the lit wires out of config into s_wires. Called by both entry points -
// auxLedTaskInit() to publish what the droid has, and auxLedTask() to drive it -
// because the two run in different tasks and neither may depend on the other
// having run first.
static void readLitWires() {
    ConfigSnapshot cfg = {};
    configCacheRead(&cfg);
    for (size_t i = 0; i < BOARD_OUTPUT_COUNT; ++i) {
        LitWire& wire = s_wires[i];
        wire.lit = outputIsLit(cfg, i);
        wire.available = false;
        wire.gpio = wire.lit ? getChannelGpio(BOARD_OUTPUTS[i].channel) : 0;
        wire.ledCount = configCacheReadServoOutputLedCount(SERVO_DRIVER_LEDC,
                                                           BOARD_OUTPUTS[i].channel);
        wire.effect = AUX_LED_EFFECT_OFF;
        wire.lastR = 255;
        wire.lastG = 255;
        wire.lastB = 255;
        // A wire whose GPIO could not be resolved cannot be driven, so it is
        // not lit however the config reads.
        if (wire.gpio == 0) {
            wire.lit = false;
        }
    }
}

static bool setAuxLedStateLocked(size_t index, bool lit, uint8_t r, uint8_t g, uint8_t b,
                                 AuxLedEffect effect, bool available) {
    bool changed = false;
    taskENTER_CRITICAL(&robotStateMux);
    AuxLedState& state = robotState.auxLed[index];
    if (state.lit != lit || state.r != r || state.g != g || state.b != b ||
        state.effect != effect || state.available != available) {
        state.lit = lit;
        state.r = r;
        state.g = g;
        state.b = b;
        state.effect = effect;
        state.available = available;
        changed = true;
    }
    taskEXIT_CRITICAL(&robotStateMux);
    return changed;
}

// A wire this droid can be told to light: one whose driver started. Read from
// robotState rather than from s_wires, because the task owns s_wires and the
// web and Console tasks are the ones asking.
static bool wireAcceptsCommands(size_t index) {
    taskENTER_CRITICAL(&robotStateMux);
    const bool ok = robotState.auxLed[index].lit && robotState.auxLed[index].available;
    taskEXIT_CRITICAL(&robotStateMux);
    return ok;
}

static uint8_t clampLedCount(uint8_t rawCount) {
    return constrain(rawCount, SERVO_LIGHT_LEDS_MIN, SERVO_LIGHT_LEDS_MAX);
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
static void renderStrip(LitWire& wire, uint8_t r, uint8_t g, uint8_t b) {
    if (wire.strip == nullptr) {
        return;
    }

    uint32_t color = wire.strip->Color(r, g, b);
    for (uint8_t i = 0; i < wire.ledCount; ++i) {
        wire.strip->setPixelColor(i, color);
    }
    wire.strip->show();
}

// Bring one wire's strip up. Returns false and says why when its driver refuses,
// which leaves the wire lit and unavailable: the builder wired a light here and
// the controller could not start it, which is a different answer from "no light
// on this wire" and is reported as one.
static bool startStrip(LitWire& wire) {
    wire.strip = new Adafruit_NeoPixel(wire.ledCount, wire.gpio, NEO_GRB + NEO_KHZ800);
    if (wire.strip == nullptr) {
        PA_LOG_ERROR(TAG, "NeoPixel allocation failed for GPIO %u count %u", (unsigned)wire.gpio,
                     (unsigned)wire.ledCount);
        return false;
    }
    if (!wire.strip->begin()) {
        PA_LOG_WARN(TAG, "NeoPixel begin failed on GPIO %u (RMT channel unavailable?)",
                    (unsigned)wire.gpio);
        return false;
    }
    wire.strip->clear();
    wire.strip->show();
    return true;
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

bool auxLedTargetIsLit(uint8_t target) {
    if (target == AUX_LED_TARGET_ALL) {
        for (size_t i = 0; i < BOARD_OUTPUT_COUNT; ++i) {
            if (wireAcceptsCommands(i)) {
                return true;
            }
        }
        return false;
    }
    return target < BOARD_OUTPUT_COUNT && wireAcceptsCommands(target);
}

bool auxLedTaskInit() {
    if (s_auxLedQueue != nullptr) {
        return true;
    }

    s_auxLedQueue = xQueueCreate(AUX_LED_QUEUE_LEN, sizeof(AuxLedCommand));
    if (s_auxLedQueue == nullptr) {
        PA_LOG_ERROR(TAG, "failed to create aux LED command queue");
        for (size_t i = 0; i < BOARD_OUTPUT_COUNT; ++i) {
            setAuxLedStateLocked(i, false, 0, 0, 0, AUX_LED_EFFECT_OFF, false);
        }
        return false;
    }

    readLitWires();
    for (size_t i = 0; i < BOARD_OUTPUT_COUNT; ++i) {
        // available follows lit here and is corrected by the task if a driver
        // refuses, which is what the single-strip form did: a command arriving
        // between init and the task's first pass is queued rather than refused.
        setAuxLedStateLocked(i, s_wires[i].lit, 0, 0, 0, AUX_LED_EFFECT_OFF, s_wires[i].lit);
    }
    return true;
}

bool auxLedQueueSetColor(uint8_t target, uint8_t r, uint8_t g, uint8_t b, CommandSource source) {
    if (s_auxLedQueue == nullptr || !auxLedTargetIsLit(target)) {
        return false;
    }

    AuxLedCommand cmd{};
    cmd.type = AUX_LED_CMD_SET_COLOR;
    cmd.source = source;
    cmd.target = target;
    cmd.r = r;
    cmd.g = g;
    cmd.b = b;

    if (xQueueSend(s_auxLedQueue, &cmd, 0) != pdTRUE) {
        logQueueDrop(QUEUE_AUX_LED, "set color command");
        return false;
    }

    return true;
}

bool auxLedQueueSetEffect(uint8_t target, AuxLedEffect effect, CommandSource source) {
    if (s_auxLedQueue == nullptr || !auxLedTargetIsLit(target)) {
        return false;
    }

    AuxLedCommand cmd{};
    cmd.type = AUX_LED_CMD_SET_EFFECT;
    cmd.source = source;
    cmd.target = target;
    cmd.effect = effect;

    if (xQueueSend(s_auxLedQueue, &cmd, 0) != pdTRUE) {
        static uint32_t lastWarnMs = 0;
        uint32_t nowMs = millis();
        if ((uint32_t)(nowMs - lastWarnMs) > 5000) {  // Rate-limit to once per 5s
            PA_LOG_WARN("AuxLed", "auxLedQueue full, dropped set effect command");
            lastWarnMs = nowMs;
        }
        taskENTER_CRITICAL(&robotStateMux);
        robotState.queueOverflowCount++;
        taskEXIT_CRITICAL(&robotStateMux);
        return false;
    }

    return true;
}

void auxLedTask(void* pvParameters) {
    (void)pvParameters;

    readLitWires();

    uint8_t litLeads = 0;
    bool broadcast = false;
    for (size_t i = 0; i < BOARD_OUTPUT_COUNT; ++i) {
        LitWire& wire = s_wires[i];
        if (!wire.lit) {
            broadcast = setAuxLedStateLocked(i, false, 0, 0, 0, AUX_LED_EFFECT_OFF, false) ||
                        broadcast;
            continue;
        }
        wire.ledCount = clampLedCount(wire.ledCount);
        wire.available = true;
#ifdef ARDUINO
        wire.available = startStrip(wire);
#endif
        broadcast = setAuxLedStateLocked(i, true, 0, 0, 0, AUX_LED_EFFECT_OFF, wire.available) ||
                    broadcast;
        if (wire.available) {
            ++litLeads;
            PA_LOG_INFO(TAG, "%s lit on GPIO %u, %u pixel(s)", BOARD_OUTPUTS[i].id,
                        (unsigned)wire.gpio, (unsigned)wire.ledCount);
        }
    }
    if (broadcast) {
        requestStatusBroadcastNow();
    }

    if (litLeads == 0) {
        // No wire to drive, and nothing will change that until a restart: the
        // lit set is read once, so this task idles rather than polling config
        // it has already been told is settled (ADR 0027).
        PA_LOG_DEBUG(TAG, "no lit wires on this droid");
        for (;;) {
            vTaskDelay(pdMS_TO_TICKS(250));
        }
    }

    AuxLedCommand cmd{};

    for (;;) {
        bool stateChanged = false;

        while (xQueueReceive(s_auxLedQueue, &cmd, 0) == pdTRUE) {
            for (size_t i = 0; i < BOARD_OUTPUT_COUNT; ++i) {
                if (!s_wires[i].available ||
                    (cmd.target != AUX_LED_TARGET_ALL && cmd.target != i)) {
                    continue;
                }
                if (cmd.type == AUX_LED_CMD_SET_COLOR) {
                    s_wires[i].baseR = cmd.r;
                    s_wires[i].baseG = cmd.g;
                    s_wires[i].baseB = cmd.b;
                } else if (cmd.type == AUX_LED_CMD_SET_EFFECT) {
                    s_wires[i].effect = cmd.effect;
                }
                stateChanged = true;
            }
        }

        const uint32_t nowMs = millis();
        bool published = false;
        for (size_t i = 0; i < BOARD_OUTPUT_COUNT; ++i) {
            LitWire& wire = s_wires[i];
            if (!wire.available) {
                continue;
            }
            if (stateChanged &&
                setAuxLedStateLocked(i, true, wire.baseR, wire.baseG, wire.baseB, wire.effect,
                                     true)) {
                published = true;
            }

            uint8_t outR = 0;
            uint8_t outG = 0;
            uint8_t outB = 0;
            resolveDisplayedColor(wire.baseR, wire.baseG, wire.baseB, wire.effect, nowMs, &outR,
                                  &outG, &outB);

            if (outR != wire.lastR || outG != wire.lastG || outB != wire.lastB) {
#ifdef ARDUINO
                renderStrip(wire, outR, outG, outB);
#endif
                wire.lastR = outR;
                wire.lastG = outG;
                wire.lastB = outB;
            }
        }
        if (published) {
            requestStatusBroadcastNow();
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
