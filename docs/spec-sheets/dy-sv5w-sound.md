# DY-SV5W Spec Sheet (DY UART, registry token `soft_uart_binary`)

Working spec for the **DY-SV5W** voice playback module, the **Sound** lineup
member that ships today ([#388](https://github.com/mattiasbrandt/protoArtoo/issues/388),
minted from [#303](https://github.com/mattiasbrandt/protoArtoo/issues/303) and
[#316](https://github.com/mattiasbrandt/protoArtoo/issues/316)), reached over the
Component Protocol the registry calls `soft_uart_binary` -- a token Section 17.3
argues is the one thing on that row that has drifted.

Research date 2026-09-12. Every frame byte, command code, electrical value and
default below was read this session from the module's own datasheet (a **scanned
4-page PDF with no text layer**, rendered to images and read page by page from
`~/Downloads/DY-SV5W ModuleDatasheet.pdf`), from three independent driver
implementations read in full, from this repository's own driver, tests, lessons
file and commit history, or from the astromech projects on this disk. **Every
checksum in this document was computed, not copied.** Claims that could not be
sourced are marked `UNKNOWN` with the artefact or bench test that would settle
them.

> [!IMPORTANT]
> **Branch note.** The research was done against `epic/operator-experience`, which
> is where [#388](https://github.com/mattiasbrandt/protoArtoo/issues/388) lives.
> Everything about the module, the protocol, the wire bytes, the driver, the tests
> and the hardware record is equally true on `main`. What is **not** on `main` yet
> is the **Component Registry** machinery this sheet cites in Sections 3, 13.1 and
> 17.3 -- `include/component_registry.inc` row 18, `src/tasks/audio_sound_member.cpp`,
> runtime member selection and the `soft_uart_binary` protocol token. On `main` the
> Sound backend is still chosen by the `PA_AUDIO_DRIVER` build flag, and
> `AUDIO_SOFT_UART` is this module. Those sections read forward to the epic.

> [!NOTE]
> **This is the sheet for a part that already works, and that changes what it is
> for.** The other Sound sheet -- [`dfplayer-mini-sound.md`](dfplayer-mini-sound.md)
> -- is an adoption study for a part nobody has wired. This one documents the
> reference implementation the whole `AudioDriver` seam was shaped around: 387
> lines in `src/drivers/audio_dy_sv5w.cpp`, 23 native tests, and a hardware
> validation record going back to 2026-03-22. So its centre of gravity is
> **Sections 11-14 and 17**: what our driver actually sends, what our droid has
> actually proven, and the six places where the shipping code, its comments and
> its docs do not agree with each other or with the module.

> [!CAUTION]
> **The vendor documentation contradicts itself about the `BUSY` pin four times
> in four pages, and gives UART mode and One-Line mode the same DIP setting.**
> Both are verified below against the document's own pages (Sections 5.5 and
> 6.3). Neither costs us anything today -- we wire neither `BUSY` nor One-Line --
> and both are exactly the trap a builder falls into first.

> [!IMPORTANT]
> **The good news is unusually good, and it is the opposite of the DFPlayer's.**
> All **17** worked byte sequences printed in the DY-SV5W datasheet's command
> tables verify against the document's own stated checksum rule -- computed this
> session, zero mismatches (Section 7.2). The DFPlayer's datasheet got **three
> out of three wrong**. Where the DFPlayer sheet's rule is *"never trust a
> printed byte sequence"*, this module's printed bytes are trustworthy and its
> **prose** is not.

> [!WARNING]
> **Our own driver's file header carries one wrong checksum**, and it is the
> exact class of defect this project has been burned by before:
> `src/drivers/audio_dy_sv5w.cpp:11` documents `vol 15 = AA 13 01 0F C7`, and the
> correct byte is **`CD`**. The code is right -- it computes the sum -- and
> `test_audio_io_seam.cpp:170` asserts `0xCD`. Only the comment a developer would
> copy is wrong. Fixed in the change that carries this sheet (Section 17.1).

## Where this sits in the lineup

The **Sound** category holds four products, and a builder picks one:

| Product | What it is | Status | Registry value |
| --- | --- | --- | --- |
| **DY-SV5W** | binary-frame voice module with a 5 W amplifier | **`supported`** | **18** |
| MP3 Trigger | SparkFun/Robertsonics VS1053 board | `supported` | 19 |
| CHIRP Audio Trigger | RP2350 multi-stream mixer, astromech-specific | `supported` | 20 |
| DFPlayer Mini | hardware-decoded single-stream player with an amplifier | `roadmap` | 21 |

These are peers with different shapes, not a ladder -- Section 16 sets out the
differences in kind. The DY-SV5W is the **default** one: it is what
`docs/goal.md:42` calls the *"primary"* module, what `PA_AUDIO_DRIVER` names for a
controller that has never been told otherwise, and the row
`componentResolveMember()` falls back to when a stored member is one this image
cannot drive (`test_component_registry.cpp:104-114`).

## 0. Authority Contract

This document is an implementation authority for the DY-SV5W UART protocol and
for the module's behaviour as protoArtoo uses it today.

Authority order for agent decisions:

1. **Measured behaviour of our own droid** -- `tasks/lessons.md`, the commit
   bodies quoted in Section 14, and the CHANGELOG's `Hardware Validated` block.
   This is the only member of the Sound family where we have this, and it
   outranks every document below.
2. **Computed truth** -- the checksum rule, applied. Nothing printed anywhere
   beats arithmetic, including this sheet's own tables.
3. **The DY-SV5W datasheet** for the frame shape, the command codes, the pinout,
   the mode table and the defaults. Its printed byte sequences are correct
   (Section 7.2); its prose is not always (Sections 5.5, 6.3, 7.1).
4. **Three independent implementations**, where the datasheet is silent or
   self-contradicting: `SnijderC/dyplayer` (C++), `Lenoirio/dy-sv5w` (Rust) and
   BetterDuino's `MDuinoSoundDYPlayer`. Where two of the three agree against the
   datasheet's prose, prefer them; Section 15.4 names the one place the most
   popular of the three is provably wrong.
5. This document.
6. Other astromech project source (Section 15) -- evidence of practice, **not** of
   correctness. Section 15.2 names a place Padawan360 is wrong.

If references conflict:

- Prefer computation over any printed byte sequence, this sheet's included.
- Prefer the datasheet's **tables** over its **prose**. Every contradiction found
  this session is prose-versus-table or prose-versus-prose; no two tables
  disagree.
- Where the datasheet contradicts *itself*, assert nothing: mark `UNKNOWN` and
  name the bench test. `BUSY` polarity is the live example (Section 5.5).
- Prefer `Lenoirio/dy-sv5w` over `SnijderC/dyplayer` **on this specific module**:
  the Rust crate was written against a DY-SV5W with the datasheet in the repo,
  and dyplayer's own header says it is *"an abstraction of basic features of the
  DY-SV17F"* (Section 15.3).

Agent requirements when using this document:

- MUST compute the checksum. MUST NOT copy `C7` from
  `src/drivers/audio_dy_sv5w.cpp:11` (Section 17.1 fixes it; the warning stands
  for anything else a comment prints).
- MUST use `0x07` to play a numbered sound. `0x06` is **next track** and cost this
  project a regression already (Section 14.2).
- MUST use `0x04` to stop. `0x03` is pause and `0x02` is resume; sending both is a
  no-op that shipped once (Section 14.2).
- MUST NOT hardcode a storage device in `switchDrive` (`0x0B`). Use what `0x09`
  reports, or send nothing (Section 14.1).
- MUST NOT assume a query response is the next bytes on the wire. Validate
  `0xAA`, the command byte **and** the length byte before believing a reply
  (Section 9.2).
- MUST NOT poll this module during playback. It does not declare
  `AUDIO_CAP_QUERY_SAFE_PLAYING` and the Sound page says so in words
  (Section 13.2).
- MUST NOT assert a `BUSY` polarity until it is measured (Section 5.5).
- MUST leave the module's own volume scale alone: 0-30 ascending, which is
  `AudioDriver`'s normalised range unchanged. No scaling, in either direction
  (Section 8.4).

## 1. Scope

Covers the board and what is on it, the electrical contract and pinout, the DIP
mode table, the serial frame and its checksum, the full command and query set
with every checksum computed, what the module returns and what it volunteers, the
storage and file-numbering contract, exactly what protoArtoo's driver sends and
does not send, the per-board transport, the capability bits and the operator
surfaces they drive, what our own droid has proven and what it cost to learn,
how the astromech hobby drives this module, how the four Sound members differ,
and the five findings this research turned up against the shipping code.

Does not cover: audio file encoding and bitrate selection; the I/O trigger modes
in operational detail beyond the mode table (a droid drives this over UART); the
One-Line single-bus protocol beyond recording its command table and bit encoding
(we do not use it and its DIP row is ambiguous); the `0x08`/`0x17` play-by-path
commands beyond their frame shape (our driver plays by index); or the DY family
siblings except as lineup context (Section 16.3).

## 2. What you are actually buying

A 40 x 40 x 9 mm blue PCB, silkscreened **`SV5W`**, carrying:

| On the board | What it is | Consequence |
| --- | --- | --- |
| A **12-way 2.54 mm header** down one edge | `5V+`, `5V-`, `TXD/IO0`, `RXD/IO1`, `IO2`, `IO3`, `IO4/ONE_LINE`, `IO5`, `IO6`, `IO7`, `BUSY`, `GND` | Section 5.1 |
| A **3-position red DIP switch**, silked `ON` / `1 2 3` | CON1, CON2, CON3 -- the mode select | Section 6, and **no external resistors needed** (Section 6.4) |
| A **micro-USB socket**, silked `DownLoad` | Datasheet annotates it *"USB DownLoad MP3 File"* | Section 5.6 -- and it will stop UART mode |
| A **TF (microSD) slot**, annotated *"TF Card 32G Bit"* | The removable storage | Section 10.3 |
| A **blue trimmer potentiometer**, annotated *"Volume Adjustment"* | Analogue master volume, in series with the digital one | Section 5.3 |
| An **8-pin SOIC** by the speaker pads, annotated *"5W Amplifier IC"* | The Class-D output stage | Section 5.4, where the "5 W" is checked with arithmetic |
| A **2-pad `Speaker` terminal** | *"4ohm 3~5W Speaker"* | Bridge-tied. Section 5.3 |
| A **3.5 mm jack** | *"3.5mm Audio Output"* | Line level for an external amplifier |

> [!NOTE]
> **Unlike the DFPlayer Mini, the identity of this part is not in doubt.** The
> DFPlayer sheet's dominant risk is Section 2 of that document: *"a 16-pin form
> factor, not a part"*, filled by at least eight unrelated silicon families that
> behave differently. Nothing comparable was found for the DY-SV5W this session.
> It is sold under many reseller brands (JESSINIE, Pzhoais, Diann and others on
> Amazon; ICStation; TinyTronics; GroboTronics; Banggood; a wall of Alibaba
> listings) but they are **the same board with the same silkscreen and the same
> DIP switch**, and no clone-behaviour database of the `DFPlayerAnalyzer` kind
> exists because nobody has needed one.
>
> What it lacks instead is **a vendor**. There is no DFRobot-equivalent product
> page, no SKU, no errata, and no official support channel: it is a generic
> module from a Shenzhen house, and the closest thing to authoritative
> documentation is the four-page PDF described below.

### 2.1 The datasheet is a Word document somebody typed up

Our local copy's own PDF metadata, read this session with `pdfinfo`:

```
Creator:         Microsoft Word for Office 365
Producer:        Microsoft Word for Office 365
CreationDate:    Thu Jun  6 10:41:03 2019 CEST
Pages:           4
Author:          <a private individual, not a company>
```

**It is not a vendor document.** It is a third party's re-typeset of the Chinese
original, from 2019, with the board photographs pasted in -- and it is the
version the whole English-speaking ecosystem uses. GroboTronics, `Lenoirio/dy-sv5w`
(which commits it into the repository) and a dozen reseller pages all serve
byte-identical or near-identical copies under the file name
`DY-SV5W Voice Playback ModuleDatasheet.pdf`.

Two consequences, and they pull in opposite directions:

- **It has no text layer** -- `pdftotext` returns 4 bytes -- so every table in
  this sheet was transcribed by reading rendered images. Same handling the
  DFPlayer's scanned DFR0299 needed.
- **Its command tables are arithmetically perfect** (Section 7.2). Whoever
  retyped it did not introduce a single checksum error across 17 worked
  examples, which is a better record than DFRobot's own typesetting.

> [!TIP]
> A transcription of the tables, made before this sheet existed, is in
> `tasks/DY-SV5W-Module-Datasheet.md` (local, untracked). It was checked against
> the rendered pages this session and is faithful, with one omission worth
> knowing: it drops the *"Busy pin will output valid signal (High) during
> playing"* sentence from the I/O Integrated Mode 0 block, which is one of the
> four horns of the `BUSY` contradiction (Section 5.5).

### 2.2 Availability

Checked 2026-09-12: **widely and continuously available**, in stock at Amazon,
eBay, Walmart, ICStation, TinyTronics, GroboTronics and Alibaba, from many
independent sellers, typically in the USD 3-8 band single-unit depending on
channel. No shortage signal, no end-of-life notice, no single vendor whose stock
state matters.

**This is the exact inverse of the Maestro's situation**
([`pololu-maestro-servo-controller.md`](pololu-maestro-servo-controller.md)) and
of the DFPlayer's: there, one vendor's stock field was the fact to get right.
Here there is no vendor to check, and the thing that makes it always purchasable
is the same thing that makes it undocumented.

> [!NOTE]
> **Negative result, recorded so nobody repeats it.** Two independent attempts to
> fetch a second copy of the datasheet PDF directly (`grobotronics.com`,
> `shop.cpu.com.tw`) returned HTTP 403 to a scripted client this session. The
> local copy plus the reseller guides and library repositories were used instead.

## 3. Project Integration

- **[`src/drivers/audio_dy_sv5w.cpp`](../../src/drivers/audio_dy_sv5w.cpp)** --
  387 lines. The driver. `sendCommand()`, `sendQuery()` with a bounded 300 ms
  timeout, a `begin()` that runs three pre-init queries and one post-init
  confirmation, `queryModuleState()` and `getCachedState()`. Section 11 is this
  file read line by line.
- **[`include/audio_dy_sv5w.h`](../../include/audio_dy_sv5w.h)** -- 86 lines. The
  class, the transport contract per board, and the `static_assert` that fails the
  build if the product id it cites is not a Component Registry row.
- **[`include/audio_driver.h`](../../include/audio_driver.h)** -- the seam.
  `AUDIO_SOFT_UART = 1` at `:47` (the build flag that still names this module as
  a fresh controller's default), the **0-30 normalised volume** contract at
  `:15`, the `AUDIO_CAP_*` bits at `:93-98`, `AudioModuleState`, and
  `classifyRxStatus()` at `:141`.
- **[`include/audio_serial_io.h`](../../include/audio_serial_io.h)** -- the
  five-function-pointer transport seam (`writeByte`, `rxAvailable`, `rxRead`,
  `delayMs`, `millisNow`) that makes every byte in Section 8 testable on a laptop.
- **[`include/component_registry.inc`](../../include/component_registry.inc)** --
  row **18**, `dy_sv5w`, `COMPONENT_CATEGORY_SOUND`, protocol `soft_uart_binary`,
  `COMPONENT_STATUS_SUPPORTED`, capabilities `STATUS_QUERY | DEVICE_TYPE |
  TRACK_COUNT | CURRENT_TRACK` (`0x0F`), no gate, `included = 1`.
- **[`src/tasks/audio_sound_member.cpp`](../../src/tasks/audio_sound_member.cpp)**
  -- `kSoundMemberDrivers[]` maps `"dy_sv5w"` to the driver instance, and
  `s_active` starts on it before `setup()` binds anything.
- **[`src/component_registry.cpp`](../../src/component_registry.cpp)** `:89-90` --
  `PA_AUDIO_DRIVER == AUDIO_SOFT_UART` resolves to `kDefaultSoundMemberId =
  "dy_sv5w"`, which is what a never-configured controller boots on.
- **[`src/tasks/audio_task.cpp`](../../src/tasks/audio_task.cpp)** -- the only
  writer to `PIN_AUDIO_TX`. `begin()` retry lifecycle, the `audioUartClaim()`
  arbitration around every query, and the `AUDIO_CAP_QUERY_SAFE_PLAYING` gate that
  keeps this module out of the 10 s auto-poll.
- **[`src/drivers/audio_soft_uart_tx.h`](../../src/drivers/audio_soft_uart_tx.h)**
  -- the bit-banged 9600 TX this module's fixed baud is met with on artoo-esp32,
  and its measured *"~1.04 ms per byte"* Core 0 cost.
- **[`include/config.h`](../../include/config.h)** -- `PIN_AUDIO_TX` /
  `PIN_AUDIO_RX` on both targets, `UART_PORT_AUDIO`, and
  `PA_CAP_DEDICATED_AUDIO_UART` (`:75`, `:80`) with the `static_assert` at `:457`
  that keeps the capability and the controller allocation from drifting apart.
- **[`include/dome_link.h`](../../include/dome_link.h)** `:128-144` --
  `audioUartClaim()` / `audioUartRelease()`, and why a denied claim is
  `AUDIO_RX_BLOCKED_BY_DOME_UART` rather than a dead module.
- **[`docs/sound_playback.md`](../sound_playback.md)** -- the audio system
  reference: the `$` command mapping, the random-playback contract, the NVS keys,
  and the SD-card numbering rule Section 10.4 builds on.
- **[`docs/pin_map.md`](../pin_map.md)** -- the S2 header on artoo-esp32 and the
  `34`/`36` rows on FireBeetle 2.
- **[`docs/api.md`](../api.md)** -- `GET /api/audio`, whose worked example is a
  DY-SV5W response.
- **`CONTEXT.md`** -- **Component Protocol**, **Component Member**, **Component
  Registry**, **Audio Step Core**, **Sound Bed**.
- **[ADR 0042](../adr/0042-component-families-are-selected-at-runtime-where-the-board-offers-a-choice.md)**
  -- the Component Member rule that made this module a runtime choice rather than
  a build flag.
- **`test/test_native/test_audio_frames/`** (13 tests) and
  **`test/test_native/test_audio_io_seam/`** (10 DY-SV5W tests) -- Section 12.4.

## 4. Sources Checked

| Source | How it was taken | What it gave |
| --- | --- | --- |
| **DY-SV5W Voice Playback Module Datasheet**, 4 pages -- local copy at `~/Downloads/DY-SV5W ModuleDatasheet.pdf` | `pdftoppm -r 150 -png`, then **read as four images**; it has no text layer | The pinout, the mode table, the frame format, all three command tables, the One-Line table and bit encoding, the defaults, the dimensions. And the four-way `BUSY` contradiction and the duplicated UART/One-Line DIP row |
| The same datasheet's **PDF metadata** | `pdfinfo` | Its provenance: a 2019 Microsoft Word document by a private individual, not a vendor publication (Section 2.1) |
| **`SnijderC/dyplayer`** `src/DYPlayer.h` + `src/DYPlayer.cpp` | fetched raw and read in full | Every command code with its precomputed checksum, the device enum with *"Onboard flash chip (usually winbond 32, 64Mbit flash)"*, `0-30` volume with *"Default volume if not set: 20"*, the by-path encoding rule, `combinationPlay`. And one wrong opcode (Section 15.4) |
| `SnijderC/dyplayer` README and issue #1 | fetched | The DY-SV5W listed as *tested*; *"The module will look for the first sound file found in the filesystem. It's not using the file name, neither does it order by file name"*; the `XY`/`ZH`/`DY` combination folder; the `1KOhm` series-resistor advice; and a reporter's *"UART mode (0-0-1)"*, which is Section 6.2's trap seen from the other side |
| **`Lenoirio/dy-sv5w`** (Rust) `src/lib.rs` + README | fetched and read in full | An independent second implementation that validates responses exactly the way ours does; *"the level for I/O-pins (including the UART) is 3.3 V"*; *"The device can't operate as a USB storage-device"*; and the USB-kills-UART warning in Section 5.6 |
| **`~/Documents/Astromech/BetterDuinoFirmwareV4`** `src/MDuinoSound.cpp:373-507`, `include/MDuinoSoundDYPlayer.h` | read on disk | The reference our driver was aligned to: identical `sendCommand`, the `delay(100)` with its own comment *"Delay needed between successive commands"*, the 9x25 bank arithmetic, and the volume ladder (max 30, mid 15, min 5, off 0) |
| **`~/Documents/GitHub/Padawan360_mega_maestro_DYSV5W`** | read on disk, both sketches | A line-for-line MP3 Trigger to DY-SV5W port with the track numbers unchanged -- and a volume control that runs backwards (Section 15.2) |
| `~/Documents/GitHub/ShadowMD`, `~/Documents/GitHub/AstroPixelsPlus`, `~/Documents/GitHub/CHIRP` | read on disk | Negative results: none of them drive a DY-SV5W. Section 15.5 |
| `~/Documents/GitHub/r2d2-astromech-simulator` (via `tasks/research-r2d2-sim-*`) | read on disk | The simulator's own DY-SV5W option note, and the contradiction it prints (Section 15.6) |
| **protoArtoo itself** -- driver, header, tests, `audio_task.cpp`, `dome_link.cpp`, `component_registry.inc`, `config.h`, `docs/`, `CHANGELOG.md` | read this session | Section 11, 12, 13, 17 |
| **protoArtoo commit history and `tasks/lessons.md`** | `git log --follow`, read in full | Section 14: the four-frame play regression, the hardcoded-`switchDrive` incident, the end-marker dialect, the spontaneous-byte defence |
| Envistia Mall DY-SV5W guide; `playfultechnology/arduino-audio`; reseller listings | fetched | Corroboration of the mode table, the DIP-versus-resistor difference against the DY-SV17F, temperature range, availability |

## 5. Electrical

### 5.1 The pinout, from the datasheet's own table

All twelve pins are on one 2.54 mm header. **Function depends on the mode**
(Section 6); the UART-mode column is what protoArtoo uses.

| No | Pin name | Instruction (verbatim) | In UART mode |
| --- | --- | --- | --- |
| 1 | `5V+` | *"Work Voltage Positive Pole"* | +5 V supply |
| 2 | `5V-` | *"Work Voltage Negative Pole"* | supply ground |
| 3 | `TXD/IO0` | *"IO trigger mode is input IO0; UART mode is TX."* | module TX -> host RX |
| 4 | `RXD/IO1` | *"IO trigger mode is input IO1; UART mode is RX."* | module RX <- host TX |
| 5 | `IO2` | *"IO trigger mode input IO2."* | unused |
| 6 | `IO3` | *"IO trigger mode input IO3."* | unused |
| 7 | `IO4/ONE_LINE` | *"IO mode input IO4; One_Line mode data receiver pin."* | unused |
| 8 | `IO5` | *"IO trigger mode input IO5."* | unused |
| 9 | `IO6` | *"IO trigger mode input IO6."* | unused |
| 10 | `IO7` | *"IO trigger mode input IO7."* | unused |
| 11 | `BUSY` | *"Output low level signal(0V) when playing and output high(3.3V) after playing."* | **see Section 5.5** |
| 12 | `GND` | *"Ground."* | ground |

Separately, not on that header: the two `Speaker` pads, the 3.5 mm jack, the
micro-USB socket, the TF slot, the volume trimmer and the DIP switch.

### 5.2 Supply and logic level, and why an ESP32 needs no level shifter

| Parameter | Value | Source |
| --- | --- | --- |
| Supply | **DC 5 V** | Datasheet pin table (`5V+` / `5V-`); every reseller spec |
| Logic level on every I/O pin, **UART included** | **3.3 V** | Datasheet `BUSY` row (*"output high(3.3V)"*); `Lenoirio/dy-sv5w` README: *"Although the module needs +5V, the level for I/O-pins (including the UART) is 3.3 V"* |
| Operating temperature | **-20 to +85 C** | Reseller specification sheets, consistently |
| Dimensions | **40 x 40 x 9 mm** | Datasheet page 4, dimensioned photograph |
| Idle current | `UNKNOWN` | Not in any document found. Open Item 6 |
| Playing current | `UNKNOWN` measured; **~0.7-1.0 A at 5 V** is the arithmetic (Section 5.4) | Open Item 6 |

> [!IMPORTANT]
> **The 5 V supply and the 3.3 V signalling are the combination an ESP32 wants.**
> Feed the module 5 V for amplifier headroom and wire `TXD`/`RXD` straight to the
> ESP32 with **no level shifting in either direction**: the module's TX idles and
> drives at 3.3 V, which the ESP32 reads natively, and the ESP32's 3.3 V TX is a
> valid high at the module's 3.3 V input.
>
> This is the opposite of the DFPlayer's trade-off
> ([`dfplayer-mini-sound.md`](dfplayer-mini-sound.md) Section 5.1), where running
> the module at 3.3 V keeps the levels compatible **at the cost of amplifier
> power**, and running it at 5 V for volume forces a divider on its TX. Here you
> get both. It is the single best electrical property of this module and it is
> why it is the default Sound member.

> [!NOTE]
> **The `1KOhm` series resistors you will read about are for a 5 V host, or for a
> different board.** `SnijderC/dyplayer`'s README says the module's pins want
> *"3.3V or at least limited by a `1KOhm` resistor according to the module
> manual, or you could damage the module"* -- advice written for an Arduino Uno
> driving a DY-SV17F. The same README's `10KOhm` pull-up/pull-down advice for the
> CON pins is likewise a DY-SV17F requirement: **the DY-SV5W has a DIP switch
> instead**, which is exactly what `playfultechnology/arduino-audio` singles out
> as its advantage -- *"has SD card slot and DIP switches for mode-select so
> requires no additional components"*. On an ESP32 at 3.3 V, neither resistor is
> needed. `dyplayer` issue #18 reports that adding series resistors to a board
> that already has them causes distortion, so adding them "to be safe" is not
> free.

### 5.3 Three ways the volume is set, and two of them are not ours

| Control | Where | Range | Who sets it |
| --- | --- | --- | --- |
| Digital volume | `0x13` over UART | 0-30, **default 20** | protoArtoo, from NVS, at `begin()` and on every change |
| Analogue trimmer | Blue potentiometer on the board | mechanical | The builder, once, with a screwdriver |
| Amplifier vs line out | Which output you wire | -- | The builder, at build time |

The trimmer is in the analogue path and the UART volume is in the digital path,
so **they multiply**. A droid that is too quiet at `setVolume(30)` has its trimmer
down; a droid that distorts at `setVolume(10)` has its trimmer up. Neither is
visible to firmware, and neither is reported anywhere -- which is worth saying out
loud on a Sound page that shows a 0-30 slider as if it were the whole story.

**The two audio outputs are not interchangeable:**

- The **`Speaker` pads** are the Class-D amplifier's **bridge-tied** output for
  a *"4ohm 3~5W Speaker"*. Neither leg is ground-referenced. **Never wire either
  pad to an amplifier input or to ground** -- same hazard as the DFPlayer's
  `SPK1`/`SPK2`.
- The **3.5 mm jack** is the line-level DAC output for driving an external
  amplifier, and is the correct connection for a droid that already has one.

### 5.4 The "5 W" is a supply-rail impossibility, and the honest number is about 3 W

The datasheet annotates the output stage *"5W Amplifier IC"* and the speaker pads
*"4ohm 3~5W Speaker"*. From the 5 V supply, into 4 ohm, bridge-tied, that is
arithmetic rather than opinion:

```
Rail-to-rail BTL differential swing  = 10 Vpp  = 5 V peak
Vrms at full scale                   = 5 / sqrt(2)        = 3.54 V
P into 4 ohm                         = 3.54^2 / 4         = 3.13 W
```

**3.13 W is the ceiling for an ideal amplifier with zero dropout, at clipping.**
A real Class-D part loses several hundred millivolts of headroom, so continuous
undistorted power into 4 ohm from a 5 V rail is **roughly 2.5-3 W**, and "5 W" is
reachable only at a high-THD rating or into a lower impedance than the board
specifies. Treat the marking the way the DFPlayer sheet treats printed checksums:
recompute it.

Two practical consequences:

- **Supply current.** ~3 W acoustic at typical Class-D efficiency (~85 %) is
  ~3.5 W drawn, i.e. **~0.7 A at 5 V** at full output, with peaks higher on
  transients. A droid feeding this module from a shared 5 V regulator should
  budget **1 A** for it and treat a brown-out on a loud scream as a power design
  problem, not a module fault. Derived, not measured -- Open Item 6.
- **A droid that needs to be heard across a hall needs the 3.5 mm jack and a real
  amplifier.** The on-board amplifier is a complete solution for a bench and a
  quiet room, and that is the same conclusion the DFPlayer sheet reaches about
  its own 3 W stage.

### 5.5 `BUSY`: the datasheet states both polarities, four times, in four pages

> [!CAUTION]
> **This module's documentation contradicts itself on `BUSY` more comprehensively
> than DFRobot's does.**
>
> Page 1, pin table, pin 11 `BUSY`:
> > *"Output low level signal(0V) when playing and output high(3.3V) after
> > playing."*
>
> Page 1, I/O Integrated Mode 0 notes, same pin:
> > *"Busy pin will output valid signal(High) during playing."*
>
> Page 2, I/O Integrated Mode 1 notes:
> > *"Busy pin will output valid signal(High) during playing."*
>
> Page 2, I/O Independent Mode 0 notes:
> > *"Busy pin will output valid signal(High) during playing."*
>
> Page 2, I/O Independent Mode 1 notes:
> > *"Busy pin will output valid signal(High) during playing."*
>
> The pin table says LOW while playing. Four separate mode blocks say HIGH while
> playing. **One table against four prose blocks, in a four-page document.**

**Unresolved, and deliberately so.** The DFPlayer sheet could settle the same
question without a bench run because a second vendor's datasheet for the same
silicon agreed with DFRobot's pin table. **No second vendor document exists for
this module** -- there is no second vendor (Section 2). The reseller guides that
answer the question (Envistia: *"0 V (LOW) during playback; 3.3 V (HIGH) when
playback has ended"*) are paraphrasing the same pin table, so they are not
independent evidence.

Two readings, both defensible:

1. The pin table is right and the four prose blocks inherited a sentence from a
   sibling module's manual, where `BUSY` is active-high.
2. The prose is right for I/O modes specifically -- the wording *"valid signal"*
   may mean "asserted", not "high" -- and the pin table describes the electrical
   level in UART mode.

**Costs us nothing today.** protoArtoo wires no `BUSY` pin on either board, and
play state comes from `0x01` over the UART instead. It matters only if we ever
want playback state without serial traffic -- which on artoo-esp32 is the one
thing the shared UART cannot promise (Section 12.3). Open Item 1 is the one-wire,
one-scope measurement that settles it.

### 5.6 The micro-USB port will stop UART mode, and probably is not a mass-storage device either

The datasheet annotates the micro-USB socket *"USB DownLoad MP3 File"* and the
silkscreen next to it reads `DownLoad`, so the intended use is clear: plug it into
a computer to load audio.

> [!WARNING]
> **Do not power the module from that socket with a data-carrying cable.**
> `Lenoirio/dy-sv5w`'s README, from someone who developed against this exact
> module:
>
> > *"If you intend to power the module via the USB-port, make sure that your
> > USB-cable is not plugged in a PC or the USB-cable just has no data-wires.
> > Reason: In case the device is communicating via USB, it will not start in
> > UART mode."*
>
> The failure presents as **a module that ignores every command with no error**,
> which is indistinguishable from bad wiring, a wrong DIP setting, or a dead TX
> line. Anyone debugging a silent DY-SV5W on a bench with a USB cable in it
> should pull the cable first.

**Whether it enumerates as a drive at all is contested.** The same README says:

> *"The device can't operate as a USB storage-device. Neither Linux nor Windows
> can detect it as a drive. It will be reported with 'lsusb' but not as a storage
> device."*

against the datasheet's own *"USB DownLoad MP3 File"* annotation and reseller
guides that describe it as *"for connecting to a PC to update audio files on the
TF card"*. Both cannot be true of the same board; the likeliest explanation is a
firmware-revision or board-revision difference. `UNKNOWN` -- Open Item 4. It has
no bearing on firmware either way: our card is prepared in a card reader.

## 6. Mode selection: the DIP switch

### 6.1 The table, transcribed from the datasheet

Columns are printed **CON3, CON2, CON1** -- in that order, which is Section 6.2's
trap.

| Control mode | CON3 | CON2 | CON1 | What the I/O pins become |
| --- | --- | --- | --- | --- |
| I/O Integrated Mode 0 | 0 | 0 | 0 | *"Key combination play, can play 2^8-1(255) Songs."* |
| I/O Integrated Mode 1 | 0 | 0 | 1 | *"Level combination play, can play 2^8-1(255) Songs."* |
| I/O Independent Mode 0 | 0 | 1 | 0 | IO0-IO7 = Song1-Song8, edge triggered |
| I/O Independent Mode 1 | 0 | 1 | 1 | IO0-IO7 = Song1-Song8, level triggered |
| **UART Mode** | **1** | **0** | **0** | **IO0 = TXD, IO1 = RXD** |
| One-Line Mode | 1 | 0 | 0 | IO4 = data (the datasheet prints `TXD` in the IO4 column) |
| Standard MP3 Mode | 1 | 0 | 1 | IO4 = RPT, IO3 = EQ, IO2 = P/P/MODE, IO1 = PREV/V-, IO0 = NEXT/V+ |

**Mode is latched from the CON pins at power-on.** Changing the DIP switch with
the module running does nothing until it is power-cycled -- which is the first
thing to check when a mode change "did not take".

The difference between the Mode 0 and Mode 1 variants of the I/O modes, verbatim:

> *"Mode 0 will continue playing the current song to the end after release level.
> Mode 1 will stop playing immediately after release level."*

### 6.2 The ordering trap: the datasheet counts down, the switch counts up

> [!WARNING]
> **The table reads CON3-CON2-CON1 and the physical DIP switch reads 1-2-3.**
> UART mode is `CON3=1, CON2=0, CON1=0`, which on the switch body is
> **`1`=OFF, `2`=OFF, `3`=ON**.
>
> Transcribe the table left-to-right onto the switch left-to-right and you get the
> mirror image -- `1`=ON, `2`=OFF, `3`=OFF -- which is **I/O Integrated Mode 1**,
> a mode that ignores the UART entirely and plays whatever the floating I/O pins
> add up to.
>
> The ecosystem is already living in this confusion: the reporter in
> `SnijderC/dyplayer` issue #1, who could not get any example to work, describes
> their setup as *"UART mode (0-0-1)"* -- the same setting as ours, written in the
> other order. **Always say which order you are quoting.**

`src/drivers/audio_dy_sv5w.cpp:150` and its warning log at `:197` both spell it
the datasheet's way (`CON3=1 CON2=0 CON1=0`), which is correct and which a
builder holding the board will still have to reverse. Section 18 states both
forms.

### 6.3 UART mode and One-Line mode have the same DIP setting, and the datasheet does not resolve it

> [!CAUTION]
> **Both rows print `1 0 0`.** Verified this session against the rendered page --
> the `UART Mode` row and the `One-Line Mode` row carry identical CON3/CON2/CON1
> values, and differ only in which I/O column is populated: UART fills IO1/IO0
> with `RXD`/`TXD`, One-Line fills IO4 with `TXD`.

The reading that makes the table consistent is that **`1 0 0` selects "serial
control" and the pin you wire chooses the dialect** -- drive IO0/IO1 and it is
UART; drive IO4 and it is One-Line. That is a hypothesis, not a vendor statement.

The ecosystem cannot resolve it either. A search this session returned the
straightforward answer for UART (`CON3=1, CON2=0, CON1=0`, agreed everywhere) and,
for One-Line, *"the exact DIP mapping for ONE_Line vs Standard MP3 mode depends on
the PCB printing, so you should use the table printed on your board or datasheet
to confirm"* -- which is a way of saying nobody knows. Envistia's reproduction of
the mode table **omits the One-Line row entirely**.

**Costs us nothing.** protoArtoo speaks UART on IO0/IO1 and never asserts IO4. It
matters only to a builder reading this sheet who wants the single-wire mode, and
Open Item 3 names the experiment.

### 6.4 One-Line mode, recorded but not used

Not used by protoArtoo, not implemented by `SnijderC/dyplayer` (*"the library does
not support the ONE_Line protocol"*), and not implemented by `Lenoirio/dy-sv5w`.
Recorded here so the research does not have to be done twice.

**Bit encoding**, from the datasheet's waveform diagram:

```
idle              high, > 2 ms before a command
bit unit          > 200 us
bit 0             high : low = 1 : 3
bit 1             high : low = 3 : 1
inter-symbol      > 400 us ... > 1200 us gaps shown in the diagram
```

**Command values** (single bytes, not framed, no checksum):

| Value | Function | Value | Function |
| --- | --- | --- | --- |
| `0x00`-`0x09` | digits 0-9 | `0x13` | Stop |
| `0x0A` | number reset | `0x14` | Previous |
| `0x0B` | confirm choosing song | `0x15` | Previous directory |
| `0x0C` | volume setting | `0x16` | Next directory |
| `0x0D` | EQ setting | `0x17` | SD card selection |
| `0x0E` | loop mode setting | `0x18` | SD card selection *(printed identically to `0x17`)* |
| `0x0F` | channel setting | `0x19` | U disk selection |
| `0x10` | interplay song setting | `0x1A` | FLASH selection |
| `0x11` | Play | `0x1B` | System sleep |
| `0x12` | Pause | `0x1C` | Stop playing |

Addressing is **by the digits of the file name**, which is the sharpest
difference from UART mode. The datasheet's own example:

> *"'selection' and 'interplay' are played according to the track name, for
> example, the track is named '00123. Mp3', and the selected data is '0x01',
> '0x02' '0x03' '0x0B', and the selection is completed."*

So One-Line addresses `00123.mp3` **by its name**, while UART `0x07` addresses by
the module's internal index (Section 10.2). They are not the same addressing model
and a card that works under one is not guaranteed to behave identically under the
other.

> [!NOTE]
> `0x17` and `0x18` are both printed *"SD card selection"*. One of them is almost
> certainly a typo -- `UNKNOWN` which, and there is nothing to check it against.
> There is also **a `System sleep` command (`0x1B`) with no UART equivalent** in
> any of the three command tables, which is the only capability One-Line appears
> to have that UART does not.

## 7. The serial protocol (normative)

### 7.1 The frame

**9600 baud, 8 data bits, 1 stop bit, no parity, full duplex.** Verbatim from the
datasheet: *"Adopt full duplex serial port communication. Baud rate 9600, data
bits 8, stop bit 1, check bit N."* No command changes the baud rate; 9600 is
fixed, which is why the artoo-esp32 bit-bang works at all (Section 12.1).

```
  byte   0      1       2        3 .. 3+n-1     3+n
        0xAA   CMD    LEN (n)    DATA[0..n-1]    SM
```

| Field | Value | Note |
| --- | --- | --- |
| Start | `0xAA` | *"Command Code: fixed to 0xAA."* |
| CMD | `0x01`-`0x1F` | Section 8 |
| LEN | `n` | *"the number of bytes of data in an command"* -- `0x00` where there is no payload |
| DATA | `n` bytes | *"high 8-bit data is in front, low 8-bit is in the back"* -- **big-endian** |
| SM | 1 byte | *"Low 8 bits of sum of all bytes. that is, When start code and data are added, take out low 8 bits."* |

So a frame is always **`4 + n` bytes**, minimum 4.

> [!NOTE]
> **One line of the datasheet's prose is wrong, and it is the one about `LEN`.**
> It reads: *"Data: Relevant data in command, when length of data is 1, means
> there is only CMD and no data bits."* Every command table entry contradicts it
> -- `Play` is `AA 02 00 AC`, with `LEN = 0` for "no data". Read `LEN` as the
> plain byte count the same sentence's first clause describes, and ignore the
> second clause.

### 7.2 The checksum, and the fact that every printed example verifies

`SM = (sum of all preceding bytes, including 0xAA) & 0xFF`. Nine lines of C, and
identical in all four implementations read this session -- ours, `dyplayer`,
`Lenoirio` and BetterDuino:

```c
uint8_t sum = 0;
for (uint8_t i = 0; i < len; i++) sum = (uint8_t)(sum + payload[i]);
```

**All 17 worked byte sequences printed in the datasheet's control and query
tables were recomputed this session. Zero mismatches.**

```
Play           AA 02 00 AC   OK      Q play status        AA 01 00 AB   OK
Pause          AA 03 00 AD   OK      Q online drive       AA 09 00 B3   OK
Stop           AA 04 00 AE   OK      Q play drive         AA 0A 00 B4   OK
Previous       AA 05 00 AF   OK      Q number of songs    AA 0C 00 B6   OK
Next           AA 06 00 B0   OK      Q current song       AA 0D 00 B7   OK
Volume +       AA 14 00 BE   OK      Q folder dir song    AA 11 00 BB   OK
Volume -       AA 15 00 BF   OK      Q folder song count  AA 12 00 BC   OK
Previous file  AA 0E 00 B8   OK
Next file      AA 0F 00 B9   OK
Stop playing   AA 10 00 BA   OK
```

> [!IMPORTANT]
> **This is the headline difference from the DFPlayer Mini, and it is worth
> stating plainly.** [`dfplayer-mini-sound.md`](dfplayer-mini-sound.md) Section 6.2
> found that *"all three worked examples in the datasheet have wrong checksums"* --
> every byte sequence a developer would copy to bootstrap a driver failed the
> datasheet's own rule. Here, 17 for 17 are right, and `SnijderC/dyplayer`'s
> precomputed constants (`0xab`, `0xac`, `0xad`, `0xae`, `0xaf`, `0xb0`, `0xb4`,
> `0xb6`, `0xb7`, `0xb8`, `0xb9`, `0xba`, `0xbb`, `0xbc`, `0xbe`, `0xbf`, `0xc6`)
> agree with both.
>
> **Compute anyway.** The rule survives contact with a document that gets it
> right, and the one wrong checksum this research found is in **our own file
> header** (Section 17.1), not in the vendor's.

### 7.3 A sum is not a CRC, and two real frames collide

The checksum is a plain modulo-256 sum, so it detects a single corrupted byte but
**cannot detect a transposition**. Two frames protoArtoo actually sends prove it:

```
play track 1     AA 07 02 00 01 B4
play track 256   AA 07 02 01 00 B4     <-- same SM
```

A soft-UART bit-bang that swapped two payload bytes -- the exact failure mode the
`portMUX` critical section in `audio_soft_uart_tx.h` exists to prevent -- would
produce a **checksum-valid frame that plays the wrong sound**. There is no framing
help either: no end marker, no escape, no length-independent resync. A receiver
that loses byte alignment recovers only by timing out and waiting for silence.

This is not a reason to distrust the protocol, and it is a reason the interrupt
protection is not optional.

### 7.4 Nothing acknowledges anything

**Control and setting commands return nothing at all.** The datasheet's `Return`
column reads `None` for all ten control commands and all eleven setting commands;
only the seven query commands reply. `Lenoirio/dy-sv5w` says it in the README:

> *"Most of the commands are fire-and-forget commands. This means there is no
> ack/nack sent from the module. Thus, it's not possible to provide the API caller
> with success information."*

So there is **no way to know a play command was accepted** except by asking
afterwards (`0x01` play state, `0x0D` current song) or by watching `BUSY`. This is
precisely the defect `tasks/lessons.md` records for 2026-03-21 -- a Sound page
badge that said "Playing" because firmware had *sent* something -- and
`AudioModuleState` exists as the answer (Section 13).

## 8. Commands

Every `SM` below was computed this session. Frames with a variable payload show
the rule and a worked example.

### 8.1 Control commands (no reply, ever)

| Command | Code | Frame | Note |
| --- | --- | --- | --- |
| Play / resume | `0x02` | `AA 02 00 AC` | Resumes the **selected** track. Not "play track N" |
| Pause | `0x03` | `AA 03 00 AD` | |
| **Stop** | `0x04` | `AA 04 00 AE` | **What protoArtoo sends for `stop()`** |
| Previous | `0x05` | `AA 05 00 AF` | |
| Next | `0x06` | `AA 06 00 B0` | **Not play.** This opcode caused a shipped regression (Section 14.2) |
| Previous file | `0x0E` | `AA 0E 00 B8` | Datasheet wording. `dyplayer` calls it *previous directory, last sound* -- Open Item 5 |
| Next file | `0x0F` | `AA 0F 00 B9` | Datasheet wording. `dyplayer` calls it *previous directory, first sound* -- Open Item 5 |
| Stop playing | `0x10` | `AA 10 00 BA` | A **second** stop. `dyplayer` names it `stopInterlude()`; BetterDuino sends it for `Quiet()`. Open Item 5 |
| Volume + | `0x14` | `AA 14 00 BE` | One step |
| Volume - | `0x15` | `AA 15 00 BF` | One step |

### 8.2 Query commands (always reply)

| Query | Code | Frame | Reply | Reply length |
| --- | --- | --- | --- | --- |
| Play status | `0x01` | `AA 01 00 AB` | `AA 01 01 <state> SM` | 5 |
| Current **online** drive | `0x09` | `AA 09 00 B3` | `AA 09 01 <drive> SM` | 5 |
| Current **play** drive | `0x0A` | `AA 0A 00 B4` | `AA 0A 01 <drive> SM` | 5 |
| Number of songs | `0x0C` | `AA 0C 00 B6` | `AA 0C 02 <SN_H> <SN_L> SM` | 6 |
| Current song | `0x0D` | `AA 0D 00 B7` | `AA 0D 02 <SN_H> <SN_L> SM` | 6 |
| First song in folder | `0x11` | `AA 11 00 BB` | `AA 11 02 <SN_H> <SN_L> SM` | 6 |
| Songs in folder | `0x12` | `AA 12 00 BC` | `AA 12 02 <SN_H> <SN_L> SM` | 6 |

`0x09` versus `0x0A` is a real distinction: **online** is what storage the module
can see, **play** is what it is currently reading from. `begin()` uses `0x09`
before the device select and `0x0A` after it, precisely to confirm the select
took (Section 11.2).

### 8.3 Setting commands (no reply)

| Command | Code | Frame | Payload | Worked example |
| --- | --- | --- | --- | --- |
| **Specified song** | `0x07` | `AA 07 02 <SN_H> <SN_L> SM` | 16-bit big-endian index | track 1 -> `AA 07 02 00 01 B4` |
| Specified path | `0x08` | `AA 08 <len> <drive> <path...> SM` | see Section 10.5 | -- |
| **Switch drive** | `0x0B` | `AA 0B 01 <drive> SM` | `00` USB / `01` SD / `02` FLASH | FLASH -> `AA 0B 01 02 B8` |
| **Set volume** | `0x13` | `AA 13 01 <vol> SM` | 0-30 | 15 -> `AA 13 01 0F CD` |
| Interlude by number | `0x16` | `AA 16 03 <drive> <SN_H> <SN_L> SM` | drive + index | SD, track 5 -> `AA 16 03 01 00 05 C9` |
| Interlude by path | `0x17` | `AA 17 <len> <drive> <path...> SM` | see Section 10.5 | -- |
| Set loop mode | `0x18` | `AA 18 01 <mode> SM` | 0-7, Section 8.5 | single-stop -> `AA 18 01 02 C5` |
| Set cycle times | `0x19` | `AA 19 02 <H> <L> SM` | 16-bit repeat count | 3 -> `AA 19 02 00 03 C8` |
| **Set EQ** | `0x1A` | `AA 1A 01 <eq> SM` | 0-4, Section 8.5 | NORMAL -> `AA 1A 01 00 C5` |
| Combination play | `0x1B` | `AA 1B <2*k> <name pairs...> SM` | k two-character file names | Section 10.6 |
| End combination play | `0x1C` | `AA 1C 00 C6` | -- | |
| Select, do not play | `0x1F` | `AA 1F 02 <SN_H> <SN_L> SM` | 16-bit index | track 1 -> `AA 1F 02 00 01 CC` |

**Precomputed tables for the two payload commands protoArtoo sends most:**

| Volume | Frame | | Volume | Frame |
| --- | --- | --- | --- | --- |
| 0 (silent) | `AA 13 01 00 BE` | | 20 (module default) | `AA 13 01 14 D2` |
| 5 | `AA 13 01 05 C3` | | 25 | `AA 13 01 19 D7` |
| 10 | `AA 13 01 0A C8` | | 30 (max) | `AA 13 01 1E DC` |
| 15 (our init) | `AA 13 01 0F CD` | | | |

| Track | Frame | | Track | Frame |
| --- | --- | --- | --- | --- |
| 1 | `AA 07 02 00 01 B4` | | 151 (Leia) | `AA 07 02 00 97 4A` |
| 2 | `AA 07 02 00 02 B5` | | 177 (SW theme) | `AA 07 02 00 B1 64` |
| 21 | `AA 07 02 00 15 C8` | | 255 | `AA 07 02 00 FF B2` |
| 52 | `AA 07 02 00 34 E7` | | 256 | `AA 07 02 01 00 B4` |
| 53 | `AA 07 02 00 35 E8` | | 999 | `AA 07 02 03 E7 9D` |
| 126 (scream) | `AA 07 02 00 7E 31` | | 65535 | `AA 07 02 FF FF B1` |

### 8.4 Volume: 0-30 ascending, default 20, and no scaling anywhere

Datasheet, communication protocol section C, verbatim:

> *"Volume: the volume is 31grades, 0-30.The default is 20grade."*

Corroborated by `SnijderC/dyplayer` (*"Set the playback volume between 0 and 30.
Default volume if not set: 20."*), by `Lenoirio/dy-sv5w` (*"volume to 10 (max
value is 30)"*), and by BetterDuino's ladder -- `VolumeMax()` = 30,
`VolumeMid()` = 15, `VolumeMin()` = 5, `VolumeOff()` = 0.

> [!IMPORTANT]
> **0 is silent and 30 is loudest, which is `AudioDriver`'s normalised range
> exactly.** `include/audio_driver.h:15` says *"Volume range is normalised 0-30 at
> the interface level; concrete drivers scale to their module's native range if
> different"*, and this driver is the one that needs **no conversion at all**:
> `setVolume(vol)` puts `vol` in the frame unchanged.
>
> That is not universal in this family and the difference bites. The MP3 Trigger's
> VS1053 register is **inverted and 0-255** (`docs/sound_playback.md`: *"vol=0 ->
> nativeVol=255 (silent) ... vol=30 -> nativeVol=0 (maximum)"*), and CHIRP scales
> 0-30 to 0-99. Section 15.2 is a live example of an astromech project carrying an
> MP3 Trigger volume comment into DY-SV5W code and getting the direction backwards.

The setting table's `Remark` column says `VOL: 0x00-0xFF`, which contradicts the
protocol section's 0-30. **Believe 0-30**: three implementations clamp there,
`AudioTask` clamps there, and what the module does with 31-255 is untested and
uninteresting. `UNKNOWN`, recorded rather than explored.

### 8.5 Play modes and EQ

**Play mode** (`0x18`), *"the default is the single stop when power on"*:

| Value | Mode | Frame | Verbatim |
| --- | --- | --- | --- |
| `0x00` | Cycle all | `AA 18 01 00 C3` | *"play the whole songs in sequence and play it after the play"* |
| `0x01` | Single cycle | `AA 18 01 01 C4` | *"play the current song all the time"* |
| **`0x02`** | **Single stop** | `AA 18 01 02 C5` | *"Only play current song once and then stop"* -- **the power-on default, and what a droid wants** |
| `0x03` | Random | `AA 18 01 03 C6` | |
| `0x04` | Directory loop | `AA 18 01 04 C7` | *"Directory don't contain subdirectory"* |
| `0x05` | Directory random | `AA 18 01 05 C8` | |
| `0x06` | Directory order | `AA 18 01 06 C9` | *"Play current folder in order & stop after play"* |
| `0x07` | Sequential | `AA 18 01 07 CA` | *"play the whole songs in order and stop after it is played"* |

**protoArtoo never sends `0x18`**, and that is correct: the power-on default is
already the one-shot behaviour a droid needs. A droid that ever gains a "play the
whole card" mode would send `0x07`, and would then have to send `0x02` back.

**EQ** (`0x1A`), default NORMAL:

| Value | EQ | Frame |
| --- | --- | --- |
| `0x00` | NORMAL | `AA 1A 01 00 C5` |
| `0x01` | POP | `AA 1A 01 01 C6` |
| `0x02` | ROCK | `AA 1A 01 02 C7` |
| `0x03` | JAZZ | `AA 1A 01 03 C8` |
| `0x04` | CLASSIC | `AA 1A 01 04 C9` |

`begin()` sends NORMAL explicitly even though it is the default -- cheap insurance
against a module whose EQ was left elsewhere by a previous owner or a Standard MP3
Mode button press, and there is no query to read it back with (Open Item 7).

## 9. What the module sends back

### 9.1 Replies

Replies reuse the command frame shape with the payload filled in:

```
play state    AA 01 01 <state> SM      state: 00 stop, 01 play, 02 pause
drive         AA 09 01 <drive> SM      drive: 00 USB, 01 SD, 02 FLASH, FF none
              AA 0A 01 <drive> SM
16-bit count  AA 0C 02 <SN_H> <SN_L> SM
              AA 0D 02 <SN_H> <SN_L> SM
              AA 11 02 <SN_H> <SN_L> SM
              AA 12 02 <SN_H> <SN_L> SM
```

Both enumerations come from the datasheet's communication-protocol section
verbatim: *"Playing State definition: the system is on the stop state when power
on. 00(stop) 01(play) 02(pause)"* and *"Disk character definition: it is stopped
after the switch disk. USB:00 SD:01 FLASH:02 NO_DEVICE: FF"*.

> [!NOTE]
> **`AudioModuleState` needs no translation, and this is the one place the
> DFPlayer needs several.** `include/audio_driver.h` documents `device` as
> `0=USB 1=SD/TF 2=FLASH 0xFF=unknown/none` and `playState` as
> `0=stop 1=playing 2=paused 0xFF=unknown` -- **byte-identical to this module's
> wire values**, which is why our driver assigns `rsp[3]` straight through.
> [`dfplayer-mini-sound.md`](dfplayer-mini-sound.md) Section 14.4 has to warn that
> a DFPlayer driver doing the same would *"report an SD card as FLASH and a USB
> stick as SD/TF"*. `AudioModuleState` was clearly drawn around this module.

**Reply latency is `UNKNOWN` and our timeout is 300 ms per query**, chosen without
a measurement to back it (`sendQuery`, `src/drivers/audio_dy_sv5w.cpp:127-143`).
It has been sufficient in practice on hardware since 2026-03. Open Item 8.

### 9.2 The module also volunteers bytes, and that has already broken a driver

There is no documented unsolicited frame -- the datasheet has no
power-on-report equivalent to the DFPlayer's `0x3F`, and no track-finished push.
But it emits bytes we did not ask for, and we know because it cost us a fix.
`tasks/lessons.md`, 2026-03-21, and commit `a6ab6ca4`:

> *"DY-SV5W emits spontaneous bytes during playback (status pushes). Without
> header validation, `sendQuery()` would capture these as a response, pass the
> `n>=4` length check, and write garbage into device/play_state fields -- showing
> 'unknown' instead of the correct cached values. Fix: check `rsp[0]==0xAA` and
> `rsp[1]==expected_cmd` before accepting any response."*

`begin()` also drains the RX buffer after its 1.5 s boot delay *"(e.g. boot
announcement)"*, which is the same phenomenon at power-on.

**What a driver must therefore do**, and what ours does
(`src/drivers/audio_dy_sv5w.cpp:296, 305, 313`):

```c
if (n >= 4 && rsp[0] == 0xAA && rsp[1] == 0x09 && rsp[2] == 0x01) { ... }
```

Start byte, **command byte and length byte**, all three, before believing a
reply. `Lenoirio/dy-sv5w` independently arrived at exactly the same three checks
in `receive_answer()` -- and, like ours, reads the trailing checksum byte and
throws it away (`let _ = self.serial.read_byte().await; // ignore CRC for now`).
`SnijderC/dyplayer` is the only one of the three that validates the reply
checksum. Section 17.4.

## 10. Storage, and the card contract

### 10.1 Our module runs from on-board FLASH, and that was a surprise worth keeping

`AudioModuleState.device` on our bench droid reports **`0x02` = FLASH**, measured
and recorded in `tasks/lessons.md`:

> *"Pre-init serial log showed `post-init: play drive = FLASH` confirming the
> module reported FLASH after `begin()`"*

`SnijderC/dyplayer`'s device enum explains what that is: *"Flash = 0x02, Onboard
flash chip (usually winbond 32, 64Mbit flash)"* -- 4 to 8 MB of SPI flash on the
board, loaded through the micro-USB socket (Section 5.6), independent of the TF
slot.

> [!IMPORTANT]
> **Do not assume a DY-SV5W plays from the card.** `tasks/lessons.md` states the
> prevention rule in so many words: *"Confirm module storage type on first
> hardware bring-up and document it. Do not assume all DY-SV5W units use SD/TF --
> some use on-board FLASH (device code 0x02)."*
>
> Whether a given board has a flash chip populated is a board-revision fact, and
> the module reports it truthfully on `0x09`. **Ask, never assume** -- Section 14.1
> is what assuming cost.

### 10.2 The index is the filesystem's order, not the file's name

This is the single most consequential behaviour of the module and three
independent sources say it:

- `SnijderC/dyplayer` README: *"The module will look for the first sound file
  found in the filesystem. It's not using the file name, neither does it order by
  file name."*
- `Lenoirio/dy-sv5w` README: *"don't expect that the file called 00001.mp3 is
  always the song that you address with number 1. It looks like the module rather
  counts the number in the FAT directory structure."*
- `docs/sound_playback.md`, our own, from bench experience: *"DY-SV5W expects
  contiguous numbering with no gaps in the sequence. If a number is missing, the
  module's internal track index does not align with filename intent and later
  files can be addressed as if they were the missing number."* With the worked
  example: files `001.mp3`, `002.mp3`, `004.mp3` present, **requesting track 003
  may play `004.mp3`**.

`0x07` addresses the **n-th file the module enumerated**. The file name is a
convention the builder maintains, not an address the module honours. There is no
play-by-name escape in UART mode except `0x08` play-by-path (Section 10.5), which
protoArtoo does not implement.

> [!NOTE]
> **The DFPlayer has an escape from this and the DY-SV5W does not**, which is the
> one place that module is genuinely better --
> [`dfplayer-mini-sound.md`](dfplayer-mini-sound.md) Section 9.1 calls choosing
> `0x12` play-by-filename *"the single most valuable decision in this sheet"*. For
> the DY-SV5W the only defence is the discipline in Section 10.4.

### 10.3 The card itself

| Property | Value | Source |
| --- | --- | --- |
| Card type | microSD / TF | Datasheet board annotation |
| Maximum size | **32 GB** | Datasheet annotation *"TF Card 32G Bit"*; every reseller listing |
| Filesystem | FAT32 (FAT16 accepted) | `docs/sound_playback.md`; ecosystem consensus. exFAT `UNKNOWN`, presumed unsupported -- Open Item 9 |
| Formats | **MP3 and WAV** | Reseller specifications, consistently |
| Maximum tracks | 65535 by frame width; **255** in the I/O modes | `0x07` payload is 16-bit; the I/O mode tables stop at `00255.mp3` |

> [!WARNING]
> **A card bought today will very likely arrive exFAT.** Anything above 32 GB is
> exFAT by default on Windows and macOS and a 64 GB card is now the cheapest on
> the shelf. Specify **FAT32, 32 GB or under**. This is the identical hazard the
> DFPlayer sheet flags, and the identical failure: the module reports no device
> online and a driver correctly concludes there is no card.

### 10.4 Practical rules for a protoArtoo card

1. **Copy the files onto an empty card in playing order, one at a time or in a
   single ordered batch.** The order they land in the directory is the order the
   module will number them. This is the rule that actually protects you; naming is
   only a reminder of it.
2. Name them `001.mp3` .. `NNN.mp3`, **strictly contiguous, no gaps**
   (`docs/sound_playback.md`). The datasheet's I/O-mode tables use five digits
   (`00001.mp3`); three works and is what the astromech packs ship. Either is
   fine, consistently.
3. **Prepare the card on Linux or Windows, or clean it afterwards.** A card
   prepared on a Mac carries a `._001.mp3` resource fork beside every track and a
   `.Spotlight-V100` directory, and under an enumeration-order index **every one
   of those is a file in the count**. `docs/sound_playback.md` already warns
   *"avoid hidden files (macOS `._` files cause issues)"*.
4. **Never delete a track from the middle.** Replace it with silence of the same
   name rather than removing it, or re-copy the whole card. Deleting file 3 does
   not free number 3; it renumbers everything after it.
5. **Read back `0x0C` at `begin()` and compare it with what the configuration
   expects.** Our driver already queries it. Nothing compares it yet -- Section
   17.5 and Open Item 10, and it is the cheapest possible detection of a card that
   was rebuilt wrong.

### 10.5 Play-by-path (`0x08` / `0x17`), recorded but not implemented

`AA 08 <len> <drive> <path...> SM`, where the path is **transformed**:

- every `.` becomes `*`
- every `/` except the leading one gets a `*` **inserted before it**
- the whole path is upper-cased

so `/SONGS1/FILE1.MP3` goes on the wire as `/SONGS1*/FILE1*MP3`. The encoder is
`byPathCommand()` in `SnijderC/dyplayer`, read this session; the datasheet only
gives the frame shape (`AA 08 Length Drive Path SM`). Names are limited to
**8 characters per directory and 8 per file name**, and `dyplayer` caps whole
paths at 36 bytes by default.

**protoArtoo does not implement this**, and the trade-off is worth naming
explicitly: it would give us name-stable addressing -- immunity to Section 10.2 --
at the cost of a variable-length frame, an 8.3 naming constraint on the card, and
a break with the community numbering every other astromech project uses
(Section 15). If Section 10.2 ever becomes a real operator complaint rather than a
documented discipline, this is the escape hatch, and Open Item 11 is the
experiment.

### 10.6 Combination play (`0x1B` / `0x1C`), recorded but not implemented

Queues several short files to play back to back -- the vendor's use case is
building a spoken number out of samples. Files must have **two-character names**
(`01.mp3`) and live in a specific directory: the datasheet says `XY`,
`SnijderC/dyplayer`'s documentation says *"a directory that can be called `DY`,
`ZH` or `XY` ... most modules use `XY` despite documentation suggesting `DY`"*.

Frame: `AA 1B <2*k> <pair1> <pair2> ... SM`, then `AA 1C 00 C6` to end it.

Interesting for a droid -- it is a hardware sequence of sounds with no host in the
loop -- but it collides with `docs/sequence-authoring.md` owning sequencing, and
the two-character naming collides with everything in Section 10.4. Recorded, not
recommended.

## 11. What protoArtoo's driver actually sends

`src/drivers/audio_dy_sv5w.cpp`, 387 lines, read line by line this session.

### 11.1 The four `AudioDriver` methods

| Method | Bytes on the wire | Post-delay |
| --- | --- | --- |
| `playTrack(n)` | `AA 07 02 <n_hi> <n_lo> SM`; **`n == 0` emits nothing at all** | 100 ms |
| `stop()` | `AA 04 00 AE` | 100 ms |
| `setVolume(v)` | `AA 13 01 <v> SM`, `v` unscaled | 100 ms |
| `begin(vol)` | Section 11.2 | -- |

`sendCommand()` computes the sum over the payload, writes the payload, writes the
sum, then **always delays 100 ms**. That number is inherited, not invented:
BetterDuino's `sendCommand()` ends with `delay(100);` under the comment *"Delay
needed between successive commands"*, and `MDuinoSoundDYPlayer::init()` puts
another 100 ms between each init frame. No vendor document states a minimum gap
-- `UNKNOWN`, Open Item 8 -- and 100 ms has worked on hardware since 2026-03.

> [!NOTE]
> **`playTrack()` clamps nothing and the header says it does.** The comment at
> `:346` reads *"We support uint16_t for forward compatibility but clamp to 255
> for DY-SV5W modules"*; the code sends the full 16 bits. The code is right -- the
> frame is 16-bit and 65535 is a legal index -- and `test_audio_frames.cpp`
> asserts the 65535 payload bytes explicitly. It is the comment that is stale; see
> Section 17.2.

### 11.2 `begin()`, and why it asks three questions before it says anything

Order, verbatim from the implementation:

1. Open the transport for this board (Section 12).
2. **Wait 1500 ms.** *"DY-SV5W needs ~1.5 s after power-on to boot and enumerate
   storage."* No vendor document states a boot time -- `UNKNOWN`, Open Item 8.
3. Drain RX (Section 9.2).
4. **`AA 09 00 B3`** -- which storage is online? Caches `m_device`, logs
   `USB` / `SD/TF` / `FLASH` / `NO_DEVICE`. A no-response logs the DIP-and-wiring
   hint.
5. **`AA 01 00 AB`** -- is it already playing from a previous session?
6. **`AA 0C 00 B6`** -- how many tracks? Caches `m_totalTracks`.
7. **`AA 0B 01 <m_device> SM`** -- switch to the drive it just reported, **or send
   nothing at all** if step 4 failed. Section 14.1 is why.
8. **`AA 1A 01 00 C5`** -- EQ NORMAL.
9. **`AA 13 01 <vol> SM`** -- the NVS volume.
10. **`AA 0A 00 B4`** -- confirm the play drive took.

Four queries and three commands, so **`begin()` blocks for at least 1.5 s and up
to about 2.7 s** (1500 ms + 4 x up to 300 ms + 3 x 100 ms). That is why
`audio_task.cpp` asserts `xPortGetCoreID() == 0` right before calling it.

It returns `bool`, and a `false` is retried by the Audio Step Core up to
`AUDIO_STEP_INIT_MAX_RETRIES` before the task gives up and reports
`AUDIO_RX_NO_RESPONSE`. **In practice it always returns `true`** -- the only
`return` in the function is `return true`, so a module that answers nothing is
reported through `linkOk`, not through a retry. That is a defensible design (the
module is write-only in the absence of RX and playback still works) but it means
the retry machinery never fires for this driver.

### 11.3 `queryModuleState()` -- three queries, ~900 ms worst case

Sends `0x09`, `0x01`, `0x0D` in that order, 300 ms timeout each, and returns true
if **any one** of them produced a validated reply. `totalTracks` is **not**
re-queried -- it is carried forward from `begin()`, deliberately, *"to keep poll
overhead low"* -- and `device` is carried forward as a starting value and updated
if `0x09` answers.

### 11.4 `getCachedState()` -- what the operator sees without touching the wire

```
linkOk       = (m_device != 0xFF)
device       = m_device          (from begin(), or the last successful poll)
totalTracks  = m_totalTracks     (from begin() only)
playState    = 0xFF              always: not cached, requires a query
currentTrack = 0                 always: not cached, requires a query
```

This is what the Sound page shows between polls, and it is why that page's copy
says *"Status is cached from boot. Use Poll to refresh -- only poll when not
playing."*

### 11.5 What the driver does not implement, and why that is fine

Not sent by protoArtoo: `0x02` play/resume, `0x03` pause, `0x05` previous,
`0x06` next, `0x08` play-by-path, `0x0A` outside `begin()`, `0x0E`/`0x0F`/`0x10`,
`0x11`/`0x12` folder queries, `0x14`/`0x15` volume step, `0x16`/`0x17` interlude,
`0x18` loop mode, `0x19` cycle times, `0x1B`/`0x1C` combination play, `0x1F`
select-without-playing.

**Everything the droid does is play-a-numbered-sound, stop, set volume, ask.** The
`$` command set (`docs/sound_playback.md` Section 3) maps `$+` / `$-` onto
`setVolume(current +/- 1)` rather than the module's own `0x14` / `0x15`, which is
correct: firmware has to know the resulting number to store it in NVS and show it
on a slider, and the module's own step commands report nothing back.

The one genuinely interesting omission is **`0x16` interlude**: play a sound *over*
the current one, then return to it. That is the shape of the **Sound Bed**
(`CONTEXT.md`), and `CONTEXT.md` currently records the DY-SV5W as one of *"the
single-track modules"* a bed contrasts itself with. The vendor's own note, quoted
through `dyplayer`, says *"'Music interlude' only has level 1"* -- one interlude,
covering the previous one -- so it is not mixing and it would not give a bed its
own volume. **It does not make this a mixing module**, and it is worth knowing
before someone re-litigates that. Open Item 12.

## 12. The transport, which differs per board

### 12.1 The two arrangements

| | artoo-esp32 | FireBeetle 2 (ESP32-P4) |
| --- | --- | --- |
| PCB header | **S2 -- Sound** | expansion-shield rows `34` + `36` |
| `PIN_AUDIO_TX` | **26**, software bit-bang | **34**, hardware UART |
| `PIN_AUDIO_RX` | **35** (input-only GPIO) | **36** |
| `UART_PORT_AUDIO` | **2** -- *the dome link's controller* | **3** -- audio's own |
| `PA_CAP_DEDICATED_AUDIO_UART` | **0** | **1** |
| TX contention | none (it is a GPIO) | none |
| RX contention | **arbitrated with DomeLink** every query | none |

The reason is HP UART controller count, not wiring: the ESP32 has three
(`SOC_UART_HP_NUM = 3`) and they are spoken for by the console, the hoverboard
drive and the dome link, so audio's TX becomes a bit-bang and its RX borrows the
dome link's controller. The ESP32-P4 has five and audio gets one outright. A
`static_assert` in `include/config.h:457` fails the build if the capability flag
and the controller allocation ever disagree.

### 12.2 The soft UART is exactly sized for this module

`src/drivers/audio_soft_uart_tx.h` bit-bangs 9600 8-N-1 with
`delayMicroseconds(104)` per bit inside a `portMUX` critical section, measured at
**~1.04 ms per byte**. This module's fixed 9600 baud is what makes that viable at
all.

| Command | Bytes | Core 0 non-preemptible |
| --- | --- | --- |
| `stop()` | 4 | ~4.2 ms |
| `setVolume()` | 5 | ~5.2 ms |
| `playTrack()` | 6 | ~6.2 ms |
| A `begin()` query | 4 | ~4.2 ms |

The header's justification -- *"Audio commands are infrequent (at most a few per
second), so this is safe for the application"* -- holds: a few plays per second is
around 2 % of Core 0, and Core 1's real-time loops (DriveTask, SBUSInputTask,
DomeLinkTask) are untouched because the critical section is per-core.

The critical section is not decoration. Its own header explains what it prevents:

> *"`delayMicroseconds()` is not interrupt-safe: the FreeRTOS tick ISR (1 ms) and
> WiFi radio ISRs on Core 0 can stretch a bit period mid-byte, producing corrupted
> frames at the DY-SV5W receiver."*

And Section 7.3 is why a corrupted frame is not always a rejected one.

### 12.3 On artoo-esp32 the read path is borrowed, and the driver does not say so

Every query in `audio_task.cpp` is wrapped in `audioUartClaim()` /
`audioUartRelease()`. A denied claim -- DomeLink is on serial and holding the
controller -- is reported as **`AUDIO_RX_BLOCKED_BY_DOME_UART`**, which the UI
renders as *"Status unavailable: DomeLink is using UART"* rather than as a dead
module. That distinction is the whole point of the arbiter.

> [!NOTE]
> **`AudioDriverDySv5w` does not override `classifyRxStatus()`, and it does not
> need to.** `include/audio_driver.h:141` warns that *"forgetting to override in
> such a driver causes false 'No module response' errors in the UI"*, and
> [`dfplayer-mini-sound.md`](dfplayer-mini-sound.md) Section 14.3 makes it a MUST
> for a DFPlayer driver. For this driver the blocked case never reaches
> `classifyRxStatus()`: `audio_task.cpp` tests the claim **before** calling the
> driver at all and sets the blocked status itself, so the driver only ever
> classifies an outcome it was actually allowed to attempt. The one call site that
> does use it (`audio_task.cpp:690`, seeding module state after `begin()`) is
> Section 17.5's finding.

### 12.4 It is verified on a laptop, and that is not a consolation prize

`AudioSerialIO` is five function pointers, so every byte in Section 8 is asserted
without hardware:

- **`test/test_native/test_audio_frames/`** -- 13 tests. Play frames for tracks 1,
  126, 256 and 65535 including the byte-boundary crossing; stop; volume 0, 15 and
  30; all five query checksums; and `test_sm_wraps_at_8_bits` for the modulo.
- **`test/test_native/test_audio_io_seam/`** -- 10 DY-SV5W tests driving the real
  `AudioDriverDySv5w` through a recording IO: exact TX byte arrays, `playTrack(0)`
  emitting **zero** bytes, the 100 ms delay being exactly one call of exactly
  100 ms, and `begin()`'s first four bytes being `AA 09 00 B3`.
- **`test/test_native/test_component_registry/`** -- asserts
  `componentPartCapabilities("dy_sv5w") == 0x0F` and that a stored member this
  image cannot drive falls back to `dy_sv5w`.
- **`test/test_native/test_audio_sound_member/`** -- asserts that binding member
  `dy_sv5w` yields a driver whose `driverName()` is `"DY-SV5W"`.

`tasks/lessons.md` records why the frame suite is written the way it is: an
earlier version *"continued to test the old `0xAB` footer as a protocol constant,
so the tests passed (checksums sometimes coincide with `0xAB`) but were certifying
the wrong behaviour"*. The current file carries an explicit comment that
`AA 01 00 AB`'s last byte is arithmetic, not a footer -- the prevention rule from
that lesson, applied.

## 13. Capabilities, status, and what the operator sees

### 13.1 The capability word

Row 18 declares `0x0F`:

| Bit | Capability | Served by |
| --- | --- | --- |
| `0x01` | `AUDIO_CAP_STATUS_QUERY` | `0x01` play status |
| `0x02` | `AUDIO_CAP_DEVICE_TYPE` | `0x09` / `0x0A` drive |
| `0x04` | `AUDIO_CAP_TRACK_COUNT` | `0x0C` number of songs |
| `0x08` | `AUDIO_CAP_CURRENT_TRACK` | `0x0D` current song |
| `0x10` | `AUDIO_CAP_QUERY_SAFE_PLAYING` | **not declared** -- Section 13.2 |
| `0x20` | `AUDIO_CAP_CATALOG` | **not declared** -- no manifest exists to read |

The word is declared **once**, on the registry row, and
`AudioDriverDySv5w::capabilities()` returns `componentPartCapabilities("dy_sv5w")`
rather than restating it -- with a `static_assert` that the id it cites is a real
row. A row and its driver cannot drift apart.

### 13.2 Why this module is not auto-polled

`AUDIO_CAP_QUERY_SAFE_PLAYING` is the bit that lets `audioStepIdle()` run a
background poll every `AUDIO_STEP_AUTO_QUERY_INTERVAL_MS` (10 s). **CHIRP declares
it; the DY-SV5W does not**, and `audio_task.cpp` says why in its own words:
*"Background polling can corrupt some module RX state machines, so the auto-query
still runs only for `AUDIO_CAP_QUERY_SAFE_PLAYING`."*

The consequence reaches the operator directly:

- status is **cached from boot** and refreshed only when someone presses **Poll**,
- the Sound page says exactly that -- *"Status is cached from boot. Use Poll to
  refresh -- only poll when not playing."* -- and
- `data/sound.js` only polls while not playing for manual-poll backends.

This is an honest limitation, not a gap: three 300 ms queries into a module that
is mid-track is the situation Section 9.2's spontaneous bytes come from.

### 13.3 The API surface

`GET /api/audio` returns the module's own confirmed state, and `docs/api.md`'s
worked example is a DY-SV5W:

```json
{"driver":"DY-SV5W","capabilities":15,"link_ok":true,"active":false,
 "play_state":"stop","device":"FLASH","total_tracks":999,"current_track":0}
```

(That example is illustrative -- `total_tracks: 999` is not a measured value from
our droid. The `device: "FLASH"` is real; Section 10.1.)

`POST /api/audio` takes `action=play|stop|volume`, `GET /api/identity/components`
reports the lineup with `"active_member":"dy_sv5w"`, and `POST /api/config` with
`soundMember=dy_sv5w` changes the module, staged at reboot. Clients branch on the
**capability bits**, never on the driver name.

## 14. What our own droid has proven, and what it cost

Three hardware findings, all recorded at the time, all still load-bearing.

### 14.1 The hardcoded `switchDrive` that broke status without breaking sound

**2026-03-22, `tasks/lessons.md`, commit `3e45db4b`.** `begin()` ran the
`0x09` query, correctly detected FLASH, cached it -- and then sent a hardcoded
`switchDrive(0x01 = SD/TF)` anyway.

> *"On a FLASH-backed DY-SV5W module (device code 0x02), this commanded the module
> to switch to a storage medium that does not exist. The module silently attempted
> SD enumeration, failed, and stopped responding to UART query commands. Play,
> stop, and volume commands still worked (the module recovered its play queue
> internally), so audio functioned normally but `queryModuleState()` returned zero
> bytes on every call -- keeping `link_ok = false` and `Module link: No response`
> permanently in the status card."*

The diagnostic value of this is out of proportion to the fix:

> [!IMPORTANT]
> **"Queries return nothing" plus "playback works" means a confused module, not a
> broken RX wire.** That is the lesson file's own prevention rule 3, and it is the
> single most useful debugging heuristic this module has. A dead RX path gives you
> silence on the wire; a bad init command gives you a module that plays and will
> not talk.

The fix has three parts and all three matter: use the detected device; **skip the
command entirely when detection failed** rather than guessing; and keep the file
header honest about it. Two later commits tried a fourth part -- `364cd9ff`
*"force FLASH switchDrive when pre-init queries fail"* -- and `50e72c76` reverted
it. **Guessing a drive is worse than sending nothing**, twice established.

### 14.2 The four-frames-per-play regression

**2026-03-21, commit `23957bb9`**, the rewrite that produced today's driver:

> *"prior driver sent 4 frames per play command -- two using opcode `0x06` which is
> 'next track' in DY-SV5W UART mode, producing random playback. Stop sent `0x03`
> (pause) then `0x02` (resume), cancelling itself out."*

Three separate errors in one driver: the wrong opcode for play, a self-cancelling
stop, and a dual-dialect frame wrapper emitting both an end-marker form and a
checksum form. All three came from writing to a protocol nobody had read.
`src/drivers/audio_dy_sv5w.cpp:369` still carries the warning in the code:
*"WARNING: `0x02` = play/resume, `0x03` = pause. Neither is stop!"*

### 14.3 The end-marker dialect that never existed

The same rewrite killed the belief that a frame ends with `0xAB`. It does not.
`AA 01 00 AB` is `0xAA + 0x01 + 0x00`, and every other frame's final byte is the
same arithmetic. The coincidence survived in the test suite after the driver was
fixed (Section 12.4) and is now explicitly commented as a coincidence in three
files.

This is the incident [`dfplayer-mini-sound.md`](dfplayer-mini-sound.md)'s opening
CAUTION cites when it tells a DFPlayer driver author to *"derive every constant
from the rule, assert it in a native test, and never trust a printed byte
sequence"*. **It happened here first.**

### 14.4 What is actually validated on hardware

From `CHANGELOG.md`'s `Hardware Validated` block and
`tasks/phase4_hardware_validation_deferral.md`:

> *"DY-SV5W audio output on Artoo PCB: named sounds, random chatter,
> play/stop/volume, module status card, volume NVS persistence confirmed"*
> (2026-03-22)

`docs/status.md` says the same in operator language: *"The DY-SV5W audio module
has also been confirmed on hardware for playback and volume control."*

**Not validated**, and named as deferred in
`tasks/phase5_hardware_validation_deferral.md`: *"DY-SV5W audio edge cases
(HW1.5-HW1.14) -- open but not blocking merge; module was not installed during the
Phase 5 bench session"*, plus the disconnected-module `No response` badge check.
Every P4 / FireBeetle 2 claim in Section 12.1 is **read from the code and the pin
map, not measured** -- this module has never been run on that board. Open Item 2.

## 15. How the hobby drives this module (non-normative)

### 15.1 BetterDuino: the reference our driver was aligned to

`~/Documents/Astromech/BetterDuinoFirmwareV4`, `MDuinoSoundDYPlayer`, behind
`#define INCLUDE_DY_PLAYER // DY-SV5W audio board`. Its `sendCommand()` is
byte-for-byte the algorithm we ship, `delay(100)` included. Commit `23957bb9`'s
message names it and `SnijderC/dyplayer` together as the sources the rewrite was
checked against.

Two things it does that we do not:

- **The 9x25 bank model in arithmetic:** `CalcSoundNr = (BankNr - 1) * 25 +
  SoundNr`, which is MarcDuino's convention (bank 1 = 1-25, bank 2 = 26-50, ...)
  computed on the host and sent as a flat index. protoArtoo does the same mapping
  through NVS category ranges instead, which is the same idea made configurable.
- **`Quiet()` sends `AA 10 00`** -- the `0x10` "stop playing" opcode, not `0x04`.
  Worth noting because its `Quiet(const bool on)` parameter is **never read**: the
  function sends the same frame whichever way you call it. A small, real defect in
  the reference, and a reminder that "the reference does X" is not the same as "X
  is right".

Its volume ladder is the clearest corroboration of the ascending scale: `Max = 30`,
`Mid = 15`, `Min = 5`, `Off = 0`, with `VolumeUp()` **adding** 2.

### 15.2 Padawan360: the same numbers survive a change of module, and the volume does not

`~/Documents/GitHub/Padawan360_mega_maestro_DYSV5W` exists to swap the sound
module -- *"This sketch is to beta test the DY-SV5W audio player instead of
sparkfun mp3"* -- and every call site keeps the old one commented directly above:

```cpp
      //mp3Trigger.play(21);
    player.playSpecified(21);
```

**The track numbers did not change when the module did.** That is the ecosystem's
own statement that a sound *number* is a property of the droid's card, not of the
module, and it is why Section 10.2's enumeration-order index is a real operator
hazard rather than an academic one.

> [!WARNING]
> **Its volume control runs backwards, and the comment is why.** At `:123`:
>
> ```cpp
> // Default sound volume at startup
> // 0 = full volume, 255 off
> byte vol = 25;
> ```
>
> That comment describes the **MP3 Trigger's inverted 0-255 register**. The code
> below it clamps to `0..30` and, on D-pad **Up** labelled *"volume up"*, does
> `vol--` and calls `player.setVolume(vol)`. On a DY-SV5W, where 0 is silent,
> **pressing volume-up makes the droid quieter.** Both sketches in that repository
> have it.
>
> This is not a criticism of that project; it is the warning for ours. **Volume
> semantics do not survive a module swap, and a stale comment is how the old scale
> gets carried forward.** protoArtoo's answer is structural -- the interface
> normalises 0-30 and each driver owns its own conversion -- and Section 17 is
> this sheet checking whether our own comments have stayed honest.

Its observable vocabulary: named one-shots at 1-12 (5 = Leia), 21, 52/53 as
drive-enable/disable chirps, and random ranges `random(13,17)`, `random(17,25)`,
`random(32,52)`.

### 15.3 `SnijderC/dyplayer`: the library everyone uses, written for a different module

The most-used DY library, and the one Padawan360 includes. Its own file header
says what it is:

> *"Abstraction of basic features of the **DY-SV17F** mp3 player board ... Instead
> of DY-SV17F I will from here on refer to it as the 'module'."*

Its README lists DY-SV5W as *tested* -- but also says *"I only have the DY-SV17F
in my possession to test at the time of writing"*, and issue #1 is a DY-SV5W and
DY-SV8F owner reporting *"None of the examples didn't work with my modules"*, with
the README's own module table found to be wrong about which model it names.

**Use it as a protocol reference, not as a DY-SV5W authority.** Its command codes
and precomputed checksums match the DY-SV5W datasheet exactly and are excellent
corroboration; its module-specific behaviour is DY-SV17F behaviour, and that is
where the 1 kOhm and 10 kOhm resistor advice in Section 5.2 comes from.

### 15.4 ... and one of its opcodes is provably wrong

Read this session, current `master`:

```cpp
  void DYPlayer::interludeSpecified(device_t device, uint16_t number)
  {
    uint8_t command[6] = {0xaa, 0x0b, 0x03, 0x00, 0x00, 0x00};
```

`0x0B` is **Switch Specified Drive**, whose payload is one byte. The datasheet
assigns *"Specified song to be interplay"* to **`0x16`**, with exactly this
three-byte `<drive> <SN_H> <SN_L>` payload. The library sends a drive-switch
opcode carrying an interlude payload.

Issue #1 lists *"Specified song interplay (`AA 16 03 Drive S.N.H S.N.L SM`)"*
among the commands the library was missing, so the most likely history is that the
method was added later against the right documentation and typed the wrong
constant.

> [!NOTE]
> **This is the clearest possible argument for Section 12.4.** The library is
> popular, actively maintained, correct in 20-odd other opcodes, and wrong in this
> one -- and there is no way a caller would find out, because the command returns
> nothing (Section 7.4). A native test asserting the byte against the datasheet's
> printed frame would have caught it in a second. Ours would.

### 15.5 `Lenoirio/dy-sv5w`: the one written for this module

A Rust `no-std` crate with the datasheet committed alongside it. Independent
value, beyond corroborating every opcode:

- It reaches the **same response-validation design as ours** without having seen
  ours -- check `0xAA`, check the command byte, check the length byte, read the
  payload, and ignore the trailing checksum (`// ignore CRC for now`).
- It states the 3.3 V I/O level plainly (Section 5.2).
- It carries the USB-kills-UART warning (Section 5.6).
- It confirms the enumeration-order index in the author's own words (Section 10.2).
- It is honest about `0x19`: *"The effect of this configuration is unclear for
  now"*, which is a better answer than a guess.

### 15.6 Negative results across the ecosystem

Recorded so nobody searches twice:

- **ShadowMD** (`~/Documents/GitHub/ShadowMD`): no sound hardware at all. It
  delegates to MarcDuino and speaks MP3 Trigger file numbers.
- **AstroPixelsPlus** (our own dome fork): drives a **DFPlayer** through
  `MarcduinoSound.h`. Nothing DY.
- **CHIRP**: a different tier entirely (Section 16).
- **MarcDuino** itself: MP3 Trigger and DFPlayer. The DY-SV5W is not in its
  vocabulary; what it contributes is the **9x25 bank convention** that every
  project, this one included, numbers cards by.
- **r2d2-astromech-simulator**: offers `DY-SV5W` as a Sound answer whose wiring
  note reads *"Serial0 via DYPlayerArduino. 30 is loudest. Watch out: the sketch's
  own `Serial.println()` shares this UART."* -- and then prints the contradiction
  *"sound -- this sketch drives a MD-YX5300"* because the bundled sketch does
  (`tasks/research-r2d2-sim-2026-09-09-wiring.md`). *"30 is loudest"* is
  independent third-party agreement on the scale direction.

## 16. How the four Sound members differ

| | **DY-SV5W** | MP3 Trigger | CHIRP | DFPlayer Mini |
| --- | --- | --- | --- | --- |
| Status | **`supported`, default** | `supported` | `supported` | `roadmap` |
| Registry value | **18** | 19 | 20 | 21 |
| Transport | **binary, 9600 fixed** | binary, 9600 (38400 factory) | ASCII, configurable | binary, 9600 |
| Frame | **`AA CMD LEN .. SM`, 4+n B** | 2 bytes | ASCII lines | fixed 10 B |
| Checksum | **sum, 1 byte** | none | none | 16-bit negated sum |
| Volume native | **0-30 ascending -- no scaling** | 0-255 **inverted** | 0-99 | 0-30 |
| Simultaneous streams | **1** | 1 | **3+, mixed** | 1 |
| Decoding | **hardware** | VS1053 | software, RP2350 | hardware |
| Addressing | **index, enumeration order** | file-name prefix | catalog + bank/page | index *or* filename |
| Play-state query | **yes (`0x01`)** | **no** -- *"always shows unknown"* | yes | yes |
| Device-type query | **yes (`0x09`/`0x0A`)** | no | yes | yes |
| Safe to query while playing | **no** | no | **yes** | untested |
| Track-finished event | no | no | no | **pushed** |
| Hardware busy pin | **yes, polarity unresolved** | no | no | yes |
| On-board amplifier | **yes, ~3 W real** | no | no | yes, <3 W |
| On-board storage | **often, 4-8 MB flash** | no | **yes, flash bank** | some clones |
| Catalog support | no | no | **yes** | no |
| Board identity | **one board, many resellers** | fixed vendor part | fixed vendor part | **unreliable** |
| Proven on our hardware | **yes, 2026-03-22** | no | yes | no |

**These are peers with genuinely different shapes.**

**What the DY-SV5W uniquely brings** is the combination that makes it the default:
5 V power with 3.3 V logic so an ESP32 needs no level shifting, an amplifier on
board, a full status vocabulary (four of the six capability bits, more than any
member except CHIRP), a protocol whose vendor documentation is arithmetically
correct, storage that can be on the board itself, and -- uniquely in this family --
**a hardware validation record on our own droid**.

**What it uniquely costs** is the enumeration-order index (Section 10.2), status
that cannot be polled during playback (Section 13.2), and no mixing. A droid that
wants an ambient bed under speech needs CHIRP and always will.

> [!NOTE]
> **The DFPlayer sheet argued its module is the cheapest with an amplifier
> included. The DY-SV5W is the same argument with better electrical manners and
> five years of astromech field use behind it.** What the DFPlayer has that this
> does not is a **pushed track-finished event**, which is the one status fact this
> module cannot give us without polling -- and polling is the thing it does not
> like. If a future protoArtoo wants "tell me when the sound ends", the DY-SV5W's
> only answer is the `BUSY` pin (Section 5.5, Open Item 1).

## 17. Findings against the shipping implementation

Six things this research turned up in our own code and docs. Three were fixed in
the change that carries this sheet; three are reported here because fixing them is
a decision rather than a correction.

### 17.1 FIXED -- the driver's file header prints a wrong checksum

`src/drivers/audio_dy_sv5w.cpp:11` documented:

```
//            vol 15 = AA 13 01 0F C7  (AA+13+01+0F = C7)
```

`0xAA + 0x13 + 0x01 + 0x0F = 170 + 19 + 1 + 15 = 205 = 0xCD`. The arithmetic is
printed **beside the wrong answer**, which is how it survived. The code is
correct, `test_audio_io_seam.cpp:170` asserts `0xCD`, and only a developer copying
the comment would have been wrong. The other two worked examples in that header
(`AA 02 00 AC`, `AA 04 00 AE`) verify.

### 17.2 FIXED -- two comments in the driver describe behaviour that changed

- `:275` said `queryModuleState()` is *"Called by AudioTask after begin() and then
  periodically every ~2 s."* Both halves are stale. The auto-poll interval is
  `AUDIO_STEP_AUTO_QUERY_INTERVAL_MS = 10000`, and it is **gated on
  `AUDIO_CAP_QUERY_SAFE_PLAYING`, which this driver does not declare** -- so for
  the DY-SV5W there is no periodic query at all, only the operator's Poll button.
  The 2 s figure is from commit `e16f4ae2`, before the capability gate existed.
- `:346` said *"we ... clamp to 255 for DY-SV5W modules"*. Nothing clamps; the full
  16-bit index goes on the wire, which is correct and which the frame tests
  assert.

### 17.3 REPORTED -- the registry's protocol token names a transport, and one board contradicts it

Row 18 declares the Component Protocol `soft_uart_binary`. `CONTEXT.md`'s own
definition of the term rejects that shape explicitly:

> **Component Protocol**: *"The wire contract firmware speaks to a Component
> Member ... _Avoid_: driver (that names the code, and implies one per family),
> backend, **transport (that names the wire, not the contract spoken over it)**"*

`soft_uart_binary` names the wire. Its three siblings do not -- `mp3trigger_serial`,
`chirp_ascii_uart` and `dfplayer_serial` all name the contract. And it is
**factually wrong on one of two boards**: on FireBeetle 2 the transport is a
hardware UART with `PA_CAP_DEDICATED_AUDIO_UART = 1`, no bit-bang anywhere.

The token is operator-visible -- it travels in the identity manifest and reaches
the browser -- so renaming it is a decision with a compatibility question attached,
not a typo fix. `dy_uart` or `dy_serial` would match the siblings. **Not changed
here.**

### 17.4 REPORTED -- we never check the reply checksum

`sendQuery()` validates `0xAA`, the command byte and the length byte, and then
uses the payload. The trailing `SM` byte is read and discarded.

Given Section 9.2 -- a module that volunteers bytes mid-playback -- the three
header checks are doing the real work, and `Lenoirio/dy-sv5w` independently made
the same call (*"ignore CRC for now"*). But `SnijderC/dyplayer` does validate it,
it costs one loop over four or five bytes, and Section 7.3 shows the checksum is
weak rather than useless. A corrupted payload byte in a reply is currently
believed.

Cheap to add, testable natively with the existing recording IO, and it is a
behaviour change to a shipping driver rather than a correction. **Not changed
here.**

### 17.5 REPORTED -- `begin()`'s queries are not arbitrated, and on artoo-esp32 they probably lose a race

Every query in `audio_task.cpp` is wrapped in `audioUartClaim()` **except the four
inside `begin()`**, which is called bare at `audio_task.cpp:657`.

On artoo-esp32 the sequence reasons out like this, read from the code this
session:

| Step | Where |
| --- | --- |
| AudioTask created, Core 0, priority 3 | `main.cpp:583` |
| DomeLinkTask created, **Core 1**, priority 3 | `main.cpp:603` |
| `AudioDriverDySv5w::begin()` opens UART2 RX-only on `PIN_AUDIO_RX`, then **sleeps 1500 ms** | `audio_dy_sv5w.cpp:165-170` |
| DomeLinkTask's `acquireDomeUart()` does `end()` + `begin(9600, PIN_DOME_RX, PIN_DOME_TX)` on the **same controller** and sets its owner flag | `dome_link.cpp:164-176, 789` |
| `begin()` wakes and sends its four queries -- to a controller now listening on the dome's pin | `audio_dy_sv5w.cpp:184-263` |

The two tasks are on different cores, so they run concurrently, and the 1.5 s sleep
makes it very likely DomeLinkTask's init wins. If it does, all four `begin()`
queries return zero bytes: device detection fails, `switchDrive` is skipped (which
is at least the safe branch -- Section 14.1), `m_totalTracks` stays 0, and the
cached state the Sound page seeds from says `linkOk = false`.

> [!NOTE]
> **This is reasoned from code, not measured**, and it is stated that way
> deliberately. Two things argue against it being a live defect: the hardware
> record in Section 14 shows `begin()`'s pre-init queries *working* on this board,
> and the driver's behaviour is safe either way -- the operator gets a cached
> "no response" they can clear with the Poll button, not a droid that will not
> play. Two things argue for looking: that record predates the current transport
> arrangement, and the shape (init queries with no claim) is exactly what the
> arbiter exists to prevent. Open Item 2 names the one-boot check that settles it.
>
> **FireBeetle 2 is unaffected** -- `audioUartClaim()` compiles to `return true`
> and nothing is shared.

### 17.6 FIXED -- `docs/sound_playback.md` documented a file that does not exist and a protocol that never did

Section 2.1 of that document named `src/drivers/audio_soft_uart.cpp` -- renamed to
`audio_dy_sv5w.cpp` by commit `9d6b8d03` -- and described the frame format as
`0xAA [CMD] [LEN] [DATA...] 0xAB`, the **end-marker dialect Section 14.3 proved
does not exist**. It also carried a hardware-validation warning that had been
discharged. Corrected to point at this sheet.

## 18. Agent Lookup Quick Reference

- Field: Baud. Required value: **9600, 8-N-1, full duplex, fixed.** No command changes it.
- Field: Frame. Required value: **`0xAA CMD LEN DATA.. SM`**, total `4 + LEN` bytes. No start-of-frame escape, **no end marker**.
- Field: Checksum. Required value: **`sum(all preceding bytes) & 0xFF`**, including the `0xAA`. Compute it; never copy a printed byte.
- Field: `0xAB`. Required value: **not a footer.** It is `0xAA + 0x01 + 0x00`, the checksum of the play-state query.
- Field: Play a numbered track. Required value: **`0x07`**, `AA 07 02 <hi> <lo> SM`, 16-bit big-endian, 1-based. Track 1 = `AA 07 02 00 01 B4`.
- Field: Play opcode to avoid. Required value: **`0x06` is NEXT TRACK**, not play. It shipped once and produced random playback.
- Field: Stop. Required value: **`0x04`** -> `AA 04 00 AE`. `0x03` is pause, `0x02` is resume; sending both is a no-op.
- Field: Second stop opcode. Required value: `0x10` -> `AA 10 00 BA` ("stop playing" / stop interlude). Not what we send.
- Field: Volume. Required value: **`0x13`, range 0-30 ASCENDING, 0 = silent, module default 20.** Identical to `AudioDriver`'s normalised range -- **no scaling**.
- Field: Volume 15. Required value: `AA 13 01 0F` **`CD`**. (Not `C7`; see Section 17.1.)
- Field: EQ. Required value: `0x1A`, 0 NORMAL / 1 POP / 2 ROCK / 3 JAZZ / 4 CLASSIC. NORMAL = `AA 1A 01 00 C5`.
- Field: Play mode. Required value: `0x18`, 0-7. **Power-on default is `02` single-stop**, which is what a droid wants; we never send it.
- Field: Switch drive. Required value: `0x0B`, `AA 0B 01 <drive> SM`. **Use the value `0x09` reported. Never hardcode. If detection failed, send nothing.**
- Field: Device codes. Required value: **`00` USB, `01` SD/TF, `02` FLASH, `FF` none** -- identical to `AudioModuleState.device`, no translation.
- Field: Play-state codes. Required value: **`00` stop, `01` play, `02` pause** -- identical to `AudioModuleState.playState`.
- Field: Query play state. Required value: `AA 01 00 AB` -> `AA 01 01 <state> SM` (5 bytes).
- Field: Query online drive. Required value: `AA 09 00 B3` -> `AA 09 01 <drive> SM` (5 bytes).
- Field: Query play drive. Required value: `AA 0A 00 B4` -> `AA 0A 01 <drive> SM` (5 bytes).
- Field: Query track count. Required value: `AA 0C 00 B6` -> `AA 0C 02 <hi> <lo> SM` (6 bytes).
- Field: Query current track. Required value: `AA 0D 00 B7` -> `AA 0D 02 <hi> <lo> SM` (6 bytes).
- Field: Reply validation. Required value: **check `0xAA`, the command byte AND the length byte.** The module volunteers bytes during playback.
- Field: Acknowledgement. Required value: **none exists.** Control and setting commands return nothing, ever.
- Field: Inter-command delay. Required value: **100 ms** after every frame (ecosystem practice, no vendor figure).
- Field: Power-on delay. Required value: **1500 ms** before the first command, then drain RX (no vendor figure).
- Field: Query timeout. Required value: **300 ms** per query in our driver; module reply latency is unspecified.
- Field: DIP for UART mode. Required value: **`CON3=1, CON2=0, CON1=0`** -- on the physical switch, **`1`=OFF, `2`=OFF, `3`=ON**. Latched at power-on only.
- Field: Supply. Required value: **5 V**. Logic on every pin including UART: **3.3 V**. No level shifter needed for an ESP32.
- Field: Amplifier. Required value: marked 5 W; **~3 W is the arithmetic ceiling** into 4 ohm from 5 V, bridge-tied. Speaker pads are BTL -- never into an amplifier input; use the 3.5 mm jack for line level.
- Field: `BUSY` polarity. Required value: **UNRESOLVED.** Pin table says LOW-while-playing; four mode blocks say HIGH-while-playing. Do not assert one (Open Item 1).
- Field: Track addressing. Required value: **the module's enumeration order, not the file name.** Copy files onto an empty card in order; keep numbering contiguous.
- Field: Card. Required value: **FAT32, 32 GB maximum**, MP3 or WAV, no hidden files. Some boards play from **on-board flash instead** -- ask with `0x09`.
- Field: Capability word. Required value: **`0x0F`** -- STATUS_QUERY, DEVICE_TYPE, TRACK_COUNT, CURRENT_TRACK. **No `QUERY_SAFE_PLAYING`: do not poll during playback.**
- Field: protoArtoo identifiers. Required value: registry value **18**, id `dy_sv5w`, name `DY-SV5W`, protocol token `soft_uart_binary`, build default `PA_AUDIO_DRIVER = AUDIO_SOFT_UART`.

## 19. Open Items

| # | Item | How to settle it |
| --- | --- | --- |
| 1 | **`BUSY` polarity, and its assertion latency** (Section 5.5) | One wire to a spare GPIO or a scope probe; play a track of known length and log the transitions. Settles a contradiction no document can. Worth doing if we ever want play-state without serial traffic |
| 2 | **`begin()`'s unarbitrated queries on artoo-esp32** (Section 17.5) | One boot with the dome link on serial: does the log say `pre-init: device online = ...` or `no response`? If the latter, either claim the UART around `begin()` or move detection to the first poll |
| 3 | **One-Line mode's real DIP setting** (Section 6.3) | Set `1 0 0`, wire IO4 only, send `0x01 0x0B 0x11` as one-line bits, hear whether track 1 plays. Only matters if we ever want the single-wire mode |
| 4 | **Does the micro-USB port enumerate as storage?** (Section 5.6) | Plug our module into a PC and look. Board-revision dependent; affects card-preparation advice only |
| 5 | **`0x0E` / `0x0F` / `0x10` semantics** (Section 8.1) | Datasheet says "previous file / next file / stop playing"; `dyplayer` says "previous directory first/last sound / stop interlude". Send each with a multi-folder card and observe |
| 6 | **Idle and playing current draw** (Section 5.2) | Inline ammeter at 5 V, silent and at `setVolume(30)` into the fitted speaker. The ~0.7-1.0 A figure is derived, not measured, and a shared 5 V rail is a real droid design input |
| 7 | **Does EQ persist across power cycles?** | No query exists to read it back. Set ROCK, power-cycle, listen. Decides whether `begin()`'s `0x1A` is insurance or ceremony |
| 8 | **The three magic numbers: 1500 ms boot, 100 ms inter-command, 300 ms query timeout** | None is vendor-stated. All three have worked since 2026-03. Only worth measuring if a symptom points at one |
| 9 | **exFAT** | Format a 32 GB card exFAT and see whether `0x09` reports a device. Expected: no. Worth one test because "no card" and "module missing" look the same to an operator |
| 10 | **Compare `0x0C` against the configuration at boot** (Section 10.4 rule 5) | Firmware change, not a measurement: log or surface a mismatch between the module's track count and the highest configured track. Cheapest possible detection of a card rebuilt wrong |
| 11 | **Play-by-path (`0x08`) as an escape from enumeration order** (Section 10.5) | Prototype against a card with 8.3 names; measure whether name-stable addressing is worth the frame and naming cost |
| 12 | **Interlude (`0x16`) versus the Sound Bed** (Section 11.5) | Read the vendor's "level 1 only" note against `CONTEXT.md`'s Sound Bed before anyone proposes this module can mix. Expected answer: it cannot |
| 13 | **FireBeetle 2 / ESP32-P4 hardware run** (Section 14.4) | Everything in Section 12.1 about that board is read from code. The module has never been run on it |
| 14 | **Phase 5 audio edge cases HW1.5-HW1.14** | Already scoped in `tasks/phase5_hardware_validation_deferral.md`; the module was not installed during that bench session |

## 20. Sources

**Primary -- the module**

- **DY-SV5W Voice Playback Module Datasheet**, 4 pages, undated content, PDF
  created 2019-06-06 in Microsoft Word by a third party (Section 2.1). Local copy:
  `~/Downloads/DY-SV5W ModuleDatasheet.pdf`. **No text layer** -- rendered with
  `pdftoppm -r 150 -png` and read as images. Also served by GroboTronics and
  committed into `Lenoirio/dy-sv5w`.
- `tasks/DY-SV5W-Module-Datasheet.md` -- this project's own transcription of that
  PDF's tables, checked against the rendered pages this session (local, untracked).

**Primary -- source code, read in full this session**

- `SnijderC/dyplayer` -- `src/DYPlayer.h`, `src/DYPlayer.cpp` (master), plus the
  README and issue #1. Every opcode, the precomputed checksums, the device enum,
  the by-path encoder, and the wrong `0x16` (Section 15.4).
  https://github.com/SnijderC/dyplayer
- `Lenoirio/dy-sv5w` -- `src/lib.rs` and README. The DY-SV5W-specific Rust crate.
  https://github.com/Lenoirio/dy-sv5w
- `~/Documents/Astromech/BetterDuinoFirmwareV4` -- `src/MDuinoSound.cpp:373-507`,
  `include/MDuinoSoundDYPlayer.h`, `include/config.h:26`. The reference our driver
  was aligned to.
- `~/Documents/GitHub/Padawan360_mega_maestro_DYSV5W` -- both sketches
  (`..._PWM.ino` and `..._BETA.ino`).

**Read locally, on this disk**

- `~/Documents/GitHub/ShadowMD`, `~/Documents/GitHub/AstroPixelsPlus`,
  `~/Documents/GitHub/CHIRP` -- negative results (Section 15.6).
- `tasks/research-r2d2-sim-2026-09-09-wiring.md` and
  `tasks/research-r2d2-astromech-simulator-2026-09-09.md` -- the simulator's
  DY-SV5W option note and the contradiction it prints.

**protoArtoo**

- `src/drivers/audio_dy_sv5w.cpp`, `include/audio_dy_sv5w.h`,
  `include/audio_driver.h`, `include/audio_serial_io.h`,
  `src/drivers/audio_soft_uart_tx.h`, `src/tasks/audio_task.cpp`,
  `src/tasks/audio_task_step.cpp`, `src/tasks/audio_sound_member.cpp`,
  `src/tasks/dome_link.cpp`, `include/dome_link.h`, `src/component_registry.cpp`,
  `include/component_registry.inc`, `include/config.h`, `src/main.cpp`
- `test/test_native/test_audio_frames/`, `test_audio_io_seam/`,
  `test_component_registry/`, `test_audio_sound_member/`
- `docs/sound_playback.md`, `docs/pin_map.md`, `docs/api.md`, `docs/goal.md`,
  `docs/status.md`, `CONTEXT.md`, `CHANGELOG.md`, ADR 0042
- `tasks/lessons.md` (three DY-SV5W entries),
  `tasks/phase4_hardware_validation_deferral.md`,
  `tasks/phase5_hardware_validation_deferral.md`,
  `tasks/phase5-bench-session-2026-05-07-08.md`
- `git log --follow src/drivers/audio_dy_sv5w.cpp` -- 24 commits, read for the
  bodies quoted in Section 14
- [`dfplayer-mini-sound.md`](dfplayer-mini-sound.md), the sibling Sound sheet

**Third party, ecosystem**

- Envistia Mall, *DY-SV5W Voice Playback MP3 Music Player Amplifier Module User
  Guide* -- an independent reproduction of the mode table and the `BUSY` row.
- `playfultechnology/arduino-audio` -- the DIP-switch-versus-resistors difference
  against the DY-SV17F.
- Reseller listings (ICStation, TinyTronics, GroboTronics, Amazon, eBay, Walmart,
  Alibaba) for availability, dimensions, temperature range and formats.

> [!NOTE]
> **Negative results, recorded so nobody repeats them.** There is no vendor
> product page, no SKU, no errata and no support channel for this module -- the
> four-page PDF described in Section 2.1 is the whole of its documentation, and it
> is a third party's Word file. Two direct fetches of a second copy of that PDF
> (`grobotronics.com`, `shop.cpu.com.tw`) returned HTTP 403 to a scripted client
> this session. No clone-behaviour database of the `DFPlayerAnalyzer` kind exists
> for the DY family, and none appears to be needed. `SnijderC/dyplayer`'s README
> is **not** a DY-SV5W authority despite listing it as tested -- the library is
> written for the DY-SV17F and says so in its own header.
