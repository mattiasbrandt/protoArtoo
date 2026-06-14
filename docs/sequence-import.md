# Migrating a community sequence (developer playbook)

This is how a maintainer turns an accepted community-sequence request into a **Migrated
Sequence** -- a Factory Sequence translated from another R2 project into protoArtoo's C++
catalog. Migration is a **code + PR** activity, not an operator runtime feature
(see [ADR 0007](adr/0007-community-sequence-migration.md)). For the Factory authoring
mechanics referenced below, see [`sequence-authoring.md`](sequence-authoring.md);
for the architecture, [ADR 0004](adr/0004-body-centric-dm-sequence-coordinator.md) and
[ADR 0006](adr/0006-learned-sequences-runtime-tier.md).

## Workflow

```
GitHub issue            maintainer            developer (PR)
Sequence request   ->   evaluate         ->   translate -> sequence_catalog.cpp
Sequence contribution   (gate below)          + registry + test + credits
```

1. **Entry point.** A request arrives via the **Sequence request** template ("can we get
   project X's sequence?") or the **Sequence contribution** template ("here is one I built"
   -- the editor's *Share to project* button targets this). Both are in
   `.github/ISSUE_TEMPLATE/`.
2. **Evaluate** against the gate below. If it does not pass, say so honestly on the issue
   (per the project's issue-rejection convention) and close it.
3. **Migrate** into the Factory catalog and open a PR.

## Evaluation gate

Accept a sequence only if all hold; otherwise decline with a written reason.

1. **Novel.** Its operator-recognizable behavior is not already produced by a Factory
   sequence or an alias. Check the name *and the behavior* against the catalog and alias
   tables in `src/tasks/sequence_catalog.cpp` -- the 16 Factory sequences plus ~26 `:SE`/`$`
   aliases already cover the classic R2 repertoire, so most "ports" are duplicates.
2. **Plays to protoArtoo's strength.** Prefer choreographies that pair a body sound with
   synced dome and body motion. A panel-only wave with no sound is almost always already an
   alias.
3. **Maps cleanly.** Panels map onto our 13-slot dome map; the signature sound maps onto an
   existing named role or `audioCat` category (a brand-new named track is a separate
   factory-side change, not part of a migration).
4. **License permits redistribution.** See the table below; record provenance.

## Migration steps

A migration is **adaptation, not replication** -- the source targets different hardware
(panel count and map, sound board, easing). Reproduce the recognizable intent and the
sound-to-motion sync; adapt the mechanics; note deviations in the catalog comment.

### 1. Panels -> protoArtoo slots

```
slot 0=P1 1=P2 2=P3 3=P4 4=P7 5=P11 6=P13   (ring)
slot 7=PP5 8=PP1 9=PP2 10=PP4 11=PP6 12=PP3  (pie)
CLOSE=800  25%=1150  50%=1500  75%=1850  OPEN=2200
```

Map each source panel to the nearest protoArtoo slot by physical role. `:SM<slot>,<pulse>,<ms>`
is non-blocking -- compose motion by *when* moves are issued (same `t` = simultaneous;
staggered `t` = wave). Re-pulse any source that uses a different travel range onto 800..2200.

### 2. Sounds -> named roles / categories

Reference a sound by **role**, not a raw track number, so it follows the operator's
configured tracks. The authoritative role list is `kNamedSlotLabels` in `src/seq_json.cpp`;
the category list is `enum AudioPlaybackCategory` in `include/audio_playback_policy.h`.

| `$` command | role | | `audioCat` category |
|---|---|---|---|
| `$H` | happy | | `general`, `chatty`, `happy` |
| `$S` | scream | | `processing`, `sad`, `sentimental` |
| `$F` | faint | | `humming`, `scream`, `surprised` |
| `$L` | leia | | (full list in the enum) |
| `$c` / `$C` | cantina_s / cantina_l | | |
| `$M` | imp_march | | |
| `$W` | sw_theme | | |
| `$D` | disco | | |

If the source's signature sound has no matching role, the sequence is not a clean migration
(gate rule 3).

### 3. Dome dialect normalization

Map source Marcduino commands onto protoArtoo's whitelisted set: `:SM`, `:CL00`, `@...`
(logic/PSI), `*...` (holo/HP), `$...` (sound), `:SE##` (2-digit, zero-padded). Drop anything
outside that set.

- **Holo / logic / PSI are dome-executed.** Keep the *trigger* (e.g. `@0T6`, `*HPF...`);
  never author per-frame LED content -- the 9600-baud slip ring cannot carry it.
- **Easing.** protoArtoo issues linear `:SM` moves; approximate a source's easing with
  staggered linear moves and note it.

### 4. Write the Factory catalog entry

Author the `SeqStep[]` table with the `SEQ_*` macros in `src/tasks/sequence_catalog.cpp` and
add a catalog row. Unlike a Learned Sequence (where Protocol Check *infers* the effect class),
a Factory table **tags** the first step that activates each persistent effect (`FX_PANEL`,
`FX_LOGIC_PSI`, `FX_HOLO`, `FX_AUDIO`); the engine auto-resets the rest. `suppressMs` must be
`>=` the terminal `STEP_END` time. Top the table with a provenance comment block:

```cpp
// DM:EXAMPLE -- migrated from <project> (<url-or-commit>), <license>.
// Adapted: <deviations, e.g. linear easing, pie remap>. See docs/sequence-credits.md.
static const SeqStep kExampleSteps[] = { /* ... */ SEQ_TERM(<endMs>) };
// catalog row: { "DM:EXAMPLE", kExampleSteps, SEQ_STEPCOUNT(kExampleSteps), <suppressMs>, TOGGLE_NONE, nullptr, 0 }
```

Use a clean, **additive** `DM:` name that collides with no existing Factory or alias name.

### 5. Registry, test, provenance

- Add a `docs/action-registry.yaml` entry for the new `DM:*` action and run
  `make check-action-drift` until clean.
- Add a native engine-timeline test (alongside `test/test_native/test_sequence_engine`) that
  runs the table through the engine and asserts the expected action log.
- Record provenance in [`docs/sequence-credits.md`](sequence-credits.md) (sequence, source,
  origin, license, the migrating PR) -- in addition to the catalog comment block.

## Licensing posture

Attribution re-expression: re-author the *behavior* in our catalog; copy no source code or
assets. Prefer permissively-licensed sources, attribute all, and **skip any source that
forbids redistribution**.

| Source | License | Notes |
|---|---|---|
| Padawan360 (dankraus) | BSD-3-Clause | attribution + notice |
| ReelTwo library | LGPL | library code, not packaged choreographies; we don't link or copy it |
| Marcduino V2/V3 firmware | CuriousMarc / N. Hutchison (not an open license) | the `:SE` repertoire; protoArtoo aliases, never copies code |
| AstroPixelsPlus (reeltwo) + forks | see repo `LICENSE` | the dome firmware base |

## Verification

A Migrated Sequence is verified like any Factory Sequence:

| Layer | Proves |
|---|---|
| `make check-action-drift` + native engine-timeline test | the table is well-formed and deterministically produces the intended action log (`software-verified`) |
| Integrated-droid run (existing hardware gate) | it reads right on the real droid -- the only fidelity proof |

State verification honestly; a software test proves self-consistency, not fidelity to the
source. Hardware fidelity joins the existing v1.0.0 hardware gate.

## Per-migration checklist

- [ ] Behavior is novel (not a Factory sequence or alias) and sound-synced
- [ ] Panels mapped to protoArtoo slots; pulses on the 800..2200 scale
- [ ] Sound resolves to an existing named role or `audioCat` category
- [ ] Dialect normalized to the whitelist; holo/logic kept as triggers; easing noted
- [ ] Additive `DM:` name (no Factory/alias collision)
- [ ] License permits redistribution; provenance in catalog comment + `sequence-credits.md`
- [ ] `SeqStep[]` table + catalog row with explicit `FX_*` tags; `suppressMs >= STEP_END`
- [ ] `action-registry.yaml` entry; `make check-action-drift` clean
- [ ] Native engine-timeline test added; `software-verified`
- [ ] Hardware fidelity added to the v1.0.0 hardware gate
