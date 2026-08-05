// =============================================================================
// include/api_events.h
//
// GET /api/events -- the live update stream the dashboard subscribes to
// (data/status_stream.js). The handler only decides whether this connection may
// have a stream and, if so, upgrades it; everything after that belongs to the
// broadcaster in include/web_event_stream.h.
// =============================================================================
#pragma once

#include "web_request.h"

void handleEventsGet(WebRequest& req);
