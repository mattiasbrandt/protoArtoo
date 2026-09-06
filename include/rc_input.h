// =============================================================================
// include/rc_input.h
//
// RcInputTask public interface.
// Handles all RC input modes: standard_pwm, single_sbus, dual_sbus.
// Runs on Core 1 at ~200 Hz poll rate.
// =============================================================================
#pragma once

#include "rc_mapping.h"

// -----------------------------------------------------------------------------
// rcInputTask()
// FreeRTOS task function  --  pin to Core 1 via xTaskCreatePinnedToCore().
// Stack: RC_INPUT_TASK_STACK_BYTES (include/config.h). Priority: 5.
// Implements Layers 1 (hardware failsafe) and 2 (software watchdog) failsafe.
// -----------------------------------------------------------------------------
void rcInputTask(void* pvParameters);

// Test-dispatch helper used by web action testing endpoint.
// Routes through the same trigger-action dispatch path as RC input runtime.
void dispatchRcTriggerActionTest(RobotActionId target, const char* payload, bool pressed);
