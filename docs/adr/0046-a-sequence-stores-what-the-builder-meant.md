# A sequence stores what the builder meant, not the commands it became

Status: accepted (2026-09-08, issue #331). Describes the **target** model.
Nothing in it ships today.

## Context

A **Learned Sequence** is a flat list of single-target commands at absolute
milliseconds. Everything a builder actually wants to say above that level —
a ripple, an alternating beat, a stagger, a phrase reused — has to be hand
expanded into that list, with the arithmetic done by hand.

The authors of the factory catalog are the best available evidence of what is
expressible, because they hit the ceiling and paid for it:

| Sequence | What it cost | Where |
|---|---|---|
| `DM:RESET` | 7 hand-written steps at 450 ms apart to close the ring safely — a measured brownout cadence, spaced by hand | `src/tasks/sequence_catalog.cpp:218-225` |
| `DM:CANTINA` | 26 hand-written body steps for a two-beat alternation, on an 1846 ms period computed off 130 BPM by hand | `src/tasks/sequence_catalog.cpp:250-290` |
| `DM:FLUTTER`, `DM:OVERLOAD` | reach for **dice** (`SEQ_RAND`) because there is no way to say *these panels, in this order, offset* | `src/tasks/sequence_catalog.cpp:371-400` |

Two premises fell to the source while this was argued.

**The vocabulary was never "a target at a time".** A dome panel already has three
shapes — `:OP` opens and stays, `:CL` closes, `:OF` flutters — over a panel or
over All / Pie / Ring (ADR 0008). And `STEP_RANDOM` already carries a set, a
shape, a per-step move time, a jitter and pick-distinct
(`include/sequence_engine.h:98-112`), every field of it exposed in the editor
(`data/seq.js:1185-1205`). Most of the expressiveness was already built —
reachable only through randomness. A builder could say *flutter a random ring
panel*; never *flutter PP3*.

**Our own dome fork already accepts a gesture vocabulary, and protoArtoo cannot
reach it.** Thirteen dynamic panel-group commands, each taking an arbitrary hex
bitmask of panels plus speed, delay and two easing methods
(`MarcduinoPanel.h:286-590`, listed in the fork's `FORK_IMPROVEMENTS.md:232`):
`:OP$` `:CL$` `:OF$` `:OC$` `:OCL$` `:OCR$` `:OW$` `:OWF$` `:OWC$` `:OMA$`
`:OAP$` `:OD$` `:OS$`. A builder can fire one today, because
`POST /api/dome/cmd` forwards verbatim (`src/web/api_drive.cpp:351-379`). They
cannot save one in a sequence, because `parsePanelIntent` requires a target of
exactly two characters (`src/protocol_check.cpp:176-181`).

So ADR 0008's vocabulary, written to keep raw `:SM` pulses out of saved
sequences, also fenced off the dome's entire expressive command set.

## Decision

**A sequence stores what the builder meant, and the meaning is resolved when it
runs.**

A **Gesture** is one authored move spread across many **Part**s, carrying a
shape, the Parts it spreads across, and how it spreads. Its order comes from
where those Parts physically are — `docs/droid-parts.yaml` already records
`bearing_deg` for every dome part — not from an order the builder typed. It is
**two independent choices**, what each Part does and how the move travels across
the set, with speed, easing and how far each Part travels as parameters.

**Who performs a Gesture is decided by who owns the Part.** A dome Gesture is
performed by the dome under **Catalog Authority**; a body Gesture is expanded by
the **Sequence Coordinator**, which is the only thing that can hold a safe
cadence there.

**A move says how far it goes**, dome and body, as a fraction of that Part's own
throw. **A sequence may contain another sequence** as one step and stays linked
to it. **A sequence has a tempo**, so a step or a Gesture can be placed on a beat;
beats resolve to absolute milliseconds before anything is scheduled.

This supersedes ADR 0008 in exactly two places: `DM:*` may be a step primitive,
and a dome step no longer resolves to a fixed command string before save. **ADR
0008's ban on raw `:SM` in anything that can replay automatically stands
untouched**, and so does the principle that produced it.

## Why

**A today-fact is not an argument.** The first version of this decision resolved
a Gesture's parts at save, argued from three facts about the current code — the
firmware has no dome geometry, ADR 0008 says resolve before save, #301 sends dome
parts to the browser only — and then offered a **Rehearsal** staleness note as
consolation for the capability that gave away. Every one of those is a
description of where we start, not of what a builder needs. Reversed on operator
direction, and recorded here because the reasoning is the reusable part.

**A builder's droid changes for months.** They print another panel, rebuild a
linkage, move an arm to a spare output. A Gesture that means *the ring* must keep
meaning the ring, or a re-fit silently invalidates choreography that still looks
correct. This is *choreograph first, wire later* (research §3.1) carried all the
way through: a sequence describes the droid's **anatomy**, not a snapshot of its
wiring.

**Execution belongs where the physics is.** The dome owns its panels' calibrated
motion, their easing and their current budget, and it already schedules its own
panel release. The body owns the cadence floor for the outputs it drives itself.
Splitting a Gesture by owner is **Catalog Authority** applied unchanged, not a new
principle.

**Two axes outlive any one dome firmware.** The thirteen commands conflate what a
part does, how the move travels, and a speed that is sometimes an argument and
sometimes baked into the name (`Wave` vs `FastWave`). Separated, the authoring
model survives our fork moving toward broader community standards: **Coordinator
Resolution** maps a pair onto whatever the connected dome can do that day, and a
combination it cannot perform is a **Rehearsal Warning** rather than an authoring
error. The reference's own beat pattern builder reaches the same split
independently — `chase / alternate / pulse` over a target group (research §6b).

## Considered and rejected

**Resolving a Gesture's parts at save.** Cheapest by a wide margin, changes no
storage contract, and what runs is exactly what was checked. Rejected: the
Gesture stops meaning *the ring* the moment it is written, and the mitigation on
offer was only that the Rehearsal could report the staleness it caused.

**An editor macro that writes the steps for you.** Zero firmware change, and what
you save is what runs. Rejected: the intent is lost at the moment of writing, so
a ripple can be regenerated but never re-tuned, and the safe-cadence floor would
live in the browser where a hand-edited or imported sequence can walk around it.

**A flat list of named effects, one per dome command.** One-to-one with the wire,
zero mapping ambiguity, and names the community already knows. Rejected: it makes
the authoring vocabulary a mirror of whichever dome firmware is fitted, which is
the framing this map exists to stop, and it keeps speed inside two of the names.

**The body expanding dome Gestures into staggered individual commands.**
Guarantees every combination performs, and `DM:RESET` proves the body can do it
safely. Rejected: two motion authorities for one set of panels, and the body
taking back a current budget that Catalog Authority deliberately left with the
dome.

**Partial travel on the body only.** Honest and cheap, since #286 already put the
ends on the **Output**. Rejected: the parts an audience looks at most are the ones
that could not then crack open, and the vocabulary would differ by where a part
happens to live.

## Consequences

- **A saved sequence stops being a list of resolved commands.** The migration
  path for existing Learned Sequences is execution work, not a decision.
- **The engine gains a stack.** A linked phrase means the flat cursor nests, with
  a bounded depth, and effect-class cleanup unions across levels.
- **The droid must know its own geometry at run time** for a Gesture to keep its
  meaning. For dome Parts this is the map's open question — *whether the dome
  publishes more about itself* — answered in one direction.
- **Protocol Check gains a Gesture grammar and loses nothing.** It still rules on
  form only; whether the connected dome can perform a given move-and-spread is
  the **Rehearsal**'s, and never blocks a save (ADR 0044).
- **Beat detection cannot happen in the browser.** Audio here is a track index on
  an SD card behind a serial module (`AudioDriver::playTrack(uint16_t)`); the
  browser never holds audio data. A tempo comes from the builder, from issue #14's
  offline skill, or from the builder analysing their own copy of the track.
- **A detected tempo is advisory and always editable.** The analysis folds metric
  levels — Cantina's ~200 BPM reads as 127.8 (research §6b).
- **Issue #319 inherits the body half**: a body Gesture's expansion, its cadence
  floor, and partial travel on a body **Output**.
