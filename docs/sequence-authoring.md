# Authoring DM:* sequences

A DM:* sequence is a named, time-ordered choreography the body coordinates from one clock:
sound, dome rotation, body and dome panel motion, and dome light/logic/PSI effects. There
are two authoring surfaces:

- **Factory Sequences** -- C++ tables compiled into firmware (`src/tasks/sequence_catalog.cpp`).
  The expert surface; reviewed in a PR.
- **Learned Sequences** -- JSON files on the controller filesystem (`/seq/`), created and
  edited without reflashing, accepted only after passing **Protocol Check**.

Both run through the same engine (`sequence_engine.cpp`). See
[ADR 0004](adr/0004-body-centric-dm-sequence-coordinator.md) and
[ADR 0006](adr/0006-learned-sequences-runtime-tier.md) for the architecture.

## The core step types

Every choreography is built from core step kinds:

| Step type | Meaning |
|---|---|
| `dome` | send a dome command (`:OP`/`:CL`/`:OF` panel intent, `@...` logic/PSI, `*...` holo, `:SE##`) |
| `audio` | play a body sound ($-command; named roles preferred -- see below) |
| `domeRotate` | body-owned timed dome rotation (speed -100..100%, duration in ms) |
| `domeBearing` | turn the dome until front, or a dome Part, faces front (see below) |
| `body` | move one body **Part** -- a door, an arm, the dataport (see below) |
| `loop` | beat/BPM iteration; repeats a body of steps |
| `random` | runtime panel pick; emits a random panel intent command |
| `audioCat` | random track from a sound category with fallback |
| `gesture` | one move spread across a set of Parts, in order round the droid (see below) |
| `sequence` | another sequence, as one step, kept linked (see below) |

Timing is **absolute** from sequence start (`tMs`). Steps inside a `loop` body use times
relative to the iteration start.

## Panel intent vocabulary

Body-authored panel movement uses high-level panel intent commands only. The dome owns
calibrated servo execution; the body commands the intent.

| Command family | Effect |
|---|---|
| `:OP<target>` | open a panel or group |
| `:CL<target>` | close a panel or group |
| `:OF<target>` | one-shot flutter effect; the dome ends it with the panel closed |

**Allowed targets:**

| Target | Panel |
|---|---|
| `00` | all panels (group) |
| `14` | pie / top panel group |
| `15` | ring / bottom panel group |
| `01` `02` `03` `04` `07` `11` `13` | ring panels P1, P2, P3, P4, P7, P11, P13 |
| `P1` `P2` `P3` `P4` `P5` `P6` | pie / top panels PP1 -- PP6 |

Do not use numeric IDs 08-10 or 12 as pie panel references. AstroPixelsPlus maps those
compatibility IDs to a mixed set; use the explicit `P1`-`P6` aliases instead.

### A flutter needs no close

`:OF` ends with the panel closed: the dome's own flutter finishes on the closed
end. A branch that issues `:OF<target>` owes no `:CL` after it, and Protocol
Check asks for none (ADR 0008 and ADR 0049, amended 2026-10-02).

`:OP` needs no same-branch close on a ring panel: terminal and abort cleanup close those one
at a time. A pie opened with `:OP` stays open unless the branch closes it.

### Non-panel dome commands

These are allowed in Advanced/raw steps and are not panel intent commands:

- `@0T...` / `@0P...` / `@1M...` -- logic / PSI / text display
- `*HP...` / `*ST00` -- holo / HP commands
- `:SE##` -- legacy Marcduino sequence trigger (2-digit zero-padded, e.g. `:SE07`);
  advanced only; not for panel control; rejected inside loops and random steps

### `:SM` is not available in sequences

`:SM<slot>,<move>,<pulse>` is diagnostic / calibration only. It is rejected by Protocol
Check in Learned Sequences and is not present in Factory catalog tables. Use the panel
intent commands above for all panel choreography.

## Dome rotation

