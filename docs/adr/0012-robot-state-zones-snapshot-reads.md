# RobotState zones, Commanded Modes, and Zone Snapshot reads

`include/robot_state.h` declares the widest interface in the codebase: ~87 fields
under one `portMUX`, touched by 27 files. Issue #18 finding 2: there is no
multi-field snapshot API — the header's own `recordFailsafeTriggerLocked` writes
five failsafe fields together while `drive`, `safety`, `validation_snapshot`, and
`web_server` read them back in separate critical sections, so multi-field
consistency holds only by convention. The 2026-07-07 design session locked the
decisions below. `robot_state.h` remains the shared-state truth (AGENTS.md); this
deepens its interface, it does not replace the struct.

**State Zones.** Fields physically regroup into commented zone blocks, each naming
its owning writer: drive output + hoverboard feedback (DriveTask), RC input
including the SBUS2 dome-receiver failsafe flags (RcInputTask — ground truth from
the survey: `failsafe_gate` owns only the drive-side flags `estop`,
`sbusSignalLost`, `sbusHwFailsafe`, `webDriveExpired` plus the failsafe
diagnostics), dome link (DomeLinkTask, with `dome_rx_parser` writing as its
parser), audio, servo, aux LED, and sequence dispatcher. Owners keep direct field
writes. `queueOverflowCount` stays a shared telemetry counter. `seqStopRequested`
and `rcConfigDirty` remain documented handshake flags (set by one side, cleared by
the other). The reorder is behavior-invisible: all access is by field name.

**Commanded Modes.** `stationary`, `sleepMode`/`sleepSinceMs`, `activeMood`,
`webControlEnabled`, and `rcDebugMode` are commanded from multiple surfaces by
design (RC bindings, web pages, dome cues, boot init). These fields are written
only through setter helpers in a new `commanded_modes` module. The existing
api_drive-local `setStationaryModeWithSound()` already is the pattern and gets
promoted: `commandedSetStationary(v, source)` performs the mux write, detects the
release edge, syncs the config cache (deliberately cache-only, not NVS — the live
toggle semantics), and queues the drive-on cue. The inline copies in `rc_input`
and the `POST /api/config` handler are deleted when their slices land.
**Amendment to ADR 0011:** `ConfigApplyActions` drops `playDriveOnCue` — the rule
is state-derived and lives at the state write; the config-derived `playDomeOnCue`
stays a core action. Whichever lands second of Apply Core slice 1 and the
`commanded_modes` module adopts the setter call in the config shell.

**Zone Snapshots.** Zone-shaped snapshot structs are the canonical multi-field
read, starting with `FailsafeDiagnostics`. Each zone gets
`copy<Zone>Locked(out)` (no mux — the caller holds it) and `capture<Zone>(out)`
(takes the mux). Consumer captures (`ValidationSnapshot`, the `api_status`
builders) compose several zone copies inside a single critical section, keeping
whole-response atomicity while the types teach ownership. The existing
single-field accessors (`isEstopActive`, `getDriveSpeed`, ...) remain.

**Delivery.** Behavior-preserving slices on `phase/v1.0.0`, queued behind Apply
Core slice 1 (ADR 0011): Z1 header zone reorder + `FailsafeDiagnostics` + its four
reader conversions; Z2 `commanded_modes` promotion (deletes the three stationary
duplicates); Z3 sleep/mood/web-control setters; Z4 on-demand consumer
compositions. Verification per slice is the existing native suite plus the
firmware build; no new dedicated suite — at most 2-3 `commanded_modes` edge cases
if the existing stub layer accommodates `audioQueuePlaySlot` without new
machinery.

## Status

accepted (2026-07-07 design session; decision tree grilled against issue #18
finding 2)

## Considered options

- **Route mode commands through owning tasks via queues** — rejected: queue
  latency and queue-full handling for one-word writes; the dome sleep cue would
  need a new path; heavy machinery where setter helpers suffice.

- **Comments-only zoning (no reorder, no setters)** — rejected: the header keeps
  teaching the mixed layout, and the transition rules stay duplicated — the
  stationary release rule exists three times today (`api_drive`, the config POST
  handler, `rc_input`), which is exactly the defect class being closed.

- **Write helpers for every zone** — rejected: churns every task file to wrap
  single-writer fields whose ownership was never ambiguous.

- **Consumer-shaped snapshots as the canonical unit** (the pre-existing
  `ValidationSnapshot` / `RcDiagnosticsSnapshot` convention) — rejected as
  canonical: each page gets fixed but within-zone reads elsewhere stay ad hoc.
  Zone structs plus single-critical-section composition give both consumer
  atomicity and typed ownership; existing consumer snapshots convert to
  composition opportunistically.

- **One global `captureStateSnapshot`** — rejected: couples every reader to one
  struct version and copies ~200 bytes where four fields are needed; zone
  semantics lost.

- **Dedicated suites for copies and the reorder** — rejected per the repo's
  risk-based verification policy: both are behavior-invisible and covered by the
  existing native gate and firmware builds.
