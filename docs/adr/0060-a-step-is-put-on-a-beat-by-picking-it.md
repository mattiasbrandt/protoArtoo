# A step is put on a beat by picking it, and a Gesture repeats on the grid

Status: accepted (2026-09-09, issue #336). Describes the **target** model; none of
it is implemented yet.

## Context

ADR 0058 gave a sequence a tempo, a grid, a confidence and a source, and gave a
step a **beat index beside its millisecond**. This ADR is the surface over that:
how a builder gets a step onto a beat, retimes a routine that is not on one, and
turns a track into something that moves.

The reference project's whole apply surface is **drag-and-snap plus two bulk
verbs**, and that design follows from a model where a step has no beat — only a
millisecond, so snapping is the only way a millisecond ever coincides with a
beat. We do not have that constraint, and its drag surface is measurably shaky:

- **Snap tolerance is 12 screen pixels** converted through zoom — 85.7 ms at
  default, 20 ms zoomed in, **400 ms zoomed out**. At 120 BPM a beat is 500 ms, so
  zoomed out the capture radius is 80% of a beat and everything snaps always. The
  same drag lands differently depending on how far the builder happened to be
  zoomed.
- **Resizing never snaps to a beat at all.** A step's end uses a 50 ms grid, and
  at 128 BPM (468.75 ms beats) no multiple of 50 ms is ever a beat.
- **Its bulk retime is destructive and its test hides it.** On its own fixture,
  snapping twelve 90 ms frames onto a 120 BPM grid put **2 of 12** boundaries on a
  beat; the other ten hit a 60 ms floor and the sequence got *longer*. The test
  passes by counting the floor as success. Its frames path has no undo.
- **Its generator emits the wrong thing.** `Build sequence` produces frames, not
  the authored blocks its own editor reads, so a generated routine opens there as
  *"there are no bricks in it to read."* The two halves never meet.
- **Its bar fit is degenerate.** `musicFitBars` claims to score onset energy but
  its test is boolean presence, so on any track with a steady pulse every offset
  ties and `barPhase` is **always 0**. Strong beats are beat 0 by accident, and an
  onset carrying `{seconds}` and no amplitude cannot be fixed without changing the
  onset.

## Decision

**A step is put on a beat by picking the beat.** The sequence already stores a
beat index, so picking is exact, needs no tolerance rule, and does not require a
pointer — which matters at a bench, the case #334 established when it ruled out
hover-only explanations. Dragging with snap remains, as a convenience over a fact
the model states directly rather than as the foundation.

**A move's duration may be expressed in beats as well as its start.** ADR 0052
stores speed as *how long a full throw takes*, in milliseconds, with the rate
derived from the **Endpoint Pair**. Expressing a duration in beats lets the tempo
drive it, so a move can fill a bar. Raising the tempo can then ask for a move the
part cannot execute — which needs no new mechanism, because #287 already made
**slowest throw** the Rehearsal's headline figure and ADR 0044 already settled
that a Rehearsal Warning never refuses a save.

**Turning a track into a routine is a repeat interval on a Gesture**, not a
generator. #331 settled the vocabulary — a shape spread across a target group,
resolved when it runs, converging independently on `chase / alternate / pulse`.
Pointing that at a track adds one thing: how often it repeats. The result is **one
editable object**, which is what ADR 0057 requires: *"the editor edits the routine
the builder wrote... editing the expansion would undo the reason ADR 0046
exists."*

**A repeating Gesture runs the length of the track.** ADR 0058 stores the
duration, so "make it dance to this" needs no number decided up front. A builder
who wants less shortens it afterwards.

**A whole routine can be retimed onto the grid, with undo and a receipt** naming
how many steps actually landed on a beat. The receipt is the point: a bulk edit
that mostly missed is worse than none, and the reference's silence about it is
what its own test rewarded. ADR 0057 already requires the undo.

**The builder sets the downbeat.** A tapped tempo's first tap is it, for free; an
analysed one gets the downbeat set on the waveform. This avoids the reference's
degenerate bar fit entirely rather than reproducing it, and it is exact.

**The grid shows every beat, strong ones weighted, with bar numbers.** There is no
snap-mode picker: a mode restricting *which beats a builder may pick* is a
restriction rather than a help. **Snap tolerance, where dragging still uses one,
is a time and not a pixel count.**

## Considered options

- **Dragging with snap as the primary act**, as the reference does and as this
  ticket's specifics 1-5 assume. Rejected: it needs a tolerance rule its own
  version got wrong, it is the hardest act at a bench, and it is a worse way to
  state something the model can now say exactly.
- **Generating from a track as the primary act.** Rejected: fastest path from a
  track to a moving droid, and it leaves a builder who wants one step on one beat
  with nothing.
- **Durations stay milliseconds; only starts are beats.** ADR 0052 unchanged and
  nothing can ever ask for the impossible. Rejected: a move can then never fill a
  bar, which is the musical thing a builder wants.
- **A beat-expressed duration as a ceiling**, taking the shorter of the physical
  time and the beat span. Rejected: nothing to warn about, and the builder's
  fill-the-bar intent is dropped silently — against the grain of every other
  decision here.
- **A generate step that writes a routine to take apart**, the familiar shape.
  Rejected by ADR 0057 outright: it is editing the expansion, on ADR 0046's own
  `DM:CANTINA` argument.
- **No generation at all**, a builder authoring Gestures one at a time. Rejected:
  smallest surface, and a builder who cannot choreograph gets an empty timeline
  and a tempo.
- **Fitting a routine to a named span of bars** instead of snapping each step.
  Preserves relative spacing and never collapses. Rejected as the primary bulk
  verb: it aligns the ends and not the middle, so steps land on beats only if they
  already did. Worth revisiting as a second verb if snapping proves too blunt.
- **No bulk retime at all**, on the grounds that picking is exact. Rejected: a
  builder retiming an existing 26-step routine would do every step by hand.
- **Computing the downbeat from onset strength.** Rejected: it is the part of the
  reference that measurably does not work, and it needs an onset to carry an
  amplitude it does not have today.

## Consequences

- A step gains a way to be given a beat directly, and the timeline gains bar
  numbers the reference does not have.
- **A duration in beats reaches ADR 0052's motion model**: what is stored is still
  a time, and the beat expression resolves to one. Raising the tempo can produce a
  Rehearsal Warning on a move that was fine before, which is correct and needs
  saying in the copy.
- A **Gesture** gains a repeat interval and an extent. `CONTEXT.md`'s entry moves.
- No snap-mode setting exists, so nothing needs persisting or explaining. The
  reference persists `PREFS.seqSnap` and has four labels for three behaviours, one
  of which ("Off / manual") still quantises to 10 ms.
- The bulk retime is the single most destructive edit on this surface. Undo and
  the receipt are properties of the first slice that ships it, not a follow-up.
- **`DM:CANTINA` would retime cleanly**: its steps sit at 0 and 923 ms against a
  461.5 ms beat — exactly two beats, because it was hand-computed to the music. A
  routine that was *not* hand-timed is where snapping collapses, and the receipt is
  how a builder finds out.
- Two reference defects are recorded as things not to reproduce rather than
  decisions: its ruler draws beats the active mode will not accept
  (`blocks-ui.js:672-681` passes `'all'`, not the active mode), and its snap label
  reads **"bar 12"** using the beat index — beat 12 in 4/4 is bar 3.
