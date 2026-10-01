// =============================================================================
// include/servo_task.h
//
// ServoTask  --  drives every Servo Output through its backend
// (include/servo_backend.h). Receives commands via servoCmdQueue and executes
// them.
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

// What ServoTask started with, per Output (ADR 0027, #364), asked by Output
// Address. SERVO_OUTPUT_BOTH_ARMS is two Outputs and answers false here - ask
// for each - and so does any address no member of this image drives.
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
bool servoTaskWiredAtStart(ServoOutputAddress output);

// servoTaskDrivesOutput(): ServoTask puts servo pulses on this Output this
// boot - wired at start, no light on its wire at start, and LEDC came up. An
// Output this answers false for is one every servo command is dropped for.
bool servoTaskDrivesOutput(ServoOutputAddress output);

// servoTaskMayTakeForRun(): a Find by Moving run may take this Output now - a
// free servo Output, one nothing drives, no Part is on and no light is on the
// wire of (include/servo_run.h servoRunMayTake(), #411). The servo route and
// the Console admit a nudge and a release on it for that reason, and ServoTask
// asks the same question before it takes one. Reads the boot snapshot above
// and the live config cache, so it is current as of the call.
bool servoTaskMayTakeForRun(ServoOutputAddress output);

// servoTaskRunHolds(): a Find by Moving run holds this Output now, so a
// release for it lets it go (a run holds one Output at most; include/
// servo_run.h). Read from Core 0 without a lock: which Output a run holds is
// one byte ServoTask writes, and a stale read can only be a release landing
// just as the run let go, which the task then drops.
bool servoTaskRunHolds(ServoOutputAddress output);
