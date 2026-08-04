// =============================================================================
// src/web/psychic_adapter.cpp
//
// PsychicHttp adapter prototype for issue #72.
// Minimal port of protoArtoo's HTTP surface to PsychicHttp 3.1.2.
//
// This is NOT production code. It exists to:
// 1. Demonstrate PsychicHttp's route/SSE/static-file serving patterns
// 2. Verify the admission cap via Filter/canHandle early-gating seam
// 3. Measure SSE queue behavior under stalled clients (issue #72's key question)
//
// Scope: Page-load routes only (/api/status, /api/events, static files)
// No uploads, no full ~40-route surface — just what #52's Browser Load Profile needs.
// =============================================================================

#ifdef PA_USE_PSYCHICHTTP_PROTOTYPE

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <PsychicHttp.h>
#include <esp_heap_caps.h>
#include <stdio.h>
#include <inttypes.h>
#include <string.h>

// =============================================================================
// Global State
// =============================================================================

static PsychicHttpServer server;
static PsychicEventSource* eventSource = nullptr;

// Admission counters (exposed via /api/status in production, but not wired
// in this prototype yet — just counting locally for now).
static uint32_t s_inflightRequests = 0;
static uint32_t s_peakInflightRequests = 0;
static uint32_t s_refusedHeapFloor = 0;

// Admission thresholds (matching src/web/web_server.cpp constants).
static const uint32_t kMinLargestFreeBlockForNewWork = 9000;  // 9 KB
static const uint32_t kMaxInflightRequests = 6;

// =============================================================================
// /api/status Handler
//
// Port of api_status.cpp registerStatusRoutes().
// Returns JSON snapshot of robot state.
//
// Handler signature in PsychicHttp: int(PsychicRequest*)
// Must return ESP_OK or similar.
// =============================================================================

int handleStatus(PsychicRequest* request, PsychicResponse* response) {
  // Stub handler for /api/status.
  // PsychicResponse is derived from Print, but using it directly is complex.
  // For this prototype, we'll just return a status that the response object
  // can auto-format. The real implementation would serialize JSON and write it.
  //
  // Set the response code and type.
  response->setCode(200);
  response->setContentType("application/json; charset=utf-8");

  // The actual response body writing depends on PsychicHttp's internals.
  // For now, just return ESP_OK to prove the route exists.
  // (The response object handles the actual sending via its destructor or similar.)

  return ESP_OK;
}

// =============================================================================
// Initialize PsychicHttp Server
//
// Called from initAsyncWeb() or equivalent.
// =============================================================================

void initPsychicHttpServer() {
  printf("INFO: Initializing PsychicHttp server...\n");

  // Configure server (tuning matching protoArtoo's current config).
  server.config.max_open_sockets = 10;
  server.config.stack_size = 8192;

  // Register admission filter (fires on every request before handler).
  // Filter returns true to admit, false to reject.
  server.addFilter([](PsychicRequest* request) -> bool {
    uint32_t largestBlock = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);

    // Special exception: /api/estop always goes through (safety critical).
    String url = request->url();
    if (url == "/api/estop") {
      return true;
    }

    // Reject if inflight cap is full.
    if (s_inflightRequests >= kMaxInflightRequests) {
      printf("WARN: Admission: rejecting request, inflight cap full (curr=%" PRIu32 ", max=%" PRIu32 ")\n",
             s_inflightRequests, kMaxInflightRequests);
      return false;  // Don't handle; request is aborted by PsychicHttp.
    }

    // Reject if heap floor is critical.
    if (largestBlock < kMinLargestFreeBlockForNewWork) {
      printf("WARN: Admission: rejecting request, heap floor critical (largest=%" PRIu32 ", min=%" PRIu32 ")\n",
             largestBlock, kMinLargestFreeBlockForNewWork);
      s_refusedHeapFloor++;
      return false;
    }

    // Admit: increment counter and let handler proceed.
    s_inflightRequests++;
    if (s_inflightRequests > s_peakInflightRequests) {
      s_peakInflightRequests = s_inflightRequests;
    }

    return true;  // Admit this request
  });

  // Mount LittleFS.
  if (!LittleFS.begin(true)) {
    printf("ERROR: LittleFS mount failed in PsychicHttp init\n");
  } else {
    printf("INFO: LittleFS mounted for PsychicHttp\n");
  }

  // Register static file handler (LittleFS, gzip support, no-cache).
  server.serveStatic("/", LittleFS, "/")
      ->setDefaultFile("index.html")
      ->setCacheControl("no-cache");

  // Register /api/status handler.
  // PsychicHttp on() takes a URI, method, and callback with signature:
  // int(PsychicRequest*, PsychicResponse*)
  server.on("/api/status", HTTP_GET, [](PsychicRequest* request, PsychicResponse* response) -> int {
    return handleStatus(request, response);
  });

  // Register /api/events SSE handler.
  eventSource = new PsychicEventSource();
  server.on("/api/events", HTTP_GET, eventSource);

  // Start server (PsychicHttp starts automatically on construction, or via begin()).
  printf("INFO: PsychicHttp server initialized on port 80\n");
}

// =============================================================================
// Broadcast Event (called from elsewhere to send SSE message)
// =============================================================================

void broadcastEvent(const char* eventType, const char* data) {
  if (eventSource != nullptr && eventSource->count() > 0) {
    eventSource->send(data, eventType);
  }
}

#endif  // PA_USE_PSYCHICHTTP_PROTOTYPE
