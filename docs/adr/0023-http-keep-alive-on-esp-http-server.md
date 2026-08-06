# Keep-alive stays, because the connection churn it removes is the pressure the guards were built to survive

`platformio.ini` documented a deliberate reliance on ESPAsyncWebServer stamping
`Connection: close` on every response, and built a whole lwIP timer
configuration on top of that assumption. The stack changed underneath it. This
records what the new stack actually does, what was measured against the
alternative, and what follows for the socket budget.

## What the new stack does

Read from the pinned sources -- `framework-espidf` 3.50502 and
`framework-arduinoespressif32-libs`, both ESP-IDF 5.5.2, and PsychicHttp 3.1.2:

- `esp_http_server` never emits `Connection: close`, and offers no per-response
  opt-out. HTTP/1.1 persistence is unconditional.
- PsychicHttp adds none either. Its only `Connection` header is the
  `keep-alive` on the event stream (`PsychicEventSource.cpp:60`).
- `httpd_config_t`'s `keep_alive_enable` / `keep_alive_idle` /
  `keep_alive_interval` / `keep_alive_count` are **TCP** keepalive probes.
  `httpd_main.c:98-123` maps them straight onto `SO_KEEPALIVE`, `TCP_KEEPIDLE`,
  `TCP_KEEPINTVL` and `TCP_KEEPCNT`. They say nothing about HTTP connection
  reuse, and reading them as an HTTP setting is the obvious trap here.
- The accept loop's `select()` takes no timeout (`httpd_main.c:290`), so an idle
  session lives until the client closes it or LRU purge takes it. Production
  leaves `lru_purge_enable` false; ADR 0022 rejected enabling it.

Confirmed on the controller rather than left as a reading. Three sequential
requests on one socket were all answered, and no response carried a `Connection`
header at all. A request that itself asked for `Connection: close` was answered
and the socket was **still** held open -- the server ignores the client's
request-side close as well.

## What was measured

Two firmwares differing in one flag. The default keeps the stack's own
behaviour; `PA_WEB_CLOSE_PER_RESPONSE` stages `Connection: close` from the
request middleware and queues `httpd_sess_trigger_close()` once the response has
flushed, exempting the event stream. Each was flashed by OTA and driven through
the same Chrome page loads of `index.html`, with counters read over a separate
single-request socket so the probe's own cost is a known constant.

The counters are `httpSocketsAccepted`, `httpSocketsOpen`, `httpSocketsOpenPeak`
and `httpRequestsServed`, added for this decision. Their ratio is the
measurement: requests per connection is what connection reuse means.

| | keep-alive (default) | close per response |
|---|---|---|
| 3 sequential requests on one socket | 3 of 3 answered | 1 of 3; server hung up |
| `Connection` response header | absent | `close` |
| cold load of `index.html` | 4 sockets, 24 requests -- **6.0 req/conn** | 25 sockets, 25 requests -- **1.00 req/conn** |
| refresh | 1 socket, 25 requests -- **25 req/conn** | 24 sockets, 24 requests -- **1.00 req/conn** |
| peak socket occupancy, one tab | 4 | 2 |
| lowest largest-free-block seen | 7412 | **3188** |
| connections shed over load + refresh | 2 heap, 0 rate | 3 heap, 6 rate |
| failed resources, cold load | 2 (one retry recovered) | 4 (three retries, `/api/logs` failed outright) |

## The decision, and why the loser lost on its own terms

Keep-alive stays, unchanged and unconfigured.

The close-per-response arm was not merely worse on connection count, which is
the number it was always going to lose. It was worse on the thing the whole
admission layer exists to protect. Its lowest observed largest-free-block was
**3188 bytes**, less than half the keep-alive arm's 7412 and far under both the
8500 accept floor and the 9000 request floor. Six connections were paced out by
the rate limiter, and the page needed three retries plus lost `/api/logs`
entirely.

That is the argument in the old `platformio.ini` note, confirmed and turned
around: connection churn is the pressure. Each connection costs a pcb and its
buffers, and 25 of them inside one page load is a heap event, not an
accounting detail. The previous stack's short lwIP recycle timers existed to
survive exactly that churn. The new stack does not produce it.

The old note is therefore not merely stale, it had the causality backwards for
the current stack: it treated `Connection: close` as the constraint to design
around, when on `esp_http_server` closing per response is the thing that
manufactures the pressure.

## Consequence for the socket budget

The ticket required `max_open_sockets` be re-examined if keep-alive was kept,
since connections now live longer. It was, and **10 stands**.

Peak occupancy was 4 with one tab and still 4 with two tabs open
(`index.html` plus `rc.html`), against a budget of 10. Steady state settled at
3: one event stream per tab plus one pooled request socket.

The reason the second tab cost nothing is that occupancy here is dominated by
the event streams, not by pooled request sockets. A browser's per-origin
connection pool is shared across tabs of the same origin and its idle members
are reclaimed by the browser, whereas each tab holds exactly one durable
`/api/events` stream. Occupancy therefore grows with **tabs**, roughly one
socket each, not with the six-way parallelism of a page load. Against 10, that
leaves real headroom.

This is what "connections living longer" turned out to mean in practice, and it
is the opposite of the worry: the long-lived sockets were already there -- the
event streams -- and keep-alive did not add a second durable class beside them.

## The short lwIP timers stay, and the reason is now a different one

`CONFIG_LWIP_TCP_MSL=5000` and `CONFIG_LWIP_TCP_FIN_WAIT_TIMEOUT=5000` were
introduced to drain the churn residue of a stack that closed every response.
That churn is gone, so the original justification is spent.

They are kept anyway, on a narrower one: connections still close -- tabs are
closed, streams are evicted, the admission guard sheds -- and on a LAN there is
no reason for a closed connection to hold its buffers for a 60 second MSL. The
setting is now ordinary LAN hygiene rather than a load-bearing mitigation.
Removing it was not measured and is not claimed either way.

`CONFIG_LWIP_SO_LINGER=y` is untouched and unrelated: it serves the event
stream's abrupt eviction (`include/web_event_stream.h`), which this decision
does not reach.

## What is not claimed

The comparison is one browser, ordinary loads and refreshes of one page, on a
warm controller. It is not a burst test, and it does not re-score the page-load
recovery envelope -- that is ADR 0017's measurement and belongs to its own gate.
What is established is the direction and its size, which is what choosing
between the two configurations required.

`ENABLE_ASYNC` per-request worker threads remain out of scope; ADR 0022 records
why, and nothing measured here changes it.

## Validation

`controller-upload-verified`. Both firmwares were flashed to the ESP32
controller over OTA and measured there; every number above is a device reading.
No integrated droid hardware was involved, and none of this touches the drive,
RC or dome paths.
