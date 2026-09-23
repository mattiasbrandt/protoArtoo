// =============================================================================
// include/aux_led.h
//
// The lit wires, and the queue that commands them.
//
// A droid may have several lit body Parts, each on its own wire (ADR 0067), so
// this task drives every Output whose Light Type says it carries a strip - not
// one selectable header. Which Outputs those are is read once at start from the
// Servo Output rows: a wire is lit when it is ticked as wired and its
// `component` names a Light Type (include/servo_output_row.h).
//
// A command names its wire by that Output's index in BOARD_OUTPUTS
// (include/board_outputs.h), the same index robotState.auxLed is keyed by, or
// AUX_LED_TARGET_ALL for every lit wire at once. All is what a sequence step
// and an RC action send: they mean "the droid's body lights", and a droid with
// one strip behaves exactly as it did before there could be several.
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "board_outputs.h"  // BOARD_OUTPUT_COUNT
#include "robot_state.h"    // AuxLedState, AuxLedEffect, robotState / robotStateMux

// Every lit wire. The value is outside BOARD_OUTPUT_COUNT by construction, so
// it can never collide with an Output index.
constexpr uint8_t AUX_LED_TARGET_ALL = 0xFF;

bool auxLedTaskInit();
void auxLedTask(void* pvParameters);

bool auxLedQueueSetColor(uint8_t target, uint8_t r, uint8_t g, uint8_t b, CommandSource source);
bool auxLedQueueSetEffect(uint8_t target, AuxLedEffect effect, CommandSource source);

// -----------------------------------------------------------------------------
// The rules below are header-only so the native build compiles the real ones:
// aux_led.cpp is a task translation unit and stays out of it, and the tests
// used to exercise hand copies of these in src/native_test_stubs.cpp (#416).
// -----------------------------------------------------------------------------

// Whether a command naming `target` reaches the wire at BOARD_OUTPUTS index
// `index`: AUX_LED_TARGET_ALL reaches every one, an index reaches only itself.
inline bool auxLedTargetReaches(uint8_t target, size_t index) {
    return target == AUX_LED_TARGET_ALL || target == index;
}

// A wire this droid can be told to light: lit, and its driver started.
inline bool auxLedWireAcceptsCommands(const AuxLedState& wire) {
    return wire.lit && wire.available;
}

// Whether `target` is a wire this droid can be told to light: a lit Output's
// index, or AUX_LED_TARGET_ALL while at least one wire is lit. It is what the
// web and Console doors ask before queueing, so a request naming a wire that
// carries nothing is refused where it arrives rather than dropped in the task.
//
// Read from robotState rather than from the task's own wires, because the task
// owns those and the web and Console tasks are the ones asking. One wire per
// critical section, the way the task publishes them.
inline bool auxLedTargetIsLit(uint8_t target) {
    for (size_t i = 0; i < BOARD_OUTPUT_COUNT; ++i) {
        if (!auxLedTargetReaches(target, i)) {
            continue;
        }
        taskENTER_CRITICAL(&robotStateMux);
        const bool accepts = auxLedWireAcceptsCommands(robotState.auxLed[i]);
        taskEXIT_CRITICAL(&robotStateMux);
        if (accepts) {
            return true;
        }
    }
    return false;
}

inline const char* auxLedEffectToString(AuxLedEffect effect) {
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

inline bool parseAuxLedEffect(const char* raw, AuxLedEffect* out) {
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
