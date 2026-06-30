// =============================================================================
// src/web/api_dome.cpp
//
// Dome proxy API endpoints
//   GET /api/dome/layout  — fetch dome layout JSON from cache (non-blocking relay)
//
// The dome layout is cached by DomeLinkTask on WiFi transport.
// The web handler retrieves from cache without blocking the AsyncTCP loop.
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

        if (status.has_data) {
            // Cache hit: copy bytes to response buffer and send
            const size_t bufCapacity = 25000;  // Slightly larger than cache cap to be safe
            uint8_t responseBuf[bufCapacity];
            size_t bytesRead = domeLayoutCacheGet(responseBuf, bufCapacity);

            if (bytesRead > 0) {
                uint32_t ageMs = millis() - status.fetched_at_ms;
                // Create response with X-Dome-Layout-Age-Ms header to indicate cache freshness
                AsyncWebServerResponse* response =
                    req->beginResponse(200, "application/json", responseBuf, bytesRead);
                response->addHeader("X-Dome-Layout-Age-Ms", String(ageMs).c_str());
                req->send(response);
                PA_LOG_DEBUG(TAG, "GET /api/dome/layout: served %u bytes (age=%u ms)", (unsigned)bytesRead,
                             (unsigned)ageMs);
                return;
            }
        }

        // Cache miss or empty: request a background fetch and return 503
        PA_LOG_DEBUG(TAG, "GET /api/dome/layout: cache miss, requesting refresh");
        domeLayoutCacheRefreshRequested();
        req->send(503, "application/json",
                  "{\"error\":\"dome layout unavailable\",\"retry\":true}");
    });
}
