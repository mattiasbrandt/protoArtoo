// =============================================================================
// src/web/psychic_adapter.cpp
//
// PsychicHttp adapter prototype for issue #72.
// Ports a representative slice of protoArtoo's HTTP surface onto PsychicHttp
// 3.1.2, matching #52's Browser Load Profile needs (page load, refresh,
// /api/events open/reconnect/hide/show/close). NOT production code and NOT
// a full ~40-route port -- see docs/research/issue-72-psychichttp-adapter-prototype.md
// for exact scope boundaries.
//
// This file is inert in every build except env:protoArtoo_psychichttp_prototype
// (guarded by PA_USE_PSYCHICHTTP_PROTOTYPE, set only in that env's build_flags).
// =============================================================================

#ifdef PA_USE_PSYCHICHTTP_PROTOTYPE

#include "psychic_adapter.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <PsychicHttp.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "web_server.h"  // buildStatusJson() -- same function the current
                          // ESPAsyncWebServer /api/status handler and the
                          // real SSE "status" event both call. Reused here
                          // as-is so this prototype's status payload is
                          // genuinely identical to production, not a
                          // hand-rolled approximation.

static PsychicHttpServer server;
static PsychicEventSource* eventSource = nullptr;
static TaskHandle_t s_broadcastTaskHandle = nullptr;

// =============================================================================
// Admission accounting
//
// Thresholds mirror the current stack's real constants (PA_ADMISSION_MIN_
// LARGEST_FREE_BLOCK=9000, 6-request inflight cap; see web_server.cpp /
// platformio.ini [flags_base]). Kept as separate local constants rather than
// including web_server.cpp's macros directly, since this prototype does not
// wire into the production admission path at all -- it exists to test
// PsychicHttp's *seam* (Filter + global middleware), not to replace or share
// state with the current stack's admission implementation.
// =============================================================================

static uint32_t s_inflightRequests = 0;
static uint32_t s_peakInflightRequests = 0;
static uint32_t s_refusedHeapFloor = 0;
static uint32_t s_refusedInflightCap = 0;

static const uint32_t kMinLargestFreeBlockForNewWork = 9000;  // bytes
static const uint32_t kMaxInflightRequests = 6;

// =============================================================================
// Admission filter -- runs BEFORE endpoint matching, static-file open, and
// request body access.
//
// Verified against the actual vendored source (not just the README), in
// PsychicHttpServer::requestHandler() (.pio/libdeps/.../PsychicHttpServer.cpp):
//   1. server->_rewriteRequest(&request)
//   2. server->_filter(&request)          <- THIS function, runs first
//      -> on reject: return request.response()->send(400)   (NOT a bare
//         TCP RST -- a real 400 response is constructed and sent; this is a
//         real behavioral difference from the current stack's abort()-based
//         rejection and worth weighing in #73's measurement, since building
//         a response costs more than dropping a connection)
//   3. middleware chain wraps _process() -- endpoint matching, static-file
//      open, and request->body() access all happen inside _process(), which
//      only runs if the filter admitted the request.
// This structurally confirms the ADR 0018 requirement: the admission check
// gates real work, it does not merely run in parallel with it.
// =============================================================================

static bool admissionFilter(PsychicRequest* request) {
  const char* path = request->pathCStr();

  // Safety-critical: never gate the estop path, matching the current stack's
  // admission bypass for latching estop (web_server.cpp).
  if (strcmp(path, "/api/estop") == 0) {
    return true;
  }

  if (s_inflightRequests >= kMaxInflightRequests) {
    s_refusedInflightCap++;
    printf("WARN: psychic_adapter: rejecting %s, inflight cap full (%" PRIu32 "/%" PRIu32 ")\n", path,
           s_inflightRequests, kMaxInflightRequests);
    return false;
  }

  uint32_t largestBlock = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  if (largestBlock < kMinLargestFreeBlockForNewWork) {
    s_refusedHeapFloor++;
    printf("WARN: psychic_adapter: rejecting %s, heap floor critical (largest=%" PRIu32 ", min=%" PRIu32 ")\n",
           path, largestBlock, kMinLargestFreeBlockForNewWork);
    return false;
  }

  s_inflightRequests++;
  if (s_inflightRequests > s_peakInflightRequests) {
    s_peakInflightRequests = s_inflightRequests;
  }
  return true;
}

