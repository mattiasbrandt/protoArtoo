# Issue #73: PsychicHttp fails most genuinely concurrent connections

**Date**: 2026-08-04
**Severity**: High — this is the single most decision-relevant finding of the whole #53 evaluation so far. It surfaced not from a synthetic stress scenario but from the harness's ordinary single-tab Browser Load Profile capture (run `run-32-psychichttp`) — i.e., completely normal page-load behavior.

## Summary

A real single-tab browser page load against `env:protoArtoo_psychichttp_prototype` (#72) failed (`primaryOutcome: "Page Failure"` in the harness's `outcome.json`). `health_signals.js` failed 2/2 retry attempts with HTTP 400 and `net::ERR_ABORTED`; everything after it in the page's strict load order never even attempted. Root-cause investigation traced this to **esp_http_server's default single-connection-at-a-time request processing model**: even 2 genuinely simultaneous HTTP GET requests (different resources, no admission-cap involvement) reliably fail 1 of 2 with a parser-level `400 Bad Request`. This is not a load-testing edge case — it is provoked by the ordinary number of parallel connections a modern browser opens per origin during a normal page load.

## How this was found

1. Ran the harness's `full` stage (#73's own scaffolding, `--build-env protoArtoo_psychichttp_prototype`) — a real, physical-power-cycled, single visible Chromium tab against `data/index.html`. Outcome: `Page Failure`. Evidence: `tasks/evidence/webload/run-32-psychichttp/`.
2. `outcome.json`'s `resourceSummary` showed `/web_api.js` succeeded on retry (400 then 200) but `/health_signals.js` failed both attempts (400, 400); everything after it in the page loader's strict dependency order (`dome_command_map.js` onward) was never attempted at all — matches the documented Resource Step Recovery behavior (stop at the first failed required resource).
3. Checked `status.ndjson` samples bracketing the failure: `refusedInflightCap: 0` and `refusedHeapFloor: 0` throughout — **the admission Filter+middleware built in #72 did not reject anything**. The 400s are not coming from this prototype's own admission logic.
4. Checked every place in PsychicHttp's vendored source that sends a bare `400` with no body/content-type (matching the observed `content-type: "", content-length: "0"` response shape) — only one match: `PsychicHttpServer::requestHandler()`'s global-filter-rejection path (`PsychicHttpServer.cpp:497`). But that path is only reached when a registered filter returns `false`, and this prototype has exactly one filter (`admissionFilter`), which the counters above prove never rejected. Contradiction — the 400 must originate somewhere lower than PsychicHttp's own wrapper.
5. Checked `esp_http_server`'s real C source (`framework-espidf/components/esp_http_server/src/httpd_parse.c`): a `400` also gets set by the HTTP parser itself on `nparsed != length` ("incomplete... with parser error") — a **request-parsing failure at the raw socket/parser layer**, independent of any application code.
6. Reproduced directly and repeatedly with `curl`, ruling out browser-specific behavior:
   - 5 parallel requests to the **same** resource (`/health_signals.js`): 4/5 failed with `400`.
   - 5 parallel requests to **5 different** resources (`index.html`, `page_loader.js`, `web_api.js`, `diagnostics.js`, `status_stream.js`): 4/5 failed with `400` — rules out same-file contention/locking as the cause.
   - **2** parallel requests to 2 different resources: **1/2 failed.** This is the critical result — the failure isn't specific to a burst of 5, it reproduces at the smallest possible concurrency (2 simultaneous connections).
