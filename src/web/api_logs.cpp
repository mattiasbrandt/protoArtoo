// =============================================================================
// src/web/api_logs.cpp
//
// GET /api/logs - recent log buffer, ported to the WebRequest seam (ADR 0021).
// One of the three routes data/app.js fetches on every page load.
// =============================================================================

#include "api_logs.h"

#include "log_buffer.h"
#include "web_server.h"

void handleLogsGet(WebRequest& req) {
    // Buffer sized to match ring capacity: LOG_BUFFER_LINES * LOG_LINE_MAX + 1.
    // Static, not stack: neither the async_tcp task nor the psychic server task
    // has 6 KB of stack to spare, and handlers serialize on one task under both
    // backends, so a shared buffer is race-free - the same argument
    // /api/status makes for its own payload buffer.
    static char body[LOG_BUFFER_LINES * LOG_LINE_MAX + 1];

    // sizeof(body), not sizeof(body) - 1: logBufferCopy() treats its argument
    // as the total buffer size and reserves the terminator itself, so the
    // "+ 1" above already covers it. Passing one less reserved the byte twice
    // and clipped the last character off a completely full ring.
    //
    // Locking against the log writers lives inside copyRecentLogs(), which
    // holds the ring's own mux for the copy. Nothing further is needed here.
    copyRecentLogs(body, sizeof(body));
    req.send(200, "text/plain", body);
}
