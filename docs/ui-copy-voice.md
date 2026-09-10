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

8. **A heading carries a count, a state or a purpose — never appears bare.**
   The second line answers the question the heading raises.
   - Flat: `Outputs`
   - Maker: `Outputs — 13 assigned, 3 spare`
   *(#298's inventory recorded heading violations.)*

9. **Two easily-confused nouns are defined together, at the point of
   confusion** — not in separate paragraphs a reader must assemble.
   `Saving keeps the sequence in your library. Put on the droid is what
   decides which sequences it can actually fire.`
   *(`CONTEXT.md`'s Flagged Ambiguities ledger exists because this recurs.)*

10. **A warning opens with the fact, and the consequence is physical and
    specific** — never "may cause damage". This sharpens rules 1 and 5.
    - Flat: `Warning: acceleration limit may be unsafe`
    - Maker: `0 means unlimited, which on a panel means it slams.`

11. **Only a refusal names a severity.** Colour carries the rest: red is
    stopped or refused, amber is you can do something about this, a Note is
    uncoloured (#327). *Error* stays Protocol Check's word alone (ADR 0044).
    Log-level names — Error, Warning, Info, Debug — are names, not
    severities: the picker matches what the serial log prints.

12. **An explanation for a control that moves something is visible text at
    the entrance.** Beside the control, before the commitment, never only in
    a confirmation and never hover-only — a `title` on a button carries no
    affordance, and a tablet at the bench has no hover at all (ADR 0059).

13. **A value the builder has never set says so**, and is visually distinct
    from one they set. Not by colour — #327 reserves those. Where the value
    has a richer provenance, that detail goes in its explanation.

## Patterns worth reaching for

Guidance, not rules — no failure of ours has paid for these yet, and per #287
an unpaid rule is cut. Kept here because they are good craft when they fit.

- An empty state says *why* it is empty and offers the action that ends it.
- A lossy operation issues a receipt naming what was lost and what survives.
- **Count and name** — a size is not an address; the reader's next question
  is which one.
- Name the misdiagnosis a builder would otherwise reach: *"...which looks
  exactly like a firmware bug."*
- A moved thing leaves a forwarding address.

## Not our vocabulary

The honesty wording of a simulator — "not simulated", "stands in", "simulator
placeholders, NOT measured on your servos" — marks *model versus reality*, a
gap protoArtoo does not have. Ours are different distinctions and get their own
words: configured-but-never-actuated, declared-but-not-detected,
endpoint-typed versus endpoint-measured, saved-but-not-yet-applied.

## Naming

- One name per concept, everywhere: UI, docs, API descriptions, and release
  notes agree. Renames are a real change, not a copy tweak.
- This file carries the *rules*, not the word list. Operator terms and any
  collision between two meanings of a word are written in `CONTEXT.md`
  (Language, and the Flagged Ambiguities ledger) - whichever audience the
  word started in. Do not start a second glossary here.
- Part names follow `docs/droid-parts.yaml`: design part names as the base,
  community shorthand (PP/P/HP) as aliases shown alongside.
- State chips are verb-free state labels readable at arm's length
  (`DRIVE OFF`, `SBUS OK`, `ESTOP LATCHED`), never sentences.

## Review gate, and what does not depend on it

Copy review is part of code review for any change touching operator-facing
text: read the new text as a maker, flag any sentence that needs firmware
knowledge to parse, and check rule 1 on every parameter mentioned.

**This gate is not the mechanism, and on its own it did not hold** — #298's
inventory is what shipped past it. Three things are enforced instead of
reviewed (ADR 0059):

- an explanation is a **required field**, asserted by a check rather than
  populated by convention;
- a raw identifier reaches a surface only through a **mapping table**, so the
  code cannot print the identifier;
- the backticked entries in `CONTEXT.md`'s `_Avoid_` lines are **greppable**,
  and a vocabulary checker reports them.

What remains for the gate is what no grep can judge: whether a sentence names
a consequence, whether an explanation is where a builder meets the control,
and one name per concept for terms that cannot be enumerated.
