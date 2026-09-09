# The first timeline ships read-only, and the droid moves only when asked

Status: accepted (2026-09-09, issue #299). Describes the **target** model; none
of it is implemented yet.

## Context

#299 asked what a first timeline slice renders and what a builder can do with
it. It was written on 2026-09-07, and its ground moved twice before it was
answered.

**The slice it proposed already exists as a prototype.** ADR 0057 resolved #289
against a built artifact rather than an argument, and that artifact —
`prototypes/289-sequence-timeline/cantina-timeline.html` on
`epic/operator-experience` (`51bca564`) — is a read-only timeline of real
`DM:CANTINA` data with a scrubbable playhead, a dome SVG posing at it, a beat
grid, grouped lanes with an `N of M` count, two lane filters, and an
authored-versus-expanded toggle. The research's whole justification for a
read-only tracer was that it *"proves layout + scrub + preview"*
(`tasks/research-r2d2-astromech-simulator.md:397`). That proof exists. What does
not exist is anything a builder can open: the prototype is a static file built
from hard-coded steps, on a branch, and it says of itself *"Nothing here talks to
a controller."*

**Three ADRs landed after the ticket was written.** ADR 0058 gave a sequence a
tempo, ADR 0060 put every beat and a bar number on the ruler, and ADR 0061 made a
**Take** one object the timeline must eventually draw. #299's specifics predate
all three, and specific 1 still describes blocks positioned from `tMs` and
`durationMs` — a C++ struct member and a field carried by two of nine step kinds.
The on-disk step time is `"t"` (`src/seq_json.cpp:137`, `:357`).

Three measurements decided the rest.

**The prototype's block rule was validated on the catalog's most favourable
sequence.** A block is derived by pairing `:OP` with `:CL` on the same Part.
`DM:CANTINA` opens 13 and closes 14, so almost everything pairs. Catalog-wide it
is **175 opens against 128 closes**, and `DM:OPENALL` opens 32 and closes none —
deliberately, because it is a `TOGGLE_ALL` sequence whose close half is a
separate step list (`src/tasks/sequence_catalog.cpp`). A routine that leaves
panels open when it ends is the normal case, not the exception.

**A tempo is authored, and this slice cannot author.** ADR 0058's three routes —
tapped, analysed, typed — are all acts a builder performs, and the result is
stored on the sequence. None of it is built and no saved sequence carries one.
The prototype had a grid only because `DM:CANTINA`'s 130 BPM is written in a
comment at `src/tasks/sequence_catalog.cpp:239`, which is exactly the fact ADR
0058 exists to make machine-readable.

**There is no preview seam.** The only way to see any part of a routine today is
to run all of it — `POST /api/seq/test`, which the handler's own comment calls
the *"same ungated path as dome/cmd"* (`src/web/api_seq.cpp:306`). A builder who
wants to know what second six looks like watches the whole thing.

## Decision

**The read-only timeline ships.** The prototype becomes a real view over the
builder's own saved sequences, read through the existing `GET /api/seq?name=`
that `data/seq.js:2422` already calls — so the slice needs no firmware change.
It is not editable, so it owes no undo; ADR 0057's undo requirement attaches to
the slice after this one.

**A move draws for as long as it takes, and then says it is still there.** A part
that opens and never closes within the routine draws as a solid block lasting
that part's own travel time, then a lighter run to the right edge meaning *still
open when this finishes*. How long a full throw takes is stored on the **Output**
(ADR 0052), so a part nothing drives gets the lighter run alone. This is the
reference's `opens-and-stays` block shape
(`tasks/research-r2d2-astromech-simulator.md:34`) with our own motion model
behind it, and it is the first surface that shows a builder what their routine
leaves open — which is what they walk away from.

**The beat grid comes only from a tempo stored on the sequence.** No stored
tempo, no grid: none at all today, and a correct one with no further decision the
moment tempo authoring ships. The view never infers a tempo from step spacing and
never holds one of its own.

**The droid moves only on a deliberate press.** The drawing follows the marker
freely and silently; a separate press sends the pose at that moment as one
command. Nothing follows a dragging finger. This is the preview #289's resolution
named — the dome SVG *plus the real droid* — with the continuous half refused,
and it is the first time a builder can check one moment without sitting through
the run.

**Parts the routine names that nothing drives are said once, above the routine.**
One line naming them; the lanes themselves stay dim, and no lane carries amber.
Not per lane: 24 of 42 catalog entries are `control: none`
(`docs/droid-parts.yaml`), so marking each one turns *author before you build*
into a screen full of things to fix, at a builder who deferred them on purpose.
This is the reference's own shape (`blocks.js:144-151`: *"4 bricks are not wired
to a channel yet: Panel 7, Panel 9"*).

## Considered options

- **Go straight to an editable first slice**, which ADR 0057's own consequence
  line requires by naming #299 as it. Rejected: read-only is already proven, and
  editing needs undo, draft persistence, and the snap-to-beat verbs ADR 0060
  requires and no ticket owns.
- **Close #299 as answered by the prototype.** Rejected: the prototype is a
  static file on a branch, so a builder gains nothing from it.
- **One block from the open to the right edge.** Simplest, and it does show the
  part is left open. Rejected: it reads as though the panel moved for fourteen
  seconds when it moved for one.
- **A mark at the instant, with no span at all.** Most honest about what the
  droid receives. Rejected: it is what the card list already shows, so the
  timeline would teach nothing new.
- **Drawing a toggle's close half beside its open half.** Rejected: it draws
  steps that are not in the routine being read.
- **A tempo typed on the view and held in the browser**, exactly as ADR 0051's
  amendment holds the dome angle — view-only, browser-local, a preference about
  the viewer. Genuinely tempting, and it would give a grid today. Rejected: it
  would compete with the stored tempo ADR 0058 defines, with a source and a
  confidence, and be re-typed on every device.
- **Inferring a tempo from the routine's own spacing**, which is what the
  prototype did for `DM:CANTINA`. Rejected: it can be confidently wrong, and a
  read-only view cannot offer the correction ADR 0058 requires of every detected
  tempo.
- **The droid following the drag.** The most direct reading of the research's
  *"flip live-arm and the physical dome follows"*. Rejected: it streams positions
  at the speed of a finger, and the first move on an uncalibrated part is a jump
  rather than a ramp.
- **The droid moving only during playback.** Rejected: close enough to what
  **Test on Droid** already does that it wins little.
- **Amber on every part nothing drives, wherever drawn.** The plainest reading of
  **Status Colour**. Rejected on the 24-of-42 arithmetic above.
- **No amber anywhere on the surface**, as the prototype has it (*"neither
  appears on a block or a lane"*). Rejected: it also drops the one signal that
  says this routine will not fully perform. The note above the routine carries
  it instead, which is what **Status Colour**'s *"a Part **in a sequence** that
  no Output claims"* already scopes.

## Consequences

- **Two things a builder can do that they could not**: read their own routine as
  time, including what it leaves open when it ends; and send the droid to one
  moment without sitting through the run.
- **The pose press is motion**, so it takes the same **Non-RC Control** consent
  that calibration and **Find by Moving** take. It inherits #292's recorded gap
  rather than creating one: that consent gates `POST /api/drive` and the
  action-test path only, and never `POST /api/servo`, a sequence or the dome. It
  needs no new bound — it commands exactly what a normal run commands at that
  instant.
- **#298's route obligation is not honoured by this slice.** The note names the
  parts and does not link to **Parts**. Deliberate, and recorded here so it is a
  known omission rather than a defect the next reader re-derives.
- **ADR 0057's consequence line naming #299 as "the first editable slice" is
  wrong**, and is corrected with this ADR. #299 is the last read-only one.
- **The snap-to-beat verbs ADR 0060 requires have no ticket.** #289 handed their
  mechanics to #299, and #299 is read-only, so nothing owns them.
- **Named and not decided**: drawing a **Take**, drawing an overlap between two
  Takes (ADR 0061 says the timeline must eventually), and how a Random Flutter
  shows an undetermined pick across the lanes of its set.
- **A sound lane and a light lane have nothing to draw from.** ADR 0057 already
  recorded that `docs/droid-parts.yaml` declares 42 entries and no light Parts;
  that gap belongs to #301 and #333 and reaches this surface unchanged.
- **#299's headroom figure is wrong and is corrected with this ADR.** The ticket
  and #175 both carry 405,509 B; the LittleFS partition is 655,360 B
  (`partitions/partitions_ota.csv:31`) and the gzipped image leaves **356,036 B**,
  or **253,952 B** once files are rounded onto 4 KiB blocks. The figure was
  already wrong at `bee25da6`, the commit it cites.
