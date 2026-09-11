// =============================================================================
// include/config_save_test_hooks.h
//
// Native-test-only control/observation hook for saveConfigToNvs()'s stub
// (src/native_test_stubs.cpp).
//
// saveConfigToNvs() itself lives in src/main.cpp, which the native build does
// not compile, so [env:native] carries a stand-in. That stand-in used to
// return true unconditionally; it now runs main.cpp's real sequence against
// the one Preferences double the native build has, and this is that double:
//
//   - g_test_config_prefs.failNextStringWrites(n) schedules the failed NVS
//     string write #375 taught the double to perform, which is the only way a
//     native test can reach a saveConfigToNvs() that returns false.
//   - g_test_config_prefs.getData() shows what actually landed, so a test can
//     ask what a half-completed save left behind.
//
// begin()/end() on the double clear neither the scheduled failures nor the
// stored keys, so both survive the call the handler under test makes.
//
// Declared once here, in one header both the stub's definition
// (native_test_stubs.cpp) and every native test that drives it include, rather
// than each consumer re-declaring its own `extern` -- the precedent
// include/drive_motion_test_hooks.h and include/commanded_modes_test_hooks.h
// already set for this shape of hook.
// =============================================================================
#pragma once

#include <Preferences.h>

extern Preferences g_test_config_prefs;
