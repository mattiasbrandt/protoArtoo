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

// The log SINK stand-in (#270, ADR 0037): the ring paLogLine() writes into,
// the drain cursor over it, and who owns the wire. src/main.cpp keeps all
// three together under one critical section; the stub keeps them here so
// test_console_log_drain can put the drain back to a known state between
// cases, the same way serialStubReset() does for the transport.
//
//   g_test_log_sink_buffer   the ring paLogLine() appends to.
//   g_test_log_wire_owned    false = pre-bind, paLogLine() writes the wire
//                            directly; true = the Console task owns it and
//                            only consoleSerialDrainLogs() writes.
//   g_test_log_drain_cursor  next totalWritten index the drain owes the wire.
//
// A SECOND ring, deliberately, and the only place the native stand-in departs
// from the target's single ring. g_test_log_buffer above is filled BY tests,
// to drive the /api/logs and system.status.logs readers; letting paLogLine()
// append to it too would mean any module that logs during a fixture's setUp -
// consoleModuleInit() does - silently adds a line to what those tests are
// counting. Which ring a line lands in is not what either suite is about.
extern LogBuffer g_test_log_sink_buffer;
extern char g_test_log_sink_storage[LOG_RING_MAX_LINES][LOG_LINE_MAX];
extern bool g_test_log_wire_owned;
extern uint32_t g_test_log_drain_cursor;
