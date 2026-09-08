# Protocol Check rules on form, the Rehearsal on intent

Status: accepted (2026-09-08, issue #287). Describes the **target** behaviour.
No code implements the Rehearsal yet; Protocol Check is as described.

## Context

**Protocol Check already exists and is binary.** `src/protocol_check.cpp` is 993
pure lines behind one composite entry point, `protocolCheck(SeqDraft&)`
(`:965`). Its verdict is the whole story:

```c
struct ProtocolCheckResult {
    bool ok;
    char field[24];    // "steps[3].cmd"
    char message[96];  // human-readable reason
};
```

(`include/protocol_check.h:33-37`.) There is no severity, no list, and no room
for either. It also **short-circuits on the first failure** (`:974`, `:977`,
`:990`; branch loop `:864`, `:890`), so it never surveys a draft — an operator
gets one problem at a time. What it rules on is form: name grammar
(`^DM:[A-Z0-9_]{1,18}$`), the dome and audio command vocabularies, branch
structure, per-step parameter bounds, toggle/close-branch coherence, retrain
coherence against the Factory catalog, and a conservative effect-class stamp.

A browser mirror, `data/seq_protocol_check.js`, exists for instant feedback. Its
own header states the contract: *"Server remains authoritative on save; client
prevents obvious errors."*

**The source project's catalog splits three ways.** Its `lint.js` grades every
rule `err` (will not work on the board), `warn` (runs but not as meant) or
`note` (worth knowing), and `err` gates upload. Several of those errs are about
*intent*, not form:

- `tgt-range` — a target outside the channel's `min..max`, because *"the board
  clamps it, so the pose you see is not the pose you get"*.
- `timing` — a target re-issued before the servo has arrived, which never
  completes. Its own header records the hard-won half: **acceleration, not
  speed, is the binding constraint** (speed 80 / accel 10 gives ~940 ms of full
  throw, not the ~344 ms speed alone suggests), and staggered opens are fine
  because targets persist, while reversals need a full throw's worth of time.

The obvious path is to copy that shape: teach our gate the intent rules and let
it reject. This ADR records why we did not.

## Decision

**Protocol Check rules on form, and is the only thing that can refuse a save.**

**The Rehearsal rules on intent, and can never refuse** — however certain a
finding is. It carries two levels, a **Rehearsal Warning** ("this will not do
what you wrote") and a **Rehearsal Note** ("worth knowing"), and there is no
third: *error* stays Protocol Check's word alone.

## Why

**Certainty is not authority.** The author can know what the droid cannot: a
linkage rebuilt since the last calibration, an output deliberately driven past
its recorded ends to seat a panel, a reversal that is meant to be cut short. A
gate that is right most of the time teaches operators to distrust it the rest of
the time, and that failure is silent — they stop reading it, including on the
occasions it was right.

**A mirror is only trustworthy while it mirrors.** The browser copy is useful
precisely because it claims exactly what the device claims. That property is
already under strain: `protocol_check.cpp:308-310`, `:402-404` and `:535-536`
document the `DL:`/`DT:`/`DH:` grammars as mirroring the browser *"exactly"*,
and nothing checks it. Putting advisories inside Protocol Check would force one
of two bad outcomes — the mirror diverges from the authority it mirrors, or
firmware enters the advice business and has to survey drafts it currently
short-circuits out of.

**The gate's promise stays small enough to hold.** "Is this a well-formed
sequence?" can be answered by rules that do not change as the droid does. "Is
this a good sequence?" cannot.

## Considered and rejected

1. **Gate on the certain ones** — promote a named set of will-not-work findings
   into Protocol Check, as the reference does. Rejected: it changes the gate's
   promise from form to quality, which is a line nobody can hold as the catalog
   grows. Every future rule would then have to argue which side it belongs on,
   and the arguments get harder as the rules get better.
2. **A third advisory level meaning "this will not work"**, non-blocking but
   visually as loud as an error, with the save receipt recording that it was
   accepted anyway. Rejected: a category that reads like an error and is not is
   precisely the legibility problem this decision exists to prevent, moved one
   step along.
3. **Fill the mirror's existing advisory channel.** `validateSequence()`
   declares `warnings` (`data/seq_protocol_check.js:939`), returns it (`:987`)
   and documents it in its return type (`:871`) — but nothing ever pushes to it
   and no caller reads it. Rejected for the same reason as (2)'s parent: it is
   inside the mirror. The field is a latent defect rather than an affordance and
   **is deleted**, so nothing invites advice back into the mirror later.

## Consequences

- **The Rehearsal can be wrong at no cost**, which is what lets it carry rules
  whose inputs are uncertain — travel time, availability, coverage.
- **Protocol Check's first-failure short-circuit stays**, so the Rehearsal
  carries no error count. A blocking error keeps the field-anchored home it has
  today: `{field, error}` rendered against the step that broke.
- **Every new rule is placed against this line before it is written.** Form, or
  intent. There is no third answer, and "it is certain" is not an argument for
  the first.
- Non-blocking advisories already exist elsewhere in this subsystem and are
  consistent with this line rather than exceptions to it: `handleSeqDelete`
  returns `200 {"ok":true,"danglingBindings":[...]}`
  (`src/web/api_seq.cpp:280-303`), and the boot scan keeps a failing file
  indexed as `valid:false` rather than dropping it (`src/seq_store.cpp:208-228`).
