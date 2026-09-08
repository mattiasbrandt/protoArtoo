# Domain Docs

How the engineering skills should consume this repo's domain documentation when exploring the codebase.

## Before exploring, read these

- **`CONTEXT.md`** at the repo root — domain language and key concepts for protoArtoo firmware.
- **`docs/adr/`** — read ADRs that touch the area you're about to work in.

If any of these files don't exist, proceed silently. Don't flag their absence.

## File structure

Single-context repo:

```
/
├── CONTEXT.md
├── docs/adr/
│   ├── 0001-split-rc-mapping-header-by-concern.md
│   └── 0002-config-persistence-seam.md
└── src/
```

## Use the glossary's vocabulary

When your output names a domain concept (in an issue title, a refactor proposal, a hypothesis, a test name), use the term as defined in `CONTEXT.md`. Don't drift to synonyms the glossary explicitly avoids.

If the concept you need isn't in the glossary yet, that's a signal — and which signal depends on what you are doing.

- **Describing something that exists**, and the glossary has no word for it: you are probably drifting to a synonym. Reconsider, and use the term the glossary already defines.
- **Planning something that does not exist yet**: needing a new word is the normal case, not a warning. Minting one is the work — `Gesture`, `Output Release`, `Rehearsal`, `Board Lane` and `Component Family` all began exactly here. Propose the term, and record it in `CONTEXT.md` when the decision that needs it lands. See AGENTS.md "Planning Mode" and `/grill-with-research`.

## Flag ADR conflicts

If your output contradicts an existing ADR, surface it explicitly rather than silently overriding:

> _Contradicts ADR-0002 (config-persistence-seam) — but worth reopening because..._
