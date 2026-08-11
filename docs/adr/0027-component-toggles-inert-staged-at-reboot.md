# Component toggles are inert and staged at reboot

Issue #166 proved that `components.*` `enabled: false` only prevented
construction: `rcInputTask` kept running its full decision pipeline with both
RC channels off, taking per-tick critical sections for `failsafeClear()` and
logging signal events that cannot occur. Take-effect semantics were also
inconsistent across tasks — drive, servo, and domeLink read their toggle once
at boot and park; rcInput, dome, and audio re-read theirs every loop
iteration.

We decided a Component Toggle means the hardware is fitted and in use, and
that off means **inert**, not merely unconstructed:

- No recurring per-tick decision work, no recurring writes to shared safety
  state, no recurring queue sends, and no recurring log emission on the
  disabled subsystem's behalf.
- No ongoing CPU or memory spent for the disabled subsystem; not spawning the
  owning task at all is the preferred form.
- One-time transition work at boot is allowed.
- Toggle changes are **staged at reboot**: saved immediately, effective at the
  next boot. Tasks read their toggles once at startup, never per iteration.
  The config UI marks component toggles as reboot-required.

Toggles and the safety machinery are independent in both directions: a toggle
never gates estop latching or the failsafe gate, and estop/safety handling
never overrides a toggle or the settings functions — a disabled subsystem
stays inert even during estop, because an inert subsystem has no output to
stop. Estop latching lives entirely in the failsafe gate and is evaluated on
explicit triggers, so task early-outs cannot break it.

## Considered options

- **Live apply for all toggles** — requires a resident parked task when off
  (retained stack contradicts the no-memory goal), mid-tick reconfiguration
  paths, and a one-time failsafe clear on live disable. Rejected.
- **Split contract by class** (safety toggles staged, cosmetic toggles live) —
  two contracts to document and audit. Rejected.

Staged-at-reboot also removes the failsafe-clear question entirely: the gate's
active mask boots clear, so an off subsystem's failsafe layers are simply
never set.

## Consequences

- `rcInputTask`, `domeTask`, and `audioTask` move from per-iteration toggle
  reads to boot-time reads.
- Defect issues filed from the #169 audit cite this ADR as their spec.
- Follows the Staged Network Switch precedent for apply/reboot handoffs.
