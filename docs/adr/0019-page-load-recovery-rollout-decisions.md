# Page Load Recovery rollout decisions (issue #59)

With the wire contract (ADR 0016), acceptance envelope (ADR 0017), and early-admission
feasibility (ADR 0018) already locked, and the Common Page Bootstrap state model
validated by the #55 prototype, #59 needed five remaining decisions to make
implementation independently grabbable. The full synthesized architecture and handoff
lives in `docs/page-load-recovery-architecture.md`; this ADR records only the new
decisions themselves and why.

## One concurrent request slot, not two

`data/web_api.js` currently allows `MAX_CONCURRENT_REQUESTS = 2` via a plain FIFO queue.
The #55 prototype used a single active-request slot to isolate the priority/ordering
logic and left "1 or 2 for real" open. Decision: **narrow to one**. #54's own evidence
showed that even 2-3 concurrent requests is exactly the load that trips the heap-floor
and inflight-cap admission checks on this board -- the two-request concurrency was
contributing to the pressure this epic exists to reduce, for payloads too small for
parallelism to meaningfully help. Matches CONTEXT.md's existing Bounded Page Attempt
wording ("a visible tab has at most one active attempt"). `/api/events` stays outside
this accounting; it is long-lived, not part of the per-request slot machinery.

## Page rollout order

Ten pages exist; `wifi.html` is fixed first by #52 itself as the tracer. The remaining
nine tie in pairs on script-chain size, so risk (not just size) orders them:
`firmware` -> `sound` -> `servo` -> `dome` -> `setup` -> `rc` -> `drive` -> `seq` ->
`index`. Cheap, non-safety pages absorb early mistakes in the newly-generalized
bootstrap; `rc`/`drive` (live vehicle control) wait until the pattern is proven
elsewhere; the two heaviest, highest-traffic pages (`seq`, `index`) go last regardless,
since they have the most to lose from an unproven bootstrap.

## Two Operation Deadline categories, not one

Checked every page for a real, already-established longer-than-default timeout rather
than inventing one. Found exactly one: `data/sound.js`'s `loadCatalog()` already uses
`timeoutMs: 12000` for `GET /api/audio/catalog` (double the 6000ms default), consistent
with prior evidence that the CHIRP catalog load is genuinely CPU/memory-heavier than an
ordinary request. No other endpoint (checked `seq.js`, the second-heaviest page) has a
custom timeout. Decision: lock **exactly two** categories -- Ordinary (6000ms, the
default for everything) and the catalog-specific 12000ms -- rather than a single
category or an invented general-purpose "long" category. `firmware.html`'s OTA/
filesystem-upload flow is exempted from both entirely: it already has its own bespoke
progress-and-reconnect mechanism (`fsProgressBar`, `otaProgress`, `waitForReconnect`)
predating this epic, which already satisfies #52's "visible progress" requirement on
its own terms -- it is not a deadline-governed flow at all.

## Cross-page generalization gate: fixture checks every slice, live-hardware envelope only at milestones

Running the full live-hardware ADR 0017 envelope (multi-tab, cooldown waits, heap
sampling) for each of the nine remaining page slices individually would mean nine
separate live-hardware sessions -- repeated soak-adjacent hardware time this project
has explicitly moved away from this session. Decision: **every** page slice requires
only the deterministic browser fault-injection checks (per #55's fixture seam --
busy/no-response/resource-fail/section-fail/hide-show/retry/deadline-cancel/
command-priority), no live hardware needed. The full live-hardware envelope run is
reserved for three checkpoints: the WiFi tracer (already required), the *first*
non-tracer page slice (proves generalization didn't silently break memory behavior),
and the *final* page (`index.html`) as the epic's true close-out.

## Stop/rollback rules

- A page slice's fault-injection checks fail: that page's migration stays unmerged;
  do not advance to the next page in the rollout order; re-diagnose via the same
  fixture checks, not live-hardware iteration, before retrying.
- A milestone live-hardware envelope check fails even though that page's own
  fault-injection checks passed: treat it as a signal the *generalization itself*, not
  just one page, may be systemically wrong. Pause the entire rollout until root-caused
  -- do not migrate further pages on top of a possibly-broken foundation.
- Standing exclusions carried through the whole rollout: no page may claim the
  rapid-refresh+3-tab scenario class as passing (ADR 0017 already leaves this open
  pending #62); the bootstrap's `liveUpdatesStarted` gating logic may generalize
  page-by-page as designed, but the underlying `/api/events` reconnect behavior it
  depends on is not "proven" beyond what is already true today until #61 lands.
