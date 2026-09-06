// =============================================================================
// include/rc_input_test_hooks.h
//
// Native-test-only observation hooks for dispatchRcTriggerActionTest()'s
// stub (src/native_test_stubs.cpp, guarded by PA_NATIVE_TEST_STUBS). rc_input.cpp
// (RcInputTask, pulseIn, esp_task_wdt) is not in [env:native]'s build_src_filter,
// so no native test can link against the real function; the stub records
// what it was called with and returns a controllable outcome.
//
// Declared once here, in one header both the stub's definitions
// (native_test_stubs.cpp) and every native test that needs to observe or
// control the stub (test_console_module.cpp, ...) include, rather than each
// consumer re-declaring its own `extern` - keeps the declaration and every
// definition/use compiler-checked against the same types.
// =============================================================================
#pragma once

#include "rc_input.h"   // RcDispatchOutcome, CommandSource (fwd-declared)
#include "rc_mapping.h"  // RobotActionId

extern unsigned g_test_dispatch_action_calls;
extern RobotActionId g_test_last_dispatch_target;
extern CommandSource g_test_last_dispatch_source;
extern RcDispatchOutcome g_test_dispatch_outcome;
// The payload dispatchRcTriggerActionTest() was last called with (#221: the
// Console now supplies a real, validated payload for DOME_ACTION_MARCDUINO_
// SEQ/CMD instead of the pre-#221 hardcoded ""; native tests need to observe
// what actually reached the dispatch core, not just that it was called).
extern char g_test_last_dispatch_payload[32];