Body-owned dome motor rotation is a first-class timed step, separate from dome serial commands.
A rotation step specifies a target speed and duration:

```json
{ "t": 1200, "type": "domeRotate", "speedPct": 35, "durationMs": 900 }
```

- `speedPct`: signed integer -100..100 (negative = left/reverse, positive = right/forward)
- `durationMs`: rotation duration in milliseconds; must be positive except for explicit neutral stop (see below)
- A **neutral stop** uses `speedPct: 0, durationMs: 0` and may appear anywhere; any other zero value is rejected

Dome rotation does **not** depend on RC/SBUS input and requires no manual cleanup — the engine stops
the motor automatically on terminal, abort, preempt, or estop.

In Factory Sequences, use the `SEQ_DOME_ROTATE(t, speedPct, durationMs)` macro.

### Turning to a bearing

A bearing step turns the dome until a target faces the droid's front: `front`
itself, or a dome Part by its catalog id.

```json
{ "t": 1200, "type": "domeBearing", "target": "pie3" }
```

It turns the short way round from where the dome **believes** it points, at the
speed its full turn was timed at, and stops on time (the Dome page records the
full turn). The Part's bearing is read when the step runs, so a corrected
`bearing_deg` reaches every saved step.

It is a different promise from `domeRotate`: a duration always completes, and a
turn to a target may not. While where the dome points is unknown (after a boot,
an estop or Sleep Mode, until **Front is here**), the dome not calibrated or the
Dome ESC off, the
step does not move the dome; the run reports `bearing-unknown`,
`dome-not-calibrated` or `component-disabled` and carries on. It saves either
way. A sequence's end stops the dome, so leave the end at least half the
dome's full-turn time after a bearing step. A pose press (the timeline's send to this moment) does not turn the dome.

## Moving a body part

A body step names the **Part** -- not a channel, not an output address -- and says
what it does in the same three words a dome panel already uses:

```json
{ "t": 400, "type": "body", "part": "doorFL", "shape": "flutter",
  "howFar": 60, "flutterMs": 1200 }
```

- `part`: a Droid Parts Catalog id (`doorFL`, `dataport`, `utilUp`, `gripArm`,
  ...). **Required.** The catalog is `docs/droid-parts.yaml`.
- `shape`: `open` | `close` | `flutter`. Optional; **omit it for `open`**. These
  are the dome's own three words, so one word means one thing across the droid,
  and a body **light** part stores the same token (a surface shows it as
  on / off / flash).
- `howFar`: 1..100, a percentage of **that part's own throw** -- the open/close
  ends recorded on the Servo Output that drives it. Optional; omit it for the
  whole throw. So the same step means the same gesture on a different linkage,
  and recalibrating the part changes the microseconds without touching the
  routine. A value below 5% is *floored* to 5% rather than refused, because the
  model has no way to mean "does not move".
- `flutterMs`: how long a flutter goes on, 50..60000. Only a flutter carries it.
  The part swings between its closed end and `howFar`, each swing at the
  Servo Output's own speed, and is back on its closed end when the time is up.
  The rest of the sequence keeps its timing while it swings. A later step that
  moves the same part ends the flutter. Two parts fluttering together take
  turns, one whole swing each, at least the Cadence Floor apart. A flutter is
  over by the end step, however long it says it lasts: a swing that would not
  be back by then does not start.

Speed, acceleration and easing are **not** on the step. They live on the Servo
Output and apply to every use of that part, so your choreography travels between
droids and your physics does not.

**Naming a part nothing drives yet is fine.** It saves, and at run time the body
reports `part-not-assigned` and carries on to the next step. Wire the part, let a
Servo Output record it, and the same saved sequence starts moving it with nothing
re-authored.

**The body undoes nothing.** A part left open when the sequence ends stays open:
write the close as a step, exactly as you already do for pie panels. A flutter
is the exception that needs none: it ends *closed*, the same as `:OF` on the
dome.

