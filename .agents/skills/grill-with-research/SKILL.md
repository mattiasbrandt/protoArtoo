---
name: grill-with-research
description: protoArtoo's planning grill for a wayfinder decision ticket. Composes the maintained grilling and domain-modeling skills, and overrules them where this project's intent lives outside the code. Use for every wayfinder:grilling ticket, and for any design decision about something not built yet.
---

# Grill with research

Call the Skill tool twice, for "grilling" and "domain-modeling".

Everything below **overrules** those two wherever they conflict. They are
general-purpose. This is protoArtoo, where the thing being planned usually does
not exist yet.

## The code prices a decision. It never bounds one.

Read the implementation to learn what a choice costs, what it breaks, and where
it would land. Never read it to decide what protoArtoo *should* do.

"It does not work that way today" is a price tag, never an argument. A question
about what the product should do is not a question of fact, and no amount of
reading answers it: the codebase only reports what somebody already built.

If you catch yourself opening a source file to settle a *should* question, stop.
You are about to convert the operator's intention into an inventory of the
present.

## Read the research before the code

`tasks/research-r2d2-*` is the operator's own curated statement of what
protoArtoo should become: dated operator decisions, a source-verified findings
pass, a ranked tier list, and 23 reference screenshots. Several load-bearing
ideas exist **only** in the screenshots.

Read it in full before you open a source file - not the sections that look
relevant to the module you were about to touch.

### Order of authority - what protoArtoo *should* become

1. A dated operator decision: an issue comment, an ADR, `AGENTS.md`.
2. `tasks/research-r2d2-*`, the design source.
3. `CONTEXT.md` and `docs/adr/`, the model as it stands. These record decisions
   already taken; they do not fence off decisions not yet taken.
4. The implementation.

**Reverse the order for what protoArtoo does today**: the code wins, and the
research is a snapshot of somebody else's project taken in August 2026.

Where the research and a later operator decision disagree, the operator decision
wins. Never quote the research's own scope notes back at the operator to refuse
work they have since asked for.

## Naming what does not exist is the work

`AGENTS.md` "Effort Policy" forbids guessing **facts**: never invent a pin
number, a wire format, or a field name in shipped code. That rule does not reach
proposals.

In a planning ticket, naming a component, a term, or a capability protoArtoo does
not have yet **is the deliverable**. Mark it as a proposal. Never mark it
`UNKNOWN`, and never suppress it because you could not cite a file for it.

## Capability first

Open every question with what a builder cannot do today and should be able to.
Close it with the current implementation, as a note on cost.

- A ticket whose first section inventories existing code is written wrong.
- A ticket whose specifics are all "A or B" about a surface that already exists
  is a polish ticket wearing a planning ticket's clothes.
- Naming, module boundaries, vocabulary and model shape are *consequences* of a
  capability decision, never substitutes for one.

## Speak the builder's language

Printed Droid's published terminology - **PP1..PP6** pies, **P1..P14** lower dome
panels, **HPn-1/-2** holo axes - is the de-facto community standard and what an
arriving builder will type. `docs/droid-parts.yaml` already carries it.

Treating it as decoration is expensive: the reference project's own real
18-channel dome file mapped 0 of 18 channels before it adopted these names.

## Author before you build

A builder choreographs for the droid they are *making*, not the one that happens
to be wired tonight. Parts nothing drives still appear, still animate in preview,
still sit in the editor dimmed, and compile to nothing until an output claims
them.

So "only 2 of 42 catalog entries are driven today" is **not** an honesty problem
for a surface to disclose. It is the normal state of a build in progress, and the
property that makes *choreograph first, wire later* possible. Every surface must
read correctly in it.

## Scope

`AGENTS.md` "depth within scope, never width past it" bounds a **build** ticket
against its own acceptance criteria. It does not bound a wayfinder map: a map's
scope is its stated **Destination**, and widening that Destination is the
operator's call, made explicitly on the map.

Do not cite the Effort Policy to refuse a widening the operator has asked for.

## Asking

One question per turn, through `AskUserQuestion` - never a numbered `Q1/Q2/Q3`
block. This overrides the `grilling` skill's whole-frontier round behaviour.
Recompute the frontier after each answer; the next question is whatever that
answer just unblocked.

Finding **facts** is your job, never the operator's: dispatch a subagent rather
than asking for something you could look up. The **decisions** are theirs.

## Documentation

`domain-modeling`'s rules stand: update `CONTEXT.md` inline as terms resolve,
keep it a glossary and nothing else, and offer an ADR only when the decision is
hard to reverse, surprising without context, and the result of a real trade-off.

Decision documentation commits straight to `main` under `docs(plan):` - shared
vocabulary must not sit unmerged on a branch.
