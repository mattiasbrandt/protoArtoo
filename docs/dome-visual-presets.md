# Dome Visual Presets (`DV:<name>`) — Cross-Repo Contract + Parity Table

Status: **APPROVED direction** (operator + codex sign-off, 2026-06-18) with the
amendments folded in below. Coordination artifact between body (protoArtoo /
Claude) and dome (AstroPixelsPlus / codex). Defines what the body *asks* the dome
for and what each body-owned Factory sequence is expected to *look* like. Codex
implements the dome `DV:` surface against this; the body then swaps its raw
`@`/`*` visual approximations for `DV:<name>` requests.

Related: [sequence-parity.md](sequence-parity.md) (per-Factory body behavior +
cleanup invariant), [commands.md](commands.md) (command surfaces / transport),
[adr/0008-body-sequences-use-panel-intent.md](adr/0008-body-sequences-use-panel-intent.md)
(body owns timelines, dome owns calibrated execution), issue #2 task #5.

---

## 1. Why this exists

The body sequence coordinator makes community/factory `DM:*` sequences tunable
per droid without recompiling/flashing the dome: the body owns the timeline,
audio, suppression, and panel intent. But the *rendered* result must still match
the dome-native sequence identity — logic displays (FLD/RLD), PSI, holos, colors,
animations, timing, and resets.

Confirmed example: body-owned `DM:ROCKMARCH` plays music and drives panels, but
the FLDs stayed **default blue**, while dome-native ROCKMARCH renders a richer
typed preset: red MARCH logic/PSI/holo with duration/color semantics. This is not
a ROCKMARCH one-off — it is a **core parity problem**. The body cannot reliably
reproduce dome-native visual identity by approximating with raw public `@`/`*`
commands, just as it could not safely reproduce panel choreography with raw `:SM`.
We need a high-level visual intent command.

> Safety work made body-owned sequences *safe to run*; this work makes them
> *worth tuning*.

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

**Body still owns:** timeline, audio, suppression, panel intent, cleanup timing,
sequence start/end.

**Dome owns:** rich visual rendering for named presets.

**Unknown `DV:<NAME>`:** log clearly, ignore safely, **no state change, no panels,
no fallback to `DM:*`**.

**Transport:** the body sends `DV:<name>` over the **same dome command path as
`@`/`*`** (engine `STEP_DOME_CMD` payload `DV:ROCKMARCH` -> `domeQueueTx` ->
dome). No new link mechanism. The dome **logs receive + dispatch + apply**, e.g.
`[DV] applied ROCKMARCH`. `DV:` is distinct from `DM:*` (a body-side trigger,
never forwarded) and from raw `@`/`*` (forwarded and interpreted verbatim) — it
is the high-level *named typed preset* the raw surface cannot express, the visual
analogue of `:OP/:CL/:OF` vs raw `:SM` (per ADR 0008).

**Visual teardown:** body-owned for the first slice (`@0T1`, `@0P1`, `*ST00` at
sequence end). Leave a future option for a dome-side `DV:RESET` / `DV:RESET_VISUALS`
if raw reset proves insufficient.

---

## 4. Source of truth for native visual identity

**AstroPixelsPlus `DomeSequences.h` is the primary source** for each sequence's
native visual identity. The dome-side `DV:` implementation should reuse/extract
the **visual-only** portions of the native `DM:*` implementations. **Do not derive
final presets only from the current body raw commands** — those are an
approximation, not authority.

Codex-confirmed example (ROCKMARCH / VADER red MARCH):
- holo: `CommandEvent::process(F("HPA0021|47"))`
- logic: `FLD.selectSequence(MARCH, kRed, 0, 47)` and
  `RLD.selectSequence(MARCH, kRed, 0, 47)`
- front/rear PSI MARCH for 47 s

ALARM, LEIA, HEART, CANTINA, etc. also contain typed visual intent in
`DomeSequences.h` that raw body `@`/`*` lines may not fully express — codex to
extract per sequence.

---

## 5. Per-Factory visual parity table

**Authority is explicit per row.** `expected_visual_identity` is only a *target*
until `authority` says it is confirmed. Authority values:
**dome-source-confirmed** (against `DomeSequences.h`), **operator-confirmed**
(seen on hardware), **body-inferred** (only from the raw `@`/`*` the body emits
today — a guess, not a requirement), **unknown**. `body_current_visual_cmds` is
fact; body guesses must not become requirements.

