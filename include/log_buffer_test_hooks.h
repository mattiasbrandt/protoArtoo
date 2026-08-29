// =============================================================================
// include/log_buffer_test_hooks.h
//
// Native-test-only log ring stand-in for the one main.cpp owns (main.cpp is
// not in [env:native]'s build_src_filter). Backed by the real log_buffer.cpp
// ring (src/native_test_stubs.cpp, guarded by PA_NATIVE_TEST_STUBS), so
// /api/logs and system.status.logs tests exercise the actual copy behavior
// rather than a canned string.
//
// Declared once here, in one header both the stub's definitions
// (native_test_stubs.cpp) and every native test that needs to fill the ring
// (test_console_module.cpp, ...) include, rather than each consumer
// re-declaring its own `extern` - keeps the declaration and every
// definition/use compiler-checked against the same types (matches
// include/rc_input_test_hooks.h's convention).
// =============================================================================
#pragma once

#include "log_buffer.h"  // LogBuffer, LOG_RING_MAX_LINES, LOG_LINE_MAX

extern LogBuffer g_test_log_buffer;
extern char g_test_log_storage[LOG_RING_MAX_LINES][LOG_LINE_MAX];
