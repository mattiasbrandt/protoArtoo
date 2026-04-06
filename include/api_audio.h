// =============================================================================
// include/api_audio.h
//
// Audio REST API
//   POST /api/audio — structured audio control
// =============================================================================
#pragma once

#include <ESPAsyncWebServer.h>

void registerAudioRoutes(AsyncWebServer& server);
void registerMoodMapRoutes(AsyncWebServer& server);
