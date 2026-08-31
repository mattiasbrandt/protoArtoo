// =============================================================================
// include/drive_motion_test_hooks.h
//
// Native-test-only observation/control hooks the Console module's drive
// motion executors (#222, src/console/console_module.cpp) need from
// src/native_test_stubs.cpp:
//
//   - g_test_millis: the settable millis() clock (native_test_stubs.cpp's
//     own comment: the drive arbiter treats timestamp 0 as "never
//     submitted", so a test that wants to observe a real submission through
//     driveArbiterResolve() must set this to something non-zero first).
//   - g_test_speed_preset_persist_ok / g_test_persisted_speed_preset: the
//     controllable success/failure and the recorded SpeedPresetId behind
//     applySpeedPresetPersisted()'s stub - the same stub
//     src/web/api_drive.cpp's handleSpeedPresetPost() already drives
//     (test_api_motion_routes.cpp).
//
// Declared once here, in one header both the stubs' definitions
// (native_test_stubs.cpp) and every native test that needs them
// (test_console_module.cpp, ...) include, rather than each consumer
// re-declaring its own `extern` - matches the precedent
// include/commanded_modes_test_hooks.h, include/rc_input_test_hooks.h and
// include/log_buffer_test_hooks.h already set for exactly this shape of
// native stub observation hook. test_api_motion_routes.cpp's own inline
// `extern` declarations for these same three symbols predate this header
// and are left as-is (harmless - identical extern of the same symbol),
// matching how include/commanded_modes_test_hooks.h treats
// g_test_commanded_stationary's own older inline declaration.
// =============================================================================
#pragma once

#include "drive_speed_preset.h"  // SpeedPresetId

extern unsigned long g_test_millis;
extern bool g_test_speed_preset_persist_ok;
extern SpeedPresetId g_test_persisted_speed_preset;
