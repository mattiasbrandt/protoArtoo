# Servo panel sequence state machine stays inline

An architecture sweep proposed extracting the servo task's panel-sequence state
machine into a Step Core (a pure `step(state, nowMs, triggers) -> actions`
module, the shape ADR 0005 and ADR 0014 gave the protoR2link Arbiter and the
Audio Step Core), so the `:SE30-:SE36` open/close timing would be natively
testable like the other task-loop decisions.

## Decision

Do not extract it. The sequence state machine stays inline in
`src/tasks/servo_task.cpp`.

## Why

The machine is far smaller than it looks from the outside. Only two states are
live -- OPENING and CLOSING; the OPEN_PAUSE value exists in the enum but no
transition reaches it -- and every one of the seven `:SE` sequence IDs executes
the same open-then-close motion with shared timing from config. The deletion
test that justifies the other Step Cores fails here in the good direction:
there is almost no decision complexity to concentrate behind a seam, so an
extraction would add an interface without hiding meaningful behaviour. A seam
with nothing varying behind it is ceremony, not depth.

## Consequences

- Future architecture reviews should not re-raise the servo Step Core; the
  asymmetry with the other task loops is deliberate, not drift.
- The revisit trigger is written down: if `:SE` choreographies ever diverge
  (distinct per-sequence motions, per-step timing, the OPEN_PAUSE state
  becoming reachable), extraction becomes the first move of that work rather
  than an afterthought.
