# An explanation is a field, and a raw name has no path to a screen

Status: accepted (2026-09-09, issue #334). Describes the **target** model; none of
it is implemented yet.

## Context

`docs/ui-copy-voice.md` has existed throughout, with seven good rules and a review
gate. #298 closed by producing an inventory of what shipped anyway: **53 bare
"controller" strings**, **19 hardcoded board-specific strings** stating another
board's wiring as fact, four verbatim-token leaks, and heading violations. It
recorded that the drift check which would have caught them **does not exist**.

So a written voice and a review gate are not the mechanism. That is the problem
this ADR is about, and the reference project's answer is structural rather than
editorial: explanation is not a habit maintained, it is a field that cannot be
left out, and machine vocabulary is refused mechanically rather than remembered.

Two things measured on `main` for this decision, both correcting the ticket:

- **`docs/action-registry.yaml` already has a `description`, populated on all 194
  entries.** Every one is a gloss — "Set drive speed and steering", "Left/right
  steering (analog axis binding)", "Persisted maximum drive speed setting". None
  says what happens to the droid, which is exactly what `ui-copy-voice.md` rule 1
  already asks for. **A required explanation field was already in place and
  already failing**, which is the same shape as the failure of the review gate.
- **Nothing enforces it.** `tools/check_action_registry_drift.py` checks
  `description` only for *parity* against `data/console_help.txt`, the C++ registry
  row and the JS fallback row. An entry added with `description: ""` passes
  `make check-action-drift` today. The 194/194 population is convention.

And one that reframes where explanation is thin. protoArtoo carries **118 visible
hint blocks** against roughly **85 tooltips**; `wifi.html` and `firmware.html` have
zero hover help and eight and nine visible `desc` blocks respectively. **We already
explain in visible prose. It is hover help we are thin on** — and a tooltip on a
`button`, `input` or `select` gets no affordance at all (`data/style.css:1742`), so
most of ours are discoverable only by accident and unreachable on a touch screen.

## Decision

**An explanation is a required field, and a raw name has no path to a screen.**
Two mechanisms, because #298's inventory records two different failures.

- **A required field**, asserted by a check rather than populated by convention.
  Its first target is `docs/action-registry.yaml`'s **62 `params` objects**, which
  carry a name, a type and a bound and nothing a person reads — a builder meets a
  number there that moves something.
- **A mapping table**: an operator surface reaches a raw identifier only through a
  table that maps it to operator words, so the code cannot print the identifier.
  Paid by #298's four verbatim-token leaks.

**A finding carries fields; the prose is composed at the surface.** ADR 0044 settled
that the Rehearsal rules on intent, can never refuse, and carries a Warning and a
Note. It did not settle a finding's shape. A finding carrying what it is about can
put a fix in front of the builder; one carrying a sentence can only be read.

**An explanation for a control that moves something is visible text on the page,
at the entrance.** Not hover-only, and never only in the confirmation — a required
field is otherwise satisfied by filling in an "are you sure?" dialog. The bound is
deliberate: the blanket version, every control on every page, has no failure of
ours behind it. Two facts fix the vehicle: a `title` on a button or input carries
no affordance, and **a touch device has no hover at all** — a builder at a bench
with a tablet cannot reach it.

**Only a refusal names a severity.** #327's two Status Colours already separate the
three levels — red for stopped or refused, amber for *"you can do something about
this, and should"*, uncoloured for a Note. ADR 0044's carve-out stands: *error*
remains Protocol Check's word alone, because a builder whose save was refused needs
the word. **Log-level names are names, not severities**: "Error", "Warning",
"Info", "Debug" are what the serial log itself prints, so the picker configuring it
matches (`data/setup.html:304-305`, `data/app.js:694-696`). The rule targets a
severity worn as a label on a message, as at `data/firmware.html:19`.

**A value shows whether it was ever set, and the source lives in the explanation.**
One visual rule, not a colour — #327 closed that door. Where a value has a richer
provenance the detail goes in the explanation slot the entrance rule already
guarantees, rather than a vocabulary of marks per source. ADR 0058's Sequence
Tempo, which stores a source and a confidence, is the first consumer.

**`CONTEXT.md`'s `_Avoid_` lines are the vocabulary checker's input, and testable
entries are written in backticks.** The glossary becomes executable: adding a term
to `_Avoid_` is how a rule is added, and there is one home rather than a second
list that drifts. The entries are a mix today — `main controller` and `brain` are
greppable, *"BPM as a measured property of a track"* is a concept no grep can test —
so the backticks mark which is which.

## Considered options

- **A required field alone.** Rejected: the action registry demonstrates that
  presence is achievable and insufficient — 194 of 194 populated, and every one a
  gloss. A check can assert a field is non-empty; it cannot assert a sentence names
  a consequence.
- **A mapping table alone.** Rejected: fully checkable and paid, but it says
  nothing about whether an explanation exists at all.
- **Close as a documentation edit**, which #334 offered if this specific was
  declined. Rejected: it would leave the review gate as the only mechanism, which
  is the thing #298's inventory disproved.
- **Severity is never a word, as the reference does across 416 strings.** Rejected:
  it overturns ADR 0044's deliberate reservation, and there are 22 sites and 25
  tokens today of which the largest cluster is log-level names that must match what
  the log prints.
- **The entrance rule binds every control on every page.** Rejected as unpaid: no
  failure of ours is behind the blanket version, and #287's third pass cut five
  rules on exactly that test.
- **The checker keeps its own rule list.** Rejected: precise and free of false
  positives, and a second list that drifts from `CONTEXT.md` is the failure mode
  this ADR exists to close.
- **A value renders its source on the control.** Rejected: most informative, and it
  needs a vocabulary of marks per source on every surface.

## Consequences

- `tools/check_action_registry_drift.py` gains a **required-field assertion**, which
  it has never had — today it checks parity only.
- `docs/action-registry.yaml`'s `params` sub-schema gains an explanation field, and
  62 parameters need one written.
- **A one-time pass over roughly a hundred `CONTEXT.md` terms** backticks the
  testable `_Avoid_` entries, and every future term follows the convention. A
  glossary edit becomes a build-affecting change.
- A vocabulary checker is built, reading `CONTEXT.md` and following
  `check_action_registry_drift.py`'s report-never-rewrite convention. #298's spec
  stands: bare "controller", pin numbers in page markup, silkscreen labels outside
  `include/component_labels.inc`.
- **22 sites carrying a severity noun** are revisited; the log-level clusters at
  `data/setup.html:304-305` and `data/app.js:694-696` stay.
- Controls that move something get visible explanation text, which is a layout
  change on the pages that today rely on an unadvertised `title`.
- `docs/ui-copy-voice.md` gains three rules with receipts — a heading carries a
  count, a state or a purpose; two easily-confused nouns are defined together; a
  warning opens with the fact and a physical consequence — and five patterns as
  **guidance rather than rules**, per #287's precedent that an unpaid rule is cut.
- **A non-goal, recorded so a later reader does not import it:** the reference's
  honesty vocabulary — "not simulated", "stands in", "simulator placeholders, NOT
  measured on your servos" — marks *model versus reality*, a gap protoArtoo does not
  have. Our distinctions are different ones: configured-but-never-actuated,
  declared-but-not-detected, endpoint-typed versus endpoint-measured,
  saved-but-not-yet-applied.
- **The coverage floor is not a tooltip count.** #334's figures measured the wrong
  thing — "8 of 11 pages carry five or fewer" is 7, and "~101 distinct tooltips" is
  about 85. The floor is that a control which moves something has visible
  explanation at its entrance; a page failing that is a defect.
