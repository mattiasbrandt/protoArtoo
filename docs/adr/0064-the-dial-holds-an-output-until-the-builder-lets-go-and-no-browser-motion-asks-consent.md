# The dial holds an Output until the builder lets go, and no browser motion asks consent

Status: accepted (2026-09-09, issue #291). Describes the **target** model; none of
it is implemented yet.

## Context

#291's grill settled the calibration dial on 2026-09-09 and left two things
unwritten. This ADR is the one its own body said was owed — *"decisions 2 and 3
earn one: they suppress a safety mechanism on purpose and replace it with a
firmware bound"* — and it settles the numbers that decision left blank.

**Calibration is a loop with the builder's senses in it** (operator, 2026-09-09).
The **Part** is steered from the RC controller and from the browser both; the
builder changes a value, then stops and watches and listens — is the servo
struggling, is the linkage behaving. That judgement is what calibration *is*.

**Output Release** walks straight into it. It is scheduled from arrival and then
cuts drive (ADR 0043), so it fires exactly when the builder has stopped moving the
Part in order to look at it. It does two things there, and the second is worse: the
recorded number stops describing where the Part is, **and a struggling servo is
only audible while it is being driven**, so the release removes the evidence the
builder is standing there to collect.

The second gap is older and wider than this ticket. **Three documents assert a
consent that does not exist.** `Non-RC Control` is checked in exactly two places —
`src/web/api_drive.cpp:330` and the action-test guard (`include/api_actions.h:30`).
`POST /api/servo` has never carried it, the flag does not persist and it boots off.
#292 measured this on 2026-09-08 and the operator chose to leave the behaviour
exactly as built. #291 drew the conclusion for the dial on 2026-09-09 — *"a gate on
the web half alone stops nothing and costs a switch every session"* — but ADR 0050
had already written the opposite the day before, and ADR 0062 and ADR 0063 repeated
it hours after #291 reversed it. ADR 0062's sentence contradicts itself: it claims
the press takes the consent, then says that consent never reaches `POST
/api/servo`, which is the path the press uses.

## Decision

**The builder ends the hold, not a timer.** **Output Release** is suppressed while
the dial has an **Output**. The Part stays driven until the builder presses
**pulses off** or closes the dial.

**Firmware bounds the hold in two ways, and the page can extend neither.**

- A **short expiry** when commands for that Output stop arriving — a few seconds.
  This is the shape the drive path already uses (`WEB_DRIVE_TIMEOUT_MS`,
  `include/config.h:480`), and it catches a closed lid or a dropped link in
  seconds rather than waiting out the ceiling. It is firmware observing arrivals,
  not the page asserting liveness, which is why it survives #291's objection that
  *"a hold that a dead browser can extend is not a bound."*
- An **absolute ceiling of ten minutes** from when the dial takes the Output.
  Long enough to fight one stubborn linkage uninterrupted, short enough that a
  bench left at lunchtime is not driving a servo all afternoon.

**When either fires the Output goes limp, and the surface says so.** Release keeps
one meaning everywhere on the droid, and nothing is commanded to move while nobody
is watching. **Resuming is one press** on the same dial: it re-takes the Output,
restarts both bounds, and leaves the recorded values untouched — which is what
keeps a bound from becoming an obstacle.

**No browser-initiated servo motion asks for `Non-RC Control` consent** — the
dial, **Find by Moving**, the timeline's pose press (ADR 0062) and the press on a
**Body View** (ADR 0063) alike. One rule for all four, and it is the one the
firmware already implements. ADR 0050, ADR 0062, ADR 0063 and `CONTEXT.md`'s
**Find by Moving** are corrected with this ADR.

## Considered options

- **A browser-liveness rule alone**, as #318's bench feed does — the hold lives
  while the surface is open. Rejected by #291 and not revisited: a hold a dead
  browser can extend is not a bound.
- **A command-arrival expiry alone**, with no ceiling. Rejected: a page left open
  on an abandoned bench keeps sending, so the one case a ceiling exists for is
  the one it would miss.
- **A ceiling alone**, with no short expiry. Rejected: a dropped link would then
  leave a servo driven for the whole ceiling, when firmware could have known
  within seconds.
- **Two minutes**, considered and set aside by the operator in favour of ten: a
  ceiling that fires during ordinary adjustment turns a deadman check into an
  interruption of the loop decision 2 exists to protect.
- **Thirty minutes.** Rejected: it only ever catches genuine abandonment, and half
  an hour is a long time for a part that is fighting its linkage.
- **A configurable ceiling**, following `WEB_DRIVE_TIMEOUT_MS`'s precedent.
  Rejected for now: one more number a builder can set wrong without knowing what
  it protects, for a bound that has no reason to differ between droids.
- **Driving the Output to its recorded close, or back to where the dial found it,
  before going limp.** Both end the Part somewhere known rather than wherever
  gravity left it. Rejected: it commands motion when the builder is by definition
  not there, and a first calibration has no recorded ends to aim at.
- **Gating browser motion on `Non-RC Control` and making the firmware enforce it**
  by adding the check to `POST /api/servo`. The honest alternative — it would make
  the words true rather than correcting them. Rejected: it reverses the operator's
  2026-09-08 decision on #292 to leave the behaviour as built, and #291 measured
  that the switch buys nothing while the RC controller moves the Part regardless.
- **Gating by how much moves at once** — no gate on the dial's single Output, a
  gate on a pose that commands many. It matches #291's own *attention* seam and
  the brownout path is real. Rejected: it is two rules for a builder to learn,
  enforced by a flag that reaches neither surface's code path.

## Consequences

- **Two things a builder can do that they could not**: find an endpoint by moving
  the Part until it looks right, and hear whether a servo is fighting its linkage
  — because the Part keeps being driven while they look and listen, instead of
  going limp the moment it arrives.
- **`Output Release` is no longer absolute.** Its `CONTEXT.md` entry said the hold
  after arrival is bounded so a jammed part cannot grind; the dial suppresses
  exactly that, and the two firmware bounds above replace it. Recorded there with
  this ADR.
- **The suppression is per-Output and lasts only while the dial has it.** Nothing
  else on the droid gains a way to hold an Output indefinitely.
- **Estop and Sleep Mode are unchanged** and still release every Output at once.
  The suppression is a carve-out inside normal operation, never inside a stop.
- **ADR 0050's Find by Moving clause, ADR 0062's pose-press consequence and ADR
  0063's press decision are corrected in place**, along with `CONTEXT.md`'s **Find
  by Moving**. All four said browser motion takes a consent that the firmware has
  never applied to `POST /api/servo`.
- **#292's recorded gap is now a decision rather than a gap.** The reach of
  `Non-RC Control` stays exactly as built, and no surface claims otherwise.
- **The press ADR 0063 puts on a Body View is this ticket's test sweep**, reached
  from the drawing instead of the dial, so it inherits #291's rule: it sweeps the
  recorded ends, and on an Output with none it is unavailable and says why.
- **What the dial *feels* like is still not decided**, and no amount of writing
  settles it. #291 stays open for that: whether turning, nudging and capturing
  actually beats typing a number is a question for something rough, driven against
  a real Output.
