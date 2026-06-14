# Sequence credits

protoArtoo's Factory Sequences are mostly original or part of the dome's AstroPixelsPlus
heritage. When a choreography is **migrated** from another community project (see
[`sequence-import.md`](sequence-import.md)), its provenance is recorded here and in a comment
block on the C++ catalog entry. This file is the single human-readable attribution list.

No runtime metadata travels with a sequence -- a Migrated Sequence is an ordinary Factory
Sequence (see [ADR 0007](adr/0007-community-sequence-migration.md)). Attribution lives in code
comments plus the table below.

## Migrated sequences

_None yet._ Community choreographies are migrated on request -- open a
[Sequence request](https://github.com/mattiasbrandt/protoArtoo/issues/new?template=sequence-request.md),
or, if you built one in the editor, use **Share to project**.

| Sequence | Source project | Origin (URL / commit) | License | Migrated in |
|---|---|---|---|---|
| -- | -- | -- | -- | -- |

## Sources and licenses

Known community sources and their licensing posture, for reference when migrating:

| Source | License | Notes |
|---|---|---|
| Padawan360 (dankraus) | BSD-3-Clause | attribution + notice |
| ReelTwo library | LGPL | library code, not packaged choreographies; we don't link or copy it |
| Marcduino V2/V3 firmware | CuriousMarc / N. Hutchison (not an open license) | the `:SE` repertoire; protoArtoo aliases, never copies code |
| AstroPixelsPlus (reeltwo) + forks | see repo `LICENSE` | the dome firmware base; verify before migrating from a fork |

Posture (attribution re-expression): re-author the behavior in protoArtoo's own catalog; copy
no source code or assets; prefer permissively-licensed sources; skip anything that forbids
redistribution. When in doubt, attribute generously and re-express rather than copy.
