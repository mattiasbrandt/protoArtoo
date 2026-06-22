# Dome Visual-Authoring Contract (`DL` / `DH` / `DT`)

Status: **draft, agreed body↔dome (codex) 2026-06-21** — implementation pending.
Scope: GitHub issue #11. Extends ADR 0008 (dome owns visual rendering) and
`docs/dome-visual-presets.md` from named Factory presets (`DV:`) to **custom**
structured authoring in the body sequence editor.

## Decision

For normal structured editor authoring, the body does **not** emit raw
`@`/`*`/`HPA` Marcduino strings. Those remain an Advanced/manual escape hatch
only. Structured editor steps generate **typed visual-intent commands** so the
dome can apply color, duration, multi-line text, and report telemetry.

The earlier "MVP: body generates raw `@0T`/`@0P` for simple logic/PSI" shortcut
is **rejected** by the dome side: raw cannot express color/duration, and we want
the editor to be a real AstroPixelsPlus authoring surface.

## Transport constraint (hard)

AstroPixelsPlus command ingress keeps Marcduino-style limits. Web validation
rejects commands over **63 chars**, and the serial/body command path is built
around short command buffers. **Every generated command must be `<= 63` chars.**
This is why the text command (`DT`) has a deliberately small grammar and tight
length caps rather than being a general message transport.

## Body-only vs. requires-dome-support (AC requirement)

| Feature | Owner | Status |
|---|---|---|
| `DV:<NAME>` Visual Preset | **body-only** wrapper (closed whitelisted name set) | done, frontend-only |
| `DL:` Logic/PSI Mode | requires AstroPixelsPlus support | implemented (body + dome), hardware-verified |
| `DH:` Holo Effect | requires AstroPixelsPlus support | implemented (body + dome), hardware-verified; strict effect/color matrix below |
| `DT:` Logic Text (multi-line) | requires AstroPixelsPlus support | implemented (body + dome), hardware-verified |

None of `DL:`/`DH:`/`DT:` are body-only: the body validates + serializes + forwards
the typed command, but **AstroPixelsPlus renders it**. On a dome without this build
the commands are inert. `DV:` is the only body-side wrapper (it selects an
already-dome-resident named preset).

## Grammar

```
DL:<target>:<mode>[:<color>[:<durationSec>]]
DH:<target>:<effect>[:<color>[:<durationOrCount>]]
DT:<target>:<color>:<durationSec>:<speed>:<encodedText>
```

General validation (server `src/protocol_check.cpp` + client
`data/seq_protocol_check.js`, identical mirror):
- uppercase command family and enum tokens; no lowercase aliases (first slice)
- full-string match only; no extra fields
- total command length `<= 63`
- unknown enum => reject
- unsupported target/effect/color combination => reject
- text decode failure => reject
- unknown/rejected commands are consumed + logged safely on the dome, never fall
  through to other Marcduino handlers

### 1. `DL` — Logic / PSI Mode

```
DL:<target>:<mode>[:<color>[:<durationSec>]]
```
- **Targets:** `FLD`, `RLD`, `LOGIC` (FLD+RLD), `FPSI`, `RPSI`, `PSI` (front+rear PSI), `ALL` (FLD+RLD+front+rear PSI)
- **Modes (initial):** `NORMAL`, `ALARM`, `FAILURE`, `LEIA`, `MARCH`, `FLASHCOLOR`, `REDALERT`, `RAINBOW`, `LIGHTSOUT` (`FIRE`, `PULSE` later if source supports)
- **Color (optional):** maps to `LogicEngineRenderer::ColorVal` where supported. Initial set: `DEFAULT`, `RED`, `BLUE`, `GREEN`, `WHITE`, `YELLOW`, `ORANGE`, `PURPLE`. If a mode ignores color, apply safely but telemetry still reports the requested color.
- **Duration (optional):** seconds, `0` = renderer default / no explicit timeout. Range `0..99`.
- **Examples:** `DL:LOGIC:MARCH:RED:47`, `DL:PSI:ALARM:DEFAULT:10`, `DL:FLD:NORMAL`
- **Dome behavior:** `selectSequence(mode, color, 0, duration)` where supported. No panel motion, no `DM:*`, no `dome=seqon/seqoff`.

### 2. `DH` — Holo Effect

```
DH:<target>:<effect>[:<color>[:<durationOrCount>]]
```
- **Targets:** `F` (front), `R` (rear), `T` (top), `A` (all)
- **Effects (initial):** `OFF`, `ON`, `RESET`, `RANDOM`, `WAG`, `NOD`, `PULSE`, `RAINBOW`, `FLASH`, `SHORTCIRCUIT`, `SOLID`
- **Color (optional, effect-dependent):** global enum `DEFAULT`, `RED`, `BLUE`, `GREEN`, `WHITE`, `YELLOW`, `ORANGE`, `PURPLE`, `RANDOM`, then the **per-effect matrix below**.
- **Duration/count (optional):** timed LED effects = seconds; `WAG`/`NOD` = count. Range `0..99`.
- **Examples:** `DH:A:FLASH:RED:10`, `DH:F:RAINBOW`, `DH:A:WAG:DEFAULT:5`, `DH:A:RESET`, `DH:T:PULSE:RANDOM`
- **Dome behavior:** translate to existing `HPF`/`HPR`/`HPT`/`HPA` and `*` internally.

