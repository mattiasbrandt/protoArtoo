# A body view shows what the droid is doing, and a click on it only ever selects

Status: accepted (2026-09-09, issue #317). Describes the **target** model; none of
it is implemented yet.

## Context

#317 asked what the body views draw and how a builder finds a part on one. That
they ship was never in question: the operator's plan of 2026-08-22 is explicit —
*"definite plan to expand our interactive SVG dome panels with equivalent BODY
views from different angles (front/rear/side elevations for arms, doors,
dataport, chargebay, AUX) [...] Direction: procedural SVG from coordinate tables,
`buildDomeMap()`-style contract (caller owns assignment) [...] and reuse the same
SVGs as live state visualization (SSE-driven) and as the timeline pose-preview
surface."* The open question was what they are **for**, which the ticket's own
thin-spot note put plainly: *"can a builder click a part on a picture of their
droid and make that part move?"*

Four measurements shaped the answer.

**Body part names already say where they are.** The ticket's premise is that *"the
body half is harder than the dome half and more necessary"*. A dome part is a
number — `P7`, `P11` — that means nothing until you see where it sits, which is
why a picture is the only way to find one. A body part is *Charge bay door*,
*Upper utility arm*, *Rear-left body door*. For the body, the name is the
location.

**The catalog cannot place a body part in any case.** All 29 `bearing_deg` values
in `docs/droid-parts.yaml` are dome; body entries carry a compass word and nothing
else, and **twelve of fourteen body parts share three words** — four at
`front-left` (`dataport`, `doorFL`, `gripArm`, `gripClaw`), four at `front-right`,
four at `front` (`drawer`, `smallDoor`, and both utility arms, which the field
cannot tell apart). It gives a quadrant, not a spot.

**And no better source is available to us.** The catalog was extracted
deliberately as *"Printed Droid terminology + MrBaddeley part NAMES only, no
geometry"* (2026-08-22, commit `6216c20`). Body coordinates cannot be taken from
the MK4 model — that is provenance, not convenience. The ticket's own stated
reason for the difficulty is wrong in the other direction: it says *"every
`cad_name` [...] is `TBD`"*, when all 26 dome entries are `TBD` and **ten body
entries carry real names** (`FLBreadpanDoor`, `DataPortDoor`, `UpperUtilityArm`
and the rest), with four `null` for the **Common Addition**s.

**The droid cannot report where anything is.** Nothing reads a servo back.
`RobotState` carries `armOpen[2]` and two target microsecond values — commanded
state for **two of fourteen** body parts, the two the body actually drives. Per
ADR 0045 a surface may claim only commanded intent.

## Decision

**A body view is a state display.** Its job is showing many parts at once — the
one thing a named list cannot do — rather than telling a builder which part is
which, which the names already do. That is what decides how accurately anything
must be drawn: enough to read at a glance, not enough to point at.

**It shows one kind of state at a time, and says which.** Either what the droid
was last told, or what a routine says at a chosen moment. Both are commanded
intent and they draw identically, so a view that mixed them would ask a builder to
remember which half they were looking at — the defect #298 spent a ticket undoing.
The routine half works for all forty parts today; the live half has two.

**The droid is drawn as an arrangement, not as a likeness.** Parts as shapes at
nominal positions on a front face and a rear face. A recognisable MK-series
silhouette would be artwork per design, and it would show someone else's droid to
the builder #333 provided for — one *"on a droid that is nobody's published
design"*. The reference's own rule for its board pictures is the right one here:
*"a photo is the promise 'this is what yours looks like', and a drawing is 'this
is the KIND of thing'."*

**A part can be moved from the drawing, on a deliberate press.** Picking a part
and pressing runs it through its travel and back, as one command. Same shape as
ADR 0062's timeline pose — never following a drag — and it takes the same
**Non-RC Control** consent that calibration and **Find by Moving** take. This is
the ticket's own capability question, answered yes.

**A click only ever selects.** Picking a part fills a panel beside the drawing
with its name, its **Part Kind**, what drives it, and the acts available on it as
named buttons — add it to the build, give it an **Output**, move it. One gesture
with one meaning, on a surface where most parts mid-build are unfitted, unclaimed
or both. That panel is also the visible explanation ADR 0059 requires at the
entrance of a control that moves something: **one slot rather than forty labels**,
and reachable on a tablet, which a `title` is not.

**Parts with no position stay listed beside the drawing.** `other1`..`other10`
have no position word by definition. They are named in a list next to the view
rather than placed or silently omitted — *"the difference between 'the map is
incomplete' and 'the map is lying'"*. A builder does not place their own hardware
on the drawing, so the view acquires no write path and the never-writes contract
stays absolute.

**Colour stays reserved.** What a part *is* is shown by shape. #327's two meanings
— amber for *you can act on this*, red for *stopped or refused* — remain the whole
of colour on this surface.

## Considered options

- **A body view for finding a part**, which is the ticket's own framing. Rejected
  on the naming measurement above: a picture earns its place on the dome because
  `P7` is a number, and the body's names already carry their location.
- **A recognisable body silhouette.** Best for pointing at a door. Rejected: it is
  artwork per design, and #333 deliberately provided for a droid that is nobody's
  published design.
- **A silhouette where we have one, an arrangement otherwise.** Rejected: two
  drawings to build and keep in step, and a view whose whole character changes
  with what the builder said they built.
- **No drawing at all — parts as a grid showing state**, which is what the
  reference actually ships for the body, and which does this job with no artwork
  and scales to any board. Genuinely competitive once the job is *seeing many at
  once*, and rejected only because the 2026-08-22 plan is a standing operator
  decision that body views ship. Worth reopening if that plan moves.
- **Live state and a routine's moment drawn together on one shape.** Rejected: it
  cannot be intent against reality, because nothing reads a servo back, so it
  would show two kinds of intent and ask the builder to keep track.
- **Motion staying on the tables**, where #318's per-row drive and **Find by
  Moving** already live. Rejected: the builder is standing at the droid holding a
  lead, and a trip to a table and back is what the picture exists to save.
- **Starting Find by Moving from the drawing.** Attractive — it would put the
  discovery run where the builder stands. Not taken here: ADR 0050 placed that run
  on an unwired **Parts** row on purpose, and moving it is that ticket's to
  revisit.
- **A click doing the obvious thing for each part's state** — add an unfitted one,
  assign an unclaimed one, select a driven one. Fewest presses. Rejected: one
  gesture with three meanings, chosen by a state the builder may not have noticed,
  and one of the three commands a horn.
- **Letting a builder place their own parts on the drawing.** The one act that
  would make the picture complete for a real droid. Rejected: it is the first
  write on a surface whose contract is that it never writes, and the list beside
  the view already tells the truth.
- **Colour by Part Kind**, on the grounds that what a part is, is not a state.
  Rejected: **Status Colour** says colour carries *exactly two* meanings, and a
  third would have to be held in a reader's head alongside them.

## Consequences

- **Two things a builder can do that they could not**: see what the whole droid is
  doing at once, and move a part by picking it on a picture of their droid.
- **The pose press inherits #292's recorded consent gap** rather than creating
  one, exactly as ADR 0062's does — that consent gates `POST /api/drive` and the
  action-test path, never `POST /api/servo`, a sequence or the dome.
- **#298's route obligation is honoured without an exemption.** The selection
  panel names the next move and offers it, so a dimmed part on this surface never
  stops at *no*. ADR 0062 needed the exemption because the timeline has no such
  panel; this one does.
- **`CONTEXT.md` **Part Kind** said it *"colours a surface"*, which #327's palette
  rule contradicts.** Corrected to treatment with this ADR; #320 wrote it three
  hours before #327 landed and it was never revised.
- **`CONTEXT.md` **Fitted Parts** said *"clicking an unfitted Part adds it"*.**
  Adding stays; it becomes a named action in the selection panel rather than a
  meaning attached to the click. Corrected with this ADR.
- **Rotation is dome-only.** ADR 0051's amendment already settled it — *"a body
  view takes none of this — an elevation is not a radial projection"* — so #317's
  proposed specific 9 applies to the dome drawing alone.
- **A front face carries twelve of the fourteen body parts and a rear face two**,
  so side elevations earn nothing and are not drawn.
- **The legend is generated from the states actually drawn**, per the pattern
  recorded on #293, rather than a fixed list that can drift from the surface.
- **A body view still has no light to draw.** #317's body says the **Charge Bay
  Indicator** and **Data Panel** give a front elevation something no dome view
  had; the catalog declares 42 entries and no light Parts at all, so today there
  is nothing there. The gap belongs to #301 and #333.
- **`dup` is not this ticket's and no longer exists to decide.** ADR 0050 made a
  Part driveable by at most one **Output**, so the reference's duplicate-mapping
  state has no counterpart here; two Parts on one Output is a shared lead and is
  legal.
- **#289 records the wrong renderer owner**, in its body and again in its
  resolution — *"#296 owns the renderer"*, where #296's own body hands rendering
  to #317. Corrected by comment on #289 with this ADR.
