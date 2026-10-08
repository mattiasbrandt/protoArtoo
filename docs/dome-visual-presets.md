# Dome Visual Presets (`DV:<name>`) - Cross-Repo Contract + Parity Table

Status: **shipped.** Defines what the body *asks* the dome (AstroPixelsPlus) for
and what each body-owned Factory sequence that sends a preset is expected to
*look* like. Both Protocol Checks accept only the known names.

Related: [sequence-parity.md](sequence-parity.md) (per-Factory body behavior +
cleanup invariant), [commands.md](commands.md) (command surfaces / transport),
[adr/0008-body-sequences-use-panel-intent.md](adr/0008-body-sequences-use-panel-intent.md)
(body owns timelines, dome owns calibrated execution).

---

## 1. Why this exists

The body sequence coordinator makes community/factory `DM:*` sequences tunable
per droid without recompiling/flashing the dome: the body owns the timeline,
audio, suppression, and panel intent. But the *rendered* result must still match
the dome-native sequence identity — logic displays (FLD/RLD), PSI, holos, colors,
animations, timing, and resets.

Example: body-owned `DM:ROCKMARCH` played music and moved panels, but the FLDs
stayed **default blue**, while dome-native ROCKMARCH renders a richer typed
preset: red MARCH logic/PSI/holo with duration/color semantics. The body cannot
reliably reproduce dome-native visual identity by approximating with raw public
`@`/`*` commands, just as it could not safely reproduce panel choreography with
raw `:SM`. It needs a high-level visual intent command.

---

## 2. Authority split

| Concern | Owner |
|---|---|
| Timeline, step timing, suppression | **Body** |
| Audio (`$...`) | **Body** |
| Panel intent (`:OP/:CL/:OF`, ring/pie, cleanup, latches) | **Body** |
| Sequence start/end, cleanup timing | **Body** |
| Which named visual preset plays, and when (request `DV:<name>` at a step time) | **Body** |
| Rich visual *rendering* of a named preset (FLD/RLD anim+color, PSI anim+color, holo effect+color, duration) | **Dome** |
| Mapping `DV:<name>` -> the same typed preset the dome uses for dome-native `DM:<name>` | **Dome** |

---

## 3. `DV:<name>` command contract

A request from the body to the dome to apply a named **visual** preset.

**Name form:** `DV:<NAME>` — **strict uppercase**, closed/known set owned by the
dome. Matches the Factory sequence base name where a 1:1 native preset exists
(`DV:ROCKMARCH`, `DV:VADER`, ...).

**Scope (visual-only):** logic displays (FLD/RLD), PSI, holos — color, animation/
preset, duration. Nothing else.

**Not allowed:**
- no panels
- no body audio
- no full `DM:*` forwarding
- no `dome=seqon` / `dome=seqoff`
- no body suppression control
- no `dome_seqRunning` ownership
- no `dome_pendingAnim` if that implies full choreography ownership

`DV:` owns no sequence timer or window: the durations in section 5 are the ones
the dome passes to its visual engines.

**Body still owns:** timeline, audio, suppression, panel intent, cleanup timing,
sequence start/end.

**Dome owns:** rich visual rendering for named presets.

**Unknown `DV:<NAME>`:** log clearly, ignore safely, **no state change, no panels,
no fallback to `DM:*`**.

**Transport:** the body sends `DV:<name>` over protoR2link, the **same dome
command path as `@`/`*`**. No new link mechanism. The dome **logs receive +
dispatch + apply**, e.g. `[DV] applied ROCKMARCH`. `DV:` is distinct from
`DM:*` (a body-side trigger, never forwarded) and from raw `@`/`*` (forwarded and
interpreted verbatim): it is the high-level *named typed preset* the raw surface
cannot express, the visual analogue of `:OP/:CL/:OF` vs raw `:SM` (per ADR 0008).

**Visual teardown:** body-owned: `@0T1`, `@0P1`, `*ST00` at sequence end (logics
NORMAL, PSIs NORMAL, holos `HPA0000`). The dome-side `DV:RESET_VISUALS` also
exists and both Protocol Checks accept it, but it is not identical: it uses the
dome-native reset helpers, which leave the holos at `HPS9`. Factory sequences use
the body teardown. There is no `DV:RESET`.

---

## 4. Source of truth for native visual identity

**AstroPixelsPlus `DomeSequences.h` is the primary source** for each sequence's
native visual identity. The dome-side `DV:` implementation reuses the
**visual-only** portions of the native `DM:*` implementations. **Do not derive
presets only from the body's raw commands**: those are an approximation, not
authority.

---

## 5. Per-Factory visual parity table

Every row's anim/color/duration cells were confirmed against `DomeSequences.h`.

