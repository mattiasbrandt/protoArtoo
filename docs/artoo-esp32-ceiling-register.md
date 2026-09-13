# What the 4 MB board costs

One row per thing the **artoo-esp32**'s 640 KiB filesystem has cost, so the price
of keeping it a fully supported target is visible in one place rather than
absorbed a ticket at a time.

> [!IMPORTANT]
> **Notice it, write the number down, keep working.** Do not diagnose it, do not
> fix it here, and do not raise a budget to make a row go away. A row is a
> sentence and a measurement.
>
> Rows are read **together, later**. A cost paid where it was found is a trade
> nobody got to see.

**The re-evaluation trigger** (ADR 0065): support for artoo-esp32 is reopened by
**the first thing a builder should be able to do that cannot ship there at all** --
the first breach of the floor itself, not a headroom number. Headroom falls for
ordinary reasons; a number would cry wolf. A row marked **breach** is that trigger
firing.

**Measuring it.** `tools/check_build_budgets.py` images the filesystem and counts
the blocks in `littlefs.bin` that are not erased. **`littlefs.bin`'s own size is
always the full partition** and is never the measurement.

## The register

| # | Date | Finding | Measured | Kind |
|---|---|---|---|---|
| 1 | 2026-09-11 | One `data/` image is built for both boards, so firebeetle2 carries the 4 MB board's filesystem byte for byte while 9,957,376 B of its own partition sits unused | both envs: **97 blocks = 397,312 B**, at `0b55e00f` | 🔬 The reason #382 exists |
| 2 | 2026-09-11 | The legacy and default asset sets diverged **before the ceiling forced it**: 16 photographs at 8 KiB would have cost 131,072 B against 258,048 B free, so they would have fitted on artoo-esp32 with 126,976 B to spare. Line drawings ship there instead for a default that is not conditional on a measurement | photos would have fitted: **258,048 B free**, need **131,072 B** | ⚖️ Chosen, **not** forced |
| 3 | 2026-09-11 | Nothing measured the filesystem at all, so three in-tree size comments drifted years out of date unnoticed -- `partitions_ota.csv` and `partitions_16mb_ota.csv` claimed "~151KB", `gzip_fsdata.py` claimed "~180 KB", against 397,312 B allocated | 2.6x understated | ✅ Fixed with the budget row |
| 4 | 2026-09-11 | The epic branch grew **+9 blocks in a day** -- 97 blocks on `main` at `0b55e00f`, 106 at `c3b2b7c2` -- so the budget's first raise was earned before the budget landed | 397,312 -> **434,176 B** | 🔬 Open: trips `fs_budget_bytes` on merge |
| 5 | 2026-09-13 | Learned Sequences capped at **ten**, down from sixteen, on both boards so a droid can do the same things on either. Sixteen never fitted here: beside the 113-block web image only ten full-size (12 KB) sequences could be saved before the free-space floor refused the next. The image was then reclaimed rather than the promise kept at sixteen (#382), and `fs_budget_bytes` is derived from the room ten need | 113-block image: **10** fit · 78-block image: **18** fit · ceiling for ten: **114 blocks** | ⚖️ Chosen (operator), not forced |

## How to add one

A row names what did not fit, what shipped instead, and the number. It does not
argue. If the thing that did not fit is something a builder should be able to do,
mark it **breach** -- that is the trigger, and it reopens the board question
rather than being absorbed here.

The arithmetic behind any budget raise lives in `fs_budget_rationale` in
`tools/build_budgets.json`, not here.
