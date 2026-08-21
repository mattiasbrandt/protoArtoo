# Connection admission on esp_http_server is two layers, and only the upper one can see the URL

Removing AsyncTCP removes the patched `tcp_accept` guard that ADR 0017 names as
where most pressure sheds. This records how the equivalent is built on
`esp_http_server`, and what honestly changes in the process. It is the
esp_http_server counterpart to ADR 0018, which answered the same question for
the pinned ESPAsyncWebServer stack and supersedes nothing here.

## Two layers, because one cannot do both jobs

**Connection Admission** runs from the server's socket-open callback, before a
single HTTP byte is parsed. **Request admission** runs from a global middleware
once the request head has been read, ahead of route matching and ahead of the
static-file open that ADR 0018 identified as the costly pre-admission step.

The split is forced, not stylistic. At the socket layer there is a file
descriptor and nothing else: no method, no URL, no headers. Every policy that
needs to distinguish one request from another -- the estop bypass, the lower
floor for read-only diagnostics, the exemption of the long-lived event stream
from the in-flight cap -- can only exist at the upper layer.

Both were needed here regardless: the PsychicHttp build had no admission
control of any kind. The prototype's version was inert outside its own build
flag, so the upper layer is a port, not an invention.

## Where the socket layer hooks, and why it is not a vendor patch

`PsychicHttpServer::config` is documented upstream as an ESP-IDF
`httpd_config` struct, configured in the window before `begin()`. The guard
installs its own `open_fn` there. Returning non-`ESP_OK` makes
`httpd_sess_new()` delete the session and the accept loop `close()` the socket
(`httpd_sess.c:216-224`, `httpd_main.c:129-137`).

Two details are load-bearing and come from reading the vendor source rather
than its documentation. The PsychicHttp constructor has already pointed
`open_fn` at its own callback, so the guard must capture and chain to it on the
admit path -- dropping it would leave admitted connections with no client
record. And on the reject path it must *not* chain, which is what makes
rejection allocate nothing: no client object is ever constructed. That matters
precisely when there is no heap to construct one from.

`httpd_sess_delete()` still calls `close_fn`, so the library's close callback
runs for a socket the guard refused. It tolerates the missing client and closes
the descriptor, so this leaks nothing.

## Rejection closes the socket rather than answering

The upper layer rejects by returning non-`ESP_OK` from the middleware, which
`esp_http_server` answers by closing the connection (`httpd_uri.c:347-350`).
This is deliberate parity with the async stack's `request->abort()`, chosen
there because building a 503 response allocates a header-list node on a path
that was the proven abort site of the burst crashes. Answering costs heap at
the exact moment the policy exists because there is none.

The consequence is operator-visible and is now named in `CONTEXT.md`: a
connection refused before HTTP carries no response, no reason and no
`Retry-After`, so a browser cannot distinguish it from an unreachable
controller and must report "No response from controller" rather than
"Controller busy". Dressing a rejected main-frame navigation in the Busy
Recovery Page (ADR 0016) replaces this return; the wire contract for that is a
separate decision.

## The estop bypass is preserved at parity, not absolutely

The safety path is exempt at the request layer: never rejected, never counted
against the cap it is exempt from. At the socket layer it cannot be exempt,
because the URL does not exist yet.

This is not a regression. The async `tcp_accept` guard was equally URL-blind;
the estop bypass only ever lived in the HTTP middleware. Parity is therefore
the honest bar, and claiming more would be a safety claim neither stack has
ever kept. Two alternatives were considered and rejected: reserving socket
headroom does not help, because the reserve is consumed by whichever connection
arrives first regardless of what it wants; and `lru_purge_enable` inverts the
design so that nothing sheds at all, while evicting the least-recently-used
socket would take the operator's own telemetry stream first.

The real estop guarantee remains the physical estop and the latching failsafe.

## The heap sample is cached, and that is the whole cost story

`heap_caps_get_largest_free_block()` walks the heap. On this stack the
socket-open callback runs on the single task that also services every other
connection, so charging every connection for a walk puts it directly in the
path of the requests already in flight. The prototype reproduced exactly that
failure: a live per-request heap scan failed 1 of 2 genuinely concurrent
requests with a parser-level HTTP 400, every time.

The sample is therefore refreshed at most once per 100 ms and shared by both
layers, which converts a per-connection charge into a bounded rate. The
decision core takes the sample through a callback and calls it only after the
rate check passes, so a connection being paced out never triggers a walk at
all.

Measured on the seated controller:

| condition | guard cost |
|---|---|
| cached sample, no walk | ~46 us |
| walk fires (typical) | ~250-300 us |
| worst observed, during a burst | 4929 us |

The worst figure is wall-clock and therefore includes any preemption of the
server task, not just the walk. It is published continuously as
`acceptGuardLastUs` / `acceptGuardMaxUs` rather than measured once, because
whether the guard is affordable on a single-task stack is a standing property
of the system, not a one-off result.

The accepted trade-off is staleness: within a 100 ms window the value can be
pessimistic, shedding connections briefly after heap has already recovered.
The accept pacing bounds how many connections that can affect.

That trade-off was initially suspected of causing the shedding observed under
burst load, on the theory that one pessimistic sample could shed a whole
window. Measurement refuted it, which is why the guard also publishes the
reading that caused each refusal (`acceptRejectLargestBlock`) and the lowest
reading it has ever taken (`acceptMinLargestBlockSeen`). Under a six-connection
burst the largest free block genuinely fell to **7412 bytes** -- below the 8500
accept floor -- and the refusal was taken on that real value, not a stale one.
A refusal count alone could not have distinguished the two, and the difference
decides whether the floor or the sampling interval is the thing to argue about.
Nothing was retuned on this evidence; recording it is what #52's out-of-scope
rule requires before anyone may.

## The counters keep their old names on purpose

The C++ symbols are project-owned and outlive the vendor patch that defined
their predecessors. The JSON field names do not change:
`tcpAcceptRejectHeap`, `tcpAcceptRejectRate` and `tcpAcceptRejectAgeMs` are a
comparability contract with the load harness and the recorded baseline
scorecard, not a description of which implementation produced them. Renaming
them would turn the migration's comparison run into a hand-diffing exercise.

## What the measurement revealed about the in-flight cap

Under 6 concurrent connections across 4 rounds, `inflightRequestsPeak` never
exceeded 1 and no request was ever refused by the cap or by the request-layer
floor. Every rejection happened at the socket layer.

That is a structural consequence of `esp_http_server` servicing connections
from one round-robin task: requests do not overlap at the HTTP layer, so a cap
of 6 concurrent in-flight requests cannot be reached. The cap is kept anyway --
retuning is out of scope, the rejection point it provides is what the recovery
page needs, and enabling the library's optional per-request worker threads
would make requests overlap again. But the protection that actually sheds load
on this stack is the socket layer, and the in-flight cap should not be read as
evidence of anything until that changes.

## Rejected for now: per-request worker threads

`PsychicHttp.h` carries a commented-out `ENABLE_ASYNC`, with live code paths
behind it, that gives each request its own thread on ESP-IDF 5.1.x. It is
absent from the upstream README, which lists worker-based multithreading under
long-term wants.

It would relieve the single-task constraint this decision works around. It is
not taken here because it changes the concurrency model underneath the
migration's comparison gate, whose value is that it varies one thing at a time
against the recorded baseline; because each worker's task stack comes from the
heap this whole effort is short of; and because it is an undocumented build
flag in a library whose documentation was found to lag its source. It remains
the first thing to reach for if the comparison run fails on concurrency rather
than on memory.
