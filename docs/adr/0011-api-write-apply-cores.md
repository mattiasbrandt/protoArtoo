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
