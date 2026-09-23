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

#include <stdint.h>

#include "robot_state.h"

// Every lit wire. The value is outside BOARD_OUTPUT_COUNT by construction, so
// it can never collide with an Output index.
constexpr uint8_t AUX_LED_TARGET_ALL = 0xFF;

bool auxLedTaskInit();
void auxLedTask(void* pvParameters);

bool auxLedQueueSetColor(uint8_t target, uint8_t r, uint8_t g, uint8_t b, CommandSource source);
bool auxLedQueueSetEffect(uint8_t target, AuxLedEffect effect, CommandSource source);

// Whether `target` is a wire this droid can be told to light: a lit Output's
// index, or AUX_LED_TARGET_ALL while at least one wire is lit. It is what the
// web and Console doors ask before queueing, so a request naming a wire that
// carries nothing is refused where it arrives rather than dropped in the task.
bool auxLedTargetIsLit(uint8_t target);

const char* auxLedEffectToString(AuxLedEffect effect);
bool parseAuxLedEffect(const char* raw, AuxLedEffect* out);
