// =============================================================================
// include/servo_task_test_hooks.h
//
// Native-test-only stand-in for ServoTask's boot snapshot (#364).
// src/tasks/servo_task.cpp is not in [env:native]'s build_src_filter, so
// servoTaskWiredAtStart() and servoTaskDrivesOutput() (include/servo_task.h)
// are answered by src/native_test_stubs.cpp from these two masks, one bit per
// slot (include/servo_backend.h servoOutputSlotOf()). Both default to every Output, so a suite that does not care about the
// snapshot sees a droid that started with everything wired and driven.
//
// Declared here rather than as an inline `extern` in each consumer, the
// precedent include/aux_led_test_hooks.h sets for this shape of stub.
// =============================================================================
#pragma once

#include <stdint.h>

extern uint8_t g_test_servo_wired_at_start_mask;
extern uint8_t g_test_servo_driven_mask;
// Which Outputs ServoTask kept LEDC off at start for a light, one bit per
// slot; none by default. servoTaskMayTakeForRun() reads it with the two masks
// above and the live cache, through the one rule (include/servo_run.h).
extern uint8_t g_test_servo_lit_at_start_mask;
// Which Output a Find by Moving run holds now, as a bit in the same per-slot
// layout as the masks above; none by default. servoTaskRunHolds() answers from
// it. Production holds one slot index, not a mask (s_runSlot,
// src/tasks/servo_task.cpp): a run holds at most one Output, so a test that
// sets more than one bit here describes a state the firmware cannot be in.
extern uint8_t g_test_servo_run_held_mask;
