# Page Load Recovery architecture and rollout handoff

> **Mechanism updated for PsychicHttp.** This document describes the page-load recovery
> architecture and rollout as currently implemented on ESP-IDF's `esp_http_server`
> backend via PsychicHttp (epic #75 migration). The product decisions -- request classes,
> Recovery Capacity, page bootstrap, rollout order, verification matrix -- remain as
> designed. For architectural decisions see
> [ADR 0021](adr/0021-project-owned-web-request-seam.md) (project-owned request seam),
> [ADR 0022](adr/0022-connection-admission-on-esp-http-server.md) (connection and request admission),
> [ADR 0024](adr/0024-response-phase-deadline-send-override.md) (response-phase deadline), and
> [ADR 0016](adr/0016-busy-recovery-page-wire-contract.md) (Busy Recovery Page).

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

Implemented across ADRs 0021 (request seam), 0022 (connection and request admission),
and 0024 (response-phase deadline). On ESP-IDF's `esp_http_server`, admission runs in
two layers:

**Connection Admission** runs from the server's socket-open callback
(`src/web/web_admission_psychic.cpp`) before any HTTP byte is parsed. It is blind to
the URL and makes a rate-limiting and heap-floor decision based only on the socket
itself. Rejected connections are never opened by the HTTP server, allocating nothing
at rejection time.

**Request Admission** runs from a global middleware that chains before route matching
and before a static file is opened. It runs once the HTTP request head has been read,
so it can see the URL and distinguish estop from ordinary requests. This layer gates
the same heap floors and the in-flight request cap that appear in the Request Classes
section below.

The two-layer split is forced, not stylistic (ADR 0022). At the socket layer there is
no HTTP context -- no method, no URL -- so policies that depend on the request must
live in the request layer. Both layers are needed because the socket layer alone
cannot distinguish which requests should bypass the cap. The consequence of this design
is that the estop bypass is exempt at the request layer but cannot be exempt at the
socket layer; this is parity with the prior stack's behavior, not a regression
(ADR 0022).

## Request classes and exception paths

The request-admission middleware (`src/web/web_admission_psychic.cpp`) enforces three
distinct classes:

- **Diagnostic** (`/api/status`, `/api/profiler`, `/api/coredump`, and `/api/events`):
  gated by the looser `PA_ADMISSION_MIN_LARGEST_FREE_BLOCK_DIAG` floor (7500) so
  operators can still see what's happening during a rejection window. These routes
  should remain reachable even under heap pressure.
- **Non-diagnostic** (everything else -- static assets, ordinary `/api/*` calls,
  uploads): gated by `PA_ADMISSION_MIN_LARGEST_FREE_BLOCK` (9000) and the inflight cap
  (`PA_ADMISSION_MAX_INFLIGHT_REQUESTS`, typically 6).
- **Estop** (`/api/estop`): the sole exception path -- bypasses request admission
  entirely, never counted against the cap, never refused. This is not new; it is the
  existing invariant this architecture must preserve, not change.

## Connection-lifetime accounting

An admitted (non-refused) request is counted in the inflight cap from the request
middleware's entry point until the handler returns (via an RAII `InflightSlot` guard
in `src/web/web_admission_psychic.cpp`). The server writes the response synchronously
from the handler, so this timing covers the whole request-response cycle. A refused
attempt is released immediately via connection closure or the Busy Recovery Page,
never incremented against the cap. This accounting boundary does not change as part of
this rollout; the Common Page Bootstrap's client-side Bounded Page Attempt/Operation
Deadline model is a client-side concept layered on top, not a replacement for it.

## Recovery Capacity boundary

Specified in ADR 0016 (wire contract) and ADR 0024 (implementation on esp_http_server).
When a request-admission floor or cap is exceeded, the middleware attempts to send a
Busy Recovery Page (503 status, `Retry-After: 5` header) before closing the connection.

The response is one `static constexpr` byte buffer (status line + headers + HTML body)
written directly to the socket via `httpd_socket_send()`, bypassing the normal
`PsychicResponse` chain. This allocates nothing at rejection time, which is load-bearing:
the header-list allocation that breaks normal response paths under heap pressure (ADR 0016)
is sidestepped by writing directly to the socket. After the buffer is sent, the
middleware returns non-`ESP_OK` so the server closes the connection.

On this stack (unlike the prior AsyncWebServer stack), no Recovery Capacity slot needs
to be reserved: the server services all connections from a single task, so at most one
response can be in flight at once, and the Busy Recovery Page reuse is structural rather
than enforced. The wire contract (503 + `Retry-After: 5`) remains unchanged, grounded in
#54's measured ~10s recovery time rather than the generic 30-120s web-service overload
convention (ADR 0016).

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
  - **Single Active Request**: a single FIFO active-request slot shared across all
    request types except estop (see ADR 0019). Estop requests bypass the slot entirely
    and are sent immediately without queueing, matching the server-side admission bypass
    in `src/web/web_admission.cpp` (see `include/web_admission.h`).
  - **Hidden Tab Pause**: hiding stops new work dispatch; the one active in-flight
    request is left to finish within its deadline; showing again resumes from the
    actual next unfinished step.
  - **Operation Deadline**: `busy` outcomes retry using the server's `Retry-After`;
    `no-response` outcomes (including a deadline auto-expiry with no result) use
    growing backoff, since no server-given value exists for that case.
- `data/web_api.js` is the real host for this reducer's I/O: its `ApiError` kind
  classification (`timeout`/`network`/`http`/`bad-json`) and 503 -> "Device
  unavailable" mapping already exist and should be read by the reducer's outcome
  classification, not duplicated. Its `MAX_CONCURRENT_REQUESTS` narrows from 2 to 1
  (ADR 0019). Its `Retry-After` header should be read directly for API-class busy
  outcomes rather than guessed.
- `data/page_loader.js`'s 3-attempt script retry-with-backoff was the prior art for
  Resource Step Recovery's script-loading half; the bootstrap absorbed that behavior
  rather than running a second, incompatible retry mechanism alongside it, and the
  loader was removed once every page had migrated.
- `data/status_stream.js`'s existing Hidden Tab Pause and reconnect-backoff for
  `/api/events` stays as the SSE transport underneath `liveUpdatesStarted` gating --
  except its known stuck-reconnect-after-first-failure defect (#61), which is a
  separate fix, not something this bootstrap rollout should paper over or wait on
  before generalizing the *gating* logic (see Stop and rollback rules).

## Page, resource, and section inventory

All 10 controller pages declare their script chain via `data-scripts` on `<html>`,
consumed by the inline recovery kernel (`data/_recovery_kernel.html`), which fetches
`page_bootstrap.js` with retry and hands it that chain. Every page shares the same base chain
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
- The `artoo_esp32_profiler`-build lifecycle-timing trace (`/api/profiler`'s
  request-start/response-ready/disconnect data) -- still not captured per #54's own
  final comment; any timing-stage thresholds beyond what ADR 0017 already locks stay
  open pending that pass.

With this document, ADR 0016/0017/0018/0019, and the #55 prototype, #52's own epic
issue may be ready to close or re-scope down to just tracking the page-by-page
rollout as a checklist -- that decision belongs to the epic's owner, not this
document.
