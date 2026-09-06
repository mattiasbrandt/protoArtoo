# Page Load Memory Recovery acceptance envelope

#52 needs one locked pass/fail bar before admission prototypes are judged against it,
so the goalposts don't move mid-implementation. Built entirely from measured evidence
already gathered in #54 (re-verified directly against that issue's comment history and
`platformio.ini`, not from paraphrase), not invented thresholds.

## Scope: production builds only

The envelope applies to production release builds (`artoo_esp32_chirp` and other
sound-backend variants), not `artoo_esp32_profiler`. #54 confirmed the profiler build's
own instrumentation costs real heap (resting `heapLargest8bit` ~11-16K profiler vs.
~18420 production, measured back-to-back on the same hardware) -- it exists to gather
evidence, not to represent what an operator runs, so it is never the pass/fail gate.

## Warmed heap / largest-block range

Four independent production-build resting readings are on record: 18420 (fully idle,
no tabs), 16372 (resting, unchanged before and after two concurrency bursts), 14324
(recovered value after a real 2-tab Browser Load Profile pass), and "~14-15K" /
"~16-18K" narrative characterizations from the same passes. These converge on a real
~14,000-18,500 band that varies legitimately with tab/SSE-client count, not a single
target -- a fixed absolute number would either falsely fail the idle-optimal case or
falsely pass genuine degradation.

Two-part rule:

1. **Relative recovery** (the real signal): after pressure stops and cooldown
   completes, `heapLargest8bit` must return to within +-2000 bytes of that same
   session's own pre-test resting reading.
2. **Absolute hard floor** (backstop regardless of #1): resting must never sit below
   **12,000 bytes** -- comfortably above the tightest existing admission floor
   (`PA_ADMISSION_MIN_LARGEST_FREE_BLOCK_DIAG=7500`), so a session whose own baseline
   had already degraded below that cannot pass merely by returning to itself.

This is provisional on n=4 single-session readings and should tighten as more
production-build passes accumulate.

## Cooldown duration

Normal load / normal refresh: sample `heapLargest8bit` every 2-3s for up to 15s after
the last request completes; pass as soon as two consecutive samples satisfy the
relative-recovery band above. 15s gives ~50% margin over the ~10s actually observed
in every recorded case.

Rapid-refresh + brief 3-tab burst: **no cooldown number is locked**. #54's own attempt
at this scenario is the one where HTTP service did not return within 90+ seconds and
the operator power-cycled before recovery could be confirmed or ruled out (tracked as
#62). This scenario class's cooldown criterion stays explicitly open pending #62's
controlled repeat -- inventing a placeholder number here would look like measured
precision that does not exist.

## Resting request / connection counts

- `inflightRequests`: must return to 0 (or 1 if the check itself is an in-flight
  status poll) **immediately** after load stops -- no cooldown window, since no
  observed lag exists here (unlike heap). Any other nonzero resting value is an
  immediate fail.
- `sseClients`: not a fixed target -- must match the actual count of open tabs with
  live `EventSource` connections (2 tabs open -> resting is 2).
- Cumulative refusal counters (`refusedInflightCap`, `refusedSseCap`,
  `refusedHeapFloor`, `refusedHeapFloorDiag`, `tcpAcceptRejectHeap`,
  `tcpAcceptRejectRate`): never reset, so "resting" means **stop increasing** once
  new load stops, not return to 0. Growth during pressure is expected; continued
  growth after load stops is the fail signal.

## Explicit-refusal behavior

No pass/fail ratio is set between the explicit Busy Recovery Page (ADR 0016) and a
plain connection abort. #54's burst evidence showed most real concurrent pressure
sheds at the pre-HTTP raw TCP guard, before a request ever reaches the admission
middleware where Recovery Capacity would fire -- so under real load, aborts are
expected to be the majority outcome, not a defect.

- The aggregate Browser Load Profile run only requires page/API classes to end up
  either completed or showing the Page Recovery View (busy or no-response) -- not a
  specific split between the two causes.
- Separately, require one dedicated, controlled check (outside the aggregate
  load-profile numbers) that deliberately drives exactly one request into the
  admission middleware while Recovery Capacity is free, proving the explicit
  503 / `Retry-After: 5` / Busy Recovery Page path fires correctly end-to-end.

## Failed-allocation rule

`failedAllocs` must not increase at all, in any scenario class -- flat at its
pre-test value or fail, zero tolerance. Grounded directly in #54's own evidence: the
counter read exactly 0 on fresh flash and only ever grew (to 986) while the #60 defect
was being actively triggered; it is a clean canary for real allocation failure,
distinct from deliberate admission-guard shedding. General soak-testing practice
favors trend/drift-based tolerance over an absolute rule, and some allocators
(`heap_caps_malloc_prefer()`-style multi-region fallback) can produce a "failed"
callback for a benign internal retry -- neither this project's own code nor its
observed behavior show that pattern (grepped clean, and the counter was rock-solid at
0 outside the #60 defect), so zero-tolerance stays the rule. Revisit if a future
dependency introduces a legitimate retry-fallback allocation strategy.

## Stop conditions

Adopting #52's own wording directly: panic, unexpected reset (any reset other than an
intentional OTA reboot), or sustained loss of diagnostics stops a run until the cause
is understood.

"Sustained loss of diagnostics" is made concrete here: **30 seconds of `/api/status`
being unreachable** (connection reset or timeout, not a slow-but-served response)
counts as sustained loss and is a stop condition. This is deliberately tighter than
the 15s heap-cooldown window, since diagnostics being down at all means recovery
can't even be observed. Chosen specifically to be well under the 90+ seconds observed
in the #62 incident -- that incident would clearly have triggered a stop under this
rule, which is a useful retroactive check that the threshold isn't set too loose.

## Scenario classes explicitly left open

- **Rapid-refresh + brief 3-tab burst**: cooldown duration and recovery confirmation
  both stay open pending #62.
- **Mobile Safari WiFi/AP recovery check**: no envelope numbers exist yet for this
  scenario class -- #54 never ran it. Same rules above apply once it is run; no
  separate number is invented here.
- **`artoo_esp32_profiler`-build lifecycle timing** (`/api/profiler`'s request-start /
  response-ready / disconnect trace): still not captured per #54's own final comment.
  Any timing-stage thresholds beyond the heap/cooldown numbers above stay open
  pending that pass.
