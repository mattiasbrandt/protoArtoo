# A recorded take is one object, in its own file

Status: accepted (2026-09-09, issue #295). Describes the **target** model; none of
it is implemented yet.

The word *take* is #295's own and the reference project's before it — deliberate
performance language, kept rather than coined here.

## Context

A builder already performs: thumbs on sticks, buttons for the big moments, in
front of people. What they cannot do is keep it. The take that got the laugh
happens once and is gone. So this is not a recording feature bolted to an editor;
it is the fastest authoring path there is, and the one that produces motion nobody
would have drawn on a timeline.

#295 was written on 2026-09-07 and recommended that a take produce **an ordinary
Learned Sequence** — no new tier, no new storage. Two decisions landed after it
and that recommendation did not survive them.

- **ADR 0046**: a sequence stores what the builder *meant*.
- **ADR 0057**: *"The editor edits the routine the builder wrote. A loop is edited
  as the one object it is; the expansion into the commands the droid receives is a
  read-only check."*

**A recorded take is the purest expansion there is** — a stream of positions
sampled off a thumb, with no authored intent layer at all. Landing it as ordinary
steps would hand a builder exactly the form ADR 0046 exists to keep them out of.
#295's own 2026-09-09 comment had already named this: *"the open question here is
what a capture becomes on the way in, not merely what it records."*

The reference had no such tension — its sequences carry no intent layer, so
*"recorded takes become ordinary sequences"* costs it nothing. It is also a
browser app with no device, so it never had to fit a performance into a controller.

We do, and the arithmetic decides the shape. `include/seq_store_util.h:60-66` caps
a sequence file at **12 KB on ESP32** (24 KB on P4), against artoo-esp32's 640 KB
LittleFS partition and a limit of 16 sequences. At roughly 18 bytes for a
timestamped position that is about **680 samples for the whole file** — a
four-part take at 20 Hz is around **eight seconds**. Stored inside its sequence,
this feature does not exist on artoo-esp32.

## Decision

**A take is one object, edited whole.** Placed, trimmed and replaced as a single
thing; the samples inside are never stepped through. This is ADR 0057's own rule
for a loop, applied to the one artifact that most needs it.

**A take covers whatever the builder mapped, and takes layer.** Controls are
assigned to Parts before recording; the take holds those Parts. A later take over
other Parts sits alongside it, so a performance is built up in passes — dome
first, then panels — rather than nailed in one go.

**The captured motion lives in its own file, referenced by the sequence.** The
sequence stays small and a take gets its own budget and its own cap, so a take's
length is a real number rather than seconds.

**Cue presses become ordinary steps beside the take.** A button fired mid-take is
an authored act with a timestamp, not motion — so it lands where ADR 0057 wants
it, as a step the builder can move, retime or delete without touching the
performance. One recording produces two kinds of thing.

**A second take over Parts a first already covers is kept, the later one wins, and
the overlap is reported.** Nothing is destroyed, two takes of the same Part can be
compared, and #287's Rehearsal already has the shape for saying two takes drive
this Part here — findings carrying fields, and never refusing a save (ADR 0044).

Confirmed from #295 unchanged: a take captures **commanded targets, not raw
stick**, so replay goes through the same firmware ramp it was performed through —
speed, acceleration and easing live on the **Output** (ADR 0052), and a take
references **the Part, not the output address**, so it survives a re-address. One
control is a puppet string or a cue trigger and never both. Recording needs no
**Non-RC Control** consent because recording from RC *is* RC motion; replaying
from the browser does. The sample rate and the size bound are properties of the
take file.

## Considered options

- **A take becomes an ordinary run of steps**, as #295 recommended and the
  reference does. Rejected: it is the expansion ADR 0057 refused to hand over, and
  it makes every sample individually editable, which nobody wants.
- **A take is raw material, converted to authored steps on the way in**, and then
  discarded. Nothing new in the model. Rejected: the feel that made the take worth
  keeping is exactly what conversion loses.
- **One take per Part, always.** Finest grain, and layering falls out for free.
  Rejected: it splits a two-handed move — which #331 calls one **Gesture** — in
  half at the moment of capture.
- **One take covering the whole droid, one pass.** Nothing to assign first.
  Rejected: re-performing one Part means re-performing all of it.
- **Storing the captured motion inside the sequence file.** One file to back up,
  export and restore. Rejected on the arithmetic above.
- **Storing only the samples where a command changed**, inside the sequence file.
  A calm take fits; a busy one still hits the cap, and how long a builder may
  record would depend on what they played. Worth having as an encoding regardless
  of where the file lives.
- **Cue presses stored inside the take.** One artifact holding the whole
  performance. Rejected: fixing when a sound fires would mean performing it all
  again.
- **A later take replacing an earlier one for shared Parts.** Nothing ever
  overlaps. Rejected: it makes comparing two takes of one Part impossible, which
  was one of the things this ticket asked to push on.
- **Refusing a second take over the same Parts.** Never ambiguous. Rejected
  against #287's advise-never-refuse posture, and it is a refusal in the middle of
  a performance session.

## Consequences

- **A second stored artifact exists**, with its own cap, its own naming, and a
  lifecycle tied to the sequence that references it — including deletion when that
  sequence goes, and a place in the backup groups ADR 0056 defines.
- The 16-sequence limit (`include/seq_store_index.h:20`) does not obviously extend
  to take files, and the LittleFS free-space floor was sized against sequences
  alone. Both need a number before this ships.
- One recording produces **two** things — a take object and any cue steps — so the
  receipt for a recording says so rather than reporting one artifact.
- Two takes claiming one Part is a new Rehearsal finding, and the timeline has to
  show an overlap it has never had to draw.
- **Three answers here go past the reference deliberately**, and its own record
  says so: *"punch-in over part of a take: does not exist"*, *"performing one part
  while the rest plays back: explicitly prevented"*, and *"undo covers only one of
  the two take species"*. ADR 0057 already gives us the stronger undo rule.
- **Punch-in over part of a take is named and not decided.** Layering answers two
  of #295's three push-further asks; punch-in needs addressing *inside* a take
  object and nothing here settles it.