// =============================================================================
// Release -- global middleware wrapping next() around endpoint dispatch.
//
// Verified against the same requestHandler() source: the middleware chain's
// runChain() takes a continuation that calls _process(); code after next()
// returns therefore runs after the handler has fully executed (including its
// response send). This pairs correctly with admissionFilter()'s increment --
// every filter-admitted request passes through this middleware exactly once
// (filter runs first, unconditionally, before the middleware chain) -- so the
// counter tracks requests genuinely in flight, not merely "ever admitted".
//
// The prior version of this prototype incremented in the filter and never
// decremented anywhere, which made the cap a one-shot "first 6 requests ever"
// lockout instead of an inflight cap. Fixed here.
// =============================================================================

static esp_err_t releaseMiddleware(PsychicRequest* request, PsychicResponse* response, PsychicMiddlewareNext next) {
  (void)request;
  (void)response;
  esp_err_t result = next();
  if (s_inflightRequests > 0) {
    s_inflightRequests--;
  }
  return result;
}

// =============================================================================
// /api/status -- real port, not a stub.
//
// Calls the actual buildStatusJson() from web_server.cpp (declared in
// web_server.h), the same function the current ESPAsyncWebServer /api/status
// handler and the SSE "status" event both call. No status-building logic is
// duplicated here.
// =============================================================================

static esp_err_t handleStatus(PsychicRequest* request, PsychicResponse* response) {
  (void)request;
  static char body[3072];  // matches web_server.cpp's static buffer size
  if (!buildStatusJson(body, sizeof(body))) {
    printf("WARN: psychic_adapter: status payload overflowed; returning fallback payload\n");
  }
  return response->send(200, "application/json", body);
}

// =============================================================================
// SSE periodic status broadcast.
//
// The real eventStreamTask (web_server.cpp) also sends "rc" and "log" events
// and is driven by an on-demand broadcast-request flag rather than a fixed
// timer. This prototype intentionally sends only the "status" event, on a
// fixed 1s interval, gated on eventSource->count() > 0 (matching the real
// task's gate) -- enough to exercise a live SSE stream with real payloads for
// #73's stalled-client test, not a full port of the event-stream task's
// scheduling logic. Documented as a scope boundary in the findings doc.
// =============================================================================

static void sseBroadcastTask(void* /*param*/) {
  static char body[3072];
  for (;;) {
    if (eventSource != nullptr && eventSource->count() > 0) {
      if (buildStatusJson(body, sizeof(body))) {
        eventSource->send(body, "status", millis());
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

// =============================================================================
// Initialize and start the PsychicHttp server.
// =============================================================================

void initPsychicHttpServer() {
  printf("INFO: psychic_adapter: initializing PsychicHttp prototype server\n");

  server.config.max_open_sockets = 10;
  server.config.stack_size = 8192;

  server.addFilter(admissionFilter);
  server.addMiddleware(releaseMiddleware);

  if (!LittleFS.begin(true)) {
    printf("ERROR: psychic_adapter: LittleFS mount failed\n");
  }

  server.serveStatic("/", LittleFS, "/")->setDefaultFile("index.html")->setCacheControl("no-cache");

  server.on("/api/status", HTTP_GET, handleStatus);

  eventSource = new PsychicEventSource();
  eventSource->onOpen([](PsychicEventSourceClient* client) {
    printf("INFO: psychic_adapter: SSE client #%u connected\n", client->socket());
    static char body[3072];
    if (buildStatusJson(body, sizeof(body))) {
      client->send(body, "status", millis());
    }
  });
  eventSource->onClose([](PsychicEventSourceClient* client) {
    printf("INFO: psychic_adapter: SSE client #%u disconnected\n", client->socket());
  });
  server.on("/api/events", eventSource);

  if (s_broadcastTaskHandle == nullptr) {
    xTaskCreatePinnedToCore(sseBroadcastTask, "PsychicSSE", 4096, nullptr, 1, &s_broadcastTaskHandle, 0);
  }

  // server.on()/serveStatic()/addFilter()/addMiddleware() above only
  // register configuration -- nothing actually binds or listens until
  // begin() (== start(), which calls httpd_start()) is called. The first
  // draft of this prototype never called this at all ("starts automatically
  // on construction, or via begin()" -- an unverified guess in a comment)
  // and the rewrite dropped the call entirely instead of resolving that
  // uncertainty, so the server logged "started" and never actually accepted
  // a single connection. Fixed here, with the return value actually checked.
  esp_err_t err = server.begin();
  if (err != ESP_OK) {
    printf("ERROR: psychic_adapter: server.begin() failed: %s\n", esp_err_to_name(err));
    return;
  }

  printf("INFO: psychic_adapter: PsychicHttp server listening on port 80\n");
}

#endif  // PA_USE_PSYCHICHTTP_PROTOTYPE
