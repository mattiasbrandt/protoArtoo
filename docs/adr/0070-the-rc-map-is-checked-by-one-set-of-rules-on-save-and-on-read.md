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
