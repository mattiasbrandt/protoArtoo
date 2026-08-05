// =============================================================================
// include/api_config.h
//
// Config API endpoints. GET /api/config is ported to the project-owned
// WebRequest seam (ADR 0021) and bound by the seam route table; the write
// routes still register against the async server until their group lands.
// =============================================================================
#pragma once

#include <ESPAsyncWebServer.h>

#include "web_request.h"

// Routes not yet ported to the seam: POST /api/config, GET+POST /api/rc/map,
// POST /api/wifi. Called only from the async registration block.
void registerConfigRoutes(AsyncWebServer& server);

void handleConfigGet(WebRequest& req);