In Factory Sequences, use the
`SEQ_BODY(t, part, shape, howFar, flutterMs)` macro.

### The numbered body routines

`:SE30`..`:SE36` -- the body buttons a builder arriving from ShadowMD already has
bound -- are the Factory Sequences `DM:SE30`..`DM:SE36`, written entirely in Body
Steps. RC and the dome link start them through the Sequence Coordinator, so they
preempt and are preempted like any other sequence, and saving a Learned Sequence
under one of those names retrains that button.

| Button | Routine | Parts it moves |
|---|---|---|
| `:SE30` | utility arms out, flick in and out twice, close | `utilUp`, `utilLo` |
| `:SE31` | doors open, arms rise and work their tools, dataport, everything folds away | `dataport`, `utilUp`, `utilLo`, `doorFL`, `gripArm`, `gripClaw`, `doorFR`, `interArm`, `interTool` |
| `:SE32` | doors, dataport and utility arms spring open and wiggle shut | `dataport`, `utilUp`, `utilLo`, `doorFL`, `doorFR` |
| `:SE33` | left door, gripper arm rises, claw snaps three times | `doorFL`, `gripArm`, `gripClaw` |
| `:SE34` | right door, interface arm rises, tool works three times | `doorFR`, `interArm`, `interTool` |
| `:SE35` | the breadpan doors take turns, faster then slower | `doorFL`, `doorFR` |
| `:SE36` | BT-1: both doors, both arms, both claws snap together | `doorFL`, `gripArm`, `gripClaw`, `doorFR`, `interArm`, `interTool` |

A Part no Output drives is skipped with `part-not-assigned` and the routine carries
on, so a droid with only the two utility arms wired still runs every one. Each
routine ends with every Part it moved closed. How fast any of them moves is the
Output's Motion Profile, not the routine's. Where the choreography came from is
in [`sequence-credits.md`](sequence-credits.md).

If estop or Sleep Mode arrives while a sequence is running, the outputs that
sequence moved snap to their close position -- the promise the body routines kept
when ServoTask ran them itself -- and any move in progress stops where it is.

## Tempo and beats

