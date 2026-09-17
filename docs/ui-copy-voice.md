# Maker Voice — operator-facing copy and layout guide

Every piece of operator-facing text — UI labels, help text, hints, toasts,
error messages, wizard steps, release notes — is written in **maker voice**:
the language of a droid builder at the bench, never firmware or backend
vocabulary. The test for every sentence: **would a maker with no firmware
knowledge get this on first read?** If not, rewrite it before shipping.

This file has two halves, and they answer two halves of one question. **The
rules** below say how a sentence is written. **The anatomy**, further down,
says where that sentence sits and what it is written on — which is the other
half of whether a builder gets it on first read, because a consequence line in
the wrong place is a consequence line nobody read (ADR 0066). Neither half
defines terms: operator vocabulary lives in `CONTEXT.md`, and the anatomy's own
term is **Surface Anatomy** there.

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

## The anatomy — where the sentence sits

Every operator surface is the same shape, so a builder who has learned one
screen has learned them all (`CONTEXT.md` **Surface Anatomy**, ADR 0066). The
decision is that ADR's; the shape is below; the numbers are tokens declared once
in `data/style.css`; and `prototypes/395-surface-anatomy/` is the drawn
reference the operator picked. A surface that departs from this is a defect,
not a taste.

The rules continue the numbering above, because they are the same gate: a
review that reads the words and not the layout has read half the page.

14. **A surface opens with its name and the question it answers.**
    One `.title`, an `<h1>` that is the name in the nav, and a `.question`
    under it in the builder's words — not a description of the screen.
    - Flat: `Foot Drive` / `Drive configuration and telemetry`
    - Maker: `Foot Drive` / *Is the droid free to move, and how fast will the
      feet go?*

15. **A section head never appears bare, and its subtitle is computed.**
    `.sect` holds an `<h2>` (or `<h3>` for a section inside one) and a `.sub`.
    This is rule 8 in markup: the subtitle is a count, a state, a provenance or
    a purpose. A count or a state is read off what the droid answered, never
    typed into the markup — a constant looks right on every screenshot and is
    wrong on every droid.
    - `Parts` / *58 parts · 4 on an output · 54 not wired*
    - `Safety` / *Failsafe · clear*
    - `Web UI files` / *these pages, as one image*

16. **The work area is a table, a picture or a form, and only one of them.**
    It sits on a `.card` — one instrument plate, chamfered, with a seam all
    round — or in a `.bay`, which is several plates divided by seams rather
    than by gaps. A row of separate boxes with air between them is what a web
    page looks like; a bay is what a row of instruments looks like.

17. **An act is named beside the thing it acts on**, and the count rides in the
    label. Acts that move hardware sit apart from acts that do not. The only
    acts that leave the surface are the ones that act on the *droid* rather
    than on the screen — those ride the topbar beside the estop.
    - Flat: `Apply` under a table of ticked rows
    - Maker: `Apply this release to all 2 ticked outputs`

18. **A feedback line sits at the foot of what it reports on**, not at the foot
    of the surface. `.feedback` is `role="status"` and `aria-live="polite"`; a
    plate with its own act gets its own line. Three acts sharing one line at
    the bottom of a tall surface answer 900 px away from the button that was
    pressed.

19. **Three voices, and each has one job.** `.prose` is the why, at the one
    reading measure (`--measure`, 70 ch). `.hint` is the cue directly under a
    control, in the mono voice. A `.note` is the consequence beside a choice,
    and takes exactly two colours: `.note-act` amber says act on this,
    `.note-info` blue says here is information. Emphasis is weight, never
    colour.

20. **Colour reports how a thing is doing, and nothing else.** Four signal
    colours, from `CONTEXT.md` **Status Colour**: `--success` nominal,
    `--warning` degraded and you can do something about it, `--danger` stopped
    or refused, and the dim ink unlit for never asked, not fitted, switched
    off. **Blue is interaction alone** — selection, the row shown, the primary
    act, the focus ring — so nothing that reports a state is blue. A **chosen
    posture takes no colour at all**: a speed preset, a sleep state, a control
    mode is a value the builder set, and colouring it makes a setting read as a
    symptom. An **Availability Family** is told apart by treatment, never by
    hue.

21. **The numbers are tokens, and there is no second spelling.** A rule that
    wants a size, a step or a colour picks one of these rather than writing a
    number:

    | What | Token | Value |
    |---|---|---|
    | Grounds, darkest first | `--bg` `--well` `--surface` `--surface-alt` | the body shell, a lamp recess, a plate, a raised face |
    | Seams | `--border` `--border-strong` | the dome's panel lines |
    | Inks | `--text` `--text-dim` `--text-faint` | body, secondary, the dim voice |
    | The one accent | `--accent` `--accent-bright` `--accent-dim` | interaction only |
    | Signal lights | `--success` `--warning` `--danger` `--text-dim` | rule 20 |
    | Spacing ladder | `--space-2xs` … `--space-2xl` | 4 / 8 / 12 / 16 / 24 / 32 |
    | Type, by job | `--fs-hint` `--fs-sect` `--fs-cell` `--fs-body` `--fs-h2` `--fs-h1` `--fs-readout` | 10 / 11 / 13 / 14 / 16 / 22 / 28 px |
    | Reading measure | `--measure` | 70 ch, on `.prose` only |
    | The frame | `--rail` `--topbar-h` `--plate-h` `--gutter` | 200 / 60 / 56 / 24 px |
    | A row | `--row` | 40 px, a table row and a health row alike |
    | Corners | `--radius-plate` `--radius-xs` | 2 px a plate, 3 px a button |
    | Mono | `--font-mono` | readouts, addresses, section heads, hints |

    Three breakpoints, and a surface reaches for those rather than inventing a
    fourth: **1100 px** the rail becomes a strip, **900 px** bays and grids
    stack and the Status Plate folds, **600 px** two-column forms go to one.

    **A colour literal outside `:root` is a defect**, and
    `test/test_web/test_style_token_layer.js` turns the web suite red over one.
    The single exception is `data/_recovery_kernel.html`, which has to render
    when the stylesheet is what failed; its literals are copied by hand and
    each one names the token it mirrors.

22. **The frame is not the surface's to draw.** The topbar, the **Latching
    Estop**, the nav rail and the **Status Plate** belong to the Operator
    Shell and are on every screen. A surface does not repeat what the plate
    already says: the plate carries one freshness statement for the whole
    screen, so a second banner saying the stream dropped is the same fact
    announced twice.

23. **An icon stands beside a word, never instead of one.** Icons come from
    the inline sprite `data/shell.js` injects, inherit `currentColor`, and keep
    their label alongside. **No emoji on any operator surface** (operator
    decision 2026-09-13 on #395, ADR 0066): an emoji is a coloured picture the
    operator cannot restyle, it renders differently on every machine, and it
    carries nothing the word beside it did not. `make check-surface-anatomy`
    fails on a pictograph in `data/` and on a `<use>` that names no symbol.

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
knowledge to parse, and check rule 1 on every parameter mentioned. Layout
review is the same pass: check the head carries a subtitle (rule 15), the
consequence sits where the builder meets the control (rules 12 and 19), and
nothing new reports a state in the blue (rule 20).

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

The layout half is enforced the same way, and the split is the same. Two things
are checked rather than reviewed — `make check-surface-anatomy` on pictographs
and unresolved icons, and `test_style_token_layer.js` on colour literals
outside `:root`. What is left for the gate is what no parser can judge: whether
a subtitle is a count the droid actually answered with, whether an act sits
beside the thing it acts on, and whether a colour is reporting a state or a
choice.
