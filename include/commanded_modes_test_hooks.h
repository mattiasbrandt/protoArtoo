// =============================================================================
// include/commanded_modes_test_hooks.h
//
// Native-test-only observation hooks for the commanded_modes.h setter stubs
// (src/native_test_stubs.cpp). src/commanded_modes.cpp is not in [env:native]'s
// build_src_filter (platformio.ini is fenced), so no native test can link
// against the real commandedSetStationary/Sleep/WebControl/RcDebug(); each
// stub records what it was called with instead of performing the real
// FreeRTOS/RobotState side effect.
//
// Declared once here, in one header both the stubs' definitions
// (native_test_stubs.cpp) and every native test that needs to observe them
// (test_console_module.cpp, ...) include, rather than each consumer
// re-declaring its own `extern` - keeps the declaration and every
// definition/use compiler-checked against the same types, and matches the
// precedent include/rc_input_test_hooks.h and include/log_buffer_test_hooks.h
// already set for exactly this shape of native stub observation hook.
// =============================================================================
#pragma once

// commandedSetStationary() - shared with the older, file-scope tests that
// already declare this one inline; declaring it again here is harmless
// (identical extern of the same symbol), and the Console's new tests
// (test_console_module.cpp) get it through this header rather than a new
// inline extern.
extern bool g_test_commanded_stationary;

// commandedSetSleep() - #226's first native stub for this setter.
extern bool g_test_commanded_sleep;
extern unsigned g_test_commanded_sleep_calls;

// commandedSetWebControl()
extern bool g_test_commanded_web_control;
extern unsigned g_test_web_control_calls;

// commandedSetRcDebug()
extern bool g_test_commanded_rc_debug;
extern unsigned g_test_commanded_rc_debug_calls;

// applyMood() (src/tasks/mood.cpp) - the real executor behind both
// system.action.set-mood and system.config.mood (#226).
extern unsigned g_test_applied_mood;

// requestStatusBroadcastNow() (src/web/web_server.h) - configCommitApplied()
// and the Commanded Mode direct executors (#226) both call this on a
// successful, state-changing write.
extern unsigned g_test_status_broadcast_count;
