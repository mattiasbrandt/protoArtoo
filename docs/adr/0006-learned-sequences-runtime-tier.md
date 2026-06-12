# Learned Sequences: a runtime tier above the Factory catalog

Issue #2 slices 3-5 add **Learned Sequences** -- DM:* choreographies defined as JSON
files on the controller filesystem, created and edited without reflashing, validated by
**Protocol Check**, and executed by the unchanged slice-2 coordinator engine. This ADR
records the architecture decided in the 2026-06-11 grill session, which extends (does not
revise) [ADR 0004](0004-body-centric-dm-sequence-coordinator.md). The slice-2 engine's
serializable-ready step model (ADR 0004 decision 2) is realized here: the JSON format maps
1:1 onto `SeqStep`/`SeqStepParams`, so the engine *is* the interpreter.

## Status

accepted

## Context

ADR 0004 locked the body as **Catalog Authority** and shipped Factory Sequences as C++
tables. The follow-up scope (a web sequence editor and community import) requires sequences
to be runtime-defined and serializable. Rather than a parallel interpreter, the JSON format
deserializes into the same `SeqStep` arrays the engine already runs. Budgets confirmed
viable: LittleFS ~209 KB free; app flash ~121 KB free; DRAM ~45 KB heap free.

## Decisions

1. **Runtime tier, not an engine fork.** A Learned Sequence parses into the existing
   `SequenceEntry`/`SeqStep` model and runs through the existing engine. The only engine
   change is four `TOGGLE_USER1..4` latch values for non-shadowing Learned toggles.
   The values are reserved for now: the engine's branch-pick/latch switches are not
   wired for them yet, so Protocol Check rejects `user1..4` on save until that lands
   (otherwise such a toggle would run open-branch-only and never latch).

2. **Runtime-first lookup precedence.** `sequenceLookup()` resolves
   **runtime -> catalog -> alias -> fallback**. A Learned Sequence bearing a Factory name
   (a **Retrained Sequence**) shadows the factory one on every trigger path; deleting it
   (**Memory Wipe**) resurfaces the factory entry. Catalog Authority holds -- the body
   still owns the namespace; the catalog simply gains a runtime layer above the compiled one.

3. **Layered store: pure index + firmware I/O.** The queryable name index
   (`seq_store_index`) is a pure in-memory table so the routing precedence stays
   native-testable. The LittleFS scan/load/save/delete (`seq_store`) is firmware-only and
   guards every index mutation together with its file operation under a mutex.

4. **Load-on-demand into static run buffers, staged two-phase.** At sequence start the
   dispatcher first *prepares* the selected file (parse + Protocol Check into a transient
   heap pair), and only commits it into the static run buffers after the previous run is
   drained — so a load that fails (corrupt file, concurrent Memory Wipe) never costs the
   running sequence. **Copy semantics:** the running sequence is a parsed copy, so a later
   save/delete never disturbs it (a wipe mid-run finishes from RAM). Two 96-step run
   buffers (~17.6 KB BSS) let the engine run toggle close branches unchanged
   (correct-by-construction); save validation uses the same transient-heap discipline.

5. **Protocol Check validates the parsed staging model, not raw JSON.** It runs one branch
   at a time (a single 96-step scratch suffices), enforcing name `DM:[A-Z0-9_]{1,18}`,
   retrain coherence, `suppressMs` 1000..120000 and >= end time, command whitelist with
   parsed bounds, loop/random structure, and `<=96` steps with an explicit `STEP_END`.

6. **Effect-class inference, not authoring.** Format v1 has no `fx` field. Protocol Check
   infers the cleanup class from each command (`:SM` pulse>800 -> panel; non-reset
   `@*T/P/M` -> logic/PSI; `@HP*`/`*` -> holo; audio -> audio; random -> panel) and stamps
   `effectClass`, so the engine's auto-reset fires correctly. An author *cannot* express a
   sequence that skips cleanup; the worst case is an idempotent over-reset. Factory C++
   tables keep explicit hand-tuned tags (the PR-reviewed expert surface) and are exempt
   from Protocol Check's meta rules.

7. **One namespace, capacity-bounded.** Guild and user files share `/seq/`; `meta.source`
   distinguishes them. Enforced on save: **16 files max**, **12 KB per file**, and a
   **24 KB LittleFS free-space floor**. Writes are temp-file + rename.

8. **Full toggle parity with retrain constraints.** `toggleGroup: none|pies|low|all|user1..4`.
   Retraining a factory toggle requires the identical group; retraining a factory non-toggle
   requires `none` (keeps OPENALL's latch-carry coherent). All latch-clear rules
   (`:CL00` / estop-clear / dome-reconnect resync) extend unchanged.

9. **Estop, suppression, and auto-reset stay engine-level invariants.** The JSON format
   cannot express a bypass: suppression is the coordinator's `domeSeqActive` hold, estop
   always aborts the cursor, and cleanup is inferred, not authored.

## Rejected alternatives

- **A second interpreter for runtime sequences (rejected).** Doubles the execution surface
  and the test burden; the serializable step model exists precisely to avoid this.
- **NVS storage (rejected).** ~20 KB total is too small for the file budget; LittleFS has
  ~209 KB free and supports temp-file + rename.
- **An `fx` field in the format (rejected).** Lets an author forget cleanup and leak a
  persistent effect. Inference is leak-proof.
- **Per-slot release commands replacing `:CL00` (deferred).** The group-release rule from
  ADR 0004 (a closed group's servos stay powered while another group is open) carries over
  unchanged; a dome-side per-group release remains a hardware-session follow-up.

## Consequences

- `sequenceLookup()` gains `SEQ_RUNTIME`; `sequenceStart()` routes it like `SEQ_CATALOG`.
- `isValidDomeSeqPayload()` (RC binding) accepts runtime-indexed names; a deleted Learned
  Sequence leaves the binding valid but inert (falls through to dome fallback).
  `DELETE /api/seq` reports such bindings in a `danglingBindings` response field and the
  inert trigger logs a warning when fired, so the no-op is never silent.
- `check-action-drift` is unaffected -- runtime names are data, not registry actions.
- The slice-3 hardware gate joins the v1.0.0 closure checklist: an editor-authored Learned
  Sequence, RC-triggered, with estop-abort honored, proven on the integrated droid.
- Factory `DM:ROCKMARCH` authors `STEP_END` 300 ms past its suppress window; a cloned copy
  is rejected on save until the user raises `suppressMs`. A later slice-2 touch-up may align
  the factory value.