| Seq | Body sends | Expected visual identity | FLD/RLD anim | FLD/RLD color | PSI anim/color | Holo behavior/color | Duration | Teardown | Seen on a droid | Notes |
|---|---|---|---|---|---|---|---|---|---|---|
| **ROCKMARCH** | `DV:ROCKMARCH` | red MARCH logics + MARCH PSI + red holo flashes ~47 s | MARCH | red (`kRed`) | MARCH front+rear, 47 s | `HPA0021` red flashes, 47 s | ~47 s | body `@0T1`/`@0P1`/`*ST00` | not yet | identical to `DV:VADER` |
| **VADER** | `DV:VADER` | red MARCH logics + MARCH PSI + red holo ~47 s | MARCH (FLD+RLD) | red (`kRed`) | MARCH front+rear, `kDefault`, 47 s | `HPA0021\|47` red flashes | ~47 s | body `@0T1`/`@0P1`/`*ST00` | yes | the dome applies the same visuals as `DV:ROCKMARCH`; audio `$M` outlasts the visual |
| **CANTINA** | `DV:CANTINA` | FLASHCOLOR blue logics + flashcolor PSI + white holo flashes ~15 s | FLASHCOLOR (FLD+RLD) | **blue (`kBlue`)** | FLASHCOLOR front+rear, `kDefault`, 15 s | `HPA0029\|15` all holos white flashes | 15 s | body `@0T1`/`@0P1`/`*ST00` | not yet | logic is **blue**, holos white; moves pies |
| **LEIA** | `DV:LEIA` | LEIA logics + LEIA PSI + Leia-message holo ~36 s | LEIA (FLD+RLD) | `kDefault` | LEIA front+rear, `kDefault`, 36 s | `HPS101\|36` front Leia seq, `HPR02\|36` rear off, `HPT02\|36` top off | 36 s | body `@0T1`/`@0P1`/`*ST00` | yes | audio `$L` outlasts the visual |
| **ALARM** | `DV:ALARM` | ALARM logics + ALARM PSI + red holo flashes ~10 s | ALARM (FLD+RLD) | `kDefault` | ALARM front+rear, `kDefault`, 10 s | `HPA0021\|10` all holos red flashes | 10 s | body `@0T1`/`@0P1`/`*ST00` | yes | - |
| **HEART** | `DV:HEART` | FLD scroll text "You're\nWonderful" + front PSI flashcolor + rainbow holos ~10 s | **FLD: scroll text** "You're\nWonderful"; **RLD untouched** | FLD `kDefault` | **front** PSI FLASHCOLOR `kDefault` 10 s; **rear PSI untouched** | `HPF006/HPR006/HPT006\|10` rainbow | 10 s | body `@0T1`/`@0P1`/`*ST00` | not on its own; uses the same dispatch and teardown as VADER/ALARM/LEIA | `DV:HEART` draws the FLD text itself (two lines), so the body sends no text of its own |
| **SCREAM** | `DV:SCREAM` | REDALERT logics + REDALERT PSI + short-circuit/wag holos | REDALERT (FLD+RLD) | `kDefault` | REDALERT front+rear, `kDefault`, 15 s | `HPA0070` short-circuit random color + `HPA105\|5` wag x5 | 15 s (logic/PSI); holos are effect cmds (only wag has count) | body `@0T1`/`@0P1`/`*ST00` | not yet | moves pies |
| **OVERLOAD** | `DV:OVERLOAD` | FAILURE logics + FAILURE PSI + short-circuit holos ~12 s | FAILURE (FLD+RLD) | no explicit color/duration in source | FAILURE front+rear, `kDefault`, 12 s | `HPA0070` short-circuit random color | PSI 12 s; logic has no explicit duration in source | body `@0T1`/`@0P1`/`*ST00` | not yet | moves pies |

Sequences with no distinctive visual identity (`DM:NOD`, `DM:HELLO`,
`DM:FLUTTER`, `DM:BLOOM`, panel/text-only) send no `DV:` preset. A `DV:HELLO`
preset exists on both sides, but the HELLO Factory sequence does not use it: it
sends its own logic text.

---

## 6. Telemetry and evidence

**Dome-side** (`/api/health` on the dome):
- current / last visual preset name
- last `DV:` command
- `DV:` apply count
- `DV:` unknown/error count
- RX/dispatch stream or recent command buffer
- `queue_full_count`, `dropped_cmd_count`
- `reset_reason` / `reset_reason_code` / `coredump_present`

**Body-side:** `GET /api/seq/last-run` ([api.md](api.md)) reports what the last
run sent and the cleanup it queued.

A `DV:` sequence counts as checked only with machine evidence that the requested
preset was applied, plus a short operator confirmation of what the dome shows.

---

## 7. Protocol Check rules for `DV:`

Both Protocol Checks (`src/protocol_check.cpp`, `data/seq_protocol_check.js`)
hold these when Factory or Learned sequences carry `DV:`:
- whitelist **strict `DV:<KNOWN_NAME>`** values only;
- **reject unknown `DV:` names** in persisted/replayable sequence authoring;
- keep `DV:` **out of panel cleanup semantics** (it is visual-only, and is no
  panel close);
- do not let the editor's **Dome command** step (a raw string) become a
  loophole for arbitrary unsafe behavior.