| Seq | body_current_visual_cmds | expected_visual_identity | FLD/RLD anim | FLD/RLD color | PSI anim/color | Holo behavior/color | Duration | reset/teardown | authority | verification_status | notes / known deviations |
|---|---|---|---|---|---|---|---|---|---|---|---|
| **ROCKMARCH** | **`DV:ROCKMARCH`** (was `@0T11`/`@0P11`/`@HPA0021\|47`) | red MARCH logics + MARCH PSI + red holo flashes ~47 s | MARCH | red (`kRed`) | MARCH front+rear, 47 s | `HPA0021` red flashes, 47 s | ~47 s | body `@0T1`/`@0P1`/`*ST00` | **dome-source-confirmed + operator-confirmed** | **DV swap applied + software-verified** (995/995, build OK); dome surface verified by codex; awaiting on-droid visual confirm via the body-owned run | **first acceptance case — body swap DONE** |
| **VADER** | **`DV:VADER`** (was `@0T11`/`@0P11`/`@HPA0021\|47`) | red MARCH logics + MARCH PSI + red holo ~47 s (shares ROCKMARCH preset shape) | MARCH | red (`kRed`) | MARCH, 47 s | `HPA0021` red, 47 s | ~47 s | body `@0T1`/`@0P1`/`*ST00` | **dome-source-confirmed** | **DV swap applied + software-verified** (1020/1020, build OK); awaiting on-droid confirm after the ROCKMARCH gate | codex to confirm whether VADER preset is identical to ROCKMARCH or distinct |
| **CANTINA** | **`DV:CANTINA`** (was `@0T2`/`@0P2`/`@HPA0029\|15`) | logic/PSI mode 2 + `HPA0029` white flashes ~15 s | TBD | TBD | TBD | TBD | ~15 s | body `@0T1`/`@0P1`/`*ST00` | body-inferred, needs dome confirmation | **DV swap applied + software-verified**; not visually verified | loop header moved to step index 2; confirm against `DomeSequences.h`; pie-involving (panels DO-NOT-TEST) |
| **LEIA** | **`DV:LEIA`** (was `@0T6`/`@0P6`/`@HPS101/HPR02/HPT02\|36`) | logic/PSI mode 6 + Leia-message holo ~36 s | TBD | TBD | TBD | TBD | ~36 s | body `@0T1`/`@0P1`/`*ST00` | body-inferred, needs dome confirmation | **DV swap applied + software-verified**; not visually verified | confirm against `DomeSequences.h` |
| **ALARM** | **`DV:ALARM`** (was `@0T3`/`@0P3`/`@HPA0021\|10`) | logic/PSI mode 3 + `HPA0021` ~10 s | TBD | TBD | TBD | TBD | ~10 s | body `@0T1`/`@0P1`/`*ST00` | body-inferred, needs dome confirmation | **DV swap applied + software-verified**; not visually verified | confirm against `DomeSequences.h` |
| **HEART** | **`DV:HEART`** + raw `@1MYou're Wonderful` (was `@1P2`/`@HPF/HPR/HPT006\|10` + text) | front text + PSI mode 2 + rainbow holos ~10 s | TBD | TBD | TBD | TBD | ~10 s | body `@0T1`/`@0P1`/`*ST00` | body-inferred, needs dome confirmation | **DV swap applied + software-verified**; not visually verified | text stays body-owned raw, sent AFTER `DV:HEART` so the preset does not clobber it; `DV:HEART` covers PSI/holo only |
| **SCREAM** | **`DV:SCREAM`** (was `@0T5`/`@0P5`/`@HPA0070`/`@HPA105\|5`) | logic/PSI mode 5 + alarm/holo, random | TBD | TBD | TBD | TBD | random | body `@0T1`/`@0P1`/`*ST00` | body-inferred, needs dome confirmation | **DV swap applied + software-verified**; not visually verified | flutter loop header moved to step index 15; confirm against `DomeSequences.h`; gated by pie-movement do-not-test (upper pie panels have known mechanical linkage issues, need repair) |
| **OVERLOAD** | **`DV:OVERLOAD`** (was `@1T4`/`@2T4`/`@0P4`/`@HPA0070`) | logic mode 4 (front/rear) + PSI mode 4 + `HPA0070`, random | TBD | TBD | TBD | TBD | random | body `@0T1`/`@0P1`/`*ST00` | body-inferred, needs dome confirmation | **DV swap applied + software-verified**; not visually verified | confirm against `DomeSequences.h`; pie-involving (panels DO-NOT-TEST) |

Codex confirms `body-inferred` rows by extracting the visual-only portion of each
native `DM:*` in `DomeSequences.h` (or by hardware), then flips `authority` and
fills the real anim/color/duration. Until then those targets are not requirements.

Sequences with no distinctive visual identity (`DM:NOD`, `DM:HELLO`,
`DM:FLUTTER`, `DM:BLOOM`, panel/text-only) do **not** need a `DV:` preset.

---

## 6. First acceptance case — `DV:ROCKMARCH`

1. Body sends **`DV:ROCKMARCH` at t=0**.
2. Dome logs receive/dispatch/apply (`[DV] applied ROCKMARCH`) and applies the
   native visual-only ROCKMARCH/VADER-style preset: **red MARCH FLD/RLD, MARCH
   PSI, red holo flashes, ~47 s**.
3. Body still owns **music**, the **ring panel wave**, the **physical-assurance
   settle close**, and the **terminal reset**.