A sequence may carry a **tempo** (`tempo` at the top level, ADR 0058): `bpm`
(1..600, one decimal), `source` (`typed`, `tapped` or `analysed`), `confidence`
(0..1), and optionally `phase` (ms where beat 1 sits), `barLen` (beats in a bar,
1..16, default 4), `barPhase`, `duration` (ms the track runs) and `hash` (the
analysed file's fingerprint; the analysed route only).

Any step may then carry `beat` (the whole beat it starts on, 0..1200) beside its
`t`; the beat wins, and changing the BPM moves every step on a beat and leaves
every step placed in milliseconds where it was. A dome turn or a body flutter may
carry `spanBeats` for its duration. A step inside a `loop` body is timed from its
pass and cannot sit on a beat: put the loop header on the beat instead. The
firmware resolves beats to milliseconds when it loads the sequence; the engine
still runs milliseconds.

## Gestures

A `gesture` step spreads one shape across a **set** of Parts (ADR 0046):

```json
{ "t": 0, "beat": 4, "type": "gesture", "set": "ring", "shape": "open",
  "spread": "chase", "direction": "cw", "start": "front", "repeatBeats": 8 }
```

- `set` is a token from the parts catalog (`ring`, `pies`, `dome`, `breadpan`,
  `bodyDoors`), or `parts` lists Part ids (all on the dome or all on the body,
  at most 24). The droid works out the Parts when the step **runs**, so a part
  fitted later joins in, and a part nothing drives is reported and skipped.
- The order comes from where the Parts sit (`bearing_deg`, 0 dead astern, 180
  dead ahead), clockwise or counter-clockwise from `front`, `right`, `rear` or
  `left`. A Part with no bearing (every body Part today) goes last.
- `spread`: `together`, `wave` (one per step, each stays), `chase` (one per step,
  the one before goes back), `alternate` (all on one step, all back on the
  next), `pulse` (back on the half step). The step is `stepMs` or `stepBeats`;
  with a tempo it defaults to one beat.
- `repeatMs`/`repeatBeats` repeats it; `extentMs`/`extentBeats` bounds the
  repeats, and by default it repeats to the end step (or the track's end, if
  sooner).
- `howFar`, `speedMs` (a full throw's time) and `easing` (`none`, `soft`,
  `overshoot`) are optional; on the body, speed and easing replace the
  Output's own Motion Profile for the Gesture's moves only.
- A **body** Gesture is expanded by the Sequence Coordinator into one move at a
  time, never closer than the Cadence Floor, whatever the spread asks.
- A **dome** Gesture is one of the dome's `$` commands over the members' panel
  addresses: `together` open/close/flutter, and `open` with `wave`, `chase`,
  `alternate` or `pulse`. The dome orders its own panels and keeps its own
  speed. Any other pair still saves, and the Rehearsal says the dome does
  nothing with it.
- A flutter Gesture flutters each member for `flutterMs`, or for one step when
  it states none, and owes no close after it: every member ends closed. Where
  the spread sends a member back (`chase`, `alternate`, `pulse`), that close
  ends its flutter.
- A Gesture stops at the end step, mid-pass if it has to: nothing it would
  move at or after the end is sent, and the Rehearsal says when a pass is cut
  short.

## How far a dome panel goes

A dome `:OP`/`:CL` step may carry `howFar` (1..100): the dome stops the panel
that far along its own calibrated travel (sent as `:MV`, a fraction, never a
pulse). PP3 and PP5 have no part-way move on our dome firmware and go all the
way.

## A sequence inside a sequence

A `sequence` step names another sequence by its stable reference: a saved
sequence's `id` (every save carries one) or a factory sequence's name.

```json
{ "t": 2000, "type": "sequence", "ref": "a1b2c3d4", "name": "DM:PHRASE" }
```

Its steps run where the step sits, loaded fresh on every run, so improving the
phrase improves every sequence that holds it. On save: the phrase must be on
the droid and not a toggle, a sequence cannot reach itself, phrases nest at
most three deep, the whole run must fit 96 steps, and a phrase cannot sit
inside a loop. In a sequence with a close half, each half is held to these
rules and counted to 96 by itself. A phrase deleted later is left out of the
run, and the log says so.

## The Rehearsal

The editor reads the sequence you are writing and says what will not happen the
way you wrote it. It is not Protocol Check: Protocol Check decides whether a
sequence can be saved, and the Rehearsal can never stop a save or a run
(ADR 0044). Its counts sit under Protocol Check's verdict. You get the full list
when you save and when you tune a Factory sequence, and a folded badge after a
test run.

Every finding is one of two levels -- a **warning** (it will not do what you
wrote) or a **note** (worth knowing) -- and carries a token beside its message
and a fix:

| Token | Level | What it catches | Why it is a rule |
|---|---|---|---|
| `dispatch-spacing` | warning | dome commands less than 200 ms apart, or at the same moment | the dome's eight-entry command queue dropped a close on 2026-06-18 |
| `retarget-before-arrival` | warning | the same open or close sent again to a panel or Part with nothing in between | `DM:HELLO`'s five identical opens made one |
| `quiet-in-sequence` | warning | a `$s` step | it turned idle chatter off until reboot on 2026-06-17 |
| `part-left-open` | note | a body Part whose last step is an open; not said on a toggle's opening half of a Part its close half closes | the body undoes nothing (ADR 0049) |
| `gesture-dome` | warning | a dome Gesture the dome performs only in part, or not at all | a dome Gesture is the dome's `$` command (ADR 0046) |
| `dome-how-far` | warning | a part-way move of PP3 or PP5 | our dome firmware has no part-way move for them |
| `tempo-confidence` | warning | a tempo that is only a guess (confidence under 0.5) | Cantina's ~200 BPM read as 127.8 (ADR 0058) |
| `tempo-hash` | warning | a dropped-in track that is not the one the tempo was measured from | the track behind a sound can change (ADR 0058) |

It also says how many steps it could check. A dome panel move, a body move and a
random pick each carry a question it cannot answer from the page -- how long the
panel or part takes to move, or which panel the dice will choose -- and those
steps are named as not checked, with what would close the gap.

## Cleanup is automatic

You do **not** author teardown. The engine tracks which persistent effects fired (panel
open, logic/PSI, holo, long audio) and emits the matching resets (`@0T1`/`@0P1`, `*ST00`,
audio stop, and an individual close for each ring panel the run left open) on the terminal `end` step and on abort/preempt/estop. In Learned
Sequences the effect class is *inferred* by Protocol Check from each command, so cleanup is
correct-by-construction; in Factory tables you tag the first activating step explicitly
(`FX_PANEL`, `FX_LOGIC_PSI`, `FX_HOLO`, `FX_AUDIO`).

**Body parts are outside all of this.** The engine stamps no effect class on a
`body` step and schedules nothing for it at the end of a run: a door left open
stays open, because the Servo Output's own release schedule already stops it
being held and the body knows exactly where the part arrived. The close is a step
you write. A body flutter needs none: it ends closed, same as `:OF`.

## Authoring a Factory Sequence (C++)

Use the `SEQ_*` macros so the positional `SeqStepParams` ordering lives in one place. Tag
the first step that activates each persistent effect; the engine auto-resets the rest.
Use panel intent commands (`:OP`/`:CL`/`:OF`) for all panel choreography.

```cpp
static const SeqStep kNodSteps[] = {
    SEQ_AUDIO(0, "$H"),                       // ack clip
    SEQ_DOME(0, FX_NONE, "@1MYes"),           // logic text
    SEQ_DOME(0, FX_PANEL, ":OP01"),           // P1 open  -> auto :CL00 at end
    SEQ_DOME(150, FX_NONE, ":CL01"),          // P1 close (explicit timed close)
    SEQ_TERM(300),
};
// catalog row: { "DM:NOD", kNodSteps, SEQ_STEPCOUNT(kNodSteps), 3000, TOGGLE_NONE, nullptr, 0 }
```

`suppressMs` (the random-suppression window) must be `>=` the terminal `STEP_END` time.

## Authoring a Learned Sequence (JSON v1)

Saved via `POST /api/seq`; the editor writes this format. It maps 1:1 onto the engine
model -- no `fx` field (inferred), no manual cleanup steps (automatic).

```json
{ "format": 1, "name": "DM:MYSEQ", "suppressMs": 8000, "toggleGroup": "none",
  "meta": { "source": "user", "origin": "", "license": "", "notes": "", "modified": false },
  "steps": [
    {"t": 0,   "type": "audio",    "cmd": "$H"},
    {"t": 0,   "type": "dome",     "cmd": ":OP14"},
    {"t": 100, "type": "dome",     "cmd": ":OFP3"},
    {"t": 600, "type": "dome",     "cmd": ":CLP3"},
    {"t": 620, "type": "body",     "part": "doorFL", "howFar": 60},
    {"t": 700, "type": "body",     "part": "doorFL", "shape": "close"},
    {"t": 800, "type": "loop",     "body": 2, "periodMs": 1846, "durationMs": 14000},
    {"t": 0,   "type": "random",   "set": "ring", "mode": "flutter",
                                   "moveMs": 300, "jitterMs": 500, "distinct": true},
    {"t": 0,   "type": "audioCat", "category": "alert", "fallback": "$S"},
    {"t": 500, "type": "end"} ],
  "closeSteps": [] }
```

- `type` is one of `dome | audio | body | gesture | sequence | loop | random | audioCat | domeRotate | domeBearing | end`.
- `dome` steps carry a single panel intent or Advanced dome command string.
- A `loop` header is followed by its `body` steps (relative `t`); loops do not
  nest, and a `sequence` step cannot sit in a loop body.
- `random` steps pick from a logical target set (`ring`, `pie`, `all`, `hold`) and emit
  panel intent commands according to `mode` (`flutter`, `open`, `close`).
- A toggle sequence (`toggleGroup` != `none`) carries a `closeSteps` branch; a non-toggle
  must not. `GET /api/seq/builtins` returns every Factory Sequence in this format as a
  starting point for cloning (clone-to-retrain).
- `takes` (optional) names the takes the sequence holds, each `{"id": "k3f9q2ab", "t": 0}`:
  a performance kept off the sticks, whose motion is a file of its own on the droid
  (ADR 0061, `docs/api.md` "Takes"). The engine and the parser ignore it; the store reads
  it, and a save keeps the takes it names and deletes the sequence's others. When the
  sequence runs, each take plays from its `t` beside the steps: where two takes cover one
  part, the later one in the array moves it, and a step that moves the part wins over
  both. A take is made by Perform in the editor, never written by hand.

### Named tracks vs `$NNN`

Prefer a named sound role over a raw track number so the sequence follows the operator's
configured tracks. The Marcduino `$NNN`/`$`-letter dialect stays valid at boundaries for
interoperability. `audioCat` plays a random track from a sound category with a named-slot
fallback.

## Protocol Check (the save gate)

Every Learned Sequence passes Protocol Check on save. It returns a field-level error and
writes nothing on rejection. Estop, suppression, and auto-reset are engine-level invariants
the format cannot express a bypass for.

| Field | Rule |
|---|---|
| `name` | `^DM:[A-Z0-9_]{1,18}$` |
| `toggleGroup` | `none|pies|low|all`; `user1..4` reserved |
| retrain | factory toggle name -> identical `toggleGroup`; factory non-toggle name -> `none` |
| `suppressMs` | 1000..120000 and `>=` sequence end time |
| branch | `<=96` steps; ends with explicit `end`; `t` non-decreasing outside loop bodies |
| `:OP`/`:CL`/`:OF` | target must be in the allowed set (see Panel intent vocabulary) |
| `:SM` | **rejected** -- diagnostic only, not allowed in sequences |
| `:SE` | exactly 2 digits (e.g. `:SE09`); not allowed inside loops or random |
| `@`/`*`/`$` | length- and charset-bounded; recognised prefix |
| `domeRotate` | speedPct -100..100; durationMs positive (or 0 paired with speedPct=0 for neutral stop) |
| `domeBearing` | `target` is `front` or a dome Part the catalog gives a bearing; nothing about the dome's calibration or belief is checked on save |
| `body` | `part` in the Droid Parts Catalog; `shape` open/close/flutter; `howFar` 1..100; a flutter's `flutterMs` 50..60000 and no duration on any other shape |
| `loop` | period 100..60000, duration `<=120000`, no nesting, body within branch |
| `random` | set: ring/pie/all/hold; mode: flutter/open/close; jitter `<=2000`, move `<=5000` |
| capacity | 16 files max. Per-file size and free-space floor depend on the controller board: **12 KB / 24 KB** on artoo-esp32, **24 KB / 48 KB** on the FireBeetle 2 (ESP32-P4). Only the larger board can hold a sequence that uses all 96+96 steps |

## Triggering

A sequence runs via `sequenceStart("DM:NAME", source)` from RC, web (`POST /api/seq/test`
or `/api/dome/cmd`), or dome RX. Lookup precedence is **runtime -> catalog -> alias ->
fallback**, so a Learned Sequence shadows a Factory one of the same name (Retrained); a
Memory Wipe restores the factory programming. RC trigger bindings accept any indexed
Learned name. Wiping a non-shadowing name leaves any RC binding to it inert:
`DELETE /api/seq` lists those bindings in a `danglingBindings` response field, and the
trigger logs a warning each time it fires into the void.
