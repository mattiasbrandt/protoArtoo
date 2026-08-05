// =============================================================================
// src/web/api_events.cpp
//
// GET /api/events. The client cap is enforced here, before the upgrade, which
// is the point: on the async stack the equivalent check had to live in a
// middleware because AsyncEventSource's connect callback runs inside a client
// constructor that cannot survive being closed (see startHttpServerOnce() in
// web_server.cpp for the two coredumps that proved it). A per-request upgrade
// point removes that constraint -- refusing here means no stream response head
// is ever written and no connection is ever registered, so a fourth tab is
// turned away rather than admitted and then reaped.
// =============================================================================

#include "../../include/api_events.h"

#include "../../include/logging.h"
#include "../../include/web_event_stream.h"

static const char* TAG = "WebEvents";

void handleEventsGet(WebRequest& req) {
    if (webEventStreamClientCount() >= PA_ADMISSION_MAX_SSE_CLIENTS) {
        g_webRefusedSseCap = g_webRefusedSseCap + 1u;
        PA_LOG_WARN(TAG, "event stream cap (%u) reached; refusing new client",
                    (unsigned)PA_ADMISSION_MAX_SSE_CLIENTS);
        // A short body rather than the bare close a heap refusal gets: the cap
        // is a "too many tabs" condition, not a memory one, so there is no
        // reason to be stingy -- and 503 reaches data/status_stream.js as an
        // EventSource error, which is already wired to its exponential backoff.
        // The operator's fourth tab reconnects on its own once one is closed.
        req.send(503, "text/plain", "event stream at capacity");
        return;
    }

    if (!req.beginEventStream()) {
        PA_LOG_WARN(TAG, "event stream could not be started");
        req.send(503, "text/plain", "event stream unavailable");
    }
    // On success the response is open-ended and belongs to the broadcaster.
    // Nothing more is sent from here.
}
