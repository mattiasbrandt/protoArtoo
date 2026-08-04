# Response-phase watchdog uses one task-owned deadline seam

Issue #68 set out to deliver an actual response-phase watchdog without another round
of response-state vendor patches or an unsafe cross-task pointer table, by assigning a
deadline to an admitted ordinary non-SSE request and closing an expired client through
one generic deadline primitive owned by the existing async TCP task. The focused
implementation spike that this ADR's stop condition called for found that seam does
not exist in application code: `ESPAsyncWebServer`'s request object claims all six
`AsyncClient` callback slots (`onConnect/onError/onAck/onDisconnect/onTimeout/onData/
onPoll`) in its own constructor, with no getter to save and chain the existing handler,
so there is no unclaimed per-connection extension point left for a deadline check to
attach to without editing that constructor directly (a vendor patch). Separately, the
alternative of a periodic housekeeping task calling `close()` on a borrowed client
confirmed the risk this ADR already flagged as open: `AsyncClient::close()` reads and
mutates the object's own non-atomic state directly on the calling task's stack before
handing only the raw `tcp_close()` off to `tcpip_api_call`, so a foreign task calling
it races the async_tcp task's own concurrent dispatch of queued events for that same
client — not a theoretical race, a same-object concurrent-access hazard verified by
reading `AsyncTCP.cpp`'s `_close()`/`_poll()`/event-dispatch path.

Per the stop condition this ADR set, the requirement moves to the server-library
decision tracked by issue #53 instead of expanding the patch surface or accepting an
unverified cross-task close(). No implementation code was added under #68.

## Status

superseded (2026-08-04): stop condition triggered, no safe no-patch seam found;
requirement carried forward to issue #53

## Considered options

- **Cross-task application table of request/client pointers** — rejected because a
  normal disconnect can delete the borrowed objects before the watchdog closes them.
- **Periodic task calling `close()` on a borrowed client (this ADR's original plan)**
  — rejected on investigation: `AsyncClient::close()` mutates non-atomic object state
  on the calling task before marshaling only the pcb close, racing the async_tcp
  task's own concurrent event dispatch for the same client.
- **Hook a per-connection `AsyncClient` callback from application code (this ADR's
  original plan)** — rejected on investigation: `ESPAsyncWebServer` claims all six
  callback slots in the request constructor with no way to chain an additional
  handler without patching that constructor.
- **More patches inside response internals** — rejected after the #60/#67 stall cascade;
  the response state machine is not the ownership boundary for a universal deadline.
- **No implementation until a perfect proof exists** — rejected because the goal is
  an actual bounded guard; the focused implementation/test slice is the practical gate
  that in fact triggered this ADR's own stop condition.
