// =============================================================================
// src/web/api_dome.cpp
//
// Dome proxy API endpoints
//   GET /api/dome/layout  - fetch dome layout JSON from cache (non-blocking relay)
//
// The dome layout is cached by DomeLinkTask on WiFi transport.
// The web handler streams the cached bytes out through a chunked response
// filler (domeLayoutCacheReadChunk), so no per-request buffer is needed on the
// server task's stack - each fill call copies only the small slice the backend
// asks for, under a brief cache-mutex hold.
// If cache is empty or transport != WiFi, handler returns 503 Service Unavailable
// and sets a refresh flag for the background task to fetch on next loop.
//
// Written against the project-owned WebRequest seam (ADR 0021) and bound by the
// seam route table.
// =============================================================================

#include "api_dome.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <stdio.h>

#include "api_json_response.h"
#include "dome_link.h"
#include "dome_link_transport.h"
#include "logging.h"
#include "robot_state.h"

static const char* TAG = "WebServer";

namespace {

// The cache generation an in-flight layout read is pinned to.
//
// File-scope because the seam's chunked body producer is a plain function
// pointer with no capture. Handlers serialize on one server task under both
// backends, so exactly one layout read can be in flight -- the same
// single-server-task argument api_system.cpp's coredump statics rest on.
uint32_t s_layoutFetchedAtMs = 0;

size_t fillDomeLayoutResponse(uint8_t* out, size_t capacity, size_t offset) {
    return domeLayoutCacheReadChunk(out, capacity, offset, s_layoutFetchedAtMs);
}

}  // namespace

// Non-blocking: takes the cache mutex briefly, returns cached bytes or a 503.
void handleDomeLayoutGet(WebRequest& req) {
    // Check transport state (thread-safe read)
    taskENTER_CRITICAL(&robotStateMux);
    DomeLinkTransport transport = robotState.domeActiveTransport;
    taskEXIT_CRITICAL(&robotStateMux);

    // Only serve if WiFi is active transport
    if (transport != DOME_LINK_TRANSPORT_WIFI) {
        PA_LOG_DEBUG(TAG, "GET /api/dome/layout: WiFi transport not active (transport=%u)",
                     (unsigned)transport);
        webSendJsonError(req, 503, "dome layout unavailable (not WiFi)");
        return;
    }

    // Attempt to get cached data
    DomeLayoutCacheStatus status = domeLayoutCacheGetStatus();

    if (!status.has_data) {
        // Cache miss or empty: request a background fetch and return 503
        PA_LOG_DEBUG(TAG, "GET /api/dome/layout: cache miss, requesting refresh");
        domeLayoutCacheRefreshRequested();
        JsonDocument doc;
        doc["ok"] = false;
        doc["error"] = "dome layout unavailable";
        doc["retry"] = true;
        webSendJsonDocument(req, doc, 256, TAG, 503);
        return;
    }

    // Cache hit: stream through a filler pinned to this fetch's generation
    // (fetched_at_ms). If a background refresh replaces the cache mid-send,
    // domeLayoutCacheReadChunk() returns 0 (clean early end) rather than
    // splicing bytes from two different fetches.
    s_layoutFetchedAtMs = status.fetched_at_ms;
    const uint32_t ageMs = millis() - s_layoutFetchedAtMs;
    char ageHeader[16];
    snprintf(ageHeader, sizeof(ageHeader), "%u", (unsigned)ageMs);
    req.addHeader("X-Dome-Layout-Age-Ms", ageHeader);

    if (!req.sendChunked("application/json", fillDomeLayoutResponse)) {
        JsonDocument doc;
        doc["ok"] = false;
        doc["error"] = "dome layout unavailable";
        doc["retry"] = true;
        webSendJsonDocument(req, doc, 256, TAG, 503);
        return;
    }
    PA_LOG_DEBUG(TAG, "GET /api/dome/layout: serving %u bytes (age=%u ms)",
                 (unsigned)status.length, (unsigned)ageMs);
}
