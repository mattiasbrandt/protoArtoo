# The RC Map is checked by one set of rules, on save and on read

Status: accepted (2026-10-09). Settled by grilling the operator after the
architecture review of the #389/#483 hot spot.

## Context

Every **RC Map** rule #389 and #483 added lives in three to six places. "The
drive reads SBUS1" is refused by the save (`api_rc_map_apply.cpp`), ignored by
the mapper at runtime (`rc_channel_mapper.cpp`), checked again for the boot hold
(`rc_input_processor.cpp`), hidden by the RC page (`driveOnSbus2`) and recognised
in the droid's refusal by a regex on its English text. Fixing that one rule took
three commits. Which **RC Receiver** a receiver type reads is written out in the
mapper and twice more, byte for byte, in the RC and validation snapshots. The dead
zone has two different checks: the save requires travel on both sides of the
centre, while a stored calibration needs only a dead zone narrower than the whole
stick. The RC page decides "pressed" for a bound **RC Channel** at 300 from 992;
the droid decides it from that binding's calibration, so the two can disagree.

## Decision

**One firmware module owns every RC Map rule, and the droid applies the same rules
when it saves an RC Map and when it reads a stored one.**

- **Every rule.** The drive reads SBUS1. Speed and Steer sit on one RC Receiver.
  Each receiver type reads only its own receivers. The dead zone leaves stick
  travel on both sides of the centre. The 11-trigger ceiling, the RC Channel range
  of each receiver, the end/centre/end order, CH17 and CH18 being on/off, and which
  RC Channels an action may sit on. The save, the mapper, the input processor and
  both snapshots ask this module; none of them keeps its own copy.
