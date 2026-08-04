# Issue #73: Concurrent-request failures — found, root-caused, and fixed

**Date**: 2026-08-04
**Status**: Resolved. This document originally shipped an incorrect root-cause attribution (see "Correction" below) — read that section first if you saw the earlier version.

## Summary

A real single-tab browser page load against `env:protoArtoo_psychichttp_prototype` (#72) failed (`primaryOutcome: "Page Failure"` in the harness's `outcome.json`, run `run-32-psychichttp`). Root cause: this prototype's admission filter called `heap_caps_get_largest_free_block()` — a full heap-structure scan, not an O(1) read — synchronously on every request. PsychicHttp/esp_http_server's default configuration services connections through one round-robin task (confirmed in Espressif's own docs: "a listening socket... selected in a round robin fashion in the server task loop"), so that scan's cost was enough to make other genuinely-concurrent connections fail their HTTP parse. **Fixed** by caching the heap value, refreshed once/second by a low-priority background task, instead of scanning it inline in the request path. Verified live: 12/12 genuinely concurrent requests succeeded across 4 rounds after the fix, with the admission filter and inflight-cap logic fully active.

## Correction

An earlier version of this document concluded the opposite: that this was an inherent PsychicHttp/`esp_http_server` architectural limitation ("fails most genuinely concurrent connections... a strong point toward no-go"). That was wrong, and the mistake is worth recording plainly: the investigation reproduced the symptom carefully and cited real source (`httpd_parse.c`'s parser-level 400, `PsychicHttpServer.cpp`'s filter-rejection path ruled out via the admission counters staying at `0`), but it never ran the one control test that would have caught the actual cause — disabling this prototype's own filter/middleware to see if the failure was still there. It wasn't. 9/9 concurrent requests succeeded with the filter disabled; the failures returned identically when re-enabled. From there, isolating the exact line (`heap_caps_get_largest_free_block()`) took one more targeted test.

The lesson isn't "always distrust yourself" — it's a specific, repeatable one: **when a "the platform is broken" conclusion is available, prefer disabling your own code first as a control before writing it up.** Source-reading and empirical reproduction are necessary but not sufficient; they can both be entirely accurate about a *symptom* while still being wrong about *attribution* if the one experiment that would separate "my code" from "the platform" never gets run.

## How this was found and fixed

1. Ran the harness's `full` stage (#73's own scaffolding) — a real, physical-power-cycled, single visible Chromium tab against `data/index.html`. Outcome: `Page Failure`. `health_signals.js` failed both retry attempts with HTTP 400; the page loader correctly stopped there per its strict dependency order.
2. Checked `status.ndjson` samples bracketing the failure: `refusedInflightCap: 0`, `refusedHeapFloor: 0` throughout. Confirmed via direct `curl` reproduction too (5 parallel requests, same and different resources: 4/5 failed either way; 2 parallel requests: 1/2 failed). The admission filter's own explicit-rejection counters never moved, so at the time this looked like evidence the filter wasn't involved at all — see "Correction" above for why that reasoning was incomplete.
3. Checked `esp_http_server`'s real C source (`httpd_parse.c`): confirms the 400 originates in the parser (`HTTPD_400_BAD_REQUEST` on `nparsed != length`, "incomplete"), below PsychicHttp's application layer.
4. Tried `backlog_conn` (5→10) and `lru_purge_enable` (false→true) — the two `httpd_config_t` connection-admission knobs. Neither changed the outcome.
5. Checked `ENABLE_ASYNC` (`PsychicHttp.h`) — read the actual mechanism (`async_worker.h`/`.cpp`) and confirmed it's narrower than a general concurrency fix (explicit long-running-handler offload, e.g. uploads), not applicable here.
6. Checked Espressif's official `esp_http_server` docs — confirmed the round-robin single-task architecture is real, documented, by-design (not a misconfiguration). Searched PsychicHttp's GitHub issues for this exact symptom — no direct prior report found.
7. **The control test that actually resolved it**: commented out `server.addFilter(admissionFilter)` and `server.addMiddleware(releaseMiddleware)` entirely, rebuilt, reflashed, retested. 9/9 concurrent requests succeeded across 3 rounds — completely clean. Re-enabled: failures returned (6/9 failed across 3 rounds), reproducing identically.
8. Isolated further: hardcoded the heap-scan result instead of calling `heap_caps_get_largest_free_block()`, keeping the rest of the filter/middleware/inflight-cap logic active. 9/9 succeeded. This pinpointed the exact line.
9. Checked whether the current (ESPAsyncWebServer) stack has the same pattern: it does — `largestFreeBlock8Bit()` (`web_server.cpp`) calls the identical function, synchronously, per-request, with no caching. It doesn't cause this failure there, because ESPAsyncWebServer is fully async — one connection's handler cost doesn't stall others' I/O. This is the real, useful finding: **not a PsychicHttp defect, but a genuine port-time sensitivity difference.** Code that's "fine" on the async stack needs re-auditing for per-request execution cost when moved to PsychicHttp's single-task model.
10. **Fix**: moved the heap-floor check to read a cached value (`s_cachedLargestFreeBlock`), refreshed once/second by `backgroundMaintenanceTask()` (the same task that already ran the SSE periodic broadcast) instead of scanning live in the request path. Rebuilt, reflashed, verified: 12/12 concurrent requests succeeded across 4 rounds with the admission filter and inflight-cap fully active.

## What this means for #53's go/no-go (#74)

This is now a *positive* data point about the evaluation process, not a negative one about PsychicHttp: a real bug, found through a real browser load test within about an hour of investigation, root-caused precisely, and fixed with a small, well-understood change (cache instead of scan-per-request). Compare against the current ESPAsyncWebServer stack's comparable history (#22, #67, #68, #61, #62): weeks across many sessions to characterize, vendor patches required, and #61/#62 remain open and unresolved.

The actual, durable finding to carry forward: **PsychicHttp's single round-robin request-processing task means per-request handler/filter/middleware code has a much lower tolerance for synchronous, non-trivial-cost operations than the current async stack does.** Any code ported from `web_server.cpp` into a PsychicHttp adapter needs this specific kind of audit — not just "does it compile and work," but "does it do anything expensive synchronously in the request path" — before being trusted under concurrent load. This is a real migration-effort cost to account for in #74's synthesis, but it is a known, boundable, auditable cost, not an open architectural risk.

## Evidence

- Original failing harness run: `tasks/evidence/webload/run-32-psychichttp/` (`outcome.json`, `browser/network.ndjson`, `status.ndjson`)
- Fix: `src/web/psychic_adapter.cpp` (`s_cachedLargestFreeBlock`, `backgroundMaintenanceTask()`), commit history on `phase/v1.0.0`
- Source citations: `esp_http_server/src/httpd_parse.c` (parser 400), `PsychicHttpServer.cpp:497` (filter-rejection 400, ruled out), `PsychicHttp.h`/`async_worker.h` (`ENABLE_ASYNC`, ruled out as inapplicable), `esp_http_server.h`/official docs (`HTTPD_DEFAULT_CONFIG`, round-robin single-task architecture), `web_server.cpp`'s `largestFreeBlock8Bit()` (current-stack comparison, same live-scan pattern, no failure because it's async)
- Reproduction: direct `curl` parallel-request tests (2026-08-04, this session, reproducible on demand against `env:protoArtoo_psychichttp_prototype`)
