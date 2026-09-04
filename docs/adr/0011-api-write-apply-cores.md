# API write Apply Cores: param-source seam, plain-data actions

`POST /api/config` grew into a ~535-line lambda (`api_config.cpp:862-1397`) welding
parameter parsing, validation, cross-field rules, snapshot mutation, runtime
transition detection, persistence, and response building to
`AsyncWebServerRequest`. `POST /api/rc/map` and the `api_audio` write handlers share
the construction. None of that decision logic is reachable by native tests, while
the read path already has the opposite shape (`populateConfigJson`, covered by
`test_api_config_json`). The 2026-07-06 architecture review (issue #18, finding 1)
surfaced this; a grilled design session locked the decisions below.

Every API write handler moves its decision logic behind an **Apply Core**: a pure
module that reads parameters through a **Param Source** — a function-pointer lookup
(`ctx` + `get(name) -> value-or-null`, `include/api_param_source.h`) — validates and
applies them onto a working snapshot, and returns a result carrying a field-level
error (field + message, byte-identical to today's 400 bodies), a bounded
applied-fields record, and plain-data actions (for config: drive-on / dome-on audio
cues, resolved active speed preset). The handler shrinks to an adapter: build the
param source over the request, snapshot live inputs (for example
`robotState.stationary`), call the core, then execute the side effects — cache
apply, mux sync, queue sends, NVS persist, response via the existing builders. The
core does not log; the shell replays the per-field `[CFG]` lines from the applied
record, the same purity discipline as ADR 0002's `configLoad` /
`configDeserialize` split. In tests the param source is a static name->value table
(the `MapReader` precedent) and the actions are asserted as plain data (the ADR
0005 house style).

Cores live in the web family, mirroring `api_config_snapshot.h`:
`api_config_apply`, `api_rc_map_apply`, `api_audio_apply`
(`include/*.h` + `src/web/*.cpp`, added to the native `build_src_filter`). The
campaign covers `POST /api/config`, then `POST /api/rc/map`, then the `api_audio`
write handlers endpoint-family by endpoint-family — all on `phase/v1.0.0` in
behavior-preserving slices, each slice verified and committed before the next.
Each core gets a representative native matrix (~15-20 cases: every cross-field
rule, transition actions, one reject per parser type, applied-record and
no-fields cases) rather than an exhaustive per-message pin; byte-identical 400
bodies hold by moving the strings verbatim.

Accepted risk, recorded deliberately: issue #16's audio-lifecycle changes are
software-verified with the hardware pass still pending, and the audio wave of this
campaign refactors the same surface before that pass runs. The owner accepted this
in the design session; the mitigation is the behavior-preserving pin plus the
existing native gate, so the hardware session still verifies the behavior that was
software-verified even though the code underneath has moved.

## Status

accepted (2026-07-06 design session; decision tree grilled against issue #18
finding 1, scope expanded from the finding's config-only recommendation to the
full write-handler campaign by owner decision)

## Considered options

- **Pre-copied params struct** — the handler copies every parameter into a fixed
  struct of value-string pointers and the core takes only the struct. Rejected:
  the ~25-pointer struct must grow in lockstep with every new field; the
  function-pointer source gives the same purity with no lockstep structure, and
  the test double stays a simple table.

- **Per-domain apply functions** (`applyDriveFields` / `applyDomeFields` / ...) —
  rejected: the cross-field rules (speed-preset uniqueness, `speedLimitMax` <->
  preset derivation and resolution) span domains, so the split forces shared
  mutable state between the pieces and multiplies the interface without hiding
  more behind it.

- **Parse/validate-only core** — extract validation but leave the runtime tail
  (transition detection, cue queueing, preset folding) verbatim in the handler.
  Rejected: that tail is exactly the glue that was untestable; returning plain-data
  actions makes it assertable in native tests with no fakes.

- **Exhaustive 400-message pin** (assert all 37 config bodies) — rejected per the
  repo's risk-based verification policy: the strings move verbatim, so the
  representative matrix covers the decision logic without encoding every string as
  a test row.

- **Config-only scope, or deferring the audio wave until after #16's hardware
  pass** — both were the review's recommendation and both were declined by the
  owner in the design session; the full campaign proceeds now on `phase/v1.0.0`
  with the #16 interaction recorded above.

## Amendment (2026-07-07, via ADR 0012)

ADR 0012's Commanded Mode zone owns the stationary release rule:
`ConfigApplyActions` drops `playDriveOnCue`, and the config shell instead calls
`commandedSetStationary()`, which detects the release edge and queues the drive-on
cue at the state write. `playDomeOnCue` remains a core action — that transition is
config-derived, not state-derived. Whichever lands second of Apply Core slice 1
and the `commanded_modes` module adopts the setter call in the config shell.

## Amendment (2026-07-08): compliance criterion; POST /api/seq dissolved

The campaign's completion test is a criterion, not a mechanism: a write handler
is compliant when its decision logic is natively reachable. An Apply Core is
the standard means, but an existing pure decomposition satisfies the campaign
without one, and handlers with no decision logic (estop, reboot, seq/stop and
peers) are compliant as thin adapters.

Judged against that criterion, `POST /api/seq` needs no Apply Core. The
2026-07-08 architecture review proposed one (working name SeqApplyCore); the
grilled design session dissolved the finding. The handler is already a ten-line
adapter over `seqStoreSave`, whose decisions sit behind pure natively-tested
modules — `seq_json`, `protocol_check`, `seq_store_util`, `seq_store_index`
(ADR 0006's decomposition). The untested residue is side-effect glue (LittleFS
temp+rename, store mutex, error-string mapping), exactly what this ADR leaves
in shells; an extracted core would be a shallow one-caller module.

The same session extracted the file's one genuinely untested decision:
`DELETE /api/seq`'s dangling RC-trigger report is now the pure
`seq_dangling_bindings` module (Factory-shadow suppression rule included), and
the 11-slot trigger enumeration it shares with the RC input task moved to the
single `rcTriggerSlotsCopy` helper next to the slot fields in `config_store.h`.

## Amended 2026-08-27

Recorded after the grilling session that charted the Controller Console (epic
#206, ADR 0034). The core stays pure and the Param Source stays the boundary;
what changes is who owns the side effects. "The HTTP handler is its adapter and
owns every side effect (state sync, queues, persistence, response)" held while
there was one adapter. With a browser console and a serial terminal sharing
every write path, a handler-owned side-effect sequence would have to be copied
into the second adapter - a second implementation of correctness. Each Apply
Core therefore gains a **Commit Step kept beside it**: the complete
transport-independent operation (validate, apply, synchronize runtime state,
persist where required, emit the canonical log and result effects), returning
a plain outcome. The HTTP handler and the Console adapter only translate input
into that contract and render its structured result; the handler keeps its
byte-identical responses. Extraction happens per path before the Console uses
it (T10/T11 on #206), and `persistSystemConfig(WebRequest&, ...)`, which sends
its own HTTP error today, is the first candidate.

## Amended 2026-09-04

Recorded after the grilling session on the two decisions #226 and #268 left
standing (epic #206). Three things were being protected as "the Apply Core
contract" that this ADR never pinned, and one thing this ADR does pin was
unenforced.

**What the contract is.** The response bytes and the plain-data outcome. How
the Working Snapshot crosses the seam is an implementation choice, and the
identity and audio Commit Steps already take it by pointer. `ConfigCommitOutcome`
carried a whole `ConfigSnapshot` (944 B, measured on the xtensa compiler) under a
comment that called it small; that single by-value crossing plus two stack
copies below it put 6000 B of project frames on the serial config-write path and
overflowed the Console task on both chips (#226). The stack raise that fixed the
panic was the correct first move and is not the resting state.

Decided:

- The config Commit Step returns `{ persisted }` and writes its post-commit
  snapshot back through `working`, matching `identitySetCommitApplied` and the
  audio Commit Steps. The REST handler renders `working`. Byte-identity is proven
  the way the extraction proved it: the field-level tests plus a captured
  byte-diff of the `POST /api/config` body before and after.
- **The Commit Step owns serialization of persistence.** One config write lock
  beside `configCommitApplied`, held across apply, commit and persist, taken by
  every adapter. Until now only the Console module locked, so a dashboard form
  POST and a serial Console write could interleave and the loser's fields were
  silently reverted before NVS - a race that did not exist before this epic
  added the second writer. An adapter never locks the seam; the Console's own
  mutex narrows to its static result or goes. A take that cannot acquire within
  its bound reports unavailable: `temporarily-unavailable` on the Console and a
  new busy response on REST, which is additive and leaves every existing
  response byte-identical.
- Commanded Mode setters sync the config cache by field, never by a
  whole-snapshot round trip. `commandedSetStationary` read all 944 B, set one
  bool and wrote all 944 B back - on the SBUS path, the httpd task and the
  Console alike - and marked the RC mapping dirty on every toggle although
  stationary is not in the RC processor config.

Considered and rejected:

- Hoisting `working` and the outcome into the Console module's static area:
  1892 B of .bss against 1644 B of artoo-esp32 static budget at the time, and it
  would not have covered the REST path.
- Leaving REST unserialized as outside #206's scope: the acceptance row reads
  "browser and serial cannot race", but the second writer is the epic's own, and
  a lock held in one adapter is a copy of correctness that the 2026-08-27
  amendment above forbids.
- A locked read-modify-write API on the config cache: serializes every writer
  including the Commanded Mode setters, but touches every caller and is more
  than the defect needs.

Consequences:

- One #206 ticket, atomic commits: write-back plus the Commit Step lock; the
  field-level setter with its own red/green on the SBUS stationary toggle; then
  both Console chains re-measured and `CONSOLE_TASK_STACK_BYTES` re-derived by
  the #248 rule. Row 226 of both bench sheets replayed on both boards.
- `sizeof(ConfigSnapshot)` gets a `static_assert` beside the struct, and the two
  comments that disagreed with it (744 B in the serializer, "small" on the
  outcome) are corrected in the same slice.
