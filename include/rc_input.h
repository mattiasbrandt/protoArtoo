// =============================================================================
// include/rc_input.h
//
// RcInputTask public interface.
// Handles all RC input modes: standard_pwm, single_sbus, dual_sbus.
// Runs on Core 1 at ~200 Hz poll rate.
// =============================================================================
#pragma once

#include "rc_dispatcher_helpers.h"  // RcDispatchOutcome, CommandSource (fwd-declared)
#include "rc_input_step.h"          // RcInputStartupPlan
#include "rc_mapping.h"

// -----------------------------------------------------------------------------
// rcInputTask()
// FreeRTOS task function  --  pin to Core 1 via xTaskCreatePinnedToCore().
// Stack: RC_INPUT_TASK_STACK_BYTES (include/config.h). Priority: 5.
// Implements Layers 1 (hardware failsafe) and 2 (software watchdog) failsafe.
// -----------------------------------------------------------------------------
void rcInputTask(void* pvParameters);

// -----------------------------------------------------------------------------
// rcInputAllocateDecoders()
// Allocates the SBUS decoders the boot RC plan reads, once, from setup(), and
// only those (#428): dual_sbus two, single_sbus one, standard_pwm none, and a
// droid with no RC input creates no task at all. Call before creating
// rcInputTask(), which starts what this allocated and allocates nothing - Core
// 1 stays heap-free after setup(). A decoder that cannot be allocated is
// logged and its receiver stays off, as one whose RMT channel will not start.
// -----------------------------------------------------------------------------
void rcInputAllocateDecoders(const RcInputStartupPlan& plan);

// Test-dispatch helper used by the REST /api/actions/test route and the
// Controller Console's non-motion action executor (#220, ADR 0036) - the
// single dispatch core shared with the RC trigger path. src attributes the
// resulting CommandSource (SRC_WEB_API, SRC_SERIAL_CONSOLE, SRC_WEB_CONSOLE,
// ...) so downstream logs/state can tell a test-dispatched command from a
// real RC trigger. Returns the real outcome instead of the pre-#220 void, so
// callers stop reporting success on a dropped command.
RcDispatchOutcome dispatchRcTriggerActionTest(RobotActionId target, const char* payload,
                                              bool pressed, CommandSource src);
