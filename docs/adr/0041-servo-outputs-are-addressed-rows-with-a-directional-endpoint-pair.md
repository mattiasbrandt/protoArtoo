# Servo outputs are addressed rows with a directional endpoint pair

Status: accepted (2026-09-07, issue #286). Describes the **target** model; it
lands incrementally, and the code at `6441966e` implements none of it yet.

## Context

At `6441966e` a servo output is two numbers on a fixed NVS key set:
`arm1_open_us` / `arm1_close_us` and the same for `arm2` and `aux1..3`
(`include/config_store.h:117-128, 260-271`), plus a `ServoComponentType`. There
is no centre, no motion model, no reverse representation, no per-output name and
no table. `setArmPosition()` writes duty directly through
`ledcPwmSetPulseWidth()`; nothing ramps.

Two forces made that shape untenable. The operator experience work (#175) needs
a calibration surface built on capture-where-it-is, which wants three positions
and a motion profile. And the body is going to gain a separate servo controller:
the pin budget cannot serve "lots of servos" — `docs/pin_map.md` records **one
free GPIO** on the ESP32-P4 board ("the sole free GPIO on this board", against
"15 GPIOs … a demand of 14") and four on the Artoo PCB, available only in SBUS
modes.

## Decision

A servo output is an **addressed row**, not a named field. Each row carries an
**Output Address** (`driver`, `channel` — today always `ledc`), the **Part** it
drives (from `docs/droid-parts.yaml`), an `open`/`centre`/`close` pulse triple,
speed, acceleration, easing, sleep-when-idle, boot behaviour, component type and
a `calibrated` bit.

Three parts of that carry the weight:

**The endpoint pair is directional.** A reversed linkage is `open > close`.
There is no invert flag anywhere and no consumer may introduce one; every
consumer takes the min and max of the pair.

**Sequences and RC bindings reference the Part, not the Address.** Moving a
servo to another channel — or onto the expander — must not change what any
saved sequence means.

**Estop bypasses the ramp.** Ramping opens a gap between commanded and actual
position; `abortSequenceAndPark()` snaps. You do not ease into a safe state.

## Considered options

**`min`/`centre`/`max`, as the reference tooling names them** — rejected. Those
names are non-directional, so a reversed linkage needs a separate invert flag,
which is the failure mode every prior implementation warns about.

**`startPulse`/`endPulse`/`neutralPulse`, as ReelTwo names them** on the dome
(`setServo()`, AstroPixelsPlus ADR 0002) — rejected for the operator surface. It
is directional and would unify vocabulary with the dome, but it is a vendored
library's language, and "start/end" does not say which end is open. Catalog
Authority already has the body owning operator vocabulary while the dome owns
calibrated execution, so the divergence is expected rather than accidental.

**Keeping five fixed named key sets** — rejected. Migrating five outputs now is
the cheapest this will ever be, and every field the richer model adds is another
field a later rewrite would have to carry.

**A stored, measured travel time** — rejected. With speed and acceleration in
firmware it is computable, and two numbers for one fact is a drift source.

## Consequences

- The NVS layout changes: fixed key sets become an addressed table. 122 keys
  exist today against a 20 KB partition, so key count is not the constraint.
- `ServoTask` gains a ramp. It already runs a non-blocking 50 Hz state machine
  (`src/tasks/servo_task.cpp:494-514`), and 20 ms is the servo frame period, so
  this is a static per-output array and an `updateRamps()` call in the existing
  tick — not a new task and not a heap allocation.
- Acceleration is not optional once speed exists: a travel time computed from
  speed alone is optimistic, because acceleration is the binding constraint.
- `seq_open_ms` / `seq_close_ms` (`servo_task.cpp:285`) is a *dwell* that today
  stands in for travel time at a guessed 1000 ms. It should default from the
  computed figure.
- `robotState.armOpen[2]` (`include/robot_state.h:220`) is written and never
  read, and its `pulseUs > SERVO_PULSE_NEUTRAL_US` derivation is wrong for any
  reversed pair. It becomes real state or it goes.
- Expander support is back in scope; the earlier "PCA9685 is not worth pursuing"
  assessment rested on having spare channels, which is not true. See #300.
- `docs/droid-parts.yaml` becomes load-bearing at runtime rather than
  documentation. See #301.

## Amended 2026-09-07

The per-output motion field this ADR called **sleep-when-idle** is renamed
**Output Release** (#300). "Sleep" already names a droid-wide Commanded Mode
that blocks commands and syncs to the dome, so the two were one word for a
posture the whole droid is in and a hardware fact about one servo.

The trigger is also sharper than "idle": a release is scheduled from the moment
the output **arrives** at its target, which this model makes knowable because
speed, acceleration and easing run in firmware. Any new command to that output
cancels a pending release. The decision and its evidence are on #300.
