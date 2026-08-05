#pragma once

#include <ESPAsyncWebServer.h>

#include "web_request.h"

// Routes not yet ported to the seam: /api/status, /api/health, /api/serial.
void registerStatusRoutes(AsyncWebServer& server);

void handleWifiGet(WebRequest& req);
