// =============================================================================
// include/audio_config_map.h
//
// audio_config_map — canonical home for the config-to-audio schema mapping
// (ADR 0013, issue #18 finding 3). Pure functions: no FreeRTOS, no
// AudioDriver, no raw Preferences, no logging.
//
// Owns:
//   - ConfigSnapshot -> AudioPlaybackConfig mapping (audioConfigMapBuild)
//   - named-track projection (audioConfigMapNamedTracks)
//   - both chirp NVS-key tables (audioChirpKeyForSlot / audioChirpKeyForCategory)
//   - the $-command table (audioSlotForDollar)
//   - the chirp binding unpackers (audioUnpackChirpBinding /
//     audioUnpackChirpCategoryBinding)
//   - the binding-cache refresh (audioBindingsRefresh), reading through the
//     ADR 0002 ConfigReader seam (PrefsReader in production, MapReader in
//     tests) instead of raw Preferences — this is what makes the refresh
//     loop (capability gate, key iteration, rejection paths) testable.
//
// AudioTask (src/tasks/audio_task.cpp) stays the shell: it owns the
// AudioBindingCache instance, opens/closes the real Preferences handle
// around the ConfigReader, computes catalogCapable from the driver, and
// keeps the per-command readPlaybackConfig() rebuild and
// executePlaybackIntent() dispatch.
//
// api_audio.cpp (the web write-handler layer) is the intended second
// consumer of these tables in a later Apply Core wave (ADR 0011) — not
// this module.
//
// Defined in src/tasks/audio_config_map.cpp.
// =============================================================================
#pragma once

#include <stdint.h>

#include "audio_dollar_parser.h"    // AudioNamedTracks
#include "audio_playback_policy.h"  // AudioPlaybackSlot, AudioPlaybackConfig, AudioBindingCache
#include "config_io.h"              // ConfigReader
#include "config_store.h"           // ConfigSnapshot

void audioConfigMapBuild(const ConfigSnapshot& cfg, AudioPlaybackConfig* out);
void audioConfigMapNamedTracks(const AudioPlaybackConfig& playback, AudioNamedTracks* out);

const char* audioChirpKeyForSlot(AudioPlaybackSlot slot);
const char* audioChirpKeyForCategory(uint8_t categoryIndex);

AudioPlaybackSlot audioSlotForDollar(const char* cmd);

bool audioUnpackChirpBinding(uint32_t packed, uint8_t* bankOut, char* pageOut, uint16_t* indexOut);
bool audioUnpackChirpCategoryBinding(uint32_t packed, uint8_t* bankOut, char* pageOut);

// Clears *out, then (if catalogCapable) populates it from `reader` via the
// chirp key tables above. Returns false (with *out left cleared) when
// !catalogCapable, matching the legacy clear-then-gate behavior.
bool audioBindingsRefresh(const ConfigReader& reader, bool catalogCapable, AudioBindingCache* out);
