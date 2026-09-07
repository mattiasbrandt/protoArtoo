# Estop releases servo outputs rather than closing them

Status: accepted (2026-09-07, issue #300). Describes the **target** behaviour; it
lands incrementally, and the code at `939ed705` implements none of it yet.

## Context

At `939ed705` estop and Sleep Mode take the same path in `ServoTask`
(`src/tasks/servo_task.cpp:265-278`): both call `abortSequenceAndPark()`, which
drives the active arm to its close position with `setArmPosition()` and leaves
PWM held. It also early-returns unless a sequence is running, so an output moved
by a direct `servo.action.set-position` is not parked by estop at all.

That behaviour is safe at today's scale — five LEDC outputs, at most two arms
closing — and stops being safe at the scale #286 designed for. Two separately
documented incidents bound the answer, and they pull in opposite directions.

**Holding drive grinds.** `~/Documents/GitHub/AstroPixelsPlus` drives the dome's
panels through two PCA9685 boards. On 2026-05-21 a body sleep sync fired
mid-panel-wave, a close fought the running sequence, and the servos ground with
no recovery short of a power cycle. `FORK_IMPROVEMENTS.md:164` states the
mechanism: the expander "is a dumb PWM emitter. After a close sequence, the servo
holds position indefinitely under active PWM. Any mechanical conflict ... grinds
the motor until power is cut." The fork's fix took four iterations to settle: a
per-group release mask with a never-shorten deadline, cancelled by any command
that re-energizes, with the delay scaled from commanded speed and capped at 30 s
because a PCA9685 gives no motion-complete signal.

**Closing many outputs at once browns out the board.** Independently, this
project already learned the other half. `src/tasks/sequence_catalog.cpp:205-212`
records the 2026-06-17/-18 fix and states the systemic invariant: "the body never
auto-emits a group close (`:CL00`/`:CL14`/`:CL15`) nor an automatic pie close.
The old `:CL00` here drove every group servo at once (brownout) and cleared the
toggle latches as a side effect." Factory sequences were re-authored to stagger
closes at a proven ~450 ms cadence, one servo actuating at a time.

So an estop that closes everything is the brownout condition, arriving at the
moment the board can least afford it — and a brownout drops the parts anyway,
*and* clears the latches. `docs/droid-parts.yaml` sizes the exposure: 13 body
parts want a servo (2 driven today), plus a deliberate 10-slot escape hatch.

## Decision

**Estop and Sleep Mode release every servo output and command no position.** A
released output is limp. Nothing is driven, so nothing can grind and nothing can
brown out, and the release reaches every output regardless of how it was last
moved — which closes the sequence-only gap for free.

**In normal operation an Output Release is scheduled from arrival**, not from
when the command was issued. #286 puts speed, acceleration and easing in
firmware and `ServoTask` already runs the ramp on a 50 Hz state machine, so
arrival is known exactly rather than estimated. Any new command to an output
cancels a release pending on it. The delay reduces to one honest value — how long
to hold after arriving.

**A bus drop is reported, not escalated.** If an expander's bus fails, firmware
cannot command a release either; affected outputs are marked unreachable and the
UI says so, while drive, estop and failsafe are untouched. This follows ADR 0032's
shape — a fault in one path has no drive-path effect — and escalating buys nothing
mechanically, since a latched estop cannot reach the expander either.

**Attachment and sizing.** The expander attaches over a bus the boards already
have: I2C costs **zero additional pins**, assigned on both targets
(`include/config.h:232-233`, `:325-326`), broken out as labelled headers on the
artoo.uk PCB (`docs/pin_map.md:155-156`), and on firebeetle2 belonging to the six
lanes committed by board bring-up rather than the fourteen contested
firmware-design pins. The row table stays open per #286, with one board expected.
The five LEDC outputs and the dome ESC stay on LEDC — mixed rows are what the
addressed model exists to express, the ESC keeps LEDC's 0.3 µs resolution against
an expander's 4.88 µs, and `ledc-direct` must remain a usable Component Family
member for a board with no expander fitted (ADR 0042).

## Considered options

- **Close and hold** — today's behaviour, made deliberate. Every part ends in a
  known position. Rejected: this is the 2026-05-21 grind, indefinite by
  construction.
- **Close, then release after a bounded delay** — the dome fork's shipped answer,
  and the option this session initially chose. Rejected once the scale was
  counted: closing 13–23 outputs together is exactly the `:CL00` brownout, and it
  would fire when the board is least able to survive it.
- **Close staggered at the proven ~450 ms cadence, then release** — respects both
  invariants and ends in a known position. Rejected: 13 outputs is ~5.9 s and 23
  is ~10.4 s before an *emergency* stop completes, and it needs the bus alive
  throughout.
- **Release immediately, with a separate operator-initiated staggered park** —
  splits safety from tidiness cleanly. Not chosen, but nothing here forecloses
  adding park later; it is an ordinary command, not a safety path.
- **Latch estop on a bus drop.** Rejected: contradicts ADR 0032's shape and buys
  nothing, because estop cannot reach the expander to release it either.
- **Move all servos to the expander for uniformity.** Rejected: it would leave a
  board with no expander fitted driving nothing, when it drives two arms today,
  and ADR 0042 requires `ledc-direct` to stay a selectable member.

## Consequences

- `abortSequenceAndPark()` stops being the estop path. Estop releases; it does not
  choose a position, so `getOpenClosePositions()` is not consulted.
- The sequence-only early-return goes: a release iterates every output.
- Sleep Mode and estop share the release path, as they already share the park
  path, but they remain distinct states — see CONTEXT.md's "sleep" ambiguity entry.
- A released panel's resting position is whatever gravity and friction decide.
  Operator-facing copy must say so plainly rather than implying the droid parks
  itself; that wording is #298's.
- The wiring view (#293) inherits the power story: a shared rail that cannot
  drive many outputs at once is the reason the estop behaves this way, and the
  view is where a builder should meet it.
- The fork's speed-scaled delay and its 30 s cap are **not** adopted. They exist
  to work around a missing motion-complete signal that this model has.
