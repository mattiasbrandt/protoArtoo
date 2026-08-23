# The live update stream is project-owned and evicts a stalled client rather than queueing for it

`/api/events` is the only long-lived response this controller serves, and it is
the one an operator needs most when something is wrong. Both web stacks this
project has run ship an EventSource class, and both carry the same defect:
`PsychicEventSourceClient::sendEvent()` retries `httpd_socket_send()` in a
`while (result == HTTPD_SOCK_ERR_TIMEOUT)` loop with no exit, against a socket
whose send timeout defaults to five seconds. One client that stops reading fills
its receive window and the broadcaster stops broadcasting — a diagnostics
blackout for every viewer, at exactly the moment diagnostics matter.

We decided the live update stream is **owned by this project, not by the web
stack**, and that a client which cannot keep up is **dropped, not buffered for**.

The decision core is `include/web_event_stream.h` plus
`src/web/web_event_stream.cpp`: a fixed-capacity socket registry, a compile-time
stream head, three-part framing that never concatenates, and
`webEventSendDecide()` — a pure function returning continue / evict-on-deadline
/ evict-on-error. It names no vendor type, no Arduino type and no clock; elapsed
time and write outcomes arrive as parameters, so `test_web_event_stream` pins
the behavior on the host without a socket. Each build supplies the two transport
functions at the bottom of the header (`webEventStreamClientCount()`,
`webEventStreamBroadcast()`) next to that build's request seam implementation
(ADR 0021).

Eviction beats queueing here because of who the stalled client is. A bounded
queue would protect other viewers from one bad one — but under this project's
single-operator profile the stalled client **is** the operator. Dropping the
socket lets `data/status_stream.js`'s existing exponential backoff reconnect
them to a live stream in seconds, where a queue would only postpone the same
blackout and hold heap while doing it. The socket is dropped with
`linger{on, 0}`; ADR 0024 later adopted the same close for the same measured
reason — a graceful close on a full send queue leaves lwIP holding the pcb and
depressing the largest free block for over a minute.

The deadline applies to attempts that made progress as well as to blocked ones.
A client accepting a handful of bytes per attempt stalls the broadcaster just as
effectively as one accepting none, and "it is still moving" is precisely the
excuse that lets a slow reader hold the stream open indefinitely.

## Considered options

- **Use the stack's EventSource class** (`PsychicEventSource`, and
  `AsyncEventSource` before it) — the unbounded send above, plus a
  `std::string` and an Arduino `String` copy per client per event on a stack
  whose whole problem is heap. Rejected. This is the option a reader is most
  likely to assume was taken, which is why this ADR exists.
- **A bounded per-client queue** — protects other viewers, but delays rather
  than prevents the blackout for the single operator who is the one stalling.
  Rejected on the operator profile, not on cost.
- **Rapid polling instead of a long-lived connection** — explicitly ruled out
  by the `Live Page Updates` term in `CONTEXT.md`. Rejected.

## Consequences

- Any build that serves `/api/events` must define the two transport functions
  itself. A build that reaches for the vendor EventSource class instead is not
  serving this project's stream, however identical the endpoint looks — a
  distinction that matters for bench and gate firmware, which measure the
  transport and can otherwise measure the wrong one (#184).
- Eviction is rare by design, which is why `g_webSseEvicted` and
  `g_webSseEvictLastMs` exist: a run that never triggers it must stay
  distinguishable from one where it silently stopped working.
- The concurrent-stream cap (`PA_ADMISSION_MAX_SSE_CLIENTS`, 3) and the send
  deadline (`PA_SSE_SEND_DEADLINE_MS`, calibrated in `platformio.ini`) are the
  two numbers this design is tuned by; ADR 0017 scores `sseClients` directly,
  so the published count must be streams actually open.
