# Handlers name a project-owned web request type with one compile-time backend

Issue #78 (epic #75, decided on #53) establishes the seam the PsychicHttp
migration is built through. Web handlers stop naming any vendor request type;
they take `WebRequest`, a protoArtoo-owned concrete class declared in
`include/web_request.h`, and register through `webRegisterRoute()`. Exactly one
backend implementation of that class is compiled into a build.

## Seam shape

`WebRequest` is a concrete class holding a single opaque backend pointer. Its
methods are declared once in the header and defined in exactly one
backend translation unit selected at compile time:

- `src/web/web_request_async.cpp` -- wraps `AsyncWebServerRequest` (default;
  temporary scaffolding, see below)
- `src/web/web_request_psychic.cpp` -- wraps `PsychicRequest` /
  `PsychicResponse`, compiled when `PA_WEB_BACKEND_PSYCHIC` is defined
- `src/native_test_stubs.cpp` -- wraps a plain params-and-captured-response
  struct for `[env:native]` host tests (`PA_NATIVE_TEST_STUBS`)

No virtual dispatch, no heap allocation, no vendor type in any handler file or
header. The surface starts minimal -- `hasParam()`, `param()` (copy-out
semantics so no backend's string lifetime leaks through the seam), `send()` --
and grows only as route groups are ported (#79, #86-#90).

### The session escape hatch is the point

The binding constraint from #53: the seam must expose `sess_ctx`, `free_ctx`
and `httpd_sess_trigger_close()`, not flatten them -- the SSE eviction path
(#83) and the ADR 0020 response-phase deadline (#92) exist because of those
capabilities. `WebRequest` therefore carries them as first-class typed
operations:

- `sessionContext()` -- reads the underlying `httpd_req_t::sess_ctx`
- `setSessionContext(ctx, freeFn)` -- sets `sess_ctx` and `free_ctx`
- `triggerClose()` -- calls `httpd_sess_trigger_close()` on the request's
  socket

On the PsychicHttp backend these reach the real `esp_http_server` request. On
the async scaffold they are documented no-ops returning null/false -- the
async stack has no equivalent capability, which is exactly why it is being
replaced (#68, ADR 0020).

### Rejected alternatives (settled on #53, not re-opened)

- Handlers naming `PsychicRequest*` directly: swaps one vendor coupling for
  another; the next server change touches every handler file again.
- A thin type alias: reads as project code, isolates nothing.
- An abstract interface with virtual backends: allows both stacks in one
  binary, which nothing needs (selection is per-build), at the cost of vtables
  and per-request indirection on an embedded path.
- A function-pointer struct (the ADR 0011 `ConfigParamSource` pattern): right
  shape for an input-only parameter seam, clumsy for a full
  request/response/session surface.

## Temporary dual-implementation scaffold

During the migration the seam carries two device backends selected at compile
time: async (default, every existing env) and psychic
(`PA_WEB_BACKEND_PSYCHIC`, env `protoArtoo_psychic`). This keeps every
intermediate commit on `phase/v1.0.0` flashable and bisectable while routes
convert one group at a time (#79, #86-#90).

This is scaffolding, not the coexistence #53 rejected. **The cutover slice
#91 deletes `web_request_async.cpp`, ESPAsyncWebServer, AsyncTCP and
`tools/patch_async_sse.py`, and makes the psychic backend the only one.**
Nothing in epic #75 is complete until the async implementation is gone.

## Abort condition for the migration (#75 open question 2)

Settled before any ported-stack measurement exists, so the threshold cannot be
rationalized after the fact. The reference is the baseline scorecard
`tasks/evidence/webload/run-31-post61fix/scorecard-adr0017.md` (#76), which
scores the incumbent async stack against ADR 0017's eight criteria: 4 PASS,
3 FAIL, 1 UNSCORABLE.

At #85 (first matched comparison run) and again at #93 (full acceptance
matrix):

1. **No regression.** The ported stack must score no worse than the baseline
   on each of the eight ADR 0017 criteria. Criteria the baseline itself
   failed or could not score cannot block the port, but must not get worse
   where scorable.
2. **Two bounded fix attempts per regressed criterion.** A regressed
   criterion gets at most two evidence-driven remediation attempts, each
   requiring new telemetry (fresh run evidence) before the next edit --
   matching the repository's regression-troubleshooting discipline. If the
   criterion still regresses after the second attempt, `git revert` of the
   migration commits on `phase/v1.0.0` is taken.
3. **Safety failures get one attempt.** A panic, unexpected reset, or 30s+
   loss of `/api/status` attributable to the ported stack (ADR 0017's stop
   condition) gets one focused fix attempt; a second occurrence after that
   fix is an immediate revert.

Rollback is `git revert` on `phase/v1.0.0`, never a compile flag (#53).

## Consequences

- `/api/identity` is the tracer: one handler source compiles and serves under
  both device backends and the native host backend, with a byte-identical
  response body.
- The host-test stub for a ported route is a small project-owned struct
  (`test/stubs/include/web_request_test_backend.h`) instead of a fake vendor
  class hierarchy; per #75 gap 5 the stubs must get simpler with each ported
  group, or the seam shape is wrong.
- Ported handlers return through `WebRequest::send()`; the psychic backend
  propagates `esp_http_server`'s `esp_err_t` internally so handler signatures
  stay `void(WebRequest&)`.
- Until #81 lands, the psychic build has no pre-HTTP admission guard; it is a
  development target, not the release configuration. The async build remains
  the default in every existing env until the cutover.
