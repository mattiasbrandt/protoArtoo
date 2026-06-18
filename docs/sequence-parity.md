# Factory Sequence Parity / Migration Checklist

Reference for body-owned Factory `DM:*` sequences: what each one is expected to do on
audio / panels / logic / PSI / holo / cleanup, plus known deviations from the dome-native
version and a verification status. Purpose: write expected behavior down **once** so neither
the operator nor the agents have to re-derive it live during testing (see
`feedback_dont_make_operator_the_diff_engine`). Pairs with the machine-readable run-evidence
work (task #6) and the `DV:<name>` visual-preset surface (task #5).

Source of truth: `src/tasks/sequence_catalog.cpp`. Keep this in sync when sequences change.

## Cleanup invariant (2026-06-18, post dome-brownout fix)

The body **never auto-emits a group close** (`:CL15`/`:CL14`/`:CL00`) on any engine/cleanup/
resync path — a group close drives every group servo simultaneously and browns out the dome
from a loaded ring. Precision differs by path:
- **Terminal / abort cleanup** closes only the ring panels left **logically open** (net-open mask),
  one at a time at ~500 ms (`:CLnn`). A sequence that closes its panels in-steps emits **no terminal
  close at all**.
- **Reconnect / estop-clear resync** is **blind ring-only staged cleanup** — it closes all ring
  panels individually (`:CL01 :CL02 :CL03 :CL04 :CL07 :CL11 :CL13`, one per ~500 ms) because the
  dome's panel state is unknown after a boot/link gap.

Neither path emits group closes or auto-closes pies; `:OF` flutters do not mark a panel open.

## Status legend

- `hardware-verified` — confirmed on the droid (body TX + dome log + operator eyes)
- `dome-log-verified` / `body-log-verified` — confirmed from one controller's logs
- `software-expected` — behavior from code/native tests; not yet on hardware
- `needs-visual-parity-work` — logic/PSI/holo differs from dome-native (color/duration/typed
  effect the public `@`/`*` commands can't express) → `DV:<name>` preset (task #5)
- `do-not-test-yet` — known hazard or unverified mechanical risk; hold

## Body-owned Factory sequences

| Seq | Audio | Panels | Logic | PSI | Holo | Cleanup | Known deviations | Status |
|---|---|---|---|---|---|---|---|---|
| **DM:VADER** | `$M` Imperial March | none | `@0T11` MARCH | `@0P11` MARCH | `@HPA0021\|47` red 47s | effect resets only (no panels) | logic/PSI/holo lack native color+duration | software-expected, needs-visual-parity-work |
| **DM:HELLO** | `$H` + logic text | ring P1 (flutter opens + close) | — | — | — | P1 closed in-steps → none | **jams dome+pies on this droid, needs power-cycle** | **do-not-test-yet** |
| **DM:NOD** | `$H` + `@1MYes` | ring P1 open/close | text only | — | — | P1 closed in-steps → none | — | hardware-verified (prior session) |
| **DM:FLUTTER** | none | ALL ring+pie open then close in-steps | — | — | — | closed in-steps → none | opens pies (movement unverified) | do-not-test-yet (pie movement) |
| **DM:BLOOM** | none | pies open ×many then `:CLP*` | — | — | — | closed in-steps → none | pie-heavy; movement unverified | do-not-test-yet (pie movement) |
| **DM:LEIA** | `$L` | none | `@0T6` | `@0P6` | `@HPS101/HPR02/HPT02 \|36` | effect resets only | typed holo/logic vs native | software-expected, needs-visual-parity-work |
| **DM:ALARM** | category | none | `@0T3` | `@0P3` | `@HPA0021\|10` | effect resets only | color/duration vs native | software-expected, needs-visual-parity-work |
| **DM:HEART** | category | none | `@1MYou're Wonderful` | `@1P2` | `@HPF/HPR/HPT006\|10` rainbow | effect resets only | typed visuals vs native | software-expected, needs-visual-parity-work |
| **DM:RESET** | `$s` stop | **authored `:CL00`** (close+release all) | `@0T1` | `@0P1` | `*ST00` | the `:CL00` IS the choreography | **authored group `:CL00` = brownout hazard** (Slice 3 / task #3 re-author + latch-clear) | **do-not-test-yet** |
| **DM:CANTINA** | `$C` | LOOP: ring+pie open/close mix | `@0T2` | `@0P2` | `@HPA0029\|15` | in-steps closes | opens pies; dense loop cadence (queue risk) | do-not-test-yet (pie movement) |
| **DM:ROCKMARCH** | `$M` (+ `$s` stop @47s) | LOOP: ring wave open/close ×7 | `@0T11` MARCH | `@0P11` MARCH | `@HPA0021\|47` | ring closed in-steps → none | **P1 can stay open (final-iter `:CL01` dropped, queue/cadence — task #4)**; visual lacks native MARCH/red/47s | hardware-verified: brownout-safe panel regression + music-stop ONLY — NOT visual/cadence complete (P1 → task #4; visual → task #5) |
| **DM:SCREAM** | category | opens ALL pies+ring, LOOP random flutter/open | `@0T5` | `@0P5` | `@HPA0070`, `@HPA105\|5` | in-steps / mask | opens pies; dense + random | do-not-test-yet (pie movement) |
| **DM:OVERLOAD** | category | random ring+pie **flutter** (`:OF`) | `@1T4`,`@2T4` | `@0P4` | `@HPA0070` | flutters don't mark open → **no terminal close** (native-test verified) | flutters pies (movement) | cleanup software-verified; do-not-test-yet on hw (pie movement) |
| **DM:PIES** (toggle) | `$H` | pie open wave (open branch) / `:CLP*` (close) | — | — | `*ST00` on close | open branch latches pies open; close runs per-pie `:CLP*` | pie open/close; pie-close mechanical safety unverified | do-not-test-yet (pie movement) |
| **DM:LOW** (toggle) | `$H` | ring open wave (open) / staggered `:CL*` (close) | — | — | `*ST00` on close | open latches ring open; close in-steps → **no terminal group close** | none — no automatic pie movement | **hardware-verified** (ring open/close + brownout-safe cleanup + group-close removal, 2026-06-18) |
| **DM:OPENALL** (toggle) | `$H` | ALL pies+ring open (open) / all individual close (close) | — | — | — | open latches all; close per-panel | opens pies; worst-case all-panel load | do-not-test-yet (pie movement) |

## Dome-native aliases (body forwards verbatim; NOT body-owned choreography)

These `DM:*` names forward a `:SE##` / `$NNN` target to the dome unchanged — the dome owns
all panel + visual behavior; the body does not choreograph them. Not body-parity rows.

> **Safety caveat:** forwarded aliases are compatibility-only and **not automatically safe on this
> droid**. Any alias that moves pies or uses dome-native panel groups needs separate hardware
> gating (same pie-movement / group-actuation cautions as the body-owned rows above).

`DM:STOP :SE00` · `DM:SESCREAM :SE01` · `DM:WAVE :SE02` · `DM:SMIRKWAVE :SE03` ·
`DM:OCWAVE :SE04` · `DM:BEEPCANTINA :SE05` · `DM:SHORT :SE06` · `DM:SECANTINA :SE07` ·
`DM:SELEIA :SE08` · `DM:DISCO :SE09` · `DM:SCREAMNOPANEL :SE50` · `DM:SCREAMPANEL :SE51` ·
`DM:WAVEPANEL :SE52` · `DM:SMIRKWAVEPANEL :SE53` · `DM:OPENWAVE :SE54` · `DM:MARCHINGANTS :SE55` ·
`DM:FAINT :SE56` · `DM:RYTHMIC :SE57` · `DM:HARLEMSHAKE $815` · `DM:GIRLONFIRE $821` ·
`DM:YODA $720` · `DM:TOPPANELS :SE12` · `DM:WIGGLE :SE16` · `DM:BYEBYE :SE58`

## Today's hardware results (2026-06-18, fw 357-g3b77028)

- **DM:LOW** — hardware-verified: ring open/close, brownout-safe cleanup (no terminal group
  close), estop abort closes only open panels staggered, estop-clear/reconnect resync staged
  individual closes. No automatic pie movement.
- **DM:ROCKMARCH** — panel-safety regression passed (no brownout, music stops at end, no group
  close). Open follow-ups: P1 can remain open (queue/cadence — task #4, fix = space cadence, NOT
  a close-all); visual parity incomplete (task #5 `DV:<name>`).

## How to use this in testing

For any sequence under test ask: which row are we verifying? what are the expected fields?
what deviations are already known? Confirm via machine evidence (body TX ↔ dome RX/dispatch,
task #6) rather than eyeballing — the operator is the final "feels good" judge, not the prover.
