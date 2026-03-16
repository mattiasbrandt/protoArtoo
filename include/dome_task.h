// =============================================================================
// include/dome_task.h
//
// DomeTask — controls dome rotation ESC via LEDC PWM.
// Receives commands via domeCmdQueue and executes them.
// =============================================================================
#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "robot_state.h"

// Initialize dome ESC hardware and start DomeTask.
// Call once from setup().
void domeTaskInit();

// The DomeTask function — runs on Core 1.
void domeTask(void* pvParameters);
