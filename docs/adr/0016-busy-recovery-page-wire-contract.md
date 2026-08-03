# Busy and no-response recovery wire contract

Every admission refusal in `web_server.cpp`'s middleware (`refusedInflightCap`,
`refusedSseCap`, `refusedHeapFloor`, `refusedHeapFloorDiag`) currently calls
`request->abort()` — a raw TCP abort with zero bytes. That was the only option proven
safe under the pressure it guards against: a normal `AsyncWebServerResponse`, even a
bare 503, unconditionally allocates a `Connection` header via a `std::list` node, a
proven crash site under this exact heap pressure. The consequence is that the browser
currently cannot tell an explicit refusal apart from a genuinely unreachable
controller — both look identical on the wire. #52 requires that distinction
(Page Recovery Status: "Controller busy" only after an explicit Immediate Request
Refusal, "No response from controller" otherwise).

Decision: emit the Busy Recovery Page as one `static constexpr` byte buffer — HTTP
status line, headers, and HTML body concatenated at compile time — written directly to
the request's raw `AsyncClient` via one `write()` call, bypassing
`AsyncWebServerResponse` entirely. Content-Length is therefore always correct by
construction (it is the same literal the bytes were built from), which also rules out
by design the class of bug fixed in #60. Reuse the identical buffer for every resource
class (page documents, static assets, APIs, uploads, `/api/events`) rather than building
per-class variants: callers that expect JSON (`data/web_api.js`) only branch on HTTP
status and `content-type`, never parse this body, so one HTML body serves every case.
Estop and the pre-HTTP-parse raw TCP accept guard are unaffected — the former bypasses
admission entirely, the latter runs before a request/route object exists and has no
response to build regardless.

Recovery Capacity is exactly one reserved slot for the whole controller, not one per
resource class. All four existing refusal call sites attempt to claim it through one
shared `tryBusyResponse()` helper before falling back to today's `abort()`; if the slot
is already occupied, they abort exactly as before. The slot is released on the same
disconnect-completion boundary already used for ordinary admitted requests, not on
`write()` return, since AsyncTCP sends are asynchronous.

The response carries `Retry-After: 5` (RFC 9110's standard mechanism for this — chosen
over the generic 30-120s web-service convention because it is grounded in this board's
own measured recovery time: #54's Browser Load Profile evidence showed heap pressure
recovering within ~10s in every observed case, not the minutes-long overload that
30-120s targets). Browsers do not auto-honor `Retry-After`, so the static page's inline
`<script>` bakes in a matching literal countdown from the same source constant used to
build the header, and calls `location.reload()` on expiry or immediately on **Retry
now**. `data/web_api.js` should read `response.headers.get("Retry-After")` for API-class
callers so the Common Page Bootstrap gets a real controller-given retry time instead of
guessing.

## Consequences

- The reused-buffer choice means the Busy Recovery Page's HTML is never rendered for
  non-navigation callers — that's fine, since none of them display response bodies, but
  it means this page's HTML must not be the only place safety-relevant information is
  conveyed; status must still come from `response.status`.
- Because there is only one Recovery Capacity slot, only one client at a time receives
  the busy response even if refusals are simultaneous; everyone else still gets a plain
  abort. This is deliberate (#52: "no more than one recovery response at a time"), not an
  oversight.
- SSE reconnect behavior after receiving this response is out of scope here — see #61 for
  the existing stuck-reconnect defect it may help surface more clearly (a distinguishable
  503 instead of an ambiguous reset), but fixing that reconnect logic is #61's job, not
  this contract's.
