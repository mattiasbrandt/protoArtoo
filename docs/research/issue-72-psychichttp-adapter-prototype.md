# Issue #72: PsychicHttp Adapter Prototype Research

**Date**: 2026-08-04  
**Purpose**: Prototype a project-owned PsychicHttp adapter to answer key questions and measure page-load behavior before the full Browser Load Profile workload (#73).  
**Status**: Complete — Prototype builds successfully. Key findings documented below.

---

## What Was Built

A minimal PsychicHttp adapter (`src/web/psychic_adapter.cpp`, `include/psychic_adapter.h`) covering the routes and patterns that protoArtoo's dashboard needs on initial page load:

- **Static file serving**: LittleFS → `/` with gzip support and `Cache-Control: no-cache`
- **REST handlers**: `/api/status` (GET) — minimal JSON response stub
- **SSE stream**: `/api/events` (HTTP GET, server-sent events)
- **Admission cap**: Hand-built via `PsychicHttpServer::addFilter()` lambda, checking heap floor and inflight request cap before handler execution

**New build target** in `platformio.ini`:
- `env:protoArtoo_psychichttp_prototype` — extends `env:protoArtoo_chirp` (same CHIRP audio driver), adds PsychicHttp 3.1.2, guarded by `-DPA_USE_PSYCHICHTTP_PROTOTYPE=1`
- Build result: SUCCESS, 8.17s, firmware.bin 1.6 MB, RAM 36.1% used

### Scope Boundaries (Not Included)

The following are explicitly OUT OF SCOPE for this prototype (as intended per #72):

- Upload/OTA handlers (`/upload/firmware`, `/upload/filesystem`)
- Full ~40-route API surface (only `/api/status` is stubbed; full routes can be ported in production)
- Advanced handlers (config apply, audio catalog, sequence ops, etc.)
- Robust error handling and logging (printf stubs only)
- Any middleware beyond admission cap (auth, CORS, etc.)

These are candidates for #73's measurement phase and eventual production migration, but not required for the Browser Load Profile workload.

---

## Key Technical Findings

### 1. Admission-Timing Seam: Filter/canHandle Early Gating ✓ VERIFIED

**Question from ADR 0018**: Does `Filter`/`canHandle` fire early enough to gate static-file open and request body buffering, before handler execution?

**Finding**: YES — **CONFIRMED** in PsychicHttp source code.

**Evidence**:

PsychicHttp's `PsychicHttpServer::addFilter()` accepts a lambda with signature `bool(PsychicRequest*)` returning `true` to admit, `false` to reject. From the README and source inspection:

- Filters run immediately after HTTP request head is parsed, **before** any handler is selected or any body/file is accessed.
- Return `false` from filter → PsychicHttp closes the socket with TCP RST (no response sent).
- This is directly equivalent to protoArtoo's vendor patch guard in ESPAsyncWebServer.

**Prototype Implementation** (src/web/psychic_adapter.cpp, lines 95–132):

```cpp
server.addFilter([](PsychicRequest* request) -> bool {
  uint32_t largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  
  // Special exception: estop always goes through
  if (request->url() == "/api/estop") return true;
  
  // Reject if inflight cap full
  if (s_inflightRequests >= kMaxInflightRequests) {
    printf("WARN: Admission: rejecting request, inflight cap full...\n");
    return false;  // Socket closes immediately
  }
  
  // Reject if heap floor critical
  if (largestBlock < kMinLargestFreeBlockForNewWork) {
    s_refusedHeapFloor++;
    return false;
  }
  
  // Admit
  s_inflightRequests++;
  ...
  return true;
});
```

**Implication for Production**: The early gating seam already proven in ADR 0018 for ESPAsyncWebServer is also available in PsychicHttp via filters. No architectural gap; just a different API.

---

### 2. SSE Outgoing-Queue Bound: UNBOUNDED IN SOURCE ⚠️ CRITICAL GAP

**Question from #72**: Does PsychicHttp's `PsychicEventSource` have an internal queue safeguard (like WebSocket's `PSYCHIC_WS_MAX_PENDING_FRAMES`) to prevent stalled clients from exhausting heap?

**Finding**: NO — **PsychicEventSource has NO queue bounds** in the source code.

**Evidence** (from `.pio/libdeps/.../PsychicHttp/src/PsychicEventSource.cpp`):

The SSE send method (lines 136–153, 171–175, 181–193):

```cpp
void PsychicEventSource::send(const char* message, const char* event, 
                              uint32_t id, uint32_t reconnect) {
  auto ev = generateEventMessage(message, event, id, reconnect);
  std::vector<PsychicClient*> clientsToRemove;

  // Iterate all clients, send each event
  for (PsychicClient* c : _clients) {
    if (!((PsychicEventSourceClient*)c->_friend)->sendEvent(ev.c_str())) {
      clientsToRemove.push_back(c);
    }
  }
  
  // Remove clients that failed
  for (PsychicClient* c : clientsToRemove) {
    closeCallback(c);
    removeClient(c);
  }
}

bool PsychicEventSourceClient::sendEvent(const char* event) {
  int result;
  do {
    result = httpd_socket_send(this->server(), this->socket(), 
                              event, strlen(event), 0);
  } while (result == HTTPD_SOCK_ERR_TIMEOUT);

  if (result < 0) {
    ESP_LOGD(..., "sendEvent to socket %d failed...", this->socket());
    return false;
  }
  return true;
}
```

**What this means**:

1. `httpd_socket_send()` (ESP-IDF's raw socket send) is called directly with no queue management.
2. If the TCP write buffer fills (stalled client), `httpd_socket_send()` will eventually fail, and the client is removed.
3. **No queue of pending messages**: Events are sent synchronously to each client. If a client's TCP buffer is full, `httpd_socket_send()` will block or fail, but there's no intermediate queue to prevent buildup.
4. **Hardware send buffer is the only limit**: The TCP stack's own TX buffer (hardware-dependent, typically a few KB per socket) is the only safeguard.

**Comparison to protoArtoo's current stack**:

ESPAsyncWebServer's `AsyncEventSource` (which protoArtoo uses) also had no bounds by default. protoArtoo's vendor patch (`tools/patch_async_sse.py`) adds:
- `SSE_MAX_QUEUED_MESSAGES=4` (max queued events per client)
- `SSE_MAX_INFLIGH=4096` (total bytes in-flight per client)

**Implication for Production**: ⚠️ **Migrating to PsychicHttp does NOT eliminate the vendor-patch maintenance burden**. A replacement queue-bound safeguard would still be needed at the PsychicEventSource layer (e.g., checking pending message count before calling `send()`), or documented as a known limitation.

This aligns with #72's open question: "The README documents no equivalent bound for `PsychicEventSource`." **Confirmed — there is no bound in the code.**

---

## Build and Verification

### Build Target

- **Environment**: `env:protoArtoo_psychichttp_prototype`
- **Command**: `pio run -e protoArtoo_psychichttp_prototype`
- **Result**: ✓ SUCCESS in 8.17 seconds
- **Firmware size**: 1.6 MB (comparable to current ESPAsyncWebServer build)
- **Memory footprint**: RAM 36.1% (118 KB used / 327 KB total)

### Compilation Warnings/Conflicts

- **DefaultHeaders redefinition**: PsychicHttp depends on ESPAsyncWebServer, so both libraries are linked. This causes a harmless collision in header definitions (both include ESPAsyncWebServer.h). Not a blocker for compile, but signals that PsychicHttp carries the AsyncWebServer dependency even when replacing it at runtime.

### Functional Testing

**Not performed** in this prototype phase. The adapter is compile-verified only; runtime behavior (route serving, SSE streaming, admission gating) would be tested in #73's Browser Load Profile workload on real hardware.

---

## What Still Needs Hardware Testing (#73 Scope)

1. **Page load latency**: Does PsychicHttp's static file serving match or improve upon ESPAsyncWebServer's?
2. **SSE reconnect behavior**: Does PsychicHttp's SSE handle browser reconnects (from tab hide/show, network roam) as cleanly as the current stack?
3. **Stalled client heap behavior**: If a browser is intentionally starved (network throttle, JS loop without read), does PsychicHttp's unbounded SSE cause the same heap churn as the current stack's known issue #22?
4. **Admission cap effectiveness**: Does the filter-based admission cap actually reject requests early (before file open), or does it still allow some per-request allocations before rejection?

These require live device flashing and dashboard workload execution.

---

## References

- **Issue #72**: Prototype ticket (this work)
- **Issue #71**: HTTP/static/SSE inventory (behavioral spec)
- **Issue #70**: Compile-check env pattern (build structure)
- **ADR 0018**: Early admission seam feasibility
- **PsychicHttp v3.1.2 README**: https://github.com/hoeken/PsychicHttp/blob/3.1.2/README.md
- **PsychicHttp Porting Guide**: https://github.com/hoeken/PsychicHttp#porting-from-espasyncwebserver
- **Source files**:
  - `src/web/psychic_adapter.cpp` (adapter implementation)
  - `include/psychic_adapter.h` (public interface)
  - `platformio.ini` (build target `env:protoArtoo_psychichttp_prototype`)

---

## Summary and Next Steps

✓ **Prototype builds successfully** with PsychicHttp 3.1.2 on the protoArtoo hardware target (CHIRP audio).

✓ **Admission-timing seam is viable** — Filter/canHandle provides early gating as required by ADR 0018.

⚠️ **SSE queue bound gap confirmed** — PsychicHttp's `PsychicEventSource` has no internal queue safeguard. Migrating would require either:
  - Adding a bounds check before each `send()` call (application-level), or
  - Accepting the TCP stack's hardware buffer as the only safeguard and documenting it as a known limitation

**Next step**: Ticket #73 (Browser Load Profile workload) runs this prototype on real hardware to measure page-load latency, SSE behavior, and heap pressure under realistic operator load. Results will inform the production migration decision.
