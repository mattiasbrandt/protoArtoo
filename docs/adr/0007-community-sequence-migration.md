# Community sequences migrate via GitHub issue, not operator import

Issue #2 slice 5 originally scoped a **Guild import** tier: community choreographies shipped
as runtime JSON in the filesystem image, with operator-facing import in the web editor and
provenance in a runtime `meta`/Lineage block. This ADR replaces that approach. It supersedes
the Guild-specific parts of [ADR 0006](0006-learned-sequences-runtime-tier.md) (the runtime
import tier); the rest of ADR 0006 -- the Learned Sequence runtime tier for **operator
self-authoring** -- stands unchanged.

## Status

accepted (supersedes ADR 0006 decisions 5 and 7 insofar as they describe a shipped Guild
batch and runtime Lineage)

## Context

A 2026-06-14 survey of the obvious sources -- the `thePunderWoman` AstroPixelsPlus fork (the
very firmware our Factory catalog was ported from), Marcduino `:SE00-58`, Padawan360 (BSD-3),
ReelTwo (LGPL) -- found the novel-choreography pool effectively **empty**: those projects
re-trigger the same repertoire already covered by protoArtoo's 16 Factory sequences and ~26
aliases. More fundamentally, *translating* a sequence from another project (mapping panels,
normalizing the Marcduino dialect, mapping sounds, validating on hardware) is a **developer**
task, not something an operator does at runtime. An operator-facing import feature solves a
problem operators do not have.

## Decisions

1. **Migration is a code + PR activity.** A community choreography becomes a **Migrated
   Sequence** -- an ordinary **Factory Sequence** translated into the C++ catalog
   (`src/tasks/sequence_catalog.cpp`) and merged via PR. It gets full Factory treatment:
   explicit `FX_*` tags, an `action-registry` entry, `check-action-drift`, a native engine
   test, and compile-time inclusion. No runtime parsing, no shipped `data/seq/` batch.

2. **GitHub issues are the entry point.** Requests arrive as a **Sequence request** ("can we
   get project X's sequence?") or a **Sequence contribution** ("here is one I built"). A
   maintainer evaluates novelty, licensing, and mapping feasibility, then migrates per
   [`docs/sequence-import.md`](../sequence-import.md).

3. **The operator surface is contribution, not import.** The web editor keeps operator
   self-authoring (create / edit / export, and import of the operator's *own* backup). It
   gains a **Share to project** action on a saved custom sequence that opens a prefilled
   contribution issue and copies the sequence JSON to the clipboard. It does **not** import
   community sequences; the runtime Guild import UI and the Guild badge are removed.

4. **Provenance lives in code + docs.** A Migrated Sequence carries its source, origin, and
   license in a catalog comment block and in
   [`docs/sequence-credits.md`](../sequence-credits.md). No runtime Lineage meta block is
   shipped. Posture: attribution re-expression -- re-author the behavior, copy no source code
   or assets, prefer permissive sources, skip anything that forbids redistribution.

## Rejected alternatives

- **Operator runtime import of community sequences (rejected).** Translation and hardware
  validation cannot be operator-driven; the feature serves a use case that does not exist.
- **Shipping a curated Guild JSON batch in the FS image (rejected).** Reintroduces the
  FS-OTA data-loss problem (a filesystem update erases user-authored sequences) and a second
  sequence tier to maintain, for content that belongs in the compiled catalog anyway.

## Consequences

- The Learned Sequence runtime tier (ADR 0006) remains, scoped to **operator self-authoring**.
  The firmware `meta` block and JSON format are unchanged (harmless; the provenance fields
  simply go unused at runtime).
- `data/seq/` ships no files; `docs/sequence-import.md` is the developer migration playbook,
  not an operator guide.
- The editor drops the Guild badge and reframes import as self-restore; it gains Share to
  project. New `.github/ISSUE_TEMPLATE` entries: `sequence-request`, `sequence-contribution`.
- CONTEXT.md retires "Guild Sequence" (runtime tier) in favor of **Migrated Sequence**
  (a Factory Sequence) and redefines **Lineage** as code-comment + credits-file provenance.
- The slice-3/4 hardware gate is unchanged; a Migrated Sequence is verified like any Factory
  Sequence (native engine test + the integrated-droid gate for fidelity).
