# The timeline edits what the builder wrote, and the card editor retires when it can

Status: accepted (2026-09-09, issue #289). Describes the **target** model.
Nothing in it ships today.

## Context

The sequence editor **shipped**, and it is a card list: `data/seq.js` renders one
`.step-card` per step (`:714`), `draggable="true"` for reorder, collapsing into a
form at `:632-654` - 131 card references across 2814 lines. Issue #10, which
asked for that redesign, is closed. A timeline is new work on top of a working
surface, not a redo of it.

Because "how should it look and behave" is the question talking cannot settle,
#289 built rather than argued: `DM:CANTINA` drawn as a timeline, from
`SEQ_LOOP(100, 26, 1846, 14000)` over a 26-step body
(`src/tasks/sequence_catalog.cpp:242`), with the beat grid derived from the
sequence rather than typed in - 1846 ms is four beats at 130.0 BPM. It put one
question to the operator: the same fourteen seconds as the one 4-beat object a
builder made, or as the 182 panel commands the droid receives.

Two measurements of the shipped editor decided the rest.

**Seven step kinds, and none of them is undrawable.** `data/seq.js:550` names
them - Sound, Panel Action, Spin Dome, Servo Loop, Random Flutter, Sound
Category, Sequence End - plus four Panel Action sub-modes (Visual Preset,
Logic / PSI Mode, Logic Text, Holo Effect) discriminated by inspecting a command
prefix at `:560-575` and absent from the editor's own reference panel. Random
Flutter looked like the exception and is not: it carries a **set**, not a target
- `SLOTSET_RING` is 7 named parts, `PIE` 6, `ALL` 13
(`include/sequence_engine.h:82-87`) - so it draws across those lanes with the
pick undetermined, structurally the same drawing as a **Gesture** over a set.
Spin Dome already carries a duration. The four light sub-modes become Part lanes,
because ADR 0045 makes a light a **Part**. Only `SLOTSET_HOLD` - *"reuse the
target picked by the previous STEP_RANDOM"* - has no lane of its own; its lane is
a previous step's dice roll, so it attaches to its predecessor.

**There is no undo.** Zero matches for undo, redo or history across 2814 lines.
The only way back is a whole-session **Revert** - *"Discard unsaved changes"*
(`data/seq.js:1342`). On a card editor that is survivable, because every edit is
typing into a field you can retype. On a timeline whose mechanics are *drag body
= move, grab edge = resize*, one mis-drag on the twentieth evening has exactly
one recovery, and it is discarding everything done that night.

## Decision

**The editor edits the routine the builder wrote.** A loop is edited as the one
object it is; the expansion into the commands the droid receives is a
**read-only check** - something a builder can read, rather than a run they have
to watch. This is ADR 0046 reaching the surface: a sequence stores what the
builder meant, and it is now also what the builder edits.

**The card editor retires once the timeline can author all seven step kinds** -
including the four light sub-modes that have no editor of their own today. Not
before. #289 specific 1 lands the timeline read-only first, and a working surface
does not go away until its replacement can do everything it did. One model sits
behind both projections, so retirement costs no migration and there is nothing to
convert.

**Every edit is undoable, one at a time.** Revert stays as the whole-session
discard it already is.

## Why

**Editing the expansion would undo the reason ADR 0046 exists.** That ADR was
argued from the factory catalog's own cost: `DM:CANTINA` is 26 hand-written body
steps for a two-beat alternation on an 1846 ms period computed off 130 BPM by
hand. Storing what the builder meant and then editing what it became would hand
that arithmetic straight back.

**Two editable projections is how views drift.** Every mechanic - selection,
snap, multi-select, undo - would be built twice over one model and kept
consistent by discipline. The divergence surfaces as a defect a builder hits
months in, on the routine they care about most.

**Undo is what makes direct manipulation safe.** The current editor's caution is
a rational response to all-or-nothing Revert. A surface whose primary verb is a
drag makes that worse rather than better, so undo is a property of the first
editable slice, not a polish ticket after it.

## Considered and rejected

- **Editing the expansion, with the authored loop as the summary.** Rejected by
  the operator. Retiming a 4-beat loop would be 182 edits.
- **Keeping the card view permanently as a second editable projection** (#289
  specific 6's own recommendation). Rejected: two editing surfaces over one
  model, for a view the timeline already covers.
- **Keeping it permanently as the read-only expanded check** - the research's
  P3.3 fate for the legacy vertical list, where Frames is read-only compiler
  output. Tempting, because the expansion needs a home. Rejected because the
  expansion is a **view of the timeline**, not a separate surface: reading 182
  commands as a vertical card list is precisely what the timeline exists to
  replace.
- **Retiring the card editor when the timeline lands.** Rejected: it would take
  the only editor for the four light sub-modes with it, and #289 specific 1
  deliberately lands read-only first.
- **Revert-only recovery on the timeline.** Rejected; see *Why*.

## Consequences

- The timeline needs an editor for `DL:` / `DT:` / `DH:` before the card editor
  can go. That is the first time those three get a first-class existence - #320
  recorded them as sub-modes with none.
- `SLOTSET_HOLD` needs a drawing that attaches a block to its predecessor rather
  than to a lane. It is the only step form with no lane of its own.
- Undo is a requirement on the first editable slice (#299), not a later ticket.
- The read-only check is a second reading of the same steps, so it needs no
  storage, no migration and no second model.
- **Found here, routed rather than closed here:** `docs/droid-parts.yaml`
  declares **42 entries and no light Parts at all** - 40 that move (6 pies, 14
  dome panels, 6 holoprojector axes, 8 body doors, 6 body arms) plus 2 non-servo
  orientation fixtures, with 2 driven today. There are no logics, no PSIs, no
  holo lights and no Magic Panel, and the holoprojector entries are pan and tilt
  only (`:84-91`). So ADR 0045's *a light is a Part*, and the capability line
  *Light a Part*, have nothing in the catalog for a lane to draw from. The
  catalog belongs to #301 and #333; this records the gap.
