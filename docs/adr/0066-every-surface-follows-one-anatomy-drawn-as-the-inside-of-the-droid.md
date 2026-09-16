# Every surface follows one Surface Anatomy, drawn as the inside of the droid

Status: accepted (2026-09-13, issue #395; identity direction continued by the
operator on 2026-09-14). Describes the **target**; the first mockups exist
locally and none of it ships yet.

## Context

On 2026-09-13 the operator observed that new surfaces were being added onto the
old design rather than aligned to the bigger picture. Measured on
`epic/operator-experience` @ `569ff095`: 13 HTML surfaces, 11 of them shell
delegates; 2 laid out after the Operator Shell existed (Parts, Dashboard) and 9
before it, now served inside it; `data/style.css` at 5,751 lines with 51
tokens. Every UI brief since B1 (#341) carried the rule *add tokens or your own
labelled block, never edit a shared rule*, which keeps one shared stylesheet
safe across three waves and, as its cost, makes every new surface a block beside
rules laid out in 2026-06.

The gap was decided into existence rather than drifted into. #325 settled the
frame (pages under a persistent shell, Activity Groups, the estop everywhere)
and recorded, verbatim, *"layout stays with the frontend work"*. #327 settled
the state system (four Availability Families, two Status Colours, dark only,
computer resolution) and folded *"which four treatments carry the families -
pixels, for the frontend work"*. No ticket owned where things sit on a page,
and "the frontend work" was each slice's worker under the fence above.

The design source already carried the anatomy. The operator's direction of
2026-08-22 was *"take a lot of his structure and basics"* from the reference
project; the second research pass measured that its section head is one shared
builder at 64 call sites (a mono uppercase title, a subtitle that is always a
count, a state, a provenance or a purpose), that a calibration dial is a panel
below the table and never a modal, that an inspector follows the clicked row,
that rows which move hardware sit apart from rows that tick boxes, and that a
repaint never rebuilds DOM. `docs/ui-copy-voice.md` had adopted the copy half
of those patterns; nothing had adopted the layout half.

## Decision

**Every surface follows one Surface Anatomy** (`CONTEXT.md`): title with the
question it answers, sections whose heading carries a count, a state, a
provenance or a purpose, the work area, the acts named beside what they act on,
a feedback line. The structure is the reference project's, adapted to a live
controller and to the shell #325 decided; it is not a pane composition and it
does not reopen #325 or #327.

**The identity is the inside of the droid: an instrument panel.** Dark surfaces,
the dome's panel lines as structure, the droid's blue as the single accent, mono
readouts, bezelled plates, restrained and modern; not a generic web application.
Amber and red stay reserved for state. **No emoji on surfaces**: icons come from
one small drawn SVG set that inherits text colour and keeps its label. The
AGENTS.md rule that preferred emoji over verbose labels is retired.

**The shell chrome is in scope** with all thirteen page bodies: the topbar, nav
and Status Plate are re-laid to the anatomy while keeping #325's substance.

**Mockups first.** Three surfaces (the chrome with Dashboard, Parts, Setup) are
mocked as static HTML before the sweep is minted, and the sweep is minted from
the mockup the operator approves. The Codex study of 2026-09-14/15 (*Service
Bay* layout, cobalt and silver palette, an MDI SVG icon subset, a collapsed
Controller Console on Dashboard) is the first candidate and is recorded on #395
as **not selected**; approval is the prototype ticket's job.

**It runs now, beside the C1 chain**, with Parts aligned last after C1e, so
Wiring (C2), guided Setup (C3) and the Body View (C4) are built to the anatomy
rather than retrofitted; D1's copy sweep and D2's guard then run once over
aligned pages. The sweep is the one slice permitted to edit shared stylesheet
rules and runs alone among UI slices in the files it holds.

## Considered options

- **Keep going and retrofit at the end of Wave 2 or Wave 3.** Cheapest to plan;
  four new destinations built bolted-on and reworked, D2's guard run twice, and
  the defect the operator had already noticed shipped three more times.
- **Accept the epic's shape and leave alignment to a later effort.** Rejected:
  the Destination says a builder *reaches for* this controller, and nine of
  thirteen surfaces would say otherwise at closure.
- **Codify what the shell and Parts already are as the specimen.** Rejected by
  the operator: it keeps the emoji-card look the observation was about.
- **Keep the emoji, or keep them on the nav only.** Rejected: the reference's
  typographic scan (uppercase mono labels, outlined pills, verb-free chips)
  carries the glance, and a hybrid is a look nobody chose.
- **R2's livery on dark** (white and silver panels, brighter and more graphic)
  and **a holo-terminal look** (glow, scanlines, ornament). The first is the
  droid's outside rather than the controller a builder stands at; the second is
  the direction most likely to tip into kitsch.
- **Build the sweep's first slice as the specimen instead of mocking.** Faster;
  a rejected look costs a real slice and a real gate run.
- **A light theme, or reopening dark-only.** Not raised; #327 stands.

## Consequences

- `CONTEXT.md` gains **Surface Anatomy**; `AGENTS.md`'s emoji rule is retired.
- A prototype ticket under #175 owns the mockup pass and carries the anatomy's
  pattern library with its citations, so an implementer does not re-derive it;
  the sweep ticket is minted from the approved mockup and inherits it.
- `docs/ui-copy-voice.md` keeps the words; the sweep lands the written anatomy
  beside it, and a surface built later is measured against both.
- The `style.css` concurrency rule stays for every slice but the sweep; briefs
  for C2, C3 and C4 carry the anatomy once it exists.
- Tickets that describe a surface in the old vocabulary (emoji headings, cards
  by subsystem) are repaired at their next brief refresh, not reopened.
