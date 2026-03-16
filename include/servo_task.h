// =============================================================================
// include/servo_task.h
//
// ServoTask — controls utility arm servos via LEDC PWM.
// Receives commands via servoCmdQueue and executes them.
// =============================================================================
#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "robot_state.h"

// Initialize servo hardware and start ServoTask.
// Call once from setup().
void servoTaskInit();

// The ServoTask function — runs on Core 1.
void servoTask(void* pvParameters);