#### Effect/color + duration matrix (strict — mirrored in both Protocol Checks)

Both `data/seq_protocol_check.js` and `src/protocol_check.cpp` enforce this matrix
identically, so unsupported combinations (e.g. `DH:A:RAINBOW:RED`) are rejected
**before send** rather than relying on the dome to reject. `DEFAULT` (or omitted
color) is always accepted; an omitted duration is treated as `0`.

| Effect | Allowed colors | Duration/count |
|---|---|---|
| `RESET` | `DEFAULT` only | none (omit or `0`) |
| `OFF` | `DEFAULT` only | none |
| `ON` | any color | none |
| `SOLID` | any color | none |
| `RANDOM` | `DEFAULT` only | none |
| `WAG` | `DEFAULT` only | count `0..99` (`0` => dome default 5) |
| `NOD` | `DEFAULT` only | count `0..99` (`0` => dome default 5) |
| `PULSE` | `DEFAULT`, `RANDOM` | none |
| `RAINBOW` | `DEFAULT` only | none |
| `FLASH` | `DEFAULT`, `WHITE`, `RED` | seconds `0..99` (`0` => dome default 5) |
| `SHORTCIRCUIT` | `DEFAULT`, `RANDOM` | none |

Rejected examples: `DH:A:RAINBOW:RED`, `DH:F:SHORTCIRCUIT:BLUE`, `DH:T:PULSE:GREEN`,
`DH:A:WAG:RED:5`, `DH:A:RESET:RED`, `DH:A:OFF:WHITE`, `DH:A:FLASH:BLUE:5`.

### 3. `DT` — Logic Text (multi-line)

```
DT:<target>:<color>:<durationSec>:<speed>:<encodedText>
```
- **Targets:** `FLD`, `RLD`, `LOGIC` (both)
- **Encoding:** **percent-encoding** (not base64). Required escapes: newline=`%0A`, percent=`%25`, colon=`%3A`. Carriage return rejected; non-printable ASCII rejected. Spaces literal or `%20` (Protocol Check normalizes/allows both).
- **Length caps (first slice):** encoded text `<= 40` chars; decoded text `<= 32` chars; max one newline; reject if final command length `> 63`; reject decoded control chars except newline; reject empty decoded text.
- **Color:** same set as `DL`; `DEFAULT` allowed.
- **Duration:** `0..99` seconds.
- **Speed:** `0..9` (renderer scale); default `0`/`1` pending what AstroPixelsPlus expects (native calls often pass `0`).
- **Direction/effect:** first slice **scroll-left only** (`selectScrollTextLeft`); no direction arg until needed.
- **Examples:** `DT:FLD:DEFAULT:10:0:You're%0AWonderful`, `DT:RLD:BLUE:8:0:General%20Kenobi`
- **Dome behavior:** decode percent-encoding, call `selectScrollTextLeft(decodedText, color, speed, duration)`. No panels/audio/`DM`/seqon.

## Telemetry

Dome exposes per-step-type applied state + counters, parallel to the existing
`visual_preset` telemetry, under `/api/health`:

```json
"visual_authoring": {
  "logic": { "last_cmd": "DL:LOGIC:MARCH:RED:47", "target": "LOGIC", "mode": "MARCH", "color": "RED", "duration": 47, "apply_count": 3, "reject_count": 0 },
  "text":  { "last_cmd": "DT:FLD:DEFAULT:10:0:You're%0AWonderful", "target": "FLD", "color": "DEFAULT", "duration": 10, "speed": 0, "decoded_length": 16, "apply_count": 1, "reject_count": 0 },
  "holo":  { "last_cmd": "DH:A:FLASH:RED:10", "target": "A", "effect": "FLASH", "color": "RED", "duration_or_count": 10, "apply_count": 4, "reject_count": 0 }
}
```

Dome logs: `[DL] applied …`, `[DT] applied …`, `[DH] applied …`, and
`[DL][reject] reason …` / `[DT][reject] …` / `[DH][reject] …`.

## Implementation order (agreed) — COMPLETE

1. ✅ **Body** structured UI model + command serialization (editor cards for DV/DL/DT/DH).
2. ✅ **Dome** implements `DL`/`DT`/`DH` with telemetry (AstroPixelsPlus).
3. ✅ **Body** Protocol Check whitelist + strict grammar; `DH` effect/color matrix mirrored.
4. ✅ **Hardware-verified** one case per family via body-driven `DM:VISTEST` (2026-06-22):
   - `DL:LOGIC:MARCH:RED:5` — applied, reject 0
   - `DT:FLD:DEFAULT:5:0:TEST%0ATEXT` — applied, reject 0
   - `DH:A:FLASH:RED:5` — applied, reject 0
   - `DV:RESET_VISUALS` — applied (cleanup)

   Body emitted all cmds over body-link UART (`overflow=0`), dome `visual_authoring`
   apply counts incremented with `reject_count` 0; operator visually confirmed FLD
   logic + text; codex confirmed dome-side `[DL]/[DT]/[DH]/[DV] applied`.

## Open items to confirm during implementation
- `DT` default `speed` (0 vs 1) — confirm against AstroPixelsPlus renderer.
- Whether spaces are kept literal or normalized to `%20` in the mirror.
- Later additions gated on confirmed source support: `DL` `FIRE`/`PULSE`, `DT` direction arg.
