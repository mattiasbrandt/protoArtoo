# Only what is shown may differ between boards, and the artoo-esp32 shows drawings

Status: accepted (2026-09-11, issue #382). **Partly implemented.** The filesystem
budget, the per-environment asset-set staging and the register landed on 2026-09-11
(`88821e2d`, `7bc768d5`, `d21abbb6`, `c2c7c2a6`). What remains unbuilt is the only
thing that is not code: the sixteen line drawings the legacy set carries, which gate
guided Setup on every board. Both set directories are therefore still absent, and
staging is a measured no-op until they exist.

## Context

One LittleFS image is built for both boards. `tools/gzip_fsdata.py` stages the same
file list for every environment, so the 4 MB artoo-esp32's **640 KiB** filesystem
sets the ceiling for the 16 MB firebeetle2's **9.88 MB** as well — a board with
sixteen times the room gets nothing for it.

Measured 2026-09-11 by replaying the staging rules against committed blobs. Two
traps under-report this image and both had already reached a ticket: `console_help.txt`
is name-excluded from gzip and ships raw at 30 KB, and `<!-- PA:INCLUDE
_recovery_kernel.html -->` expands into all nine served pages before gzipping.

| | `main` @ `8a238516` | `epic/operator-experience` @ `1bd92529` |
|---|---|---|
| content | 299,320 B | 318,133 B |
| in 4 KiB blocks | 401,408 B (98) | 425,984 B (104) |
| headroom against 655,360 | **253,952 B (248 KiB)** | **229,376 B (224 KiB)** |

Block rounding is not a rounding error here: it costs **102,088 B** on `main`, and
70,892 B of that comes from the 26 files that are already larger than a block.

The firmware settled this question for **code** two ADRs ago and settled it the
other way. ADR 0029 refused "ship everything everywhere and toggle at runtime"
precisely so *"the artoo-esp32 build must not pay for unavailable features"*, and
gave it two compile-time tiers. Assets never got the equivalent, and nothing in
`tools/build_budgets.json` or `tools/check_build_budgets.py` budgets the filesystem
at all — which is why `partitions/partitions_ota.csv` and `tools/gzip_fsdata.py`
both still carry size comments that predate the UI's growth.

`CONTEXT.md`'s **Supported ESP32 Board** entry had ruled the other way in one
sentence — *"ESP32-P4 Target support does not relax the current requirement"* — and
the operator reopened it on 2026-09-11. A larger-flash module is not the way out:
the ESP32 is socketed on the Artoo PCB and Espressif ships WROOM-32E in N8 and N16,
but no D1-Mini32-footprint board with more than 4 MB fits it. That was researched
before this ticket and is closed (operator, 2026-09-11). **The 4 MB board is
permanent**, so every lever here is a software one.

## Decision

**The artoo-esp32 gets a floor, not a veto.** Best effort goes into making things
fit it, it stays a fully supported first-class target, and what that costs is
recorded rather than absorbed.

**Only what is *shown* may differ between boards.** Same surfaces, same choices,
same behaviour, everywhere. Nothing a builder can do depends on which board they
bought. Flash size never decides what a droid can do — that remains the **Board
Capability Gate**'s question, about silicon.

**Two asset sets.** The **default** set is the wider one and carries photographs.
The **legacy** set is the narrower one and carries line drawings in their place.
The name describes what the set carries, never the board: *the artoo-esp32 is not a
legacy board*, and calling it one is still wrong.

**Each file declares which boards carry it**, so the fact sits next to the thing
and cannot go stale. A per-board manifest was rejected for going stale the first
time somebody adds a file; packing until the budget is full was rejected for making
the contents of an image a property of build order.

**This is not a fifth tier.** "Is it in this image" is already one of the four
questions the ledger names, and it belongs to the **Build Feature Flag**, which
widens from compile-time `PA_*` flags to what the image carries at all. The
**Board Capability Gate**'s own `_Avoid_` line already pointed here: *"what fits in
the image is a different tier's answer"*.

**The boards diverge from the start.** The legacy set ships drawings even while
photographs would still fit, so the default is clean rather than conditional. The
sixteen drawings are made for this project rather than sourced: the reference
project's hand-drawn SVG art is MIT and reusable, but one consistent hand across
the set is worth more than the work it saves.

**The filesystem becomes a budgeted artifact**, per environment, reusing the schema
flash and RAM already use — `fs_ceiling_bytes` from the partition, `fs_budget_bytes`
as the ratchet, a baseline commit, and a `budget_rationale` string. A budget raise
and a record entry are therefore the same act.

**The re-evaluation has a trigger, not a someday.** The artoo-esp32's support is
reopened by **the first thing a builder should be able to do that cannot ship there
at all** — the first breach of the floor itself, rather than a headroom number,
which falls for ordinary reasons and would cry wolf.

## Considered options

- **One image forever** (the sentence this replaces) — spends nothing and gives the
  firebeetle2's flash no purpose at all. Rejected: it is the code-side posture ADR
  0029 already refused, applied to assets by inertia rather than by decision.
- **Let flash size decide what a droid can do** — a surface or capability on one
  board and not the other. Rejected: it puts flash size beside silicon in deciding
  what a droid is, and #369's goal that Setup and Configuration *"can never show
  different choices"* is the thing worth protecting.
- **The spare room is margin, never content** — one identical image, with the
  budget derived from the partition. Rejected as the same answer wearing a budget.
- **Diverge only when the photographs stop fitting** — they fit today (16 at 8 KiB
  is 131,072 B against 248 KiB) and the drawings would be a fallback nobody has
  drawn. Rejected in favour of a default that is not conditional on a measurement.
- **A fifth tier for what a board has room to show** — rejected for the reason the
  ledger has twice refused to multiply a term: `capability envelope` became
  **Framework Envelope**, and a board-declared pin value became **Board Lane**.
- **Reuse the reference project's MIT drawings** — explicitly permitted (*"Use it,
  change it, ship it; keep the notice"*, and only its 3D geometry and board photos
  are carved out), and four of our roadmap parts are hardware it also covers.
  Rejected for one consistent hand across sixteen cards.

## Consequences

- **Sixteen line drawings now gate guided Setup on every board.** The picker cannot
  ship with a board where every card is blank, so the art lands ahead of #316's
  photography rather than beside it. It is the critical path of #382.
- **#316's photographs ship to the default set only.** Its per-image cap also wants
  moving to a block boundary: at 9 KB each photo costs three 4 KiB blocks, at 8 KiB
  it costs two, and 1 KB of quality per image returns 65,536 B across the set.
- The **Component Picker**'s rule that a card missing its photo is *"a cosmetic
  gap"* now has a second case beside it: on the legacy set a drawing is the normal
  state and not a gap at all.
- Two stale size comments — `partitions/partitions_ota.csv` ("~151KB") and
  `tools/gzip_fsdata.py` ("~180 KB") — are wrong against a measured 242,546 B and
  are corrected when the budget row lands.
- A budget set from `main`'s numbers trips when `epic/operator-experience` merges,
  by 24,576 B of blocks. That is the ratchet working; the raise names the growth.
