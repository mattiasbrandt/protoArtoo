# The response-phase deadline lives in a session send override, because that is the only place on this stack that can see every byte

ADR 0020 set out to bound the response phase and stopped, because
`ESPAsyncWebServer` offered no unclaimed per-connection extension point and
`AsyncClient::close()` could not be called safely from another task. It carried
the requirement to the server-library decision (#53). PsychicHttp is that
decision's outcome, so this records where the guard actually lands on
`esp_http_server`, what the new stack allows, and what it still does not.

## What the new stack allows, read from the pinned sources

`framework-espidf` ESP-IDF 5.5.2 and PsychicHttp 3.1.2:

- `httpd_sess_trigger_close()` queues the close through `httpd_queue_work()`
  onto the server's control socket (`httpd_sess.c`), and the httpd task
  processes it from its own `select()` loop (`httpd_main.c`,
  `httpd_main_ctx_process()`). A foreign task can therefore ask for a close, but
  cannot make one happen while the server task is inside a handler.
- There is no primitive at all -- public or private -- that lets a foreign task
  interrupt a send the server task is blocked in. `httpd_sess_set_send_override`
  and `httpd_sess_set_recv_override` exist; nothing symmetric to them does.
- `send_wait_timeout` is 5 s by default and reaches the socket as `SO_SNDTIMEO`
  in `httpd_accept_conn()` (`httpd_main.c`). It is the only bound on a single
  write out of the box.
- Every response byte goes through the session's send function.
  `httpd_send_all()` calls it in a loop for partial writes (`httpd_txrx.c`), and
  `httpd_socket_send()` dispatches to it as well. A negative return aborts the
  response with `ESP_FAIL`.
- `sess_ctx` / `free_ctx` are **already occupied**: `PsychicRequest.cpp:20-28`
  stores PsychicHttp's own `SessionData` in them. The seam exposes them
  (ADR 0021) and they are real, but they are not free for this.

## The decision

The deadline is enforced **in-band, from a session send override** installed on
every admitted socket at Connection Admission, alongside the `open_fn` guard
ADR 0022 put there. `PsychicHttpServer` installs no send override on the plain
HTTP server -- only its HTTPS sibling does, through `esp_https_server` -- so
nothing is displaced, and there is no getter that would let one be chained if
something were.

Placing it there is what makes the guard universal. The seam's own
`sendChunked()` loop could have carried a check between chunks, but it would
have covered only the routes that use it: not `PsychicResponse::send()`, and not
`PsychicStaticFileHandler`, which serves every asset of a page load through the
library's own chunk loop. The send override sees all three without a line of
vendor code changing.

The write itself is issued non-blocking and retried on a 5 ms delay -- the
pattern `sendEventBounded()` already proves for the event stream. That is what
makes the deadline mean anything: with a single blocking attempt, the socket's
5 s send timeout would decide the granularity, and a 4 s deadline could not fire
before 5 s had passed. Lowering `send_wait_timeout` instead was rejected:
`httpd_send_all()` treats any negative return as fatal without retrying, so a
short socket timeout would abort legitimately slow clients rather than stalled
ones. The guard now depends on `SO_SNDTIMEO` for nothing.

## One record, not a table, and why that is not a shortcut

`esp_http_server` services every connection from one round-robin task, and
ADR 0022 rejected the library's optional per-request worker threads. At most one
request can therefore be in its response phase at any instant, and the deadline
state is a single record: the armed socket, the phase start, and two flags.

The armed descriptor is compared on every call rather than assumed. That is what
makes an event-stream broadcast -- written from the event task, against a socket
this record does not own -- pass through untouched, and it is what would make
the omission visible if `ENABLE_ASYNC` were ever turned on: the check would
start refusing writes for the wrong request instead of silently guarding the
wrong one. Turning those workers on requires this to become a per-socket table.

That is enforced rather than merely written down: defining `ENABLE_ASYNC` fails
the build. The failure mode it prevents is invisible at runtime -- the guard
would go on publishing counters while holding the wrong request's deadline --
so a comment would not have been protection.

## The clock starts at the first byte, not at admission

A handler that spends time building a body has not entered its response phase,
and an upload spends nearly all of its time receiving one. Charging either to
the deadline would make the firmware upload path the thing most likely to trip
it -- the exact opposite of what the guard is for. The clock starts on the first
write of the phase, and that first write always proceeds: a response cannot be
late before it has begun.

The breach latches for the remainder of the phase. A single refusal is not
enough, because the failure is returned into a vendor send path that this
module cannot promise will not attempt one more write. Arming the next request
clears the latch -- on a keep-alive connection (ADR 0023) the request after a
stalled one arrives on the same socket, and it is innocent.

## The breach path, and why the slot cannot leak

A breach returns `HTTPD_SOCK_ERR_FAIL`, which `httpd_send_all()` turns into
`ESP_FAIL`, which travels out through PsychicHttp's response call and back to
the request middleware. The in-flight slot is held by an RAII guard in that
middleware, so it is released by the same unwinding that carries the error --
the slot cannot outlive the request that took it, whether the response ended,
failed, or breached. This is the failure ADR 0020 named explicitly: a deadline
that fires and leaves the slot occupied makes the cap worse, not better.

The socket is dropped with `linger{on, 0}` before
`httpd_sess_trigger_close()`, for the same reason the event stream eviction
does it: a client that stalled a response has a full send queue, and a graceful
close leaves lwIP holding that queue while it retransmits into a peer that has
stopped answering -- measured on this controller as a depressed largest free
block for over a minute. The close is queued rather than immediate because it is
issued from inside the handler whose response is being abandoned.

## What is measured, and what the value is set from

Four fields join `/api/status`, and the harness run evidence:

| field | meaning |
|---|---|
| `responseDeadlineClosures` | responses dropped by the deadline, cumulative |
| `responseDeadlineAgeMs` | age of the last one; `-1` until one fires |
| `responseLastMs` | duration of the most recent completed response phase |
| `responseMaxMs` | longest completed response phase this boot |

`responseMaxMs` is the margin evidence, and it excludes breached phases by
construction: a deliberately stalled client must not be able to raise the very
maximum the deadline is measured against. It is published continuously rather
than captured in one calibration session, because whether the deadline still
clears the slowest legitimate response is a standing property of the system --
the same reason ADR 0022 publishes `acceptGuardMaxUs` rather than quoting a
past measurement. A new maximum also logs the route that set it, which is what
a calibration run needs and what a bare number cannot supply.

`PA_RESPONSE_DEADLINE_MS` lives in `platformio.ini [flags_base]` beside
`PA_SSE_SEND_DEADLINE_MS`, the stream-side deadline it is the request-side
counterpart to. `0` disables the guard.

## What a stall costs the other connections, measured

The deadline bounds head-of-line blocking; it does not remove it. On a stack
that serves every connection from one task, a stalled response holds that task
for as long as it lives, and what the deadline changes is how long that is.

Measured on the seated controller with `PA_RESPONSE_DEADLINE_MS=4000`, polling
`/api/status` once per second on a separate connection through three deliberate
stalls (`tools/response_deadline_probe.py`):

| reading | value |
|---|---|
| median poll latency | 121 ms |
| worst poll latency | 4202 ms |
| slow polls | exactly one per breach |
| `heapLargest8bit` before / after | 34804 / 34804 |

The worst figure is the deadline plus the breach path, and it lands on one poll
per stall; every other poll is unaffected. That is the honest form of "normal
traffic on other connections is unaffected" on this stack: bounded and
attributable, rather than the unbounded hold it replaces, where each write could
block for the socket's 5 s timeout and nothing limited the number of writes.

The heap figures matter as much as the latency. ADR-era measurement of the event
stream found that closing a socket whose send queue is full leaves lwIP holding
the pcb and the whole queue, depressing the largest free block for over a
minute. The `linger{on, 0}` abort means that does not happen here: the largest
free block returns to its pre-stall value after every breach.

## What this does not do

It does not bound a handler that stalls without writing anything. The clock
starts at the first byte by design, so a handler stuck before its response --
in a filesystem read, or a queue it never gets an answer from -- is outside this
guard entirely. On a single-task server that failure is just as fatal, and it is
a different watchdog's problem.

It does not bound the request phase. A client that opens a connection and sends
its headers a byte at a time is held off by `recv_wait_timeout`, which is
unchanged.

It does not give the guard any authority from another task. If the server task
is blocked inside a handler, nothing here shortens that; what the design does is
ensure the server task's own writes cannot be the thing blocking it.

## Open: the client does not observe the disconnect

Server-side reclaim is proven -- the socket leaves the census, the in-flight
slot is released, and the heap returns to its pre-stall value. The stalled
*client*, however, saw its own TCP connection stay ESTABLISHED for the whole
17 s the probe waited, on all three breaches.

The two observations do not yet agree, and the reason is not recorded here
because it is not yet known. A FIN cannot be delivered into a zero window --
it is sequenced behind body bytes the client never reads -- which would explain
a client that notices nothing; but the heap returning implies lwIP did abort
rather than close gracefully, and an abort's RST is not window-limited. What
this means in practice is bounded: the controller has released everything it
holds, so the consequence falls on the stalled peer's own socket rather than on
the controller. It is recorded as open rather than explained away.

## Status

accepted (2026-08-06), implemented. Server-side behaviour verified on the
controller; the client-side observation above is open.

## Relationship to ADR 0020

ADR 0020 is superseded by this decision. Its stop condition was correct and its
rejections still hold for the stack it was written against; the requirement it
carried forward is discharged here. The mechanism differs from everything
ADR 0020 considered -- it considered no send override, because
`ESPAsyncWebServer` has no equivalent -- which is why this is a new decision
rather than an update to that one.

## Validation

`controller-upload-verified`, partial. The firmware was flashed to the ESP32
controller over OTA and every number above is a device reading: the calibration
sweep (198 requests, 0 closures, slowest legitimate phase 184 ms) and the stall
probe (3 breaches, 3 closures, in-flight slot released, heap recovered).

No integrated droid hardware was involved, and nothing here touches the drive,
RC or dome paths. What is not proven: the client-side disconnect above, and
behaviour under genuine network degradation -- the margin argument for a slow
link is reasoning about what the LAN measurement cannot bound, not a
measurement of it.
