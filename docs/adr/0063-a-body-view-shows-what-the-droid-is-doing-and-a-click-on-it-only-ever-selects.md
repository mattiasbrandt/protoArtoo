# A body view shows what the droid is doing, and a click on it only ever selects

Status: accepted (2026-09-09, issue #317), amended 2026-09-18 (#408): the droid
is drawn as a recognisable R2-D2, and the "arrangement, not a likeness" rule is
withdrawn. Every other decision here stands. C4a (#352) built the renderer.
**Amended again 2026-09-19 (#408, the operator's pick of the `claude-sonnet` mock):**
open/closed is drawn in colour, the legend is a fixed one-word list, the "Other
part" slots leave the picture, and the body and the top-down dome share one card.
See *Amendment 2026-09-19* at the end; where it and the text above disagree, the
amendment wins.

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

**The droid is drawn as R2-D2, recognisably.** *(Amended 2026-09-18, operator,
#408.)* The drawing is based on the actual R2-D2, the Star Wars astromech droid:
a front view and a rear view a builder knows at a glance, with each part drawn
where it sits. The starting point is the operator's reference drawing
(`tasks/prototypes/r2d2_vector_transparent.svg`, a flat front view), and parts are
named the way Printed Droid's terminology names them.

**The features drawn follow the builder's design origin:** the **Body Design** and
its **Design Variant**, for example MrBaddeley MK4 Complex or MK4 Basic. A
Complex body shows its doors and hatches; a Basic body does not show doors it
never had; a **Common Addition** appears only once it is added. The droid shape
is R2-D2's for every builder, and the drawing says which design and variant it
is showing.

*Withdrawn:* the 2026-09-09 rule that the droid be "an arrangement, not a
likeness" — parts as shapes at nominal positions, and no recognisable silhouette
because it would be "artwork per design" and "someone else's droid". It produced
pictures no builder recognised as their droid, and every R2 build shares the
body shape; what differs between builds is which parts exist, which the variant
already answers.

**A part can be moved from the drawing, on a deliberate press.** Picking a part
and pressing runs it through its travel and back, as one command. Same shape as
ADR 0062's timeline pose — never following a drag — and like every other
browser-initiated servo move it asks for no **Non-RC Control** consent
(ADR 0064). Corrected 2026-09-09; this originally claimed the consent, which the
firmware has never applied to `POST /api/servo`. This is the ticket's own
capability question, answered yes.

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
- **A recognisable body silhouette.** Rejected 2026-09-09 as artwork per design;
  **taken on 2026-09-18** (see the amended decision above).
- **Parts as shapes at nominal positions, with no silhouette** (the 2026-09-09
  decision). Withdrawn 2026-09-18: nobody recognised the result as their droid.
- **A silhouette where we have one, an arrangement otherwise.** Rejected: two
  drawings to build and keep in step. Still rejected: there is one drawing, and
  the variant decides which parts are on it.
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

## Amendment 2026-09-19: the picked picture (#408)

The operator picked the `claude-sonnet` mock on #408 (`tasks/prototypes/408-droid-picture/claude-sonnet/`, gitignored, its `README.md` carries the dated history). Five decisions above change with it:

- **Open and closed are shown by colour, not geometry.** An open Part's footprint fills with the interaction accent and its stroke brightens; closed spends no colour; the footprint never leaves its own bay. This is the pattern the interactive dome already uses (`data/dome_panel_model.js`). Operator: *"open close should simply be represented with the colors (same as we did with the interactive dome design)"*. It replaces "open is drawn open" and narrows *Colour stays reserved*: amber still marks ends not measured, red still marks stopped or refused, and the accent now also marks an open Part on a droid picture.
- **The legend is a fixed list of seven one-word states, always shown:** Closed, Open, Unmeasured, Limp, Unassigned, Unfitted, Picked. Operator: *"make it short and concise. ONE word only. Drop the extra unnecessary text"*, and *always all seven* (2026-09-19). It replaces "the legend is generated from the states actually drawn". A legend that never changes shape cannot drift from the surface either.
- **The "Other part" slots (`other1`..`other10`) are not on the picture, and not listed beside it.** Operator: *"these 'spare parts' are not a proper or common droid part concept"*. They stay what the catalog made them, a name for off-model hardware on a spare output, mappable on the Parts table and usable in a Sequence. Only the picture drops them. This replaces *Parts with no position stay listed beside the drawing*; #374, which existed to list them, closes as not planned.
- **One card, three faces: Front, Rear, Dome (Top).** The body is drawn front and rear from the operator's hand-edited line art (`front.svg`/`rear.svg`); the dome stays **top-down** (a front or rear view cannot fit every panel) and is redrawn in the same line language. It has the body's full parity: legend, Parts list, and Open/Close for every dome-link piece. Holoprojectors pan and tilt and never open, so they carry no open state and no Open action. The redraw replaces the current look of `data/dome_layout_render.js` wherever it is shown. With #409's catalog, the front carries eleven body Parts plus the arms, and the rear four (two breadpan doors, Body Panels 7 and 8).
- **A press opens or closes; it no longer runs a Part through its travel and back.** The selection panel offers *Open it* / *Close it* (the pick's buttons), and each press sends one command: a body Part's Output goes to the end the builder recorded for that side, and a dome piece gets its Panel Intent. This replaces *pressing runs it through its travel and back* and the panel's *move it*. **The estop holds every move started from a droid picture**, the Dashboard's dome included, while it is latched or not yet known. Operator, 2026-09-19 (#372): *"estop should stil prevent the interactive drawing movement of servos"*.

The selection panel's field is **Servo**, never "drives"; a Part with no Output is **Unassigned** and one not on this build is **Unfitted** (operator: *"that is stupid wording ... non operator word is openable or 'servo'"*). Surface details (vents, coin slots and the like) are drawing only: never selectable, never a state, never in the legend.

