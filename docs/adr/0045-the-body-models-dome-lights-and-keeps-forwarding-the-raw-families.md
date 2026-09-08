# The body models dome lights and keeps forwarding the raw families

Status: accepted (2026-09-08, issue #320). Describes the **target** model.
The forwarding half is what ships today and does not change.

## Context

protoArtoo is the body master of a droid whose lights live in the dome. How much
it knows about them was assumed to be "nothing", and that assumption was wrong in
four places at once:

| Where | What it already knows |
|---|---|
| `data/dome_layout_render.js:115-127` | The **Dome Layout View Model** carries `element_type` of `panel` / `holo` / `psi` / `logic`, and the renderer draws all four |
| `data/seq.js:111-160` | Operator-labelled authoring — `FLD` -> "Front display", `F`/`R`/`T`/`A` -> "Front holo" / "Rear holo" / "Top holo" / "All holos", eleven named holo effects, a colour set |
| `src/protocol_check.cpp:302-620` | `DL:` / `DT:` / `DH:` validated with per-effect colour **and** duration matrices |
| `docs/droid-parts.yaml` | `lit:` on six panels — Magic Panel, both PSIs, both Logic Displays |

Alongside that, `docs/commands.md:59-64` forwards `*`, `@`, `%`, `&` and `!` to
the dome **uninterpreted**, and `dome_layout_render.js:171` gates commandability
on `element_type === "panel" && elem.mapped`, so a PSI is drawn and never
actionable.

So the real problem was never a missing model. It was **four vocabularies for one
set of physical things, with nothing saying which is authoritative** — and a
passthrough that changes those same devices behind whichever one wins.

## Decision

**The Droid Parts Catalog is the model.** A light is a **Part** exactly as a pie
panel is one; the catalog's `lit:` annotation grows into a **Part Kind**; and the
**Dome Layout View Model** stops being a second inventory and becomes what it
already is for panels — the connected dome's report of what it can drive now.
That is the identity-versus-availability split this project already uses in the
**Editor Availability Gate** and in #303's project-claim-versus-controller-claim
axis.

**And the raw families keep forwarding, unchanged.** `*` and `@` still reach the
dome uninterpreted, for the very devices the model describes.

The two coexist because of what the model claims: **it records what protoArtoo
commanded, never what the device is**, and every surface that shows it says so.

## Why

**A builder's bindings are worth more than our internal tidiness.** ShadowMD's
function catalogue is 89 numbered MarcDuino functions across 83 button slots, and
it is the hobby's lingua franca. A controller that refuses `*RD00` because it
would rather own the vocabulary is a controller a builder has to work around on
their first evening.

**Intent-not-truth is already this project's answer to exactly this shape.** #286
made the position bar report the commanded ramp because nothing reads a servo
back; ADR 0044 made the **Rehearsal** advise rather than gate because certainty is
not authority. A model that claims commanded intent can be honest while a
passthrough exists beside it; a model that claims device truth cannot.

## Considered and rejected

1. **Take ownership: stop forwarding, make `DL:`/`DT:`/`DH:` the only way in.**
   One writer, model always true. Rejected: it breaks every builder arriving from
   the established lineages, and it spends their muscle memory on our consistency.
2. **Forward, and sniff the forwarded commands to update the model.** Rejected: it
   is a parser for somebody else's vocabulary, maintained by us, that goes quietly
   wrong the first time the dome fork adds a verb — and the dome fork adds verbs.
3. **Have the dome report light state back**, making the model true rather than
   merely honest. Not rejected on merit — the layout already carries per-element
   `active` / `disabled` and refuses to trust them unless freshly fetched, so the
   channel exists. Deferred because it commits the dome firmware to a new contract
   from a body-side decision ticket, which is the same thing #287 declined to do
   about dome motion timing. It is fog on the map rather than a decision here.
4. **The layout as the model.** Rejected: it describes one connected dome, so with
   the dome unplugged the body would forget the droid has PSIs — and a Magic Panel,
   which is a body prop, would be unnameable.

## Consequences

- **A holoprojector is three Parts** — pan, tilt and light — because a Part is one
  thing an **Output** drives. Grouping them for the operator is presentation.
- **Part Kind is advisory and never refuses a mapping.** Plenty of builds move
  something the reference drawing shows as a display.
- **`Servo Output` narrowed.** A row that can drive an RGB strip or an indicator
  chain is an **Output**; a Servo Output is the servo kind of one. Calibration,
  the endpoint pair and **Output Release** belong to that kind, not to every row.
- **Light steps split by who executes.** A dome light is authored against the Part
  and resolved to a `DL:`/`DT:`/`DH:` string before save and run, per ADR 0008, so
  no new storage shape arrives and the deferral in **Saved-Sequence Storage**
  stays deferred. A body light rides whatever #319 settles for our own outputs.
- **Charge Bay Indicator and Data Panel become body Parts.** ReelTwo classifies
  them under `body/`, they sit on the front of the body, and driving them from the
  dome sends MAX7219 clock and data through a rotating slip-ring contact.
  BadMotivator and FireStrip stay dome-side, where ReelTwo puts them and where the
  fork already wires them. Modelling them forces no rewire.
- **A surface must never present a modelled light state as fact.** That is the
  same honesty obligation the position bar and the Rehearsal already carry.
