# A part's motion is set in time, not in rate

Status: accepted (2026-09-08, issue #286, second pass). Amends
[ADR 0041](0041-servo-outputs-are-addressed-rows-with-a-directional-endpoint-pair.md)
in three named places; everything else in it stands. Describes the **target**
model. Nothing in it ships today.

## Context

ADR 0041 gave a **Servo Output** a motion model — *"speed, acceleration,
easing"* — and said `ServoTask` gains a ramp. It never said what any of those
three are made of. Two tickets have been waiting on the answer ever since: #291
cannot lay out the calibration dial without knowing what it edits, and #318
cannot lay out the output table without knowing what its columns are.

**#286's own body still carries a stale 🔄 on this.** It reopens *"does the
motion profile enter firmware?"* on the grounds that the first pass answered
"no" from a fact about today. That reopening was right about the reasoning and
late about the outcome: ADR 0041 already answered **yes** on 2026-09-07, and
four decisions now lean on it — ADR 0043 schedules an **Output Release** *from
arrival* precisely because firmware knows when arrival is, ADR 0046 makes speed
and easing **Gesture** parameters, ADR 0049 keeps them on the Output with only a
Gesture overriding, and #287's *slowest throw* cannot exist without them. What
was open was never *whether*, only *what*.

The reference's model is the obvious thing to copy and three parts of it do not
transfer.

**Its units are a vendor's.** `speed` is quarter-microseconds per 10 ms and
`acceleration` is quarter-microseconds per 10 ms per 80 ms, range 0–255
(`servo-cfg.js`, spec §3). That is the Pololu Maestro's kinematics tick.
protoArtoo has no Maestro, `ServoTask` runs a 50 Hz state machine, and LEDC
resolves about 0.3 µs rather than 0.25.

**Its own hardest-won lesson is that a rate misleads about time.** Its lint
header records that speed 80 with acceleration 10 gives roughly 940 ms of full
throw, "not the ~344 ms speed alone suggests" — and *acceleration, not speed, is
the binding constraint*.

**Its kinematics never overshoot**, stated twice in the spec: the trapezoidal
profile ramps up, holds, ramps down, and stops. The `overshoot` **ease** is a
separate layer above it, which deliberately aims about a twelfth past the target
and settles back, only on moves worth more than an eighth of travel, and *"never
past your endpoints"*. So easing is not a flavour of acceleration; it is the one
mechanism in the whole model that drives somewhere the builder did not ask for.

One place we are already ahead and should stay ahead: the reference's
`calibrated` flag is a runtime boolean, *"not persisted, reset on page reload"*.
ADR 0041 makes it a stored bit, which is the only reason an uncalibrated-output
warning can mean anything after a reboot.

## Decision

**A builder sets three things about how a Part moves, and two of them are
times.**

**Speed is how long a full throw takes.** **Acceleration is how long the move
spends getting up to that speed.** Both in milliseconds. The rate is derived
from the recorded **Endpoint Pair**; the time is what is stored.

**Easing is the shape of the move**, and there are three: `none` stops dead on
the number; `soft` eases the acceleration itself in, so a long linkage or a
heavy panel breathes into motion; `overshoot` aims a little past the target and
settles back, which is what makes a pie read as snapping open rather than
arriving.

**Overshoot never passes the recorded ends.** On an Output whose `calibrated`
bit is unset there are no ends to work within, so it degrades to `none` and the
surface says so.

**Boot behaviour is three modes** — limp, go home and hold, or go home and
release — **limp by default**, and calibrating an Output never ticks it. **The
boot pass is generated, so the Cadence Floor paces it** (#319) and
thirteen-to-twenty-three Outputs never start together.

**The profile lives on the Output.** A builder sets a heavy door's character
once; only a **Gesture** overrides it (ADR 0049).

### What this amends in ADR 0041

1. **"speed, acceleration, easing" is now defined**: two times in milliseconds
   and a three-valued shape.
2. **Its rejection of "a stored, measured travel time" is narrowed, not
   reversed.** That rejected keeping a time *beside* speed and acceleration —
   *"two numbers for one fact is a drift source"*, and it was right. Here there
   is one number and the rate is derived from it, so no second copy exists.
3. **"boot behaviour" is now three named modes** with a stated default and a
   stated pacing rule.

## Why

**A time is the consequence; a rate is something you convert into one.** This
project's copy standard is that every parameter ends in a physical consequence,
and *"this door takes nine hundred milliseconds end to end"* is one a builder can
check with a stopwatch. Storing the time deletes an entire class of
misunderstanding that the reference documented against itself, and it is stable
across a recalibration in the way a builder means: nudging an endpoint changes
the travel, and the door should still take about a second.

**The cost of that is honest and small.** Full throw is only defined once the
ends are recorded, so an uncalibrated Output has no derivable rate — which is
consistent with what #286 already says about it, that the first move is a jump
rather than a ramp.

**Overshoot earns its place and its fence in the same sentence.** It is the only
one of the three that changes how a droid *reads* to an audience, and the only
one that drives past the target. Making the reference's nicety our invariant
means the dangerous case — an unmeasured linkage — is the case where the feature
switches itself off, rather than one a builder has to know about.

**Boot is the third place mass motion meets a shared rail.** ADR 0043 refused a
staggered close at estop and #319 refused an unmeasured stagger in the engine;
this reuses the machinery both produced rather than inventing a boot-specific
rule. Limp by default because a power-up should never move a part while somebody
has their hands in the droid.

## Considered and rejected

**Rates in physical units** — microseconds per second, and per second squared.
Directly what the hardware does, survives a change of tick rate, and an
uncalibrated Output still has a defined rate. Rejected: it is one step removed
from what a builder cares about, and mis-reading a rate as a time is the specific
error the prior art paid for.

**Maestro units, as the reference stores them.** Numbers copied from a forum post
or an imported file would mean what they say with no conversion. Rejected: a
vendor's unit for a vendor we do not have, on a 10 ms tick we do not run and a
quarter-microsecond step our LEDC does not use.

**Speed and acceleration only, with no easing.** Two numbers, exactly the
trapezoidal profile firmware runs, and nothing ever deliberately drives past a
target. Rejected: the character of a *part* could then only be set per authored
move, so every routine touching a heavy door would have to ask for the gentle
start again.

**Named presets — gentle, normal, snap — with the numbers behind an Advanced
gate.** Reads at a glance down twenty-three rows. Rejected: a preset has no
physical consequence to explain, which is what the maker voice is built on, and
the row-count problem is a bulk-edit row instead (#318).

**Overshoot bounded by the absolute 500–2500 µs clamp.** One rule, works from the
first move on every Output. Rejected: that is a servo's bound, not a linkage's,
and the calibration copy already warns that a horn driven into a hard stop at
full travel strips gears.

**Boot with hold dropped, leaving limp and home-and-release.** Consistent with
ADR 0043 refusing held drive at estop. Rejected: a heavy breadpan door that
gravity swings open, and a gripper that should boot closed, both have a real
reason to ask for hold. Boot is an ordinary start-up, not a safety path, and the
grind risk is stated on the option rather than decided for the builder.

**Everything boots limp, with no boot mode at all.** No boot motion can ever
brown out or grind. Rejected: the droid would power up with its panels wherever
they were left, and tidying becomes something the operator remembers.

## Consequences

- **#291 and #318 are unblocked** and inherit their column sets: *time to full
  throw*, *time to get up to speed*, *ease*, *boot*, beside the **Endpoint
  Pair**, **Output Release** and the `calibrated` bit.
- **The Rehearsal's *slowest throw* is a stored number, not a computed one** —
  for a body Part it is the Output's own full-throw time scaled by how far the
  move goes. That is simpler than the trapezoidal estimate the reference needs.
- **"Speed" now names three unrelated quantities on one droid** — a **Foot
  Drive** preset tier, a signed dome percentage, and a Servo Output's *time*.
  Recorded in `CONTEXT.md`'s Flagged Ambiguities; never used bare in operator
  copy.
- **An uncalibrated Output is now visibly less capable, on purpose**: no
  derivable rate, and overshoot degraded to `none`. That is a reason to
  calibrate, stated where the builder meets it.
