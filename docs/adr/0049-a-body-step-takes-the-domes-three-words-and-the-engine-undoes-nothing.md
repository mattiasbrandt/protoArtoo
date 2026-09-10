# A body step takes the dome's three words, and the engine undoes nothing

Status: accepted (2026-09-08, issue #319). Describes the **target** model.
Nothing in it ships today.

## Context

**A Learned Sequence cannot move a body part.** There are nine step types and
none of them does (`include/sequence_engine.h:26-46`), and the actions the engine
emits reach the dome TX queue and the audio queues only (`:204-209`). So every
servo-shaped decision this map has taken - #286's motion model, #301's **Part** on
an **Output**, #296's assignment surface - stops at the edge of the sequence
engine, and a sequence is a dome-and-audio artifact.

The gap is narrower and worse than "the body does not move". The body moves
perfectly well: an RC switch, the browser and the Controller Console all drive a
body servo today through `servo.action.open` / `close` / `set-position` /
`toggle-*`. What nobody can do is put that door in a routine.

**And the firmware already names seven body routines it does not perform.**
`src/tasks/servo_task.cpp:158-181` accepts `:SE30`..`:SE36` - *utility arm
open-and-close*, *all body panels open and close*, *all body doors open and
wiggle-close*, *ping-pong body doors*, *BT-1 two-gripper*. Every one of them runs
the same hardcoded state machine: drive arm(s) to open, wait `seq_open_ms`, drive
to close, wait `seq_close_ms`. There are no doors, no wiggle, no ping-pong, and
only two arms exist. The vocabulary a builder arriving from ShadowMD types is
already in this firmware, stubbed.

Two neighbours handed this decision its inputs. ADR 0046 split a **Gesture** by
owner and left the body half here: expansion, the cadence floor, partial travel on
a body **Output**. #320 added the body light, which has no position at all, only a
state.

**One premise had to be corrected before the last of those could be answered.**
The ~450 ms "proven safe cadence" is the *dome's*. The 2026-06-17 repro recorded
`esp_reset_reason=BROWNOUT` on the **dome** controller - seven ring servos on two
PCA9685 boards, dropping by about the third close, overlapping inrush exceeding
the dome supply (`src/tasks/sequence_catalog.cpp:517-525`,
`src/tasks/sequence_engine.cpp:162-170`). The body has never browned out. ADR 0043
carries the lesson across to a future body expander by inference, which is sound
engineering; the *number* is a measurement of a different controller's supply.

## Decision

**A body step is its own step type, it says one of the dome's three words, and
nothing undoes it afterwards.**

A **Body Step** carries the **Part**, a **Move Shape**, how far the Part goes as a
fraction of its own throw, and - for a flutter - how long it goes on. It is its
own type rather than a body target smuggled into a dome command's payload, on the
precedent `STEP_DOME_ROTATE` set for a body-owned motion step.

**The Move Shape is open, close or flutter** - the three the dome's **Panel Intent
Command** already carries, so one word means one thing across the droid. The
surface names them by **Part Kind**: a servo Part reads open/close/flutter, a light
Part reads on/off/flash, and *how far* reads as travel on one and brightness on the
other. The stored token is the same either way, which is what lets a Gesture spread
one shape across a mixed set.

**A body flutter is performed by the Sequence Coordinator**, ends open, and owes a
later close in the same branch - the rule Protocol Check already enforces for a
dome flutter (`src/protocol_check.cpp:956`).

**Speed, acceleration and easing stay on the Output.** Only a **Gesture** overrides
them, because a Gesture is one authored move whose feel is part of what was
authored. A plain step carries no physics.

**The engine undoes nothing.** A body step stamps no effect class. A Part left open
when a sequence ends stays open; the **Output Release** schedule already stops it
being held; the close is a step the author writes. "This routine leaves the
dataport open" is a **Rehearsal Note**, because the sequence performs exactly as
written.

**A Cadence Floor paces only what the body generated** - a Gesture's expansion, a
flutter - never steps an author wrote by hand, which keep their timing and earn a
**Rehearsal Warning** when they overlap. The floor is settable and is stated as
**unmeasured on the body**.

**`:SE30`..`:SE36` become Factory Sequences authored from Body Steps**, so the
seven names finally do what they say, and stay retrainable like any other.

## Why

**One vocabulary is the whole point of taking the dome's words.** A builder who
has learned that a dome panel opens, closes and flutters has learned the body too,
and a Gesture that spreads a shape does not have to ask which half of the droid it
is on. The alternative - a body step that only says *where to put it* - leaves a
Gesture's "shape" with nothing to be, and puts the wave arithmetic back in the
author's hands, which is what ADR 0046 exists to stop.

**The reason the dome closes its ring does not reach the body.** Terminal cleanup
closes what a run left open because a dome panel held open under PWM grinds and
the body cannot see the panel's state. On the body we know both: an Output's own
release schedule un-holds it, and the body knows arrival exactly. What remains of
an auto-close is tidiness, and tidiness that drives many outputs together is the
shape ADR 0043 refused for estop. The body already declines to auto-close pie
panels for exactly this reason; body Parts join them rather than becoming the
exception.

**A guess that is labelled is not the same object as a guess that is borrowed.**
The body needs *some* floor, because the first Gesture a builder writes across a
full complement would otherwise start every servo at once. Adopting the dome's
figure would launder a dome measurement into a body fact, and the surface would
then report a number nobody took. Stating it as a conservative default awaiting a
bench run keeps the safety and keeps the honesty, and it is the same posture #293
took with the wiring view's travel figures.

**The firmware's own seven names are the acceptance criteria.** "All body doors
open and wiggle-close" is a specification somebody already wrote down here; the
only thing missing was a step that could express it. Making them real costs seven
authored sequences and turns a standing untruth into the demonstration that this
decision works.

## Considered and rejected

**A body step that carries only a position.** Most general - it can express an
asymmetric move no intent word covers - and it needs no new vocabulary at all.
Rejected: a **Gesture**'s shape would have nothing to be except a position, a
flutter returns to hand-written steps with the builder doing the timing, and the
body's words would be a subset of the dome's rather than the same ones.

**A body flutter that goes out and back once, self-completing.** Cheapest third
word: no repeat generator, no close owed, no second timing authority. Rejected:
the same word would end the Part open on the dome and closed on the body, so
Protocol Check's flutter-owes-a-close rule would have to become dome-only and
"flutter" would stop meaning one thing.

**Lights with their own words and their own step type.** Reads unambiguously in
saved JSON and never puts the word *open* on a Magic Panel. Rejected: a Gesture
could not span a mixed set without translating between two vocabularies, and
Protocol Check would gain a second grammar to gate.

**Terminal cleanup that closes body Parts, staggered, as `addRingClose()` does.**
Dome parity, and every routine would end with the droid in a known position.
Rejected: it is a many-at-once close held apart only by a stagger - the shape ADR
0043 refused for estop - and it adds seconds to the tail of every sequence that
touched the body.

**Terminal cleanup that releases what the sequence moved.** Cheap, never fights a
linkage, and it is the answer estop already gives. Rejected: a door the author
deliberately left open would sag shut at the end of every routine, so the model
would contradict what the builder wrote.

**A per-step speed override behind an Advanced affordance**, as the reference has
it ("opens and closes at your servo settings; turn on Advanced to override for
this brick"). Rejected: two motion authorities in the common path, and an imported
sequence would carry its author's physics into somebody else's linkage. A flutter
is already a Gesture over one Part, so "shake this door fast" is sayable without
putting speed on every step.

**Adopting ~450 ms as the body's cadence floor.** Rejected: see above - it is a
measurement of the dome's supply, and firmware enforcing it would be reporting a
number nobody took on this board.

**Retiring `:SE30`..`:SE36`.** One model, nothing legacy, nothing lying. Rejected:
it breaks the button bindings of exactly the builder this map is aimed at, and the
dome forwards those numbers, so it is a change with reach outside the body.

## Consequences

- **A new step type and a new emitted action**, which the dispatcher maps onto the
  servo command path exactly as `SEQ_ACT_DOME_ROTATE` maps onto the dome ESC. The
  body servo path stops being reachable only from RC, web, console and the dome.
- **Protocol Check gains a Body Step grammar and stays form-only** (ADR 0044): the
  Part token from the generated catalog vocabulary, the shape one of three, how
  far within 0..100, a flutter's duration bounded, and a flutter owing a later
  close in the same branch. Whether the Part is assigned, whether the target is
  reachable and whether the move completes in time remain the **Rehearsal**'s and
  never block a save.
- **#287's body rules gain a subject.** Target outside the recorded ends, a target
  re-issued before arrival, two steps driving one Output at once, a Part no Output
  records, parts left open with no close, an Output whose **Component Toggle** is
  off - all of them start firing, and *slowest throw* stops reporting a **Rehearsal
  Gap** on any sequence that touches the body.
- **`seq_open_ms` / `seq_close_ms` stop being the body's only timing.** The global
  dwell exists because `:SE3x` had nowhere else to get one; once those are authored
  sequences it is a default, not the mechanism.
- **The Cadence Floor is a bench measurement waiting to be taken.** Until it is,
  the surface that shows it says so. #318 inherits the same answer for bulk actions
  on the output table.
- **Estop and preemption are unchanged and gain nothing to special-case.** A body
  step in flight ends where it got to and goes limp, because estop aborts the
  sequence (Sequence Preemption) and releases every Output (ADR 0043); nothing
  resumes afterwards.
