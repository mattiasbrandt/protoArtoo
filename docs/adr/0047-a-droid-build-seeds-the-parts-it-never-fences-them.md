# A Droid Build seeds the parts, it never fences them

Status: accepted (2026-09-08, issue #333). Describes the **target** model.

What ships since #343: the Droid Build is stored on the controller, a fresh
flash records the catalog's pre-selected design at its default variant with
that design's complement fitted, one `applyDroidBuild()` seam seeds a stated
design without removing a fitted Part, and tier 3 of the Layout Fallback
Hierarchy consults the stated Dome Design instead of assuming MK4. What does
not: the three card kinds, the roadmap card, every mapping view's third visual
state and add-by-clicking, **Common Addition** vocabulary, and the
reconciliation surface for a dome whose reported layout disagrees with the
stated design.

## Context

protoArtoo does not know what droid it is bolted into, and has never been asked.
It assumes one, everywhere, silently:

| Where | What it assumes |
|---|---|
| `docs/droid-parts.yaml:1` | *"MrBaddeley MK4 (default model)"* — one catalog, one lineage |
| `docs/droid-parts.yaml:45` | `model: design: MrBaddeley MK4` — a label, not an axis: nothing varies by it |
| `CONTEXT.md` **Panel Command Target** | *"bounded to the MK4 commandable set"* |
| `CONTEXT.md` **Dome Layout View Model** | *"the vendored MK4 model as offline fallback"* |
| `CONTEXT.md` **Layout Fallback Hierarchy** tier 3 | *"render the offline MK4 model"* |

A real droid is a mixture. The operator's own is an **MK4 complex dome on an MK4
simple body**, assembled months apart, and that is the ordinary case rather than
an edge one. A builder with an MK3 body meets a parts list for a droid they did
not build.

Two facts already in the repository show the axis is needed and is being faked in
prose. `docs/droid-parts.yaml` carries **four** parts with `cad_name: null` —
`gripArm`, `gripClaw`, `interArm`, `interTool` — belonging to no design at all;
exactly one of them explains itself, in a comment reading *"common MK4 addition;
not in the base MK4 design exports"*. And `dome_pies_small: 6` on line 48 is a
design-level fact living in a header block because there is nowhere else to put
it.

**There is no prior art to copy.** The reference simulator's wizard step 1 is
called `Model`, and the research doc translated it as *"droid design origin:
MrBaddeley MK2/MK3/MK4"* — but the reference's own step is a different axis
entirely. Its four cards are *R2-D2 MK4 / Anzellan head / Polar Mouse + chariot
/ Builder*: which **robot** is loaded, one CAD payload at a time, with an amber
note reading *"This picks what is on the stage, not what is switched on"*
(`tasks/research-r2d2-sim-shots/wizard-01-model.png`). A simulator loads one
model; a builder owns one droid made of parts from several sources. The single
transferable idea is its fourth card — *Builder: build your own mechanism from
parts* — an escape hatch for a droid that is no published design.

## Decision

**A Droid Build seeds the parts, it never fences them.**

A **Droid Build** is everything protoArtoo knows about which droid it is bolted
into: a **Dome Design** and a **Body Design**, each with a **Design Variant**,
together with the **Fitted Parts** they seeded and any **Common Addition** the
builder added.

**The Fitted Parts are the truth; the designs are a running start at them.**
Choosing MK4 fits the parts that design carries in one press. The builder then
adds a part it does not carry — the gripper arm they printed — or drops one they
never fitted. Nothing downstream is gated on the result: a Part outside the set
is still authorable, still saveable, and still reports `part-not-assigned` at run
if no **Output** claims it (#301).

**Two answers, not one**, because the two halves have different authorities. The
**Body Design** stands alone; nothing reports a body complement back. The **Dome
Design** is the builder's statement and replaces the hardcoded vendored MK4 at
tier 3 of the **Layout Fallback Hierarchy**, and when a connected dome reports a
layout that disagrees, the difference is surfaced for the builder to resolve and
never silently overwritten.

**Every surface draws the design's whole complement**, with what is not fitted in
its own treatment, distinct from the dimming that already means *nothing drives
it yet*. Clicking an unfitted part adds it. There is no *planned* state: a builder
choreographing for the arm they print this weekend fits it early.

**Three card kinds.** A design we carry is selectable; one we intend to carry is
an inert roadmap card (#298); and **my own build** is always selectable and seeds
nothing, so a droid that is nobody's published design is never locked out.
"Not supported yet" costs exactly the one-click seed.

**A design declares its complement.** In `docs/droid-parts.yaml` a Part is
declared once with everything true wherever it appears, and a `designs:` section
lists which part ids each design and variant seeds. A **Common Addition** is
simply a Part no design lists.

**Stored on the device, read by nobody in firmware.** The compiled Part
vocabulary becomes the union of every design's complement plus Common Additions,
so a Droid Build can never change what **Protocol Check** accepts and never
changes what saves.

## Why

**A builder authors for the droid they are making.** *Choreograph first, wire
later* (research §3.1) is already the map's second rule, and a design that fenced
the catalog would break it at the one moment it matters — the weekend a part is
being printed. Every argument for fencing is an argument for refusing work a
builder is in the middle of.

**The catalog already contains the counter-example.** `gripArm` is on an
unambiguously MK4 droid and in no MK4 export. A fence deletes it; a seed keeps it
and gives it, and its three silent siblings, an honest home as **Common
Additions**.

**Adding a design is the operation this exists to make cheap.** Designs are few
and parts are many, so a design listing its parts is one new block where an
applicability column on every part row would be dozens of edits. It also reads as
what a design now is: here is what this seeds.

**Bearings are community geometry, not design geometry.** Every `bearing_deg` in
the catalog comes from Printed Droid's published R2-D2 terminology drawing — one
drawing for the character. P4 sits where P4 sits whether the dome is MK3 or MK4,
so no shape of this decision needs per-design coordinates, and duplicating them
per design would be a drift surface with no reader.

**The gate rules on form.** ADR 0044 settled that **Protocol Check** is the only
thing that can refuse a save and that it rules on form. Narrowing its vocabulary
to the Fitted Parts would put intent into that gate and refuse the save for a
part being fitted this weekend.

**Silence is the failure mode this map already knows.** The reference's own UX
review found that finishing setup drops you into silence. Defaulting to no design
at all would rebuild that: an empty map for the builder who has not yet learned
there is a parts list. MK4 ships pre-selected and **named wherever it is acted
on**, including the wiring sheet's build summary, so it is an answer a builder
reads and corrects rather than an assumption printed as fact.

## Considered and rejected

**The design fences the catalog.** Simplest to reason about, and #333 recommended
it: a part the design does not carry cannot be named, assigned or authored.
Rejected — it deletes `gripArm`, contradicts *author before you build* at the one
moment it counts, and makes a normal mixed droid an error to describe.

**The design picks the drawing only.** Cheapest, and honest that geometry is the
one thing that genuinely differs per design. Rejected: the ticket's first promise
— that an MK3 builder is not handed a parts list for a droid they did not build —
is then not kept at all.

**No design value at all; ask which parts are fitted.** Maximum honesty about
what is real. Rejected: the mapping views have nothing to key a drawing off, the
dome reconciliation has no stated claim to reconcile against, and support and
sharing lose the one word that describes a build.

**A file per design.** Rejected on #301's reasoning without re-arguing it: one
declaration, one generator, two committed outputs, and a file per design
multiplies the exact drift surface that generator's check exists to guard.

**A per-design bearing table.** The most general shape, and the only one that
could express a design placing a panel differently. Rejected: it buys a
capability nothing has asked for, against a community drawing that is
design-independent.

**Narrowing the Protocol Check vocabulary to the Fitted Parts.** The strongest
possible honesty about what a save means. Rejected on ADR 0044 and on the seed
rule together: it refuses a save for the part you are about to fit.

## Consequences

- **`docs/droid-parts.yaml` gains a `designs:` section and loses its single
  lineage.** The `model:` block at line 45 becomes one design entry among
  several; `dome_pies_small: 6` stops being a header fact.
- **Three `CONTEXT.md` definitions stop naming MK4.** **Panel Command Target**
  becomes bounded to the catalog's commandable set across every design;
  **Dome Layout View Model** and **Layout Fallback Hierarchy** tier 3 fall back
  to the stated **Dome Design** rather than a vendored MK4.
- **The Component Picker gains an interaction it does not have.** A design's
  variant set is its own and may be empty, so the second control appears,
  repopulates and disappears with the first.
- **Every mapping view gains a third visual state** — not fitted, distinct from
  nothing-drives-it — and an add-by-clicking affordance. #317 and #296 inherit
  this.
- **A reconciliation surface is owed** for a dome whose reported layout
  disagrees with the stated Dome Design. The disagreement is the builder's to
  resolve; #328 shapes it.
- **Device config gains the Droid Build**, so an NVS shape and a backup/restore
  field come with it. No firmware logic branches on it.
- **#297's step count is settled at nine no longer.** Design origin is two
  questions, dome and body, so the wizard gains steps rather than one step.
