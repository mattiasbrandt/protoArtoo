# Issue #71: HTTP/Static/SSE/Upload/Middleware/Connection-Lifecycle Inventory

**Purpose**: Consolidated catalog of every HTTP-surface behavior the current ESPAsyncWebServer/AsyncTCP stack provides. A replacement HTTP server (e.g., PsychicHttp for issue #53) must preserve or deliberately change each behavior.

**Last Updated**: 2026-08-04  
**Tracked By**: issue #71  
**Related**: issue #52 (page-load recovery epic), issue #53 (PsychicHttp evaluation), issue #67 (web server stabilize), issue #68 (response-phase watchdog seam)

---

## 1. REST Handlers (API Surface)

### Current Implementation

**22 API handler files** register HTTP routes via `AsyncWebServer::on()` pattern:

- **Diagnostic/Status APIs**: `/api/status`, `/api/health`, `/api/logs`, `/api/profiler`, `/api/profiler/trace/{start,stop}`, `/api/coredump`, `/api/coredump/status`, `/api/coredump/erase`
- **Safety/Control**: `/api/estop` (POST, emergency-only)
- **Drive/Motion**: `/api/drive` (POST)
- **Dome**: `/api/dome` (POST)
- **Servo**: `/api/servo` (POST)
- **Audio**: `/api/audio/mood-map` (GET/POST), `/api/audio/catalog` (GET)
- **Configuration**: `/api/config`, `/api/identity` (GET/POST), `/api/rc` (GET), `/api/wifi` (GET)
- **Real-Time Events**: `/api/events` (GET, SSE/EventSource)
- **Sequences**: `/api/seq` (GET/POST), `/api/seq/builtins` (GET, split to prevent OOM)
- **System**: `/api/system` management, `/api/sleep`, `/api/wake`, `/api/reboot`, `/api/manual-command`
- **Data-Apply Routes**: `/api/audio/tracks/apply`, `/api/audio/category-range/apply`, `/api/audio/mood-map/apply`, `/api/config/apply`, `/api/rc/map/apply`, `/api/wifi/apply`
- **Upload/OTA**: `/upload/firmware` (POST, multipart), `/upload/filesystem` (POST, multipart)

**Key Traits**:
- All registered via `server.on(path, HTTP_METHOD, [handler])` lambda handlers
- Each handler receives `AsyncWebServerRequest* req` for parsed method/URL/headers
- Handlers call `request->send()` or other response methods to construct and emit response
- No explicit route ordering guarantees; static-file handler runs after named routes
- Estop (`/api/estop`) is a special exception (see admission cap below)

**Location**: `src/web/web_server.cpp:1230-1247` (route registration) and 22 handler files in `src/web/api_*.cpp`

---

## 2. LittleFS Static-File Serving with Gzip Handling

### Current Implementation

**Static file serving** is registered after all named routes:

```cpp
server.serveStatic("/", LittleFS, "/")
    .setDefaultFile("index.html")
    .setCacheControl("no-cache");
```

**Location**: `src/web/web_server.cpp:1250`

**Gzip Support**:
- ESPAsyncWebServer's `serveStatic()` natively serves `.gz` variants when present
- Build time: web assets are gzip-compressed at build via standard ESPAsyncWebServer mechanisms
  - `index.html.gz`, `web_api.js.gz`, etc. are pre-built and included in LittleFS image
  - Uncompressed originals also present for fallback
- Runtime: when browser sends `Accept-Encoding: gzip`, the library automatically serves `.gz` file with `Content-Encoding: gzip` header
- No explicit content-type mapping needed; library infers based on the base filename before `.gz`

**File Mount**:
- LittleFS mounted at `/` (root) on device
- Files served from `/` path (e.g., `/index.html`, `/web_api.js`)
- Mount initialization: `LittleFS.begin(true)` in WiFi startup (`web_server.cpp:1479`)
- **Failure mode**: if LittleFS mount fails, static file serving is skipped; API-only mode remains functional
  - Log: `LittleFS mount failed - API only mode` (line 1483)

**Cache Control Policy**:
- All static assets: `Cache-Control: no-cache` (browser must revalidate, but may use cached copy)
- Rationale: firmware/FS updates may change assets; operator must see new UI on reload
- No ETag/Last-Modified headers configured

**Admission Gating** (Pre-Middleware):
- Static file **open** is gated by vendor patch before middleware sees request
  - `tools/patch_async_sse.py` guards the LittleFS `_fs.open()` call inside `_searchFile()` (ESPAsyncWebServer's `WebHandlers.cpp:167-180`)
  - Checks heap floor: if `largestFreeBlock8Bit() < PA_ADMISSION_MIN_LARGEST_FREE_BLOCK` (9000), aborts file open before allocation
  - This is the "early admission seam" described in ADR 0018
- File read itself happens after admission middleware passes (safe path)

**Fragility Alert for PsychicHttp Adapter**:
- The vendor-patch guard **must** be replicated or the static-file handler must apply the same heap-floor check at a similarly early seam (before allocating file-handle resources)
- If the replacement server does not guard pre-handler file opens, static-file requests can exhaust heap even when middleware would reject them

---

## 3. `/api/events` SSE (Server-Sent Events) Semantics

### Current Implementation

**Handler Registration**:
```cpp
static AsyncEventSource events("/api/events");
// ... in routes setup:
server.addHandler(&events);
server.on("/api/events", HTTP_GET, [](AsyncWebServerRequest* req) { ... });
```

**Location**: `src/web/web_server.cpp:232`, 1128-1143

**Connection Lifecycle**:

1. **Client connects**: Browser or JavaScript creates `new EventSource("/api/events")`
   - HTTP GET request arrives
   - Middleware admits or rejects (see section 5)
   - Handler checks SSE client cap; if full, calls `request->abort()` (rejects pre-upgrade)
   
2. **Upgrade to SSE**: If admitted, handler upgrades to HTTP 101 Switching Protocols (via `AsyncEventSource` library)
   - Connection becomes bi-directional stream (client receives events)
   - Connection is long-lived (stays open until explicitly closed)
   - Counted in `events.count()` (concurrent client cap)

3. **Event Streaming**: While connected, server sends events via `events.send(event, data, id)`
   - Event format: SSE protocol (UTF-8, CRLF-delimited fields)
   - No guarantee of delivery order under burst load
   - Backpressure: if TCP send buffer fills, `send()` blocks the async_tcp task briefly

4. **Connection Close/Reconnect**:
   - Browser auto-reconnects if connection drops (exponential backoff in `data/status_stream.js`)
   - **Known Issue #61** (open, not yet fixed): SSE stuck-reconnect after first-connect reset — if browser drops first connection and immediately reconnects, the server may not properly track the new connection vs. the old one, stalling the reconnect
   - **Known Issue #62** (open, not yet fixed): sustained-refusal repro with 3+ tabs + rapid refresh creates pathological SSE state

5. **Disconnect Detection**: `AsyncEventSource` tracks client disconnects via the underlying TCP connection close
   - `events.count()` decrements when client closes or connection times out
   - No active ping/pong mechanism (pure passive event stream)

**Reconnect Behavior**:
- Not server-enforced; client-side `data/status_stream.js` handles reconnect logic
- Browser EventSource auto-reconnects with exponential backoff by default
- Server sends no `retry:` directives in SSE protocol (uses browser defaults, ~3000ms)
- No server-side timeout for idle connections; they stay open indefinitely

**Client Cap**:
- **Hard cap**: `PA_ADMISSION_MAX_SSE_CLIENTS` (default 3, set in `platformio.ini`)
- Enforced in middleware: `if (sse && events.count() >= kMaxSseClients) request->abort()`
- Rationale: long-lived connections + large heap allocations per client; real operator use is 1-2 browser tabs
- Rejection counter: `s_refusedSseCap` (incremented each time cap is hit, visible in `/api/status`)

**Fragility Alert for PsychicHttp Adapter**:
- SSE reconnect logic (#61, #62) is **not** fixed in this inventory — those issues are explicitly deferred (ADR 0019, page-load-recovery-architecture.md "Open items")
- A replacement server must preserve the same reconnect/disconnect semantics (no breaking changes in timing or protocol)
- Client cap enforcement must happen before connection upgrade, not after (current `abort()` pre-upgrade is correct)
- The cap must survive server restarts (reset to 0 on reboot, not accumulated across sessions)

---

## 4. Upload/Multipart Handling

### Current Implementation

**Two OTA upload routes** handle firmware and filesystem updates:

```cpp
server.on("/upload/firmware", HTTP_POST, [](AsyncWebServerRequest* req) { ... });
server.on("/upload/filesystem", HTTP_POST, [](AsyncWebServerRequest* req) { ... });
```

**Location**: `src/web/api_system.cpp:145-270`

**Multipart Processing**:
- Uses Arduino Update library (standard ESP32 OTA API)
- Request body is standard HTTP multipart form encoding (browser's `<input type="file">`)
  - Form field name: ignored (library reads raw binary stream)
  - File data: raw firmware or filesystem binary in multipart payload
  - Content-Length: **full multipart body** (includes boundary overhead), not just binary payload
- Handler uses `UPDATE_SIZE_UNKNOWN` because `req->contentLength()` includes multipart wrapping
  - Arduino Update library handles size validation internally (compares `Update.size()` against partition size)

**Upload Flow**:

1. **Size Check**: If `contentLength() > PA_OTA_MAX_UPLOAD_SIZE` (4 MB firmware, 1.5 MB filesystem), reject immediately
   - Response: HTTP 413 Payload Too Large, JSON error body
   - No allocation or I/O attempted

2. **Update Begin**: Arduino Update library initializes for the target partition (U_FLASH or U_SPIFFS)
   - Allocates internal buffers, prepares partition for writing

3. **Stream Data**: Handler reads multipart body and feeds binary data to Update library
   - Library validates magic bytes, format, etc.
   - If validation fails, returns error

4. **Finalize**: On success, library closes partition, validates hash/signature
   - Firmware: device reboots automatically after successful finalization
   - Filesystem: device reboots (via `ESP.restart()`) after successful finalization

**Error Handling**:
- Upload state machine: `UPLOAD_STATE_REJECT_OVERSIZE` (pre-upload), `UPLOAD_STATE_REJECT_INTERNAL` (during update)
- Errors logged to serial/syslog via `PA_LOG_ERROR()`
- Response: HTTP 413 or 500 JSON error
- Reboot scheduled on success (automatic, not operator-initiated)

**Connection Behavior During Upload**:
- TCP connection stays open for the full upload duration (may be 10s–60s for large payloads)
- Browser/client must wait for response before considering upload complete
- OTA progress **not** streamed back to client (set-and-forget pattern)
- Client-side progress UI is a UI-only timer, not server-driven

**Fragility Alert for PsychicHttp Adapter**:
- Upload handler must support **streaming** multipart bodies (not buffering the entire body in memory before processing)
- Size limit enforcement **must** happen before any partition writes begin (reject oversized payloads early)
- Multipart boundary parsing must be robust (edge case: binary payload containing the boundary string requires proper MIME codec)
- The handler must integrate with Arduino Update library's synchronous API (no async I/O for partition writes)

---

## 5. Middleware Admission Cap (Request Queue / Inflight Requests)

### Current Implementation

**Purpose**: Prevent unbounded request queue buildup under bursty load. Browsers open up to 6 parallel connections per host; without a cap, concurrent page reloads can fill the request queue and exhaust heap before any response is sent.

**Location**: `src/web/web_server.cpp:235-287` (constants), 1130-1228 (middleware logic)

**Constants**:

| Constant | Default | Config Override |
|----------|---------|---|
| `kMaxInflightRequests` | 6 | `PA_ADMISSION_MAX_INFLIGHT_REQUESTS` |
| `kMaxSseClients` | 3 | `PA_ADMISSION_MAX_SSE_CLIENTS` |
| `kMinLargestFreeBlockForNewWork` | 20000 bytes | `PA_ADMISSION_MIN_LARGEST_FREE_BLOCK` |
| `kMinLargestFreeBlockForDiagnostics` | 10000 bytes | `PA_ADMISSION_MIN_LARGEST_FREE_BLOCK_DIAG` |

**Inflight Request Counting**:

- **Incremented**: when a non-SSE, non-rejected request passes middleware (line 1195)
- **Decremented**: in the `request->onDisconnect()` callback (line 1212-1214), fired when response is fully sent and TCP connection closes
- **Not counted**: SSE connections (they stay open for their whole lifetime; capped separately by `events.count()`)
- **Not counted**: rejected/aborted requests (they release immediately)

**Admission Logic** (middleware, lines 1144-1188):

1. **Estop Exception**: `/api/estop` bypasses all checks, never counted, never rejected
   - Even if inflight cap is full and heap is critical, estop always goes through
   - This is a deliberate safety invariant

2. **SSE Client Cap**: If `url == "/api/events"` and `events.count() >= kMaxSseClients`
   - Call `request->abort()` (closes socket immediately, no response)
   - Increment `s_refusedSseCap`
   - Rationale: closing pre-upgrade is safe; closing after upgrade crashes the EventSource library constructor

3. **Inflight Cap**: If `!sse && s_inflightRequests >= kMaxInflightRequests`
   - Call `request->abort()` (closes socket immediately, no response)
   - Increment `s_refusedInflightCap`
   - Rationale: rejecting with a 503 response still allocates response headers, which can fail under heap pressure

4. **Heap Floor Check**: Measure `largestFreeBlock8Bit()` (largest contiguous 8-bit heap block)
   - **Diagnostic requests** (`/api/status`, `/api/profiler`, `/api/coredump`, `/api/events`):
     - Floor: `kMinLargestFreeBlockForDiagnostics` (10 KB, looser)
     - Rationale: operators need to see device state during rejection window
   - **Non-diagnostic requests** (everything else):
     - Floor: `kMinLargestFreeBlockForNewWork` (20 KB)
     - Rationale: normal work (response construction, body copy) needs breathing room
   - If `largestBlock < floor`: call `request->abort()`, increment `s_refusedHeapFloor` or `s_refusedHeapFloorDiag`

**Rejection Counters** (read-only visibility, never reset):

| Counter | Meaning |
|---------|---------|
| `s_inflightRequests` | Current in-flight non-SSE request count (0–6) |
| `s_peakInflightRequests` | Highest in-flight count seen since boot |
| `s_refusedInflightCap` | Total requests rejected due to cap (never resets) |
| `s_refusedSseCap` | Total SSE connections rejected due to cap (never resets) |
| `s_refusedHeapFloor` | Total non-diagnostic requests rejected due to heap floor (never resets) |
| `s_refusedHeapFloorDiag` | Total diagnostic requests rejected due to heap floor (never resets) |

All counters visible in `/api/status` JSON response and intended for operator visibility during/after load tests.

**Early Admission Seam Gap** (ADR 0018):

- Static file **open** (LittleFS) is gated by heap floor in `tools/patch_async_sse.py` **before** middleware runs
- Main middleware checks inflight cap and SSE cap **after** static file is already open
- **Gap**: if inflight cap is full but heap is still healthy, a static file can be opened, then immediately rejected by middleware, wasting the file handle
- **Remediation**: small future follow-up (not blocking), would need cross-translation-unit accessor for `s_inflightRequests` and SSE cap in the vendor patch

**Fragility Alert for PsychicHttp Adapter**:
- The inflight request **must** count only requests being actively served (not queued/waiting)
- Counting must decrement **only** when response is fully sent and TCP connection is closed (not just when handler returns)
- SSE connections must be capped separately from inflight requests (they are long-lived, not transient)
- Heap floor checks must sample the **largest contiguous free block** (not total free), because allocation fragmentation matters
- Rejection **must** happen via socket close/abort, not HTTP 503 response, to avoid allocating response headers under pressure
- All rejection counters must be exposed via an always-on status endpoint (for diagnostics/monitoring)

---

## 6. Connection-Lifecycle/Teardown Behavior

### Current Implementation

**Request Lifecycle**:

1. **TCP Accept**: Raw TCP connection arrives at port 80
   - Pre-checked by `tools/patch_async_sse.py` vendor patch: if `largestFreeBlock8Bit() < 9000`, connection is rejected at the socket level
   - Log (always-on): `g_asyncTcpAcceptRejectHeap` counter (tracks rejected accepts due to heap)
   - This is the outermost guard, before HTTP parsing

2. **HTTP Parse**: AsyncTCP task parses HTTP request line (method, URL, version) and headers
   - URL/method become readable in `ESPAsyncWebServer/WebRequest.cpp:_parseReqHead()`
   - Handler selection immediately follows

3. **Handler Selection** (`_attachHandler()`):
   - For static files: `_searchFile()` opens the file from LittleFS (guarded by heap floor check)
   - For named routes: checks if any handler's `canHandle()` returns true
   - Middleware chain not yet invoked

4. **Middleware Chain** (lines 1130-1228):
   - Runs after handler selection but before handler body
   - Can reject via `request->abort()` or call `next()` to proceed
   - Admission logic (see section 5) lives here
   - Non-SSE request increments `s_inflightRequests` and registers `onDisconnect` callback

5. **Handler Execution** (request body):
   - Handler calls `request->send()` or builds response via `AsyncWebServerResponse` API
   - Response construction allocates headers, body buffer
   - Handler-specific logic (read state, format JSON, etc.)

6. **Response Send**:
   - `request->send()` triggers AsyncTCP to write response to socket
   - Write is non-blocking; library queues data and signals completion via `onDisconnect` or timeout

7. **Disconnect**:
   - When response is fully sent **and** TCP connection closes (or times out)
   - `onDisconnect` callback fires (registered in line 1207-1220)
   - For non-SSE requests: decrements `s_inflightRequests`
   - For profiler-enabled builds: records disconnect time in request trace ring

**Disconnect Callback Detail** (lines 1207-1220):

```cpp
request->onDisconnect([...captured state...]() {
    if (s_inflightRequests > 0) {
        s_inflightRequests--;
    }
    #if PA_HEAP_PROFILE
    if (traced) {
        s_requestTrace[traceIdx].disconnectMs = millis();
    }
    #endif
});
```

- Runs after response fully sent or on TCP timeout
- Safe to call across different task boundaries (async_tcp task runs the callback)
- **Critical**: must fire exactly once per request, never skipped or double-fired

**Timeout Behavior**:

- ESPAsyncWebServer default TCP timeout: ~15 seconds (configurable in vendor library)
- If handler never calls `send()`, connection times out and `onDisconnect` fires
- If response is sent but client doesn't read it, connection times out after read buffer fills

**SSE Disconnect**:

- SSE connections don't use the inflight request counter (not counted in cap)
- Disconnect is detected by `events.count()` decrementing when client closes or times out
- No explicit `onDisconnect` callback for SSE; the EventSource library owns that lifecycle

**Error Conditions**:

| Condition | Action |
|-----------|--------|
| Admitted request fails to construct response | Handler returns error; `send()` is never called; timeout fires after ~15s; `onDisconnect` decrements counter |
| TCP buffer fills during response send | AsyncTCP blocks the async_tcp task briefly, then resumes; response eventually completes |
| TCP connection reset by client mid-response | AsyncTCP detects reset; `onDisconnect` fires; counter is decremented (cleanup is safe) |
| Estop arrives while another request is executing | Estop bypasses admission entirely; does not wait for other requests to finish (interrupt-like) |

**Fragility Alert for PsychicHttp Adapter**:

- The replacement server **must** fire a disconnect callback exactly once per request, after response is fully sent and TCP connection is closed or timed out
- The callback **must** be safe to fire from any task context (async I/O libraries may fire callbacks from different threads)
- Counting `s_inflightRequests` **must** decrement in this callback, not when handler returns
- If the replacement server has a different TCP timeout, it should be set to match ESPAsyncWebServer's ~15s default (or configurable)
- Estop must remain a hard bypass of admission checks (no queueing, no refusal)

---

## Summary of Key Invariants for Replacement Server

| Invariant | Why | Where to Check |
|-----------|-----|---|
| 6 concurrent non-SSE requests max | Browsers open 6 connections per host; unbounded causes heap exhaustion | ADR 0017, middleware section 5 |
| 3 concurrent SSE clients max | Long-lived connections; each allocates sizable buffer | ADR 0017, SSE section 3 |
| Heap floor checks before allocating response | Response construction can fail under heap pressure; abort is safer than 503 | ADR 0018, sections 2 & 5 |
| Diagnostic requests get looser floor (10 KB vs 20 KB) | Operators need status visibility during rejection window | Middleware section 5 |
| Estop bypasses all admission checks | Safety critical; must always work | Middleware section 5 |
| Disconnect callback decrements counter after response sent and TCP closed | Counting drift causes cap to fail (requests pile up) | Section 6 |
| Static-file opens gated by heap floor **before** middleware | File open is expensive; must reject early if heap is critical | ADR 0018, section 2 |
| SSE rejection happens via `abort()` before upgrade, not after | Closing post-upgrade crashes the EventSource library | Middleware section 5 |
| No buffering of entire multipart body in memory | Upload can be 4 MB; streaming is required | Section 4 |
| Cache-Control: no-cache for all static assets | Firmware updates change assets; browser must revalidate | Section 2 |

---

## Known Fragility & Open Issues

**Issue #61** (SSE stuck-reconnect after first-connect reset): Open, explicitly deferred (not fixed by page-load-recovery rollout). See ADR 0019 "Open items not resolved."

**Issue #62** (rapid-refresh + 3-tab sustained-refusal): Open, explicitly deferred. Related to SSE connection state under burst conditions.

**Issue #68** (response-phase watchdog seam as infeasible): Closed as not-planned. Was exploring a 6-callback-slot exhaustion issue in AsyncWebServerRequest; determined to defer to a broader architectural review (#53).

**Issue #67** (web server stabilize): Closed. Fixed OOM stalls via bounded write retries and zero-read handling:
- `fix(web): bound write_send_buffs OOM retries to stop the #67 hang` (commit f0bd6a6)
- `fix(web): stop write_send_buffs treating a premature zero-read as EOF` (commit 63d1921)
- Root cause: AsyncTCP buffer management under sustained high throughput; fixed in patch, not source stack replacement

---

## References

- **ADR 0016**: Busy Recovery Page wire contract (one static buffer, HTTP 503 on refusal)
- **ADR 0017**: Page Load Memory Recovery acceptance envelope (heap floor thresholds, cooldown windows)
- **ADR 0018**: Early admission seam feasibility (static-file guard is already proven)
- **ADR 0019**: Rollout decisions (6-request slot, two deadline categories, cross-page generalization gates)
- **docs/page-load-recovery-architecture.md**: Full rollout handoff and open items
- **src/web/web_server.cpp**: Middleware (lines 1130–1228), admission constants (lines 235–287), static file serving (line 1250)
- **src/web/api_system.cpp**: Upload handlers (lines 145–270)
- **tools/patch_async_sse.py**: Vendor patches for early admission seam (static-file guard, AsyncTCP accept guard)
- **tools/tools/serial_monitor.py**: Serial capture for debugging web server crashes
