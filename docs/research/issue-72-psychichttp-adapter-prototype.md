# Issue #72: PsychicHttp Adapter Prototype

**Date**: 2026-08-04
**Purpose**: Prototype a project-owned PsychicHttp adapter to answer key questions and measure page-load behavior before the full Browser Load Profile workload (#73).
**Status**: Complete — real handlers wired and running, not stubs. See "Revision history" at the bottom; this document was corrected after an operator review found the first draft's admission cap didn't work and `/api/status` never sent a response.

---

## What Was Built

A PsychicHttp adapter (`src/web/psychic_adapter.cpp`, `include/psychic_adapter.h`) covering the routes protoArtoo's dashboard needs on initial page load, wired to actually run:

- **Static file serving**: LittleFS → `/` with gzip support (PsychicHttp auto-detects `.gz` siblings) and `Cache-Control: no-cache`, matching #71's inventory of current behavior.
- **`/api/status`**: real port. Calls `buildStatusJson()` (declared in `web_server.h`, defined in `src/web/web_server.cpp`) — the *same* function the current ESPAsyncWebServer `/api/status` handler and the real SSE `"status"` event both call. No status-building logic is duplicated or approximated.
- **`/api/events` SSE**: `PsychicEventSource` with `onOpen`/`onClose` logging, an initial `"status"` event sent to a client on connect, and a periodic broadcast task (`sseBroadcastTask`, 1s interval, gated on `eventSource->count() > 0`) sending real `buildStatusJson()` payloads under the `"status"` event name — matching the real stack's `events.send(s_sseStatusBody, "status", nowMs)` call in `web_server.cpp`.
- **Admission cap**: a global `Filter` (rejects before endpoint dispatch) paired with a global `addMiddleware` (releases after the handler completes) — see "Admission-Timing Seam" below for why this pairing matters and how it was verified.
- **Wiring**: `src/main.cpp`'s `webServerInit()` call site now branches on `PA_USE_PSYCHICHTTP_PROTOTYPE` — calls `initPsychicHttpServer()` instead of `webServerInit()`, not alongside it, matching the "one build at a time" evaluation design (#53's map explicitly rejected running both servers simultaneously).

**Build target** in `platformio.ini`: `env:protoArtoo_psychichttp_prototype` — extends `env:protoArtoo_chirp` (same CHIRP audio driver, per operator direction on #70), adds PsychicHttp 3.1.2, guarded by `-DPA_USE_PSYCHICHTTP_PROTOTYPE=1`.

**Build result**: SUCCESS, 20.99s. Flash 1,462,472 bytes (85.8%), RAM 106,136 bytes (32.4%). These numbers are now a meaningful PsychicHttp-only comparison point: because `main.cpp` genuinely stops calling `webServerInit()` in this build (rather than linking both stacks and running neither cleanly), the linker's `--gc-sections` strips the now-unreachable ESPAsyncWebServer registration code — flash dropped from the first draft's 93.1% to 85.8% purely from that fix, with no functional code removed.

### Scope Boundaries (Not Included)

Explicitly out of scope for this prototype, matching #52's Browser Load Profile (page load, refresh, SSE open/reconnect/hide/show/close — not uploads):

- Upload/OTA handlers (`/upload/firmware`, `/upload/filesystem`).
- The full ~40-route API surface — only `/api/status` is ported; other routes (`/api/wifi`, `/api/health`, `/api/logs`, `/api/serial`, config/audio/sequence endpoints) are not.
- The `"rc"` and `"log"` SSE events the real `eventStreamTask` also sends — only `"status"` is ported. The real task is also driven by an on-demand broadcast-request flag rather than a fixed timer; this prototype uses a simple fixed 1s interval instead, sufficient to exercise a live stream for #73's stalled-client test but not a full port of the scheduling logic.
- Authentication, CORS, and any middleware beyond the admission cap.
- Robust error handling (`printf` logging only, no structured logging).

These are candidates for #73's measurement phase and eventual production migration, not required for the Browser Load Profile workload this prototype exists to support.

---

## Key Technical Findings

### 1. Admission-Timing Seam (ADR 0018): Verified Against Source, Not Just Docs

**Question**: Does `Filter`/middleware fire early enough to gate static-file open and request-body access, before handler execution — and does an admitted request's admission state get released when it actually completes, not merely "ever admitted"?

**Finding: YES to both, verified by reading `PsychicHttpServer::requestHandler()` directly** (`.pio/libdeps/protoArtoo_psychichttp_prototype/PsychicHttp/src/PsychicHttpServer.cpp`), not by citing the README:

```cpp
esp_err_t PsychicHttpServer::requestHandler(httpd_req_t* req) {
  PsychicRequest request(server, req);
  server->_rewriteRequest(&request);

  // global filters run FIRST — before any middleware or endpoint dispatch
  if (!server->_filter(&request)) {
    return request.response()->send(400);   // <- a real 400 response is
                                              //    constructed and sent here,
                                              //    NOT a bare TCP RST
  }

  // middleware wraps _process(); endpoint matching, static-file open, and
  // request->body() access all happen inside _process(), reached only via
  // the middleware chain's continuation
  ret = server->_chain->runChain(&request, [&]() { return server->_process(&request); });
  ...
}
```

This structurally confirms the ADR 0018 requirement: the filter gates real work rather than running in parallel with it, and it directly corrects the first draft of this document, which claimed a rejected request gets a bare TCP RST with no response sent. It does not — `request.response()->send(400)` still constructs and writes a real HTTP response. **This is a genuine behavioral difference from the current stack's rejection path**, which uses a cheaper `abort()` before any response construction (per #71's inventory). Whether PsychicHttp's costlier rejection path matters under real pressure is a question for #73's live measurement, not something this source read can answer.

