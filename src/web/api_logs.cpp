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
    // The body buffer is allocated once at boot alongside the ring, sized to
    // the ring's capacity (capacity * LOG_LINE_MAX + 1). Shared, not stack:
    // neither the async_tcp task nor the psychic server task has 6 KB of
    // stack to spare, and this handler's own callers - the async_tcp task or
    // the psychic server task, never both in the same build - serialize on
    // one task, so one buffer is race-free for THIS caller - the same
    // argument /api/status makes for its own payload buffer.
    //
    // This is narrower than it used to read: the Console task
    // (src/tasks/console_task.cpp) is a second, independent Core 0 task, and
    // its system.status.logs query answers the same "recent log lines"
    // question without going through this buffer at all - it reads the ring
    // directly via getLogBufferCount()/copyLogLineAt() (include/web_server.h,
    // src/console/console_module.cpp), each call taking the ring's own lock
    // for one line rather than sharing this allocation (#239). Do not widen
    // this function or this buffer to a second caller; add a ring-direct
    // reader instead, the way the Console did.
    size_t bodySize = 0;
    char* body = recentLogsBodyBuffer(&bodySize);
    if (body == nullptr) {
        // Boot-time ring sizing failed (allocation); the bootstrap ring still
        // logs, but no response buffer exists.
        req.send(503, "text/plain", "log buffer unavailable");
        return;
    }

    // bodySize, not bodySize - 1: logBufferCopy() treats its argument as the
    // total buffer size and reserves the terminator itself, so the "+ 1" in
    // the allocation already covers it. Passing one less reserved the byte
    // twice and clipped the last character off a completely full ring.
    //
    // Locking against the log writers lives inside copyRecentLogs(), which
    // holds the ring's own mux for the copy. Nothing further is needed here.
    copyRecentLogs(body, bodySize);
    req.send(200, "text/plain", body);
}
