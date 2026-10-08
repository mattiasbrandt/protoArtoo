# The project is protoR2

Status: accepted (2026-10-06, issue #477; default AP password 2026-10-07).

The project is called **protoR2**: `protoR2` in prose and titles, `protor2` where
lower case is required (mDNS, file names, lock paths, `PROTOR2_` environment
variables). It was called protoArtoo until v1.3.x. "Artoo" tied the project to the
artoo.uk PCB, which is now one supported **Body Controller** among others; protoR2
sounds the same spoken and matches the protocol name the project already used,
**protoR2link**.

## What stays

- **History stays history.** Released `CHANGELOG.md` sections and the ADRs written
  before this one keep their wording.
- **The Artoo board keeps its name.** "Artoo PCB", the `artoo_esp32` environments,
  `PA_BOARD_ARTOO_ESP32` and the board's `artoo.local` mDNS default name the
  artoo.uk board, not the project.
- **The `PA_` prefix stays.** It is internal and means nothing to a builder.
- **Stored settings survive.** `NVS_NAMESPACE` stays `"proto"`, and a backup
  carries no project name, so a droid already set up keeps its name and WiFi and an
  old backup restores.
- **Names owned outside this repository follow later.** The GitHub repository and
  its URLs, the MemPalace wing, and the AstroPixelsPlus fork's references are
  renamed after the rename lands, not by it.

## Device defaults

What a fresh or reset controller starts with (`include/config.h`):

| Default | Before | Now |
|---|---|---|
| Setup network (SSID) | `protoArtoo` | `protoR2` |
| Default AP password | `protoArtoo1` | `protoArtoo123` |
| Droid name | `protoartoo` | `protor2` |

A builder whose droid is reset finds a new network name, so the release notes say
so. The droid name is also the mDNS host when the droid is told to use it; the
per-board mDNS default (`artoo`, `firebeetle2`) is unchanged.

## Considered options

- **Rename in stages, a few files at a time.** Rejected: the name is in nearly every
  file, and a half-renamed tree reads as two projects. One slice, landing with the
  epic closure, so the closure release is the first under protoR2.
- **Default AP password `protoR21`.** Rejected by the operator (2026-10-07) for
  `protoArtoo123`.
