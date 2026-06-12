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

## The four primitives

Every choreography is built from four step kinds:

| Primitive | Meaning |
|---|---|
| `dome("...")` | send a Marcduino string to the dome (`:SM`, `:CL00`, `@...`, `*...`, `:SE...`) |
| `audio("$X")` | play a body sound ($-command; named roles preferred -- see below) |
| `wait(ms)` | advance the cursor; expressed as the absolute `t` of the next step |
| structured | `loop` (beat/BPM iteration) and `random` (runtime slot/pulse pick) |

Timing is **absolute** from sequence start (`tMs`). Steps inside a `loop` body use times
relative to the iteration start. `:SM<slot>,<pulse>,<ms>` starts a non-blocking move and
returns immediately -- compose motion by *when* you issue moves (simultaneous = same `t`;
serial wave = stagger `t` by the move time).

### Panel slot map and pulse constants

```
slot 0=P1 1=P2 2=P3 3=P4 4=P7 5=P11 6=P13   (ring)
slot 7=PP5 8=PP1 9=PP2 10=PP4 11=PP6 12=PP3  (pie)
CLOSE=800  25%=1150  50%=1500  75%=1850  OPEN=2200
```

### Cleanup is automatic

You do **not** author teardown. The engine tracks which persistent effects fired (panel
open, logic/PSI, holo, long audio) and emits the matching resets (`:CL00`, `@0T1`/`@0P1`,
`*ST00`, audio stop) on the terminal `end` step and on abort/preempt/estop. In Learned
Sequences the effect class is *inferred* by Protocol Check from each command, so cleanup is
correct-by-construction; in Factory tables you tag the first activating step explicitly
(`FX_PANEL`, `FX_LOGIC_PSI`, `FX_HOLO`, `FX_AUDIO`).

## Authoring a Factory Sequence (C++)

Use the `SEQ_*` macros so the positional `SeqStepParams` ordering lives in one place. Tag
the first step that activates each persistent effect; the engine auto-resets the rest.

```cpp
static const SeqStep kNodSteps[] = {
    SEQ_AUDIO(0, "$H"),                      // ack clip
    SEQ_DOME(0, FX_NONE, "@1MYes"),          // logic text
    SEQ_DOME(0, FX_PANEL, ":SM0,2200,150"),  // P1 open  -> auto :CL00 at end
    SEQ_DOME(150, FX_NONE, ":SM0,800,150"),  // P1 close
    SEQ_TERM(300),
};
// catalog row: { "DM:NOD", kNodSteps, SEQ_STEPCOUNT(kNodSteps), 3000, TOGGLE_NONE, nullptr, 0 }
```

`suppressMs` (the random-suppression window) must be `>=` the terminal `STEP_END` time.

## Authoring a Learned Sequence (JSON v1)

Saved via `POST /api/seq`; the editor (slice 4) writes this format. It maps 1:1 onto the
engine model -- no `fx` field (inferred), no manual cleanup steps (automatic).

```json
{ "format": 1, "name": "DM:MYSEQ", "suppressMs": 8000, "toggleGroup": "none",
  "meta": { "source": "user", "origin": "", "license": "", "notes": "", "modified": false },
  "steps": [
    {"t": 0,   "type": "audio",    "cmd": "$H"},
    {"t": 0,   "type": "dome",     "cmd": ":SM0,2200,150"},
    {"t": 100, "type": "loop",     "body": 2, "periodMs": 1846, "durationMs": 14000},
    {"t": 0,   "type": "random",   "set": "ring", "pulseMin": 1150, "pulseMax": 1500,
                                   "moveMs": 300, "jitterMs": 500, "distinct": true},
    {"t": 0,   "type": "audioCat", "category": "alert", "fallback": "scream"},
    {"t": 500, "type": "end"} ],
  "closeSteps": [] }
```

- `type` is one of `dome | audio | loop | random | audioCat | end`.
- A `loop` header is followed by its `body` steps (relative `t`); no nesting.
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
| `toggleGroup` | `none|pies|low|all`; `user1..4` are reserved and rejected until the engine wires their latches |
| retrain | factory toggle name -> identical `toggleGroup`; factory non-toggle name -> `none` |
| `suppressMs` | 1000..120000 and `>=` sequence end time |
| branch | `<=96` steps; ends with an explicit `end`; `t` non-decreasing outside loop bodies |
| `:SM` | slot 0..12, pulse 800..2200, move 50..5000 |
| `:SE` | exactly 2 digits (the canonical Marcduino zero-padded form, e.g. `:SE09`) |
| `@`/`*`/`$`/`:CL00` | length- and charset-bounded; recognised prefix |
| `loop` | period 100..60000, duration `<=120000`, no nesting, body within branch |
| `random` | pulse 800..2200, jitter `<=2000`, move 50..5000, known slot set |
| capacity | 16 files max, 12 KB per file, 24 KB LittleFS free-space floor |

## Triggering

A sequence runs via `sequenceStart("DM:NAME", source)` from RC, web (`POST /api/seq/test`
or `/api/dome/cmd`), or dome RX. Lookup precedence is **runtime -> catalog -> alias ->
fallback**, so a Learned Sequence shadows a Factory one of the same name (Retrained); a
Memory Wipe restores the factory programming. RC trigger bindings accept any indexed
Learned name. Wiping a non-shadowing name leaves any RC binding to it inert:
`DELETE /api/seq` lists those bindings in a `danglingBindings` response field, and the
trigger logs a warning each time it fires into the void.
