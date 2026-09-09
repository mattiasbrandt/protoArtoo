# A backup restores in three groups, and replaces what it lands on

Status: accepted (2026-09-09, issue #294). Describes the **target** backup and
restore contract. Nothing in it ships today beyond the four-checkbox restore
panel it replaces.

## Context

A builder's controller holds four kinds of thing they made: the routines they
authored, the description of the droid those routines run on, the RC Map that
decides what the droid will do tonight, and the controller's own setup. A
filesystem wipe, a reflash, a rebuilt linkage or a replaced board destroys some
subset of that, and today only part of it can be recovered.

**Measured 2026-09-09.** The backup download fetches exactly four endpoints -
`/api/config`, `/api/rc/map`, `/api/audio/tracks`, `/api/audio/mood-map`
(`data/setup.js:1277-1281`). There is no `/api/seq`, so **Learned Sequences are
not in a backup at all**, while `data/setup.html:451` tells the builder it will
*"Save all your droid settings to a file on your computer."* The one thing that
took months to author and the one thing a filesystem wipe destroys is the one
thing the file does not contain.

The restore half is better than the ticket assumed: a two-decision flow already
ships - choose a file, read a summary, tick which sections to write, press
*Restore selected* (`data/setup.html:463-482`, `data/setup.js:1534-1537`). Its
groups are `config`, `rc_map`, `audio_tracks`, `audio_mood_map`.

### Cross-droid sharing is deferred, not decided

The map excluded *endpoint-normalised* cross-droid sequence sharing on
2026-08-21, when a step was assumed to carry raw servo positions. That premise
has since expired. A **Body Step** names a **Part** and *how far it goes as a
fraction of that Part's own throw*; speed, acceleration and easing live on the
**Output** and not in the sequence (ADR 0052, #319, #331) - `CONTEXT.md` already
puts it as *"a builder's choreography travels and their physics does not"*. Dome
steps are **Panel Intent Command**s. **Nothing in a protoArtoo sequence is
expressed in microseconds**, so the ~60-line rescaling the reference needs has no
work to do here.

The operator's call on 2026-09-09 was nonetheless **not now**: cheap to build is
not the same as wanted, because a shared routine still assumes a dome complement
and the moment builders trade files the project owns a compatibility promise it
never made. Recorded as **deferred, not decided** - it may open later.

Deferring it costs nothing, which is unusual and worth stating: portability is a
property the step model already has rather than a feature anyone would have to
add. Opening sharing later is a policy change, not a format change - provided a
foreign file is refused today as *not supported* rather than *impossible*.

## Decision

**Import is for your own droid, across time and hardware.** Backup, restore after
a rebuild, and moving to a replaced or second controller. A file from anywhere
else is identified and refused with a reason, never half-read.

**A backup carries three restorable groups**, and the second decision picks which
land:

| Group | What it holds | What it is exposed to |
|---|---|---|
| **Choreography** | Learned Sequences | authoring - changes when the builder writes |
| **The droid** | the **Droid Build**, **Fitted Parts**, Part-to-**Output** assignments and calibration, component config, audio roles | the workshop - changes when a linkage is rebuilt |
| **Tonight** | the **RC Map** | the hall - changes on the day |

They are three because they go stale at three different rates, and because the
RC Map is the one with a clock on it: since #330 it is the running order, so
restoring a month-old one twenty minutes before a show replaces what the droid
will do. The shipped panel already keeps `rc_map` apart from `config` for
reasons nobody wrote down; this states them.

**A restore replaces within a group, never merges.** Restoring choreography makes
the sequences exactly the file's. The 16-slot cap can therefore never be
exceeded, because the file was itself a valid droid, and a builder knows what
they have afterwards without reading a report. A copy of what is about to be
replaced is offered first, and **a failed save stops the replace** - which is what
makes the offer load-bearing rather than decorative.

**A cross-board restore takes what is board-independent and says what it left.**
The file records which board wrote it. Restoring onto a different one writes
choreography, the RC Map and the audio roles - none of which names a pin - and
leaves board-specific config behind, naming it on the receipt. A builder moving
from artoo-esp32 to firebeetle2 keeps everything they authored and re-answers
only what is genuinely about the new board.

**Nothing is clamped.** The reference clamps because its frames are targets tuned
against particular endpoints. Ours are not: a Part key is the same key on every
droid and a travel fraction is bounded by construction. What an import does is
**report** - as **Rehearsal** findings, each a token plus what to do about it
(ADR 0044) - and a step naming a Part no Output claims already has its answer,
`part-not-assigned` (#301).

## Consequences

- **A backup gains the thing it was missing.** Sequences enter the file, and
  `data/setup.html:451`'s claim to save *all your droid settings* becomes true
  instead of being quietly wrong.
- **The Part-to-Output assignments must ride along** - not so targets can be
  re-expressed, which a travel fraction already handles, but because without them
  restored sequences land on nothing and every step reports `part-not-assigned`.
- **Specific 10's three-tier matcher is not needed.** The reference arbitrates
  between part role, name and channel number because its files carry all three
  and they disagree. Ours match on the Part key alone; there is no fallback to
  refuse.
- **The author's physics cannot travel even if someone wanted it to.** ADR 0052
  put speed, acceleration and easing on the Output, so an imported sequence has
  nowhere to carry them.
- Each group's **TOUCHES / LEAVES ALONE** lines are written against these three
  and no others, which is what keeps the contract checkable.

## Alternatives considered

**Merge, with clashes renamed and reported** - the reference's shape. Rejected:
on 16 slots a merge fills the droid with near-duplicates and leaves a builder
unable to tell which of two similar names they were running last week.

**One group, all or nothing.** Rejected: recovering one deleted routine would
mean taking back a month of unrelated settings with it.

**Four groups, splitting the controller's own setup out.** Rejected for now: it
puts WiFi credentials in a backup file, which is a decision of its own and not
one this ticket needed to take.

**Refusing a cross-board restore.** Rejected against the operator's standing rule
of 2026-09-09 - limitations are presented and explained, never simply disallowed
- and because it would discard the sequences too, which have nothing to do with
the board.
