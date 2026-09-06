// =============================================================================
// include/aux_led_test_hooks.h
//
// Native-test-only observation hook for the AUX LED command queue stubs
// (src/native_test_stubs.cpp). src/tasks/aux_led.cpp is not in [env:native]'s
// build_src_filter (platformio.ini is fenced), so no native test can link
// against the real auxLedQueueSetColor()/auxLedQueueSetEffect(); the stub
// applies its side effect to robotState.auxLed directly (native_test_stubs.cpp's
// own header comment) instead of going through the real FreeRTOS queue/task.
//
// Declared once here rather than as an inline `extern` in each consumer
// (test_console_module.cpp's #221 remainder aux.action.led-color/-effect
// executors) - matches the precedent include/audio_test_hooks.h and
// include/commanded_modes_test_hooks.h already set for this shape of native
// stub observation hook.
// =============================================================================
#pragma once

// auxLedQueueSetColor()/auxLedQueueSetEffect() share this one refusal toggle.
extern bool g_test_aux_led_queue_ok;
