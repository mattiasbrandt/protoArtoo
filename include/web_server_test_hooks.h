// =============================================================================
// include/web_server_test_hooks.h
//
// Native-test-only observation hook for requestSystemRestart()'s stub
// (src/native_test_stubs.cpp). src/main.cpp (the real deferred-restart flag
// requestSystemRestart() sets, consumed by loop()) is not in [env:native]'s
// build_src_filter (platformio.ini is fenced), so no native test can link
// against the real implementation; the stub counts calls instead of setting
// the real flag.
//
// Declared once here, in one header both the stub's definition
// (native_test_stubs.cpp) and every native test that needs to observe it
// (test_api_motion_routes.cpp's POST /api/reboot coverage,
// test_console_module.cpp's #225 system.action.reboot coverage) include,
// rather than each consumer re-declaring its own `extern` - the same
// precedent include/audio_test_hooks.h and include/commanded_modes_test_hooks.h
// already set for this shape of native stub observation hook, and the one
// the slice gate's own diff check (tools/slice_verify.py check 8: no new
// `extern` inside a .cpp file) now requires for a symbol shared across more
// than one native test binary.
// =============================================================================
#pragma once

// requestSystemRestart() (include/web_server.h)
extern unsigned g_test_restart_requests;
