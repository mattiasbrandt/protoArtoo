# Body-centric DM:* sequence coordinator

The body controller becomes the owner of `DM:*` sequence choreography. It holds the
sequence catalog (which sequences exist, their names, routing, and timing) and runs a
non-blocking timing cursor that dispatches each step -- sound, dome rotation, body and
dome panel motion, dome light/logic/PSI effects -- to the correct effector at the correct
time. The dome becomes a command executor for the steps routed to it. This is an optional
architecture improvement, not a bug fix: the dome already runs all 17 `DM:*` sequences
correctly as non-blocking ReelTwo animations (`DM:LOW`, `DM:ROCKMARCH` hardware-verified
on AstroPixelsPlus `main` @ `c388690`). The coordinator must therefore earn its complexity
against a working baseline -- its payoff is one clock, exact sound-to-motion sync, a single
edit point for panel/audio/rotation timing, and a body-owned sequence catalog.

This ADR records the architecture decided in the 2026-05-30 grill session on issue #2,
which closed the pre-implementation gate.

## Status

accepted

Superseded in part by [ADR 0008](0008-body-sequences-use-panel-intent.md): the body
still owns the DM:* sequence timeline and routing, but body-authored panel movement now
uses dome panel intent commands (`:OP`, `:CL`, `:OF`) instead of raw `:SM` servo-slot
pulse commands.

## Scope and boundary (catalog authority vs execution)

- The body owns the `DM:*` namespace and definitions (**catalog authority**). One place
  defines which sequences exist and where each step routes.
- Execution stays **split**. The body executes sound, dome rotation, and body panels;
  the dome executes its own panels, holos, logics, and PSI. The body coordinates *when*
  a dome effect fires; the dome owns the effect itself.
- **Non-goal:** the body does not become the frame-by-frame driver of the dome's LED/holo
  engine. Hard constraint behind this: the slip-ring link is 9600 baud 8N1 (~960 B/s).
  Panel choreography is cheap (CANTINA peak ~13 `:SM` per 923 ms beat, ~180 B/s); per-frame
  LED/holo animation is not (a single ~30 fps holo fade would consume ~1/3 of the link),
  so it stays dome-local.

## Decisions

1. **Full scope, flat-first delivery.** The task covers all 17 `DM:*` and all four pattern
   classes (flat, toggle, loop, random). The first slice ships flat sequences only
   (VADER, HELLO, and one new sound-synced sequence); loop (CANTINA/ROCKMARCH) and random
   (SCREAM/OVERLOAD) follow.
2. **Typed, data-driven, serializable-ready step model.** A POD step struct tagged by
   step type (`STEP_FLAT` now; `STEP_LOOP`/`STEP_RANDOM` designed-in). Statically
   preallocated; no per-step/per-sequence heap. The representation must map cleanly to a
   future serial format so a runtime/web sequence editor is not precluded.
3. **Dedicated `SequenceTask` on Core 0.** The coordinator sits *above* `DomeTask` in the
   call graph and dispatches only through existing non-blocking queues (`audioQueueDollar`,
   `domeQueueTx`, the dome rotation queue, the servo queue). Core 0 keeps the 50 Hz safety
   loops on Core 1 unburdened and leaves the runtime-loaded-sequence path open (Core 1's
   no-heap rule would otherwise constrain it).
4. **Preempt concurrency.** A new `DM:*` cancels and restarts: abort the active sequence
   with minimal safety cleanup (reset persistent logic/PSI, release the suppression
   window; skip cosmetic close-all), then start the new one. Estop always aborts.
5. **Coordinator-owned suppression window.** A `sequenceActive` hold flag in shared state
   replaces the dome's old `seqon`/`seqoff`. The random dome-rotation state machine and the
   random-audio timer consult it and pause initiating new idle actions; neither's configured
   mode is mutated. Aligns with the existing `domeSeqActive` gate in `DomeTask`.
