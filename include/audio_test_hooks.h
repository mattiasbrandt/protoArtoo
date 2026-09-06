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
// are declared here. The catalog contents and the refresh/play-banked
// counters were test_api_audio_routes.cpp's alone until #221 wired
// sound.api.get-catalog/-refresh-catalog/-play-banked, so they moved here; the
// declarations that file still carries for them are an identical
// redeclaration, which is legal and left alone rather than churned.
// =============================================================================
#pragma once

#include <stdint.h>

#include "audio_driver.h"  // AudioCatalogEntry - the catalog stub's element type

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

// audioGetCapabilities() - the driver capability word the catalog rows gate on
// (AudioDriver::AUDIO_CAP_CATALOG). Second caller as of #221:
// test_console_module.cpp drives sound.api.get-catalog/-refresh-catalog/
// -play-banked's own catalog-support gate with it.
extern uint8_t g_test_audio_capabilities;

// audioIsCatalogReady() / audioGetCatalogEntries() - sound.api.get-catalog's
// `ready` field and its item records.
extern bool g_test_audio_catalog_ready;
extern AudioCatalogEntry g_test_audio_catalog_entries[16];
extern uint16_t g_test_audio_catalog_entry_count;

// audioQueueRefreshCatalog() - sound.api.refresh-catalog.
extern unsigned g_test_audio_refresh_catalog_calls;

// audioQueuePlayTrackBanked() - sound.api.play-banked.
extern unsigned g_test_audio_play_banked_calls;
extern uint16_t g_test_audio_last_banked_index;
extern uint8_t g_test_audio_last_banked_bank;
extern char g_test_audio_last_banked_page;
