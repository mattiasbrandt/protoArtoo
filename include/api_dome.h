// =============================================================================
// include/api_dome.h
//
// Dome proxy API endpoint registration.
// =============================================================================
#pragma once

class AsyncWebServer;

// Register GET /api/dome/layout endpoint (dome layout JSON cache relay).
void registerDomeRoutes(AsyncWebServer& server);
