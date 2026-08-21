# Maker Voice — operator-facing copy guide

Every piece of operator-facing text — UI labels, help text, hints, toasts,
error messages, wizard steps, release notes — is written in **maker voice**:
the language of a droid builder at the bench, never firmware or backend
vocabulary. The test for every sentence: **would a maker with no firmware
knowledge get this on first read?** If not, rewrite it before shipping.

## The rules

1. **End every parameter in its physical consequence.**
   The number alone is not an explanation; say what it does to the droid.
   - Flat: `suppressMs: cooldown period after sequence`
   - Maker: `Suppress is how long the droid ignores repeat triggers after a
     sequence ends — too short and a bouncy switch plays it twice.`

2. **Define by contrast and feel, not by category.**
   Say what a thing is *as opposed to* its neighbour, and what it looks like
   on the droid.
   - Flat: `Flutter mode: rapid panel actuation`
   - Maker: `Flutter is excitement, not opening — the panel trembles around
     closed and must be followed by a real close.`

3. **State units concretely once, then stop leaning on them.**
   - `Dome speed is in percent of the ESC's full rate; 20 is a slow scan,
     100 is as fast as the dome ever turns.`

4. **Explain the surface in the same breath as the data.**
   Help text covers how the screen behaves, not only what fields mean:
   `The name column stays pinned while you scroll, so the output you are
   testing never leaves the screen.`

5. **Fold safety into the sentence, not a warning box.**
   - Flat: `Warning: value may exceed servo limits`
   - Maker: `Positions are clamped to the ends you measured — a shared
     sequence can never push your servo past them.`

6. **Plain imperative verbs**: tick, name, pick, press, drag, type, watch.
   The operator does things to the droid; the copy says which thing.
   Backend nouns (dispatch, task, NVS, handler, payload) stay in code and
   internal docs.

7. **Be honest about model vs reality in one clause.**
   When a control acts on a simulation, a cached value, or a disconnected
   device, the copy says so where the operator is looking:
   `The bar shows the last position the controller reported — unplug the
   dome and it is a memory, not a measurement.`

## Naming

- One name per concept, everywhere: UI, docs, API descriptions, and release
  notes agree. Renames are a real change, not a copy tweak.
- Part names follow `docs/droid-parts.yaml`: design part names as the base,
  community shorthand (PP/P/HP) as aliases shown alongside.
- State chips are verb-free state labels readable at arm's length
  (`DRIVE OFF`, `SBUS OK`, `ESTOP LATCHED`), never sentences.

## Review gate

Copy review is part of code review for any change touching operator-facing
text: read the new text as a maker, flag any sentence that needs firmware
knowledge to parse, and check rule 1 on every parameter mentioned.
