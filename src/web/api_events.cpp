// =============================================================================
// src/web/api_events.cpp
//
// GET /api/events. The client cap is enforced here, before the upgrade, which
// is the point: on the stack this replaced, the equivalent check had to live in
// a global middleware, because that library's connect callback ran inside a
// client constructor that could not survive being closed -- two coredumps
// proved it. A per-request upgrade point removes that constraint: refusing here
// means no stream response head is ever written and no connection is ever
// registered, so a fourth tab is turned away rather than admitted and then
// reaped.
// =============================================================================

#include "../../include/api_events.h"

#include "../../include/logging.h"
#include "../../include/web_event_stream.h"
#include "../../include/web_server.h"   // requestStatusBroadcastNow()

static const char* TAG = "WebEvents";

// Called from whichever task the active backend dispatches requests on -- the
// psychic server task on the device, a test's own thread on the host. Never
// from core 1.
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
        return;
    }

    // The stream is delta-triggered: it carries a status when something asks
    // for one and at no other time. So a client that connects to a droid which
    // is not changing is told nothing at all, and a tab RECONNECTING after a
    // latch shows whatever it had cached until some unrelated change happens
    // to come along. Publishing every failsafe edge (src/failsafe_gate.cpp)
    // fixes the browser that was already listening; it cannot fix the one that
    // was not there when the edge went by (#346).
    //
    // So admission is itself an edge: a client arriving is a change in who
    // needs to know. This asks rather than sends -- the payload is built on the
    // event stream task with its own measured stack and its own buffer, and
    // building 3 KB of JSON inline here would put it on the web handler's stack
    // and race the broadcaster for that buffer. The ask reaches every open
    // client rather than only this one, which costs one extra frame to at most
    // two other tabs and is never wrong: what they receive is current.
    //
    // Refusals above return without asking. A client that was turned away has
    // no stream to be told anything on.
    requestStatusBroadcastNow();
}
