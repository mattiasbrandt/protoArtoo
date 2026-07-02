// =============================================================================
// src/web/api_dome.cpp
//
// Dome proxy API endpoints
//   GET /api/dome/layout  — fetch dome layout JSON from cache (non-blocking relay)
//
// The dome layout is cached by DomeLinkTask on WiFi transport.
// The web handler streams the cached bytes out via a chunked response filler
// (domeLayoutCacheReadChunk), so no per-request buffer is needed on the
// AsyncTCP task stack — each fill call copies only the small slice AsyncTCP
// asks for under a brief cache-mutex hold.
// If cache is empty or transport != WiFi, handler returns 503 Service Unavailable
// and sets a refresh flag for the background task to fetch on next loop.
// =============================================================================

#include "api_dome.h"

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

#include "dome_link.h"
#include "dome_link_transport.h"
#include "logging.h"
#include "robot_state.h"

static const char* TAG = "WebServer";

void registerDomeRoutes(AsyncWebServer& server) {
    // GET /api/dome/layout — retrieve cached dome layout JSON
    // Non-blocking: takes mutex briefly, returns cached bytes or 503 error.
    server.on("/api/dome/layout", HTTP_GET, [](AsyncWebServerRequest* req) {
        // Check transport state (thread-safe read)
        taskENTER_CRITICAL(&robotStateMux);
        DomeLinkTransport transport = robotState.domeActiveTransport;
        taskEXIT_CRITICAL(&robotStateMux);

        // Only serve if WiFi is active transport
        if (transport != DOME_LINK_TRANSPORT_WIFI) {
            PA_LOG_DEBUG(TAG, "GET /api/dome/layout: WiFi transport not active (transport=%u)",
                         (unsigned)transport);
            req->send(503, "application/json", "{\"error\":\"dome layout unavailable (not WiFi)\"}");
            return;
        }

        // Attempt to get cached data
        DomeLayoutCacheStatus status = domeLayoutCacheGetStatus();

        if (!status.has_data) {
            // Cache miss or empty: request a background fetch and return 503
            PA_LOG_DEBUG(TAG, "GET /api/dome/layout: cache miss, requesting refresh");
            domeLayoutCacheRefreshRequested();
            req->send(503, "application/json",
                      "{\"error\":\"dome layout unavailable\",\"retry\":true}");
            return;
        }

        // Cache hit: stream via a chunked filler pinned to this fetch's
        // generation (fetched_at_ms). If a background refresh replaces the
        // cache mid-send, domeLayoutCacheReadChunk() returns 0 (clean early
        // end) rather than splicing bytes from two different fetches.
        const uint32_t fetchedAtMs = status.fetched_at_ms;
        const uint32_t ageMs = millis() - fetchedAtMs;
        AsyncWebServerResponse* response = req->beginResponse(
            "application/json", status.length,
            [fetchedAtMs](uint8_t* buf, size_t maxLen, size_t index) -> size_t {
                return domeLayoutCacheReadChunk(buf, maxLen, index, fetchedAtMs);
            });
        response->addHeader("X-Dome-Layout-Age-Ms", String(ageMs).c_str());
        req->send(response);
        PA_LOG_DEBUG(TAG, "GET /api/dome/layout: serving %u bytes (age=%u ms)", (unsigned)status.length,
                     (unsigned)ageMs);
    });
}