**The admission cap itself was rewritten to use this ordering correctly.** The first draft incremented a request counter in the filter and never decremented it anywhere — after 6 requests were ever admitted (cumulative, not concurrent), every later request was rejected permanently, which would have made any live #73 test look broken for reasons unrelated to what's being evaluated. The fix pairs `admissionFilter()` (increments on admit) with a global `addMiddleware` callback (`releaseMiddleware`, decrements after `next()` returns) — verified from the same source read that every filter-admitted request passes through the middleware chain exactly once, so the increment/decrement pair correctly brackets a request's real in-flight lifetime.

### 2. SSE Outgoing-Send: Unbounded AND Blocking, Not Just Unbounded

**Question**: Does `PsychicEventSource` have an internal queue safeguard (like WebSocket's `PSYCHIC_WS_MAX_PENDING_FRAMES`) against a stalled client?

**Finding: No queue exists at all — sends are synchronous and retry-blocking.** From the real source (`PsychicEventSource.cpp`):

```cpp
bool PsychicEventSourceClient::sendEvent(const char* event) {
  int result;
  do {
    result = httpd_socket_send(this->server(), this->socket(), event, strlen(event), 0);
  } while (result == HTTPD_SOCK_ERR_TIMEOUT);
  ...
}
```

The first draft of this document framed this correctly as "no queue, TCP hardware buffer is the only limit" — a memory-growth framing. That's true, but incomplete: **this is a blocking retry loop**, not just an unbounded queue. A stalled client (`HTTPD_SOCK_ERR_TIMEOUT` repeatedly) makes the calling task retry `httpd_socket_send()` in a spin until it either succeeds or the socket reports a hard failure. For protoArtoo specifically, this matters beyond memory growth: `AGENTS.md` prohibits blocking real-time control loops, so *which task* ends up calling `eventSource->send()` is safety-relevant, not just a performance concern. This prototype's `sseBroadcastTask` runs on its own dedicated FreeRTOS task (Core 0, priority 1) specifically to keep this blocking risk isolated from anything else — but a production port must not call `send()`/`broadcastEvent()`-equivalent from a shared or time-sensitive task without the same isolation.

**Comparison to protoArtoo's current stack**: ESPAsyncWebServer's `AsyncEventSource` also has no bounds by default; protoArtoo's vendor patch (`tools/patch_async_sse.py`) adds `SSE_MAX_QUEUED_MESSAGES`/`SSE_MAX_INFLIGH` to bound it. **Migrating to PsychicHttp would not eliminate this patch-maintenance burden** — it would need an equivalent application-level safeguard (e.g., tracking per-client failure/backoff before calling `send()`, or accepting the blocking-retry risk and isolating it to a dedicated low-priority task as this prototype does).

---

## Build and Verification

- **Environment**: `env:protoArtoo_psychichttp_prototype`
- **Command**: `pio run -e protoArtoo_psychichttp_prototype`
- **Result**: SUCCESS, 20.99s
- **Flash**: 1,462,472 bytes (85.8%) — down from the first draft's 93.1% once `main.cpp` genuinely stopped linking both server stacks' reachable code
- **RAM**: 106,136 bytes (32.4%) — down from 36.1% for the same reason

### Functional Testing

**Still not performed** — this remains compile-verified plus source-level structural verification (the dispatch-order and SSE-send-loop findings above are read directly from PsychicHttp's real source, not asserted from docs or untested code). Actually exercising the admission cap, SSE reconnect, and stalled-client behavior under load requires live hardware — that is #73's scope, not this ticket's.

---

## What Still Needs Hardware Testing (#73 Scope)

1. **Page load latency**: static file serving and `/api/status` timing vs. the current stack.
2. **SSE reconnect behavior**: tab hide/show, network roam, browser-driven reconnect using PsychicHttp's `retry:` field (`client->send(msg, event, id, reconnect)` — not yet exercised in this prototype).
3. **Stalled-client behavior**: does the blocking `httpd_socket_send()` retry loop identified above cause observable stalls or heap pressure under a real half-open connection, and does isolating it to `sseBroadcastTask` actually contain the risk as intended?
4. **Admission cap effectiveness under real concurrent load**: the filter/middleware pairing is now structurally correct per the source read above, but has not been exercised under actual concurrent requests on hardware.
5. **Rejection-path cost**: whether PsychicHttp's `send(400)`-on-reject path (vs. the current stack's cheaper `abort()`) is measurably more expensive under heap pressure — directly relevant to whether admission rejection stays cheap when it matters most.

---

## References

- **Issue #72**: this ticket
- **Issue #71**: [HTTP/static/SSE inventory](https://github.com/mattiasbrandt/protoArtoo/blob/phase/v1.0.0/docs/research/issue-71-http-surface-inventory.md) (behavioral spec)
- **Issue #70**: [compile-check env pattern](https://github.com/mattiasbrandt/protoArtoo/blob/phase/v1.0.0/docs/research/issue-70-psychichttp-compile-probe.md)
- **ADR 0018**: early admission seam feasibility
- **PsychicHttp v3.1.2 source** (ground truth for both findings above): `.pio/libdeps/protoArtoo_psychichttp_prototype/PsychicHttp/src/PsychicHttpServer.cpp`, `PsychicEventSource.cpp`
- **Source files**: `src/web/psychic_adapter.cpp`, `include/psychic_adapter.h`, `platformio.ini` (`env:protoArtoo_psychichttp_prototype`), `src/main.cpp` (wiring)

---

## Revision history

**First draft (superseded)** built and compiled successfully but had three real defects found in operator review, not just missing polish:
1. `/api/status` set a response code/content-type but never called `response->send(...)` — the request likely never completed at all.
2. The admission counter was incremented on every admitted request with no decrement anywhere, making the "inflight cap" a one-time "first 6 requests ever" lockout.
3. `initPsychicHttpServer()` was never called from anywhere reachable — the server would never have started even if flashed.

The draft also mislabeled the admission-timing finding as "✓ VERIFIED / CONFIRMED" while its own "Functional Testing" section admitted no test was actually run, and incorrectly claimed a filter rejection sends a bare TCP RST (it sends a real HTTP 400) and that "PsychicHttp depends on ESPAsyncWebServer" (both were linked only because the prototype env extends `protoArtoo_chirp`, which retains ESPAsyncWebServer in `lib_deps`, and nothing stripped it out — not an actual PsychicHttp dependency). This revision fixes all of the above and re-verifies both headline findings against PsychicHttp's real source rather than its docs.
