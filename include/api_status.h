#pragma once

#include <ESPAsyncWebServer.h>

#include "web_request.h"

// Routes not yet ported to the seam: /api/health, /api/serial.
void registerStatusRoutes(AsyncWebServer& server);

void handleWifiGet(WebRequest& req);

// GET /api/status. Ported ahead of the rest of its route group because the
// admission counters it carries are what the load harness and the migration
// scorecard read; without it the guard's evidence is unobservable.
void handleStatusGet(WebRequest& req);
