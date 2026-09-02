// =============================================================================
// include/audio_test_hooks.h
//
// Native-test-only observation hooks for the audio command queue stubs
// (src/native_test_stubs.cpp). src/tasks/audio_task.cpp is not in
// [env:native]'s build_src_filter (platformio.ini is fenced), so no native
// test can link against the real audioQueuePlayTrack()/audioQueueSetVolume();
// each stub records what it was called with instead of performing the real
// FreeRTOS/AudioTask side effect.
//
// Declared once here, in one header both the stubs' definitions
// (native_test_stubs.cpp) and every native test that needs to observe them
// (test_api_audio_routes.cpp, test_console_module.cpp's #221 remainder
// sound.action.play-track/set-volume executors and #258's sound.action.*
// remainder) include, rather than each consumer re-declaring its own
// `extern` - matches the precedent include/commanded_modes_test_hooks.h and
// include/rc_input_test_hooks.h already set for exactly this shape of
// native stub observation hook. Only the fields more than one caller needs
// are declared here; test_api_audio_routes.cpp keeps its own extra externs
// for the fields only it observes (catalog contents, refresh/play-banked
// counters).
// =============================================================================
#pragma once

#include <stdint.h>

// audioQueuePlayTrack()/audioQueueSetVolume() and every other audio command
// queue stub share this one refusal toggle.
extern bool g_test_audio_queue_ok;

// audioQueuePlayTrack()
extern unsigned g_test_audio_play_track_calls;
extern uint16_t g_test_audio_last_track;

// audioQueueSetVolume()
extern unsigned g_test_audio_volume_calls;
extern uint8_t g_test_audio_last_volume;

// audioQueueTrackStop()
extern unsigned g_test_audio_stop_calls;

// audioQueueQueryStatus()
extern unsigned g_test_audio_query_calls;

// audioQueueDollar() - the $-letter shortcuts #258 wires (named-track plays,
// quiet, random-on/off, volume shortcuts) and sound.action.dollar-command's
// operator-supplied raw command all funnel through this one stub.
extern unsigned g_test_audio_dollar_calls;
extern char g_test_audio_last_dollar[16];
