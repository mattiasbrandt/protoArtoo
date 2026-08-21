// =============================================================================
// include/aux_led.h
//
// AUX LED task and command queue interface.
// Drives a single WS2812B strip on a selectable AUX header.
// =============================================================================
#pragma once

#include <stdint.h>

#include "robot_state.h"

bool auxLedTaskInit();
void auxLedTask(void* pvParameters);

bool auxLedQueueSetColor(uint8_t r, uint8_t g, uint8_t b, CommandSource source);
bool auxLedQueueSetEffect(AuxLedEffect effect, CommandSource source);

const char* auxLedEffectToString(AuxLedEffect effect);
bool parseAuxLedEffect(const char* raw, AuxLedEffect* out);
