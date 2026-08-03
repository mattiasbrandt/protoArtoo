# Early admission seam is already proven feasible without a fragile patch

#57 asked whether the pinned ESPAsyncWebServer/AsyncTCP stack can admit or refuse
route-aware work before the costly paths the current global middleware
(`server.addMiddleware(...)`, `src/web/web_server.cpp`) reaches too late, without
resorting to a broad/fragile vendor patch.

Traced the vendor library's actual request lifecycle
(`.pio/libdeps/*/ESPAsyncWebServer/src/WebRequest.cpp`, `WebServer.cpp`,
`WebHandlers.cpp`): method/URL become readable in `_parseReqHead()`
(`WebRequest.cpp:318-329`), then the library immediately calls
`_attachHandler()` (`WebRequest.cpp:993`), which loops every registered
handler's `canHandle()` (`WebServer.cpp:145-152`) *before* `addMiddleware`'s
chain runs (`WebRequest.cpp:1015`). For the static-file handler specifically,
`canHandle()` is not cheap: it calls `_searchFile()`, which opens the file (and its
`.gz` variant) directly from LittleFS (`WebHandlers.cpp:167-180`) — real filesystem
I/O and allocation, done before our own admission middleware ever runs. Body-buffer
allocation, by contrast, is deferred until after the middleware chain
(`WebRequest.cpp:1006-1007`), so it is already protected by the existing checks.

**This exact gap was already closed once, for exactly this reason, before this
session.** `tools/patch_async_sse.py`'s "Issue #21, round 4" patch
(`STATIC_OPEN_GUARD_BEFORE`/`AFTER`, lines ~629-679) already guards the real
`_fs.open()` seam inside `_searchFile()` with a heap-floor check
(`ASYNC_STATIC_MIN_LARGEST_FREE_BLOCK`, `9000` in `platformio.ini`) that calls
`request->abort()` and skips the open entirely when the floor isn't cleared. It
ships in the current production build, not a hypothetical. The vendor library
provides no earlier documented hook between URL/method parsing and
`_attachHandler()` — reaching one would mean patching `_parseLine()`'s core
state machine itself, which is the broad/fragile outcome #57 explicitly treats as
a no-go. That broader patch is not needed: the one costly pre-middleware step
(static-file open) already has its own small, targeted, anchor-based guard, in the
same low-risk style already accepted for the #60 fix.

## The one remaining gap

The existing guard only checks the heap floor. It does not check
`s_inflightRequests`/`kMaxInflightRequests` or the SSE client cap — those remain
main-middleware-only checks that still run *after* a static file has already been
opened. Practically: under heap pressure the file never opens (already handled);
under a request-count/SSE-cap-only squeeze with heap still healthy, a static file
can still be opened and then immediately rejected by the main middleware,
wasting the file-handle/read-buffer cost that was avoidable.

Closing this cleanly is a small extension of the same guard, not a new seam — but
it has one real added cost worth flagging honestly: `s_inflightRequests` and the
SSE client count are `static` (internal-linkage) variables in `web_server.cpp`,
a different translation unit than the vendor-patched `WebHandlers.cpp`. Reaching
them from the patch requires a small `extern`-accessor seam across that boundary
(e.g. a tiny header declaring a C-linkage getter), not just a macro constant like
the existing heap-floor check. Still small and targeted, not broad/fragile, but
not literally free either.

## Disposition

No vendor-replacement work or broad patch is needed to answer #57's question —
verdict is a clean **yes, feasible, already proven**. The inflight/SSE-cap gap on
the static path is real but small; closing it is a follow-up implementation slice
(extend the existing `STATIC_OPEN_GUARD` seam plus a small cross-TU accessor), not
part of this feasibility answer, and should wait until ADR 0016's Busy Recovery
Page mechanism actually exists in code (it is design-only as of ADR 0016/#58) if
the goal is to also let that guard return the explicit busy response instead of a
plain abort.
