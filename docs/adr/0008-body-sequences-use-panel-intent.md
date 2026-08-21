# Body sequences use dome panel intent commands

Issue #2 keeps the body as the owner of `DM:*` sequence timelines, trigger routing,
and Learned Sequence override precedence, but body-authored sequences no longer use
raw `:SM` servo-slot pulse commands for panel choreography. Panel movement in Factory
and Learned body sequences uses high-level dome panel intent commands (`:OP`, `:CL`,
`:OF`) so the dome can apply its calibrated logical panel behavior.

## Status

accepted

## Supersedes

This ADR supersedes the parts of [ADR 0004](0004-body-centric-dm-sequence-coordinator.md)
and [ADR 0006](0006-learned-sequences-runtime-tier.md) that treat `:SM` slot/pulse
commands as the normal panel authoring primitive for body-owned sequences. The body
still owns the sequence timeline and catalog authority; it does not own raw panel
servo pulse authority.

## Decision

Body-authored panel motion uses these command families only:

- `:OP<target>` opens a logical panel or group.
- `:CL<target>` closes a logical panel or group.
- `:OF<target>` runs a one-shot flutter effect on a logical panel or group.

Allowed ring targets are `01`, `02`, `03`, `04`, `07`, `11`, and `13`. Allowed group
targets are `00` for all panels, `14` for pie/top panels, and `15` for ring/bottom
panels. Pie/top panels use explicit aliases (`P1`..`P6`) such as `:OPP1`,
`:CLP1`, and `:OFP1`; body-authored sequences must not treat numeric `08`..`13`
as a pie-panel range because the Marcduino compatibility mapping mixes pie and ring
identities.

`:SM` remains valid only for deliberate one-off diagnostic/manual command paths such
as `POST /api/dome/cmd` or a serial diagnostic console. It must not be stored anywhere
that can replay automatically: Learned Sequence JSON, Factory sequence tables, clone
JSON, editor raw sequence steps, RC command bindings, and other saved actions reject it.

Non-panel raw dome commands remain available for advanced authoring. `@...` logic/PSI
commands and `*...` holo commands stay valid. `DM:*` is rejected inside sequence steps
because it is a sequence trigger, not a step primitive. `:SE##` stays available only as
an advanced legacy Marcduino compatibility step.

The earlier idea of forwarding Factory `DM:*` names to the dome by default is superseded.
That path was useful while isolating the hardware failure, but it does not meet the product
goal of body-owned tuning and retraining. Factory and Learned timelines remain body-owned;
the dome owns calibrated panel execution.

## Learned Sequence validation

Protocol Check validates panel intent structurally:

- `:OP` infers `FX_PANEL`; terminal and abort cleanup may close panels through the
  engine cleanup path.
- `:CL` is a stable close command.
- `:OF` is an effect-only flutter command and must have an explicit later cleanup
  in the same branch. Valid cleanup is the same target close, the matching group
  close, or `:CL00`.
- `:SE##` does not satisfy panel cleanup and is not treated as harmless. It should
  infer a conservative dome-sequence effect class, preferably `FX_DOME_SEQUENCE` or
  otherwise the combined panel, logic/PSI, and holo cleanup classes. It is allowed only
  as a direct authored advanced step, not inside loops or random generators.

Engine terminal/abort cleanup is a safety net, not sufficient to satisfy the `:OF`
authoring rule.

Learned Sequence JSON v1 remains command-string based (`{ "type": "dome", "cmd":
":OP01" }`). The web editor should offer structured panel controls on top of this
storage model and generate the command string; a structured panel JSON format is deferred
until command strings limit validation, tooling, or editor behavior.

Existing Learned or Retrained Sequence files on LittleFS are preserved but must be
revalidated under the new Protocol Check before execution. A runtime sequence that fails
validation is refused and reported clearly; it must not silently fall through to the
Factory sequence of the same name, because that would hide that an operator's retrained
sequence is not running. The file remains available for repair, export, or explicit
deletion.

## Random panel behavior

`STEP_RANDOM` no longer selects servo slots or pulse ranges. Random panel behavior
selects from whitelisted logical targets and emits only intent commands:

- `flutter`: choose a target and emit `:OF<target>`.
- `open_close`: choose a target, emit `:OP<target>`, hold, then emit `:CL<target>`.
- `open_only` / `close_only`: allowed only when the branch has explicit cleanup.

Random partial-open pulse ranges are intentionally lost from body-authored sequences.
If calibrated partial-open behavior is needed later, add a dome-owned calibrated
command instead of restoring `:SM`.

Factory sequences that previously depended on partial-open pulse values are
intent-adapted rather than pulse-faithfully ported. The rewrite preserves rough duration,
major event order, broad rhythm/density, and operator-recognizable feel; it does not
preserve exact partial-open percentages, pulse values, easing curves, or random drift
positions. Tests should assert the new command contract and broad timing shape rather
than old `:SM` payloads.

## Toggle state

Body toggle latches remain operator-behavior state, not physical servo-state proof.
They are tracked by logical group:

- `piesOpen` is the last commanded logical state for group `14`.
- `ringOpen` is the last commanded logical state for group `15`.
- `DM:OPENALL` derives all-open as `piesOpen && ringOpen`; it has no separate latch.

Group `:OP` / `:CL` commands update the matching latch. `:OP00` / `:CL00` update both.
Individual open commands do not claim the whole group is open; individual close commands
may invalidate the matching group-open latch. Body boot, estop recovery, dome reconnect
resync, explicit `:CL00`, and sequence abort cleanup that closes all panels reset both
latches closed.

Retrained Sequences keep the existing runtime-first lookup behavior. A retrained toggle
Factory name must declare the same toggle group metadata as the Factory sequence so the
operator-facing toggle action keeps its meaning.

## Consequences

Factory and Learned sequence docs, Protocol Check, the web editor, tests, and migration
guidance must stop teaching `:SM` as an authoring primitive. The sequence editor should
make panel movement a structured intent control and reserve raw dome command entry for
advanced non-panel commands. Community sequence migration becomes choreography-intent
adaptation rather than servo pulse translation; migrations that depend on exact calibrated
partial-open or easing behavior are deferred until the dome exposes a calibrated command
for that behavior.

The rewrite should land in three slices: first Protocol Check/model safety so no new
unsafe Learned JSON can be saved, then runtime/catalog conversion to panel intent, then
editor and documentation updates that make the safe model the normal authoring path.
