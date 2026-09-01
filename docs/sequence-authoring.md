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
| `loop` | beat/BPM iteration; repeats a body of steps |
| `random` | runtime panel pick; emits a random panel intent command |
| `audioCat` | random track from a sound category with fallback |

Timing is **absolute** from sequence start (`tMs`). Steps inside a `loop` body use times
relative to the iteration start.

## Panel intent vocabulary

Body-authored panel movement uses high-level panel intent commands only. The dome owns
calibrated servo execution; the body commands the intent.

| Command family | Effect |
|---|---|
| `:OP<target>` | open a panel or group |
| `:CL<target>` | close a panel or group |
| `:OF<target>` | one-shot flutter effect (panel state after is undefined -- requires explicit close) |

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

### `:OF` cleanup rule

`:OF` flutter does not leave a defined final panel state. Any branch that issues
`:OF<target>` must later issue a matching close in the same branch:

| Flutter | Valid close |
|---|---|
| `:OF01`-`:OF04`, `:OF07`, `:OF11`, `:OF13` (ring) | `:CL<same>`, `:CL15`, or `:CL00` |
| `:OFP1`-`:OFP6` (pie) | `:CLP<same>`, `:CL14`, or `:CL00` |
| `:OF14` (pie group) | `:CL14` or `:CL00` |
| `:OF15` (ring group) | `:CL15` or `:CL00` |
| `:OF00` (all) | `:CL00` |

`:OP` does not require an explicit same-branch close; terminal/abort cleanup handles it.

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

## Cleanup is automatic

You do **not** author teardown. The engine tracks which persistent effects fired (panel
open, logic/PSI, holo, long audio) and emits the matching resets (`:CL00`, `@0T1`/`@0P1`,
`*ST00`, audio stop) on the terminal `end` step and on abort/preempt/estop. In Learned
Sequences the effect class is *inferred* by Protocol Check from each command, so cleanup is
correct-by-construction; in Factory tables you tag the first activating step explicitly
(`FX_PANEL`, `FX_LOGIC_PSI`, `FX_HOLO`, `FX_AUDIO`).

The `:OF` cleanup rule is the one exception where Protocol Check requires explicit same-branch
close authorship. Auto-reset is a safety net, not a substitute for authored flutter cleanup.

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
    {"t": 800, "type": "loop",     "body": 2, "periodMs": 1846, "durationMs": 14000},
    {"t": 0,   "type": "random",   "set": "ring", "mode": "flutter",
                                   "moveMs": 300, "jitterMs": 500, "distinct": true},
    {"t": 0,   "type": "audioCat", "category": "alert", "fallback": "$S"},
    {"t": 500, "type": "end"} ],
  "closeSteps": [] }
```

- `type` is one of `dome | audio | loop | random | audioCat | end`.
- `dome` steps carry a single panel intent or Advanced dome command string.
- A `loop` header is followed by its `body` steps (relative `t`); no nesting.
- `random` steps pick from a logical target set (`ring`, `pie`, `all`, `hold`) and emit
  panel intent commands according to `mode` (`flutter`, `open`, `close`).
- A toggle sequence (`toggleGroup` != `none`) carries a `closeSteps` branch; a non-toggle
  must not. `GET /api/seq/builtins` returns every Factory Sequence in this format as a
  starting point for cloning (clone-to-retrain).

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
| `:OF` | same-branch explicit close required for every flutter target |
| `:SE` | exactly 2 digits (e.g. `:SE09`); not allowed inside loops or random |
| `@`/`*`/`$` | length- and charset-bounded; recognised prefix |
| `domeRotate` | speedPct -100..100; durationMs positive (or 0 paired with speedPct=0 for neutral stop) |
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
