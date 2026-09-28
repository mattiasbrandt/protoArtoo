// =============================================================================
// include/servo_task.h
//
// ServoTask  --  controls utility arm servos via LEDC PWM.
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

// The ServoTask function  --  runs on Core 1.
void servoTask(void* pvParameters);

// What ServoTask started with, per Output (ADR 0027, #364). armId is the
// BOARD_OUTPUTS index (include/output_wire.h); 255, the ARM1+ARM2 broadcast, is
// two Outputs and answers false here - ask for each.
//
// A wired tick saved after boot takes effect at the next start
// (src/config_settings.cpp, ApplyTiming::AtReboot), so the saved tick and
// these two can disagree until the droid restarts. These are the running
// truth, and the only one: POST /api/servo refuses an act on an Output that
// servoTaskDrivesOutput() says nothing drives, and GET /api/servo/outputs
// reports both beside the saved tick, so a page never works either out from
// config.
//
// Lock-free on purpose: the snapshot is written once, in servoTaskInit(),
// which setup() runs before it starts ServoTask, the Console task, or WiFi -
// the web server only starts from the WiFi event callback - and it is never
// written again.
//
// servoTaskWiredAtStart(): the Output's wired tick as the droid started with
// it.
bool servoTaskWiredAtStart(uint8_t armId);

// servoTaskDrivesOutput(): ServoTask puts servo pulses on this Output this
// boot - wired at start, no light on its wire at start, and LEDC came up. An
// Output this answers false for is one every servo command is dropped for.
bool servoTaskDrivesOutput(uint8_t armId);