4. `DV:` must **not** move panels, forward `DM:ROCKMARCH`, emit
   `dome=seqon/seqoff`, or start dome sequence state.
5. Terminal visual reset stays body-owned (`@0T1`/`@0P1`/`*ST00`).
6. Safety unchanged: no group/pie closes, dome stays POWERON, no queue-full/drop.

---

## 7. Operator-verification requirement (not a nice-to-have)

Asking the operator to visually diff every sequence detail in real time does not
scale — they are driving the droid, coordinating two agents, watching panels,
listening for audio, and checking safety. So **each parity test is prepared as a
bounded checklist before the run**:

- Agents state **exactly what the operator should watch**, in plain physical
  terms, **short (ideally 1-3 visual confirmations per run)**.
- Everything machine-checkable is checked **by the agents** from logs/health/API.
- The operator report is treated as **physical observation**, not the primary
  diff mechanism.
- After the run, agents compare machine evidence against this parity table and
  report **PASS / FAIL / UNKNOWN (needs operator confirmation) / known deviation**.

For `DV:` specifically: the dome exposes current/last visual-preset telemetry so
agents verify `DV:ROCKMARCH` actually applied; the operator only confirms the
qualitative result ("FLDs are red MARCH, not default blue"). **A `DV:` sequence is
not verified merely because the operator says "looks okay"** — it needs machine
evidence the requested preset was applied **plus** a small predeclared operator
confirmation.

---

## 8. Telemetry and evidence (issue #2 task #6)

**Dome-side (codex):**
- current / last visual preset name
- last `DV:` command
- `DV:` apply count
- `DV:` unknown/error count
- RX/dispatch stream or recent command buffer
- `queue_full_count`, `dropped_cmd_count`
- `reset_reason` / `reset_reason_code` / `coredump_present` (already exposed in
  `/api/health`, verified 2026-06-18)

**Body-side (Claude):**
- last-run sequence name
- start/end timestamp
- TX stream
- cleanup emitted
- inferred panel/effect scopes
- net-open / touched ring masks
- dome-link drop/retry counts

This evidence validates against the parity table so the operator is not the diff
engine.

---

## 9. Rollout order

1. **(this artifact)** Body writes `DV:` contract + visual parity table.
2. **Codex** implements the dome `DV:` surface + basic telemetry (parse
   `DV:<NAME>`, map to typed visual-only preset from `DomeSequences.h`, log apply,
   expose last/current preset name).
3. **Body** whitelists `DV:` in Protocol Check and swaps **one** Factory sequence
   first (ROCKMARCH): body emits `DV:ROCKMARCH` instead of `@0T11`/`@0P11`/
   `@HPA0021|47`, keeps timeline/audio/panels/reset.
4. **Hardware-test ROCKMARCH visual parity** (bounded checklist + machine evidence).
5. Roll `DV:` through the rest of the Factory table as codex confirms each row.
6. Continue broader #6 machine-readable evidence and #3 prevention cleanup.

**Do not wait for the entire evidence system before proving the first `DV:` path.**

---

## 10. Protocol Check note (when body stores/sends `DV:`)

When body-owned Factory/Learned sequences carry `DV:`:
- whitelist **strict `DV:<KNOWN_NAME>`** values only;
- **reject unknown `DV:` names** in persisted/replayable sequence authoring;
- keep `DV:` **out of panel cleanup semantics** (it is visual-only, does not
  satisfy `:OF` cleanup or close requirements);
- do not let the "advanced raw command" path become a loophole for arbitrary
  unsafe behavior.

---

## 11. Decisions confirmed (operator + codex, 2026-06-18)

- New `DV:` command family: **yes**.
- Existing TX path (same class as `@`/`*`): **yes**.
- Strict known/uppercase name set: **yes**.
- Visual-only; no panels / audio / `DM:*` forwarding: **yes**.
- No `seqon`/`seqoff`, no suppression control: **yes**.
- Body-owned teardown for now: **yes**.
- Authority-explicit parity table: **yes**.
- ROCKMARCH first acceptance case: **yes**.
- Operator burden reduction is a **requirement**, not a nice-to-have.

## 12. Open questions for codex

1. `DV:` framing/length constraints on the body->dome link for `DV:<NAME>`?
2. Is the `DV:` name set strictly closed (reject unknown) — confirmed casing?
3. Can a preset render with **no** implicit `seqon/seqoff` and **no** dome
   sequence-state ownership?
4. Is the VADER preset identical to ROCKMARCH or distinct (per `DomeSequences.h`)?
5. Extract + confirm the `body-inferred` rows (CANTINA/LEIA/ALARM/HEART/SCREAM/
   OVERLOAD) from `DomeSequences.h`.
6. Teardown preference long-term: keep body `@0T1`/`@0P1`/`*ST00`, or add a
   dome-side `DV:RESET_VISUALS`?
