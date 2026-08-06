# Page Load Recovery architecture and rollout handoff

> **Partly superseded.** This document was written against the ESPAsyncWebServer /
> AsyncTCP stack, which epic #75 replaced with PsychicHttp over ESP-IDF's
> `esp_http_server`. The product decisions here still stand -- request classes,
> Recovery Capacity boundary, page bootstrap, rollout order, verification matrix --
> but every statement about *where the seam sits in the server* is obsolete, and the
> vendor patch it refers to no longer exists. For the current mechanism read
> [ADR 0021](adr/0021-project-owned-web-request-seam.md) (project-owned request seam),
> [ADR 0022](adr/0022-connection-admission-on-esp-http-server.md) (admission) and
> [ADR 0023](adr/0023-http-keep-alive-on-esp-http-server.md) (keep-alive).

Implementation-ready design for issue #52 ("Design safe web page-load recovery under
constrained ESP32 memory"), synthesized once the lifecycle evidence, acceptance
envelope, browser prototype, current-stack feasibility, and wire contract were all
resolved (issue #59). This document is the handoff -- it makes each remaining
implementation slice independently grabbable without reopening the decisions below.
It is a synthesis and pointer document, not a replacement for the ADRs it references.

## Table of Contents

- [Admission seam](#admission-seam)
- [Request classes and exception paths](#request-classes-and-exception-paths)
- [Connection-lifetime accounting](#connection-lifetime-accounting)
- [Recovery Capacity boundary](#recovery-capacity-boundary)
- [Common Page Bootstrap interface](#common-page-bootstrap-interface)
- [Page, resource, and section inventory](#page-resource-and-section-inventory)
- [Page rollout order](#page-rollout-order)
- [Operation Deadline categories](#operation-deadline-categories)
- [WiFi-first tracer gate](#wifi-first-tracer-gate)
- [Cross-page generalization gates](#cross-page-generalization-gates)
- [Verification matrix](#verification-matrix)
- [Stop and rollback rules](#stop-and-rollback-rules)
- [Open items not resolved by this document](#open-items-not-resolved-by-this-document)

## Admission seam

Locked in ADR 0018. The vendor request lifecycle (`.pio/libdeps/*/ESPAsyncWebServer/`)
parses method/URL, then calls `_attachHandler()` -- which runs every handler's
`canHandle()` **before** `server.addMiddleware()`'s chain. For the static-file
handler, `canHandle()` opens the file directly from LittleFS: real I/O before our
admission middleware ever sees the request. This exact gap is already closed in
production via `tools/patch_async_sse.py`'s existing `STATIC_OPEN_GUARD` (issue #21,
round 4), which checks `ASYNC_STATIC_MIN_LARGEST_FREE_BLOCK` (9000) at the real
`_fs.open()` seam and aborts before opening if the floor isn't cleared. No vendor
replacement or broad patch is needed. One small gap remains open (not blocking): that
guard checks the heap floor only, not the inflight/SSE caps, because those counters
are internal-linkage `static` variables in a different translation unit
(`src/web/web_server.cpp`) from the vendor-patched file. Closing it needs a small
cross-TU `extern` accessor -- scoped as a short future follow-up.

## Request classes and exception paths

Three broad classes, matching the existing middleware
(`src/web/web_server.cpp:1143-1242`):

- **Diagnostic** (`/api/status`, `/api/profiler`, `/api/coredump`, and `/api/events`):
  gated by the looser `PA_ADMISSION_MIN_LARGEST_FREE_BLOCK_DIAG` floor (7500) so
  operators can still see what's happening during a rejection window.
- **Non-diagnostic** (everything else -- static assets, ordinary `/api/*` calls,
  uploads): gated by `PA_ADMISSION_MIN_LARGEST_FREE_BLOCK` (9000) and the inflight cap
  (`kMaxInflightRequests`).
- **Estop** (`/api/estop`): the sole exception path -- bypasses admission entirely,
  never counted, never refused. This is not new; it is the existing invariant this
  architecture must preserve, not change.

## Connection-lifetime accounting

An admitted (non-refused) request remains counted until the server's disconnect
completion point (`request->onDisconnect(...)`), because response, file, and TCP
memory may still be live before then -- already implemented
(`src/web/web_server.cpp:1221-1234`). A refused attempt is released immediately via
`abort()` or the ADR 0016 Busy Recovery Page, never parked. This accounting boundary
does not change as part of this rollout; the Common Page Bootstrap's client-side
Bounded Page Attempt/Operation Deadline model is a client-side concept layered on top,
not a replacement for it.

## Recovery Capacity boundary

Locked in ADR 0016. Exactly one reserved slot for the whole controller, shared across
every resource class -- not one per class. All four existing refusal call sites
(`refusedInflightCap`, `refusedSseCap`, `refusedHeapFloor`, `refusedHeapFloorDiag`)
attempt to claim it via a shared `tryBusyResponse()` helper before falling back to
`abort()`. The response is one `static constexpr` byte buffer (status line + headers +
HTML/inline-script body), written directly to the raw `AsyncClient`, bypassing
`AsyncWebServerResponse` entirely (a bare 503 through the normal response path is a
proven crash site under this exact pressure). Carries `Retry-After: 5`
(Recovery Retry Interval), grounded in #54's measured ~10s recovery time, not the
generic 30-120s web-service overload convention.

## Common Page Bootstrap interface

Validated by the `prototype/issue-55-bootstrap` prototype (throwaway branch, not
merged). The shape to implement for real:

- A pure reducer over `{ resources, sections }`, mirroring
  `prototype/issue55-bootstrap/core.js`'s validated transitions:
  - **Resource Step Recovery**: resources load in declared order via a single
    cursor; a failure pauses the cursor and retries only that step; earlier
    completed steps are untouched; `resourcesReady` flips only once every resource
    has succeeded.
  - **Section Recovery**: sections are independent once resources are ready; one
    section's failure does not block or reset another; `sectionsStable` requires
    every section to be either done or visibly waiting to retry, not all succeeded.
  - **Page Startup Order**: `/api/events` (Live Page Updates) starts only once
    `resourcesReady && sectionsStable`.
  - **Browser Request Priority**: a single active-request slot (see ADR 0019),
    drawn from a priority order -- Estop bypasses the slot/queue entirely; user
    commands next; then startup/first-section attempts; then background retries.
    Estop is never queued and never auto-retried; user commands are never
    auto-retried on failure either.
  - **Hidden Tab Pause**: hiding cancels queued-but-not-started work and stops new
    dispatch; the one active in-flight request is left to finish within its
    deadline; showing again resumes from the actual next unfinished step.
  - **Operation Deadline**: `busy` outcomes retry using the server's `Retry-After`;
    `no-response` outcomes (including a deadline auto-expiry with no result) use
    growing backoff, since no server-given value exists for that case.
- `data/web_api.js` is the real host for this reducer's I/O: its `ApiError` kind
  classification (`timeout`/`network`/`http`/`bad-json`) and 503 -> "Device
  unavailable" mapping already exist and should be read by the reducer's outcome
  classification, not duplicated. Its `MAX_CONCURRENT_REQUESTS` narrows from 2 to 1
  (ADR 0019). Its `Retry-After` header should be read directly for API-class busy
  outcomes rather than guessed.
- `data/page_loader.js`'s existing 3-attempt script retry-with-backoff is prior art
  for Resource Step Recovery's script-loading half; the real bootstrap absorbs this
  behavior rather than running a second, incompatible retry mechanism alongside it.
- `data/status_stream.js`'s existing Hidden Tab Pause and reconnect-backoff for
  `/api/events` stays as the SSE transport underneath `liveUpdatesStarted` gating --
  except its known stuck-reconnect-after-first-failure defect (#61), which is a
  separate fix, not something this bootstrap rollout should paper over or wait on
  before generalizing the *gating* logic (see Stop and rollback rules).

## Page, resource, and section inventory

All 10 controller pages declare their script chain via `data-scripts`, consumed by
`page_loader.js`. Every page shares the same base chain
(`web_api.js`, `status_stream.js`, `shell.js`, then page-specific script(s), then
`footer.js`); `index.html` and `setup.html` additionally load `diagnostics.js`.

| Page | Script count | Notes |
|---|---|---|
| `wifi.html` | 5 | Tracer -- fixed first by #52 |
| `firmware.html` | 5 | OTA/filesystem upload flow exempt from Operation Deadline (see below) |
| `sound.html` | 5 | CHIRP catalog load uses the 12000ms deadline category |
| `servo.html` | 5 | |
| `dome.html` | 5 | |
| `setup.html` | 6 | Adds `diagnostics.js` |
| `rc.html` | 5 | Safety-adjacent (RC mapping) |
| `drive.html` | 5 | Safety-adjacent (live vehicle control) |
| `seq.html` | 10 | Adds `seq_protocol_check.js` plus the dome layout/panel-model chain |
| `index.html` | 11 | Heaviest, highest-traffic dashboard |

## Page rollout order

Locked in ADR 0019: `wifi` (done by #52) -> `firmware` -> `sound` -> `servo` ->
`dome` -> `setup` -> `rc` -> `drive` -> `seq` -> `index`. Cheap/non-safety pages
first to absorb early mistakes in the newly-generalized bootstrap; `rc`/`drive` wait
until the pattern is proven elsewhere; the two heaviest/highest-traffic pages go last.

## Operation Deadline categories

Locked in ADR 0019: exactly two categories, no more.

1. **Ordinary** (6000ms) -- the default for everything: page documents, static
   resources, ordinary API calls, `/api/events` connect. Matches the existing
   `web_api.js` `DEFAULT_TIMEOUT_MS` and the validated prototype's
   `OPERATION_DEADLINE_MS`.
2. **Catalog** (12000ms) -- `GET /api/audio/catalog` only, matching its
   already-established real value (`sound.js:1322`).

`firmware.html`'s OTA/filesystem-upload flow is exempt from both -- it is not a
deadline-governed flow, it is a separate bespoke progress-and-reconnect mechanism
(`fsProgressBar`, `otaProgress`, `waitForReconnect`) predating this epic.

## WiFi-first tracer gate

Per #52's own text: `wifi.html` must be the first page migrated, because it must
remain reachable during WiFi Provisioning and Network Recovery Mode. No other page
may generalize past what the tracer proves until both visible recovery and warmed
memory recovery pass on the Supported ESP32 Board for that tracer.

## Cross-page generalization gates

Locked in ADR 0019. Every page slice requires only the deterministic browser
fault-injection checks (per the #55 fixture seam -- busy, no-response,
required-resource failure, section failure, hide/show, retry, queued-deadline/
cancellation, command-priority), no live hardware. The full live-hardware ADR 0017
envelope run is reserved for three checkpoints only: the WiFi tracer, the *first*
non-tracer page slice, and the *final* page (`index.html`).

## Verification matrix

| Check | Scope | Cadence |
|---|---|---|
| `pio check` / native build | Every code-changing commit | Always |
| Browser fault-injection checks (fixture seam) | Every page slice | Every slice, mandatory gate |
| ADR 0017 live-hardware envelope | WiFi tracer, first non-tracer page, final page (`index.html`) | 3 checkpoints only |
| `/api/audio/catalog` 12000ms deadline behavior | `sound.html` slice specifically | Once, at that slice |
| OTA/filesystem-upload flow unaffected | `firmware.html` slice specifically | Once, at that slice (confirm bootstrap changes did not touch the existing exempt flow) |

## Stop and rollback rules

Locked in ADR 0019:

- A page slice's fault-injection checks fail: that page's migration stays unmerged;
  do not advance to the next page in the rollout order; re-diagnose via the same
  fixture checks, not live-hardware iteration, before retrying.
- A milestone live-hardware envelope check fails even though that page's own
  fault-injection checks passed: pause the entire rollout until root-caused --
  do not migrate further pages on top of a possibly-broken foundation.
- No page may ever claim the rapid-refresh+3-tab scenario class as passing (open
  pending #62). The bootstrap's `liveUpdatesStarted` gating logic may generalize
  page-by-page as designed, but the underlying `/api/events` reconnect behavior is
  not "proven" beyond what is already true today until #61 lands.

## Open items not resolved by this document

- **#61** (SSE stuck-reconnect after first-connect reset) and **#62** (rapid-refresh
  +3-tab sustained-refusal repro) -- both filed, both explicitly out of scope for this
  architecture; the standing exclusions above account for their existence without
  resolving them.
- The small inflight/SSE-cap gap in the early admission seam (ADR 0018) -- scoped as
  a short future follow-up, not blocking this rollout.
- The `protoArtoo_profiler`-build lifecycle-timing trace (`/api/profiler`'s
  request-start/response-ready/disconnect data) -- still not captured per #54's own
  final comment; any timing-stage thresholds beyond what ADR 0017 already locks stay
  open pending that pass.

With this document, ADR 0016/0017/0018/0019, and the #55 prototype, #52's own epic
issue may be ready to close or re-scope down to just tracking the page-by-page
rollout as a checklist -- that decision belongs to the epic's owner, not this
document.
