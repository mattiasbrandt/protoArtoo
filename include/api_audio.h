// =============================================================================
// include/api_audio.h
//
// Audio REST API, written against the project-owned WebRequest seam (ADR 0021)
// and bound by the seam route table (src/web/web_seam_routes.cpp).
//
//   GET  /api/audio                   — live module status
//   POST /api/audio                   — structured audio control
//   GET  /api/audio/tracks            — named track assignments + random range
//   POST /api/audio/tracks            — set one named track or random-range bound
//   POST /api/audio/category-range    — set one category lo/hi pair atomically
//   GET  /api/audio/mood-map          — mood -> category mask map
//   POST /api/audio/mood-map          — set the mood -> category mask map
//   GET  /api/audio/catalog           — cached CHIRP catalog
//   POST /api/audio/catalog/refresh   — enqueue a catalog refresh
//   POST /api/audio/query             — enqueue an on-demand module status poll
//   POST /api/audio/play-banked       — play one CHIRP entry by bank/page/index
//   POST /api/mood                    — apply a mood preset
//
// The write paths decide nothing here: they read their fields through the
// ADR 0011 apply cores (api_audio_tracks_apply.h, api_audio_category_range_apply.h,
// api_audio_mood_map_apply.h) and the canonical schema stays in the ADR 0013
// audio config map behind its ConfigReader seam. These handlers carry values
// across and answer.
// =============================================================================
#pragma once

#include "web_request.h"

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
