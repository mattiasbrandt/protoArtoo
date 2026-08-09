// =============================================================================
// include/api_audio.h
//
// Audio REST API, written against the project-owned WebRequest seam (ADR 0021)
// and bound by the seam route table (src/web/web_seam_routes.cpp).
//
//   GET  /api/audio                   - live module status
//   POST /api/audio                   - structured audio control
//   GET  /api/audio/tracks            - named track assignments + random range
//   POST /api/audio/tracks            - set one named track or random-range bound
//   POST /api/audio/category-range    - set one category lo/hi pair atomically
//   GET  /api/audio/mood-map          - mood -> category mask map
//   POST /api/audio/mood-map          - set the mood -> category mask map
//   GET  /api/audio/catalog           - cached CHIRP catalog
//   POST /api/audio/catalog/refresh   - enqueue a catalog refresh
//   POST /api/audio/query             - enqueue an on-demand module status poll
//   POST /api/audio/play-banked       - play one CHIRP entry by bank/page/index
//   POST /api/mood                    - apply a mood preset
//
// The write paths decide nothing here: they read their fields through the
// ADR 0011 apply cores (api_audio_tracks_apply.h, api_audio_category_range_apply.h,
// api_audio_mood_map_apply.h) and the canonical schema stays in the ADR 0013
// audio config map behind its ConfigReader seam. These handlers carry values
// across and answer.
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "web_request.h"

// Format JSON response for audio status endpoint.
// Pure function - no globals, no Arduino, no FreeRTOS.
// params: buf          - output buffer (must not be null)
//         bufSize      - size of buf in bytes (256 bytes sufficient with RX diagnostics)
//         driverName   - driver name string e.g. "DY-SV5W" (must not be null)
//         capabilities - AudioDriver::AUDIO_CAP_* bitmask; controls which fields are meaningful
//         linkOk       - true if module responded to at least one UART query
//         active       - true if firmware sent a play command recently (audioActive)
//         playState    - 0=stop 1=playing 2=paused 0xFF=unknown
//         device       - 0=USB 1=SD/TF 2=FLASH 0xFF=none/unknown
//         totalTracks  - total tracks reported by module (0 if unknown)
//         currentTrack - currently selected track (0 if unknown)
//         rxStatus     - compact RX diagnostic string (must not be null)
//         rxDetail     - operator-readable RX diagnostic (must not be null)
// thread-safe: yes (pure function, no globals)
void formatAudioStatusJson(char* buf, size_t bufSize, const char* driverName,
                           uint8_t capabilities, bool linkOk, bool active,
                           uint8_t playState, uint8_t device, uint16_t totalTracks,
                           uint16_t currentTrack, const char* rxStatus,
                           const char* rxDetail);

void handleAudioGet(WebRequest& req);
void handleAudioPost(WebRequest& req);

void handleAudioTracksGet(WebRequest& req);
void handleAudioTracksPost(WebRequest& req);
void handleAudioCategoryRangePost(WebRequest& req);

void handleAudioMoodMapGet(WebRequest& req);
void handleAudioMoodMapPost(WebRequest& req);

void handleAudioCatalogGet(WebRequest& req);
void handleAudioCatalogRefreshPost(WebRequest& req);

void handleAudioQueryPost(WebRequest& req);
void handleAudioPlayBankedPost(WebRequest& req);

void handleMoodPost(WebRequest& req);
