# The dome believes where it is pointing, and says so

Status: accepted (2026-09-08, issue #322), amended 2026-09-09. Describes the
**target** model. Nothing in it ships today. **The 2026-09-09 amendment at the
foot withdraws the fixed-map half of one answer; the moving marker stands.**

## Context

Dome position is a performance instrument. A builder wants the dome to face
front on command, to snap to a panel before opening it, to land a turn on a beat,
and to return home at the end of a show. protoArtoo has none of that: the dome is
**rate control end to end**. An RC binding sets a speed, `STEP_DOME_ROTATE`
carries `speedPct` and `durationMs`, and the status stream reports
`domeTargetSpeed`. There is no bearing, no home, no target, and no way to ask
where the dome is.

**And the data to reason about it has been sitting in the repository unread.**
`docs/droid-parts.yaml` gives every dome pie, panel and holoprojector a
`bearing_deg` on the Printed Droid convention — 0 dead astern, 180 dead ahead —
for all 26 dome parts. Nothing at runtime consumes it. A dome map that places a
panel at a bearing, driving a dome with no idea which way it faces, is half a
mechanism.

Two premises had to be corrected before the shape could be chosen.

**The prior art does not know where the dome is either.** ShadowMD is cited for
doing this open-loop "since long before us", and `autoDome()`
(`ShadowMD/src/Shadow_MD_DualController_Template.ino:5748-5848`) is in fact a
**there-and-back idle animation**: when `domeTargetPosition == 0` it picks a
random bearing and schedules a turn out; otherwise it schedules the turn back and
sets `domeTargetPosition = 0`. There is no accumulation and no correction, and
manual dome control cancels the automation while leaving that variable asserting
whatever it last set. `time360DomeTurn` calibrates *that animation*, not a
position system. So tracking a bearing is going **further** than the hobby does on
the same hardware, not adopting what it already has.

**There is no heading sensor anywhere in protoArtoo** — no IMU, compass, gyro or
accelerometer in `include/` or `src/`. So "hold a bearing while the droid drives"
has nothing to hold against: the body does not know it turned. The one path that
exists is unmentioned in the prior discussion — the hoverboard reports per-wheel
speeds (`hbSpeedL`, `hbSpeedR`, `hbFeedbackValid`), so differential odometry could
estimate body heading — and it exists only on a feedback-capable drive, which #304
deliberately made optional by reducing a driver's contract to
`send(speed, steer)`.

## Decision

**The dome carries a Dome Bearing it believes rather than measures, and every
surface says which.**

It is **integrated from commanded turns** against a calibrated full-turn time, and
it is an angle **from the droid's own front** — never a heading in the room.
**Home is front**, the orientation `bearing_deg` already anchors.

**The belief decays honestly.** Any manual turn, any coast, any estop or **Sleep
Mode** leaves the bearing **unknown**. It is restored the way this project restores
every other unmeasured fact: the builder turns the dome to front and **says so**.
A separate go-home command drives to the believed front, and is the end-of-show
act rather than the recovery one.

**A sequence gains a bearing step, and keeps the timed one.** `STEP_DOME_ROTATE`'s
speed-and-duration is untouched; a bearing step joins it, because the two are
different contracts with different failure modes — a duration always completes, a
bearing may not. A bearing step authored while the bearing is unknown is a
**Rehearsal Warning** and a run-time report, never a refused save.

**The dome map stays fixed and grows a marker.** The drawing keeps its canonical
orientation and shows where the dome's front currently sits relative to the body's
front. When the bearing is unknown the marker takes the unknown treatment and the
picker is untouched.

**An index sensor is a roadmap item that corrects this model rather than replacing
it**, so fitting one later changes the honesty tier of the readout and nothing
else.

## Why

**A believed bearing is worth more than no bearing and less than a measured one,
and the only failure mode is pretending otherwise.** Every positional act a builder
wants — face front, turn until PP3 is forward, land a turn on a beat, go home —
works from an integrated estimate on the hardware they already own. What breaks
trust is a readout that keeps asserting a number after the dome has been coasted,
hand-turned or power-cycled. Marking it unknown costs a builder one button press
and buys a readout that is never quietly wrong.

**Recovery is the calibration idiom, not a new one.** *"You turn it until the panel
is where you want it and record that number; you do not know it in advance"* is the
dial's philosophy, and #296's **Find by Moving** is the same move applied to
mapping. Declaring front is the third instance. A go-home command cannot be the
recovery path, because when the belief is wrong it drives confidently *away* from
home.

**Two step kinds, because a spin is not a bearing.** Three fast turns as
punctuation is a real thing a builder authors, and it has no target. Collapsing the
timed turn into a bearing would make every existing saved sequence's dome steps the
exception the model has to keep explaining, to buy a vocabulary that is smaller but
says less.

**This costs the dome nothing.** The dome ESC is driven from the body, so a Dome
Bearing needs no widening of **Catalog Authority** and asks the dome firmware for
nothing. That is worth stating because the map's open question — *whether the dome
publishes more about itself* — is untouched by this decision and stays open.

**A fixed map keeps the picker usable.** The same renderer a builder clicks to
author is the one that would rotate, and targets that slide under the pointer are
worse to use than a marker to read. Rotating would also need counter-rotated labels
to stay legible — a lot of machinery for a fact one mark carries.

## Considered and rejected

**Home-relative only** — the droid knows *at home* or *away*, and every positional
act is a turn from home, exactly as the prior art does. Cheapest, and it can never
be wrong about a bearing because it never claims one. Rejected: *"turn until PP3
faces front, then open it"* is unsayable, and ADR 0046's bearing-ordered dome
**Gesture** loses the one fact that would make a wave land where a builder chose.

**A sensed bearing, with an index magnet and hall sensor at front.** The only
option where the readout survives a slip, a hand-turn or a power cycle — and a slip
ring dome slips. Rejected as the *first* answer: it makes dome position a feature a
builder cannot have tonight and cannot have at all without opening the dome. Kept
as a roadmap item that corrects this model.

**A bearing replacing speed-and-duration.** One vocabulary for every dome move.
Rejected: a spin has no target, and it orphans every saved sequence's dome steps.

**Speed and duration only, with the bearing as a live control.** Nothing in the
saved format changes. Rejected: the believed bearing would then buy a readout and a
home button, and the performance act the ticket exists for stays unsayable.

**A go-home command as the only control.** One concept, nothing to declare.
Rejected: it acts on a belief and cannot recover an unknown one, and when the
belief is wrong it drives away from home with confidence.

**Only a sensor may re-home.** The strongest honesty position. Rejected: every
builder tonight, and every builder who never fits the sensor, would lose the
bearing permanently the first time they straighten the dome by hand.

**Rotating the dome map to the believed heading.** You look rather than calculate.
Rejected: the picker's targets would move under the pointer, and the labels need
counter-rotation to stay readable.

**Braking at estop instead of coasting.** `setDomeNeutral()` already emits neutral
on *"disable, estop, timeout, and startup"* (`src/tasks/dome_task.cpp:8, 70-76`)
and an RC ESC at neutral coasts. Rejected: braking would be a new behaviour on a
safety path with no measured need, and neutral-and-coast is the dome's analogue of
ADR 0043's release — do not drive a thing you are stopping.

## Consequences

- **A new step kind and a new persisted calibration** — the full-turn time — plus
  a believed bearing and its unknown flag in `RobotState`. `STEP_DOME_ROTATE` is
  untouched, so no saved sequence changes meaning.
- **The Rehearsal gains a dome figure it has never had.** It could not time a dome
  step because the dome owns panel motion; a *bearing* turn is timed by the body
  from its own calibration, so slowest-throw stops reporting a **Rehearsal Gap**
  for that one case. Panel physics remains a gap and remains the dome's.
- **`bearing_deg` is finally read at runtime**, after being carried since `6216c20`
  for a consumer that did not exist.
- **The map's open question is untouched.** The dome ESC is body-driven, so nothing
  here asks the dome to publish anything about itself; *whether the dome publishes
  its panel physics and light state* stays exactly as fogged as it was.
- **"Hold a bearing while driving" is not delivered and is not promised.** With no
  heading sensor the body cannot know it turned. If it is ever wanted, hoverboard
  wheel-speed odometry is the only path in the current hardware, and it exists only
  on a feedback-capable drive.
- **#287 is unblocked**, and inherits both the new timed case and the unknown-state
  rule.

## Amended 2026-09-09 — the drawing turns for the builder, and the marker still speaks for the droid

Recorded after a grilling session with the operator on 2026-09-09, which reopened
issue #322 under #175's rule: *"not to re-litigate taste, but when it was argued
from a current limitation rather than from what a builder needs."*

The rejected option above gave two reasons, and the 2026-09-09 research pass
measured both against a working implementation:

- **The legibility reason costs eight lines.** One SVG group is rotated and every
  `<text>` is counter-rotated about its own anchor. The angle persists with a
  reset. Eight lines is not a cost that decides anything.
- **The pointer-target reason had no evidence.** The reference applies rotation to
  the *same renderer it uses as an assignment picker*, has shipped it that way for
  many releases, and records it as a problem in none of its four review documents.
  Its own stated reason for building it is a builder problem we have more of: *"a
  builder is stood over an open dome... reading a fixed drawing means doing the
  rotation in your head on every single panel. That is exactly where P7 gets
  mapped to P11."*

**The dome map rotates, by an angle the builder sets.** A control and a reset. It
does not follow anything the droid reports.

**One angle serves every dome drawing.** The dome page, the picker, and any later
surface that draws the dome read the same accessor, so two surfaces can never
disagree about which way the dome faces. The reference paid for the alternative:
its `wizard.js:1981` is headed *"ONE DRAWING, ONE ORIENTATION (v1.67.0)"*, written
after two of its surfaces drew the same map and disagreed. A body view takes none
of this — an elevation is not a radial projection, and "rotate to match how you
are standing" is a different act there.

**The rotation is view-only, and the marker stays.** Two things now turn on one
picture for different reasons: the marker moves because the dome turned, and is a
fact about the droid; the drawing turns because the builder asked, and is a
preference about the viewer. Neither writes to the other.

**The angle lives in the browser, per device.** It describes where a person is
standing, so a laptop and a tablet at the same bench hold their own, and nothing
about a viewer is stored on the droid.

### Rejected in the amendment

- **Rotating the drawing automatically to the Dome Bearing.** The one option no
  picture-only project can have, since our dome actually turns and the droid holds
  a belief about it. Rejected on this ADR's own rule: any manual turn leaves the
  bearing **unknown**, and hand-turning the dome to reach a panel *is* the mapping
  session. It would be unavailable exactly when it is wanted.
- **Letting the builder's alignment set an unknown bearing**, recovering it without
  turning the dome to front. Rejected: the drawing's angle and the bearing agree
  only if the builder is standing at the droid's front, so anyone working from the
  side would set a bearing wrong by exactly their own position — and this ADR's
  posture is that a bearing is believed, never quietly assumed.
- **The rotation replacing the marker.** One number and one control, nothing to
  confuse. Rejected: it turns a view preference into a claim about the droid, and
  every idle nudge would rewrite what the droid believes.
- **One angle per surface.** Each surface remembers how the builder left it.
  Rejected as the regression already run upstream.
- **Storing the angle on the droid** so every device agrees. Rejected for the same
  reason the rotation does not write to the bearing: a viewer's standing position
  is not a fact about the droid.
- **Not persisting it at all.** Rejected on the reference's own argument — a bench
  does not move, and re-orienting on every visit is the cost that kills the
  feature.

### Consequences of the amendment

- The **fixed map** half of the decision above is withdrawn; the **moving marker**
  half stands unchanged, including its unknown treatment.
- One accessor holds the angle, and every dome drawing reads it.
  `tools/check_dome_panel_drift.py` covers only one edge of the three-source
  triangle — the dome's `/api/dome/layout`, the vendored `dome_panel_model.js`
  fallback, and a body surface — so nothing today would catch two drawings
  disagreeing.
- **The drawing's design stays #317's.** Its specific 9 proposed rotation and is
  still open. This amendment settles the policy — it ships, one angle, view-only,
  browser-local — and #317 keeps the tick weighting, the counter-rotated labels,
  the FRONT indicator and where the control sits.
- Two rotating indicators on one picture is a real confusion risk and a copy
  problem: the marker and the drawing must not read as the same kind of thing.