6. **Track-name authoring resolved via `AudioNamedTracks`.** Sequence sound steps reference
   a config-backed named role from the existing `AudioNamedTracks` namespace, resolved at
   dispatch through the same instance the rest of the system uses, so a sequence's sound
   follows the operator's configured track number. Marcduino `$NNN`/`$`-letter dialect stays
   valid at command boundaries for interop. The role set extends as needed; full catalog
   enumeration is deferred.
7. **Coordinator-tracked auto-reset cleanup.** Steps that set persistent dome state carry an
   effect-class tag (`LOGIC_PSI`, `PANEL_GROUP_OPEN`). The coordinator records active classes
   and auto-emits the matching reset (`@0T1`/`@0P1`, `:CL00`) on terminal transitions,
   emitting only what was actually activated.
8. **Body-authoritative latched panel state.** Toggle sequences (PIES/LOW/OPENALL) latch:
   per-group open/closed state persists across sequences (press = open, press again = close).
   The dome has no panel-state query, so the body tracks assumed state. Latched groups are
   closed only on estop-clear and on body-boot/dome-reconnect resync (assume closed, send
   `:CL00`). A transient open inside a non-toggle sequence still auto-closes at its end.
9. **Estop = freeze, resync on clear.** On estop: abort the cursor, withhold all new panel/
   rotation actuation (the dome motor is already forced neutral by `DomeTask`; the
   coordinator withholds panel TX). On estop-clear: resync to a known safe state
   (`:CL00`, `@0T1`/`@0P1`). Honors estop-means-stop and defers actuation to re-enable.
10. **Single interception choke point with dome fallback.** All `DM:*` entry points (RC,
    web API, internal) call one `sequenceStart(name, source)`. Names present in the body
    catalog dispatch in the body and are *not* forwarded raw (no double-execution); names
    not yet implemented fall through to today's raw forward to the dome, which already runs
    them non-blocking. `:SE##`/`$NNN` aliases keep forwarding directly. This makes migration
    a safe per-sequence flip and keeps preempt/suppression/cleanup logic in one place.

## Considered options (where the choice was non-obvious)

- **Fold the cursor into `DomeTask` (rejected).** Tightest rotation co-location and fewest
  tasks, but it is a layering inversion (a body-wide coordinator living inside the dome
  motor loop), binds the dispatcher to Core 1's no-heap rule -- which fights the
  serializable/runtime-sequence goal -- and risks jitter on the 50 Hz safety loop when a
  step fans out many `:SM` commands. The only gain was one ~20 ms queue hop on rotation
  dispatch, negligible next to 9600-baud serial latency. Chose a dedicated Core 0 task.
- **Queue or ignore concurrent `DM:*` (rejected).** Queueing replays stale choreography up
  to ~47 s after the press; ignore-while-busy silently eats operator input. Preempt matches
  the dome's existing model and feels responsive.
- **A second sound-name table in the action registry (rejected).** Creates a second source
  of truth to keep in sync with `AudioNamedTracks` and operator config (drift risk). Reusing
  `AudioNamedTracks` keeps one resolution layer.
- **Fixed global teardown / declarative per-sequence teardown (rejected).** Global teardown
  is heavy-handed and can stomp an incoming preempting sequence; declarative teardown leaks
  persistent effects when an author forgets a reset. Coordinator-tracked auto-reset is
  leak-proof and minimal.
- **Big-bang body-only cutover (rejected).** Requires all 17 sequences ported before the
  path switches and loses the working dome behavior for any not-yet-ported sequence. The
  dome-fallback choke point delivers incrementally instead.

## Consequences

- The existing RC path that copies `DM:NAME` into `domeTxCmd` and forwards it raw must route
  through `sequenceStart()` for catalog-implemented names.
- The dome retains its `DM:*` implementation as the fallback target and for autonomy; the
  body must not double-dispatch (forward + decompose) for the same sequence.
- Deferred and unchanged from the issue: multiline logic text uses single-line first; easing
  uses linear `:SM` first pass; every panel step must carry an explicit `target`
  (`TARGET_DOME_CMD` vs `TARGET_BODY_SERVO`).
- A max sequence-duration safety bound (cursor must complete or be abortable; longest known
  sequence ~47 s) should be an acceptance criterion.
