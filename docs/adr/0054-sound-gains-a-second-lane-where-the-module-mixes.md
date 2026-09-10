# Sound gains a second lane, where the module mixes

Status: accepted (2026-09-08, issue #326). Describes the **target** model.
Nothing in it ships today.

## Context

Sound is the most-fired thing on a droid and the one an operator reaches for in
front of people, and protoArtoo owns it outright — no other component in the droid
plays a sound. The map gave it an area only on 2026-09-08.

**The interface throws away the capability of the module fitted to this droid.**
`AudioDriver` (`include/audio_driver.h`) is `playTrack(uint16_t)`,
`playTrackBanked(index, bank, page)`, `stop()` and `setVolume(uint8_t)`. There is
no stream anywhere in it. But this project's own documentation describes the
CHIRP Audio Trigger as *"an RP2350-based **multi-stream** audio board … a
significant capability step up from single-track binary modules"*, with **"3+
independent simultaneous streams"** (`docs/sound_playback.md:93-99`), and CHIRP's
own README lists stream-addressed verbs: `STOP` stops *"all streams or specified
stream"*, `VOL` sets *"global volume or individual stream volume"*, `STAT` gets
*"the status of a specified stream"*. Its SD layout exists for it — **Bank 1 is
reserved for primary droid vocals, Banks 2–6 for music and effects**.

Our driver sends bare `STOP` (all streams), a global `VOL:N`, and queries
`STAT:0` — it asks about stream zero and calls the answer the module's state
(`src/drivers/audio_chirp.cpp:23-27`).

**Two other things were settled before this ticket could be answered.**

Specific 6 assumed the reference keeps volume on its always-visible strip. It does
not: `VOL 14` sits in the viewport corner beside `LOOP` and `UPTIME`, not on the
chip plate (`12-dark-theme.png`) — and #324 had already ruled that a chip earns a
place only if seeing it would change the operator's next move, which puts
telemetry on **Dashboard**.

And #332 closed the other half of what sound can drive: `AudioModuleState` reports
`playState` and `currentTrack` and never a level, so nothing can *follow* a sound.

## Decision

**Sound gains a second lane where the fitted module offers one.**

A **Sound Bed** is a track that plays *under* what else the droid is saying, with
its own volume, which a vocal fires over without stopping. It is a **Sequence**
step like any other sound — a sequence starts a bed, fires over it and stops it —
so a show is one authored thing rather than a live juggling act.

**It is a Component Family capability, not a promise.** A bed authored against a
module that cannot mix is a **Rehearsal Warning** and a run-time report; the
sequence still saves and still runs, without the bed. That is #319's shape for an
unassigned **Part**, applied to sound.

**A sound is addressed in two layers that do different jobs.** The fitted module's
own address is shown the way the builder arranged it — bank, page and index on
CHIRP, `NNN` inside its community category range on a flat module — because that
structure is something they physically built and can see on their own card. A
**Named Track** sits over it as the portable half, and a sequence references that.

**Names from the module and Named Tracks cannot overrule one another**, because
they are not the same claim: a module name *describes what is on the card*, a
Named Track *names a role*. The one real collision — a Named Track pointing at an
index whose file changed underneath it — is a **reconciliation the builder
resolves**, never a silent re-point.

## Why

**The capability is already paid for.** The builder bought a board that mixes, the
wire protocol addresses streams, the SD card is organised into vocals and music,
and the only thing in the way is our own interface. Declining it would mean
protoArtoo drives a multi-stream board as a trigger and keeps `STAT:0` as a
permanent joke at its own expense.

**A bed makes an existing decision pay off.** ADR 0046 gave a sequence a
**Sequence Tempo** and beats as first-class placement, and #14's offline analysis
produces the beat grid — but with the music coming from a phone on a table, beats
line up against nothing the droid controls. With the music playing *from* the
droid under the routine, a tempo has something real to be true about.

**Capability-gated is the honest shape and the machinery exists.** #302 established
the Component Family with a capability bitmask, #303 established that the lineup is
not bounded by what we ship, and #327 gave every unavailable thing a family that
names the next move. A bed is one more bit in a pattern already built, not a new
kind of exception.

**Two addressing layers, because one would lose something real either way.** A flat
protoArtoo-wide number discards the folders the builder made, so our screen stops
matching their file manager. Names-only has nothing to show on the two modules in
the lineup that cannot list their contents. Showing the module's own address and
laying a portable role over it keeps both the thing the builder arranged and the
thing a sequence can safely reference.

## Considered and rejected

**One sound at a time; a bed is out of scope.** The driver interface would stay as
it is, every droid's sound would behave identically whatever module is fitted, and
a shared sequence would mean one thing everywhere. Rejected: the most-fired thing
on a droid stays the least expressive, and the builder who chose CHIRP for its
mixing cannot reach the reason they chose it.

**Music is somebody else's job** — protoArtoo owns vocals, and a builder wanting a
bed runs a separate player, which is what most builds do today. Rejected: it hands
away the thing that would make protoArtoo the droid's sound *system* rather than
its trigger, and #303 put sound in the lineup as a category we intend to own.

**A bed as an operator control only**, started from the sound surface with cues
fired over it by hand. Nothing enters the saved format and no sequence can be wrong
about a module. Rejected: the show could never be one thing — the timing between
the music and the vocals would be re-performed live every time, which is what a
Sequence exists to stop.

**A bed as a droid-level mode**, set like a **Mood** and persisting until changed.
One place to set it, and no sequence carries a capability it might not have
elsewhere. Rejected: a sequence that needs its bed cannot ask for one and a
sequence that must land in silence cannot get it, so the two halves of a
performance stay unable to refer to each other.

**One flat address everywhere.** Rejected above.

**Names only, with the raw address never surfaced.** Rejected above.

**The module authoritative for names**, cached but never stored as ours. One source
of truth, no drift. Rejected: two of the three modules in the lineup cannot list
their contents, so those droids would have no names at all and a builder's own word
for a sound would have nowhere to live.

**The droid authoritative, module names ignored.** Portable, and works on a backend
with no catalog. Rejected: a builder who renames a file sees our old name forever,
and the catalog browser — which exists to read the card — becomes a thing that
shows you what you already told us.

## Consequences

- **`AudioDriver` gains a stream concept**, and `stop()` and `setVolume()` gain a
  scope. Single-track backends implement the one-stream case and report the
  capability as absent.
- **A new Sequence step kind**, gated by **Protocol Check** on form only (ADR
  0044): the bed's target and volume are form; whether the fitted module can mix
  is the **Rehearsal**'s.
- **CHIRP's variant groups stay unreached**, and are recorded here so they are not
  rediscovered as new: files sharing a base name are randomly selected with no
  repeat (`docs/sound_playback.md`), which is `STEP_RANDOM`'s job done in hardware.
  Not decided by this ticket.
- **A backend with no catalog keeps the same surface, honestly emptied** — Named
  Tracks still work and the browser says the module cannot list its contents rather
  than disappearing. A page that changes structure with the hardware teaches the
  operator two apps.
- **Failure reporting composes from machinery already decided** rather than needing
  its own: the **Ignored Input Notice** (#324) covers pressing something that
  cannot act, a sound step that cannot play reports at run as an **Availability
  Reason** does, and the Rehearsal warns at authoring time.
- **Volume is Dashboard's, not the Status Plate's** (#324), and specific 6's
  premise about the reference is corrected in this ADR's Context.