- **Same rules on read.** A binding in a stored RC Map that a save would refuse
  is not read: that binding stays still, and `GET /api/rc` names it and says why,
  in the same field-and-reason form as a refusal. A drive stored on SBUS2 (#483)
  was the first case; a stored dead zone that swallows one side of the stick is
  now another.
- **The page keeps no copy (ADR 0068, extended to the RC Map).** RC drops its own
  checks. A refusal on save carries field, reason and accepts, and RC words it.
  It never matches the droid's text.
- **What RC offers comes from the droid.** `GET /api/rc/map` says which RC
  Receivers this receiver type reads and which one the drive may use. Each action
  in `docs/action-registry.yaml` says what it needs: a stick or an on/off RC
  Channel, and whether a Reaction may fire it. RC builds its pickers from these
  fields, and the save checks the same fields.
- **Pressed is the droid's verdict.** For a bound RC Channel, `GET /api/rc` carries
  whether the droid reads it as pressed. The raw number and the bar stay as display
  arithmetic in the page. Unbound RC Channels are not judged.
- **One home for words.** Refusal reasons and idle-binding reasons go in the
  browser's one words table (ADR 0068), under the same check that fails when a
  reason has no words.
- **One test matrix.** Every rule is tested at save and at read through the
  module's interface. The tests that pinned each scattered copy go with the
  copies.

The module sits below every consumer of `rc_binding_types.h` in the include
graph, so it cannot bring back the cycle ADR 0001 avoided.

## Considered and rejected

- **Page checks fed from GET.** ADR 0068 rejected this for Settings. Here it would
  add one more reader of each rule, and it would cost payload on the artoo-esp32.
- **Offer everything and refuse on save.** Simpler, but it reverses #389's "the
  RC page no longer offers it on SBUS2", and the builder would learn by being
  refused.
- **Read rules case by case.** Only the drive receiver and the dead zone would
  stay still on read. Any other rule would then let a stored RC Map run something
  a save refuses, which is the gap #483 closed for one case.
- **Keep the two dead zone checks**, explained by a comment. A stored calibration
  could then drive an axis that a save of the same values would refuse.
- **Judge all 18 RC Channels on the droid.** It costs payload on every
  `/api/rc` poll for channels that fire nothing.
- **Keep the per-copy tests alongside the matrix.** They pin call sites rather
  than rules, and they fail on every refactor of a caller.

## Amended 2026-10-09: an unread binding says why on the RC Map, and the save refuses PWM cues and dead payloads

Settled while building it (#486). Two points the decision above names are made
precise, and two rules join it.

- **Why a binding is not read is said on `GET /api/rc/map`, not `GET
  /api/rc`.** Each entry the droid would not read carries `"read": false` with
  the refusal a save would give it (`field`, `reason`, `accepts`). The verdict
  changes only when the map or the receiver type changes, so it rides with the
  map rather than on the live poll, whose rc SSE event is within about 240 B of
  its buffer. A page needs the verdict there anyway: the RC Map it posts back
  has to leave such an entry out, or the droid refuses the whole map over it.
  `GET /api/rc` carries the live part, `pressed`.
- **What RC offers comes as three lists.** `GET /api/rc/map` `receivers` says
  which RC Receivers the saved type reads (`read`), which the drive may use
  (`drive`), and which carry a cue or a puppet string (`cues`). Each action on
  `GET /api/actions` says whether it needs a stick (`rc_input`) and whether a
  Reaction may fire it (`reaction`).
- **A cue on PWM is refused** (operator, 2026-10-09 on #486). The PWM path never
  read the RC Map's cue slots, so a PWM receiver carries the drive and dome axes
  only. The legacy PWM arm and sound slots outside the RC Map are not covered by
  this ADR.
- **A payload the dispatcher would never send is refused**: a body sequence
  other than 30-36, or a Marcduino command that does not start `:`, `$` or `#`.
  Such a binding was saved and fired nothing.

## Amended 2026-10-10: conflicts are judged on read too, and Reactions are read through the same rules

Settled after the Codex review of #486, with the operator.

- **Two stored bindings on one control both stay still.** "Same rules on read"
  first judged each stored binding on its own, on the reasoning that every
  stored RC Map came through a save. That does not hold: NVS commits each key
  on its own, so a save cut short by a power loss can leave, say, the drive's
  Speed and an arm switch on one RC Channel. The conflicts a save refuses (one
  RC Channel, one job; one Part, one puppet string) are now judged on read.
  Both bindings in a conflict stay still, since neither is known to be the one
  the operator meant, and `GET /api/rc/map` marks both unread with the
  conflict. Only bindings that pass their own rules are judged against each
  other: one that is not read takes no control from another.
- **A Reaction is read through the RC Map's rules.** ReactionTask read the
  stored slots through the stored form's own check, which does not hold a
  Reaction's payload to these rules, so a `:SM` Reaction marked unread still
  fired. It now leaves out a Reaction the rules refuse, alone or in a conflict
  with another Reaction, and the evaluator releases a press it still holds.
- **Mapping an axis again mends a calibration the rules refuse.** A remap on the
  same RC Channel carries the old calibration over only when the rules take it;
  otherwise the axis starts from the defaults. Before, the old calibration was
  carried over and refused the whole save, so "map it again" could not mend it.

## Amended 2026-10-10: read means the droid reads it, 11 means any 11, and one switch per toggle

Settled with the operator after the 2026-10-10 architecture review, which found
the readers of a stored RC Map still each assembling the verdict from the rules.

- **`read` says what the droid reads now, not what a save would keep.** A drive
  pair the rules refuse leaves both axes still, so `GET /api/rc/map` marks both
  Speed and Steer unread. The refused axis carries its own refusal; the other
  carries a reason of its own that names the refused axis, so the builder still
  sees which one to fix. It does not carry the refused axis's refusal: the RC
  page acts on a refusal's field (a calibration refusal offers Reset ends), and
  would act on the wrong axis. Before, only the refused axis was marked and the
  other read `true` while it moved nothing.
- **Readers never see a binding the droid does not read.** The rules module takes
  a stored RC Map and leaves still every binding it refuses, on its own or in a
  conflict. The input processor, the mapper, the snapshot behind `GET /api/rc` and ReactionTask
  use what is left and keep no check of their own. Only `GET /api/rc/map` asks
  why a binding was left still. This runs once a frame on core 1, with no
  allocation, as the conflict check already did.
- **Any free stored place takes any trigger binding.** Six of the 11 places were
  kept for the arm, aux and op mode toggles, so a Sequence, sound or Reaction had
  five, and the droid refused the sixth while the RC page counted toward 11. The ceiling the droid enforces is now the 11 the glossary states,
  and it is a rule of this module. Nothing outside storage reads a place by its
  name.
- **An arm or aux toggle or the op mode sits on one RC Channel.** This limit came
  from the reserved places. It is kept as a rule (refused on save, both still on
  read), because two switches toggling one arm fight each other. A Sequence or
  sound may still sit on several RC Channels, and a Reaction is not held to it.

Considered and rejected: keeping `read` as "a save would keep it" (the RC Map
would then say an axis is read that the droid never moves), a verdict list each
reader applies itself (each would keep a skip step that can drift), and keeping
the reserved places with a per-kind ceiling on the page (the operator set 11).