7. Tried the two `httpd_config_t` knobs `esp_http_server` exposes for connection-admission behavior: `backlog_conn` (default 5, raised to 10) and `lru_purge_enable` (default `false`, set `true` — evict oldest idle connection instead of refusing a new one). **Neither fixed it.** Re-ran the 2-concurrent-request test after flashing with both changes: still 1/2 failed, same signature.
8. Checked `PsychicHttp.h` for `ENABLE_ASYNC` (`// #define ENABLE_ASYNC // ESP-IDF 5.1.x, each request can be handled in its own thread`, commented out by default) — flagged by #69's earlier research as possibly relevant. Read `async_worker.h`/`.cpp`: this is a **narrower** mechanism than a general concurrency fix — it lets specific handlers (PsychicHttp's own upload/multipart code paths use it) explicitly offload themselves to a worker-thread pool so they don't block the httpd control task on long-running work. It is not a blanket "make ordinary GET requests handle concurrently" switch, and nothing in this prototype's code path (`/health_signals.js`, `/api/status`) uses it. Not tested as a fix because reading the mechanism ruled it out as applicable — noted here so it isn't retested by a future session under the same false assumption #69's research briefly carried.

## What this is not

- **Not the admission cap.** `refusedInflightCap`/`refusedHeapFloor` stayed at `0` through every reproduction. This prototype's Filter+middleware pairing (#72) is uninvolved.
- **Not a same-resource/file-locking issue.** Reproduces identically whether hitting the same URL 5x or 5 different URLs once each.
- **Not (so far as tested) a `backlog_conn`/`lru_purge_enable` tuning gap.** Both were raised/enabled and retested live; no change.
- **Not `ENABLE_ASYNC`-fixable** by the mechanism's own design — that flag targets a different problem (explicit handler offload), not general connection concurrency.

## What this likely is

`esp_http_server`'s default configuration processes HTTP requests through one control task, servicing sockets serially even though several can be simultaneously *open* (`max_open_sockets`). The exact trigger for the parser producing `HTTPD_400_BAD_REQUEST` specifically (rather than just added latency) when a second connection's request bytes arrive while the first is still being serviced was not root-caused to a specific line of `esp_http_server`'s socket-read/parser interaction — that would require deeper `esp_http_server` internals work (e.g. tracing `httpd_sess.c`'s `select()`/`recv()` loop against the exact byte-level timing) than this evaluation ticket's scope justifies. The *symptom* is conclusively reproduced and isolated to below the application layer; the exact internal mechanism is not.

## Why this matters for #53's go/no-go (#74)

This is a severe, ordinary-usage-triggering regression relative to ESPAsyncWebServer's fully-async model, which handles genuinely concurrent connections as a matter of course (that's the entire premise of "Async" in its name). A real browser's normal per-origin connection parallelism — not a stress test, not three tabs, not #52's already-elevated pressure scenarios — is enough to break page loads on this prototype's current configuration. If this cannot be resolved (a deeper `esp_http_server` concurrency fix, or an architectural change such as running multiple httpd instances on different ports, a known pattern for this exact limitation in other esp_http_server-based projects), **it is a strong point toward "no-go"** regardless of how favorably the deadline-seam (#69), admission-timing (#72), or author-reported benchmark (#53's Notes) findings read — none of those matter if the server can't reliably serve one ordinary browser tab.

## Open question for further investigation (not yet attempted)

Whether running **multiple `PsychicHttpServer`/`httpd` instances** (e.g. one per expected concurrent connection, or a small pool on different `ctrl_port` values as PsychicHttp's own HTTPS-redirect-server example demonstrates is possible) resolves this, at the cost of additional RAM/task overhead per instance. This is the most promising lead surfaced by this investigation but was not tested — flagging for whoever picks this up next rather than treating this document as exhausting the question.

## Evidence

- Full harness run: `tasks/evidence/webload/run-32-psychichttp/` (`outcome.json`, `browser/network.ndjson`, `status.ndjson`)
- Reproduction: direct `curl` parallel-request tests (2026-08-04, this session, not separately archived — reproducible on demand against `env:protoArtoo_psychichttp_prototype`)
- Source citations: `esp_http_server/src/httpd_parse.c` (parser 400), `PsychicHttpServer.cpp:497` (filter-rejection 400, ruled out), `PsychicHttp.h`/`async_worker.h` (`ENABLE_ASYNC`, ruled out as inapplicable), `esp_http_server.h` (`HTTPD_DEFAULT_CONFIG`, `backlog_conn`/`lru_purge_enable` defaults)
