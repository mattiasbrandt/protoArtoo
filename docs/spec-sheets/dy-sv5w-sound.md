# DY-SV5W Spec Sheet

The **DY-SV5W** is a voice playback module: a 40 x 40 mm board with a hardware
MP3/WAV decoder, a microSD slot, often an on-board flash chip, a Class-D
amplifier, and a 9600-baud binary serial protocol with a one-byte checksum. It is
a generic module from a Shenzhen house, sold under many reseller brands, and the
astromech hobby uses it as a drop-in sound board for MarcDuino-style numbered
sound cards.

Research date 2026-09-12. Every frame byte, command code, electrical value and
default below was read from the module's own datasheet (a **scanned 4-page PDF
with no text layer**, rendered to images and read page by page), from three
independent driver implementations read in full, from bench measurements of a
module over its UART, or from the astromech projects that drive it. **Every
checksum in this document was computed, not copied.** Claims that could not be
sourced are marked `UNKNOWN` with the artefact or bench test that would settle
them.

> [!CAUTION]
> **The vendor documentation contradicts itself about the `BUSY` pin four times
> in four pages, and gives UART mode and One-Line mode the same DIP setting.**
> Both are verified below against the document's own pages (Sections 4.5 and
> 5.3). A host that wires neither `BUSY` nor One-Line is unaffected, and both are
> exactly the trap a builder falls into first.

> [!IMPORTANT]
> **The good news is unusually good, and it is the opposite of the DFPlayer's.**
> All **17** worked byte sequences printed in the DY-SV5W datasheet's command
> tables verify against the document's own stated checksum rule -- computed,
> zero mismatches (Section 6.2). The DFPlayer's datasheet got **three out of
> three wrong**. Where the DFPlayer sheet's rule is *"never trust a printed byte
> sequence"*, this module's printed bytes are trustworthy and its **prose** is
> not.

## 1. Scope

Covers the board and what is on it, the electrical contract and pinout, the DIP
mode table, the serial frame and its checksum, the full command and query set
with every checksum computed, the timing the datasheet does not state, what the
module returns and what it volunteers, the storage and file-numbering contract,
how the astromech hobby drives this module, and how it differs from three other
sound boards the hobby uses.

Does not cover: audio file encoding and bitrate selection; the I/O trigger modes
in operational detail beyond the mode table (a droid drives this over UART); the
One-Line single-bus protocol beyond recording its command table and bit encoding
(its DIP row is ambiguous); the `0x08`/`0x17` play-by-path commands beyond their
frame shape and encoding; or the DY family siblings except where a library
written for one of them is quoted (Section 10.3).

## 2. What you are actually buying

A 40 x 40 x 9 mm blue PCB, silkscreened **`SV5W`**, carrying:

| On the board | What it is | Consequence |
| --- | --- | --- |
| A **12-way 2.54 mm header** down one edge | `5V+`, `5V-`, `TXD/IO0`, `RXD/IO1`, `IO2`, `IO3`, `IO4/ONE_LINE`, `IO5`, `IO6`, `IO7`, `BUSY`, `GND` | Section 4.1 |
| A **3-position red DIP switch**, silked `ON` / `1 2 3` | CON1, CON2, CON3 -- the mode select | Section 5, and **no external resistors needed** (Section 4.2) |
| A **micro-USB socket**, silked `DownLoad` | Datasheet annotates it *"USB DownLoad MP3 File"* | Section 4.6 -- and it will stop UART mode |
| A **TF (microSD) slot**, annotated *"TF Card 32G Bit"* | The removable storage | Section 9.4 |
| A **blue trimmer potentiometer**, annotated *"Volume Adjustment"* | Analogue master volume, in series with the digital one | Section 4.3 |
| An **8-pin SOIC** by the speaker pads, annotated *"5W Amplifier IC"* | The Class-D output stage | Section 4.4, where the "5 W" is checked with arithmetic |
| A **2-pad `Speaker` terminal** | *"4ohm 3~5W Speaker"* | Bridge-tied. Section 4.3 |
| A **3.5 mm jack** | *"3.5mm Audio Output"* | Line level for an external amplifier |

> [!NOTE]
> **Unlike the DFPlayer Mini, the identity of this part is not in doubt.** The
> DFPlayer sheet's dominant risk (`dfplayer-mini-sound.md`, 'What you are
> actually buying') is *"a 16-pin form factor, not a part"*, filled by at least
> eight unrelated silicon families that behave differently. Nothing comparable
> was found for the DY-SV5W. It is sold under many reseller brands (JESSINIE,
> Pzhoais, Diann and others on Amazon; ICStation; TinyTronics; GroboTronics;
> Banggood; a wall of Alibaba listings) but they are **the same board with the
> same silkscreen and the same DIP switch**, and no clone-behaviour database of
> the `DFPlayerAnalyzer` kind exists because nobody has needed one.
>
> What it lacks instead is **a vendor**. There is no DFRobot-equivalent product
> page, no SKU, no errata, and no official support channel: it is a generic
> module from a Shenzhen house, and the closest thing to authoritative
> documentation is the four-page PDF described below.

### 2.1 The datasheet is a Word document somebody typed up

The PDF's own metadata, read with `pdfinfo`:

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
- **Its command tables are arithmetically perfect** (Section 6.2). Whoever
  retyped it did not introduce a single checksum error across 17 worked
  examples, which is a better record than DFRobot's own typesetting.

### 2.2 Availability

Checked 2026-09-12: **widely and continuously available**, in stock at Amazon,
eBay, Walmart, ICStation, TinyTronics, GroboTronics and Alibaba, from many
independent sellers, typically in the USD 3-8 band single-unit depending on
channel. No shortage signal, no end-of-life notice, no single vendor whose stock
state matters.

**This is the exact inverse of the Pololu Maestro's situation and of the
DFPlayer's**: there, one vendor's stock field was the fact to get right. Here
there is no vendor to check, and the thing that makes it always purchasable is
the same thing that makes it undocumented.

> [!NOTE]
> **Negative result, recorded so nobody repeats it.** Two independent attempts to
> fetch a second copy of the datasheet PDF directly (`grobotronics.com`,
> `shop.cpu.com.tw`) returned HTTP 403 to a scripted client. A local copy plus
> the reseller guides and library repositories were used instead.

## 3. Sources Checked

| Source | How it was taken | What it gave |
| --- | --- | --- |
| **DY-SV5W Voice Playback Module Datasheet**, 4 pages | `pdftoppm -r 150 -png`, then **read as four images**; it has no text layer | The pinout, the mode table, the frame format, all three command tables, the One-Line table and bit encoding, the defaults, the dimensions. And the four-way `BUSY` contradiction and the duplicated UART/One-Line DIP row |
| The same datasheet's **PDF metadata** | `pdfinfo` | Its provenance: a 2019 Microsoft Word document by a private individual, not a vendor publication (Section 2.1) |
| **`SnijderC/dyplayer`** `src/DYPlayer.h` + `src/DYPlayer.cpp` | fetched raw and read in full | Every command code with its precomputed checksum, the device enum with *"Onboard flash chip (usually winbond 32, 64Mbit flash)"*, `0-30` volume with *"Default volume if not set: 20"*, the by-path encoding rule, `combinationPlay`. And one wrong opcode (Section 10.4) |
| `SnijderC/dyplayer` README and issue #1 | fetched | The DY-SV5W listed as *tested*; *"The module will look for the first sound file found in the filesystem. It's not using the file name, neither does it order by file name"*; the `XY`/`ZH`/`DY` combination folder; the `1KOhm` series-resistor advice; and a reporter's *"UART mode (0-0-1)"*, which is Section 5.2's trap seen from the other side |
| **`Lenoirio/dy-sv5w`** (Rust) `src/lib.rs` + README | fetched and read in full | An independent implementation written against this module, with the reply validation Section 8.2 describes; *"the level for I/O-pins (including the UART) is 3.3 V"*; *"The device can't operate as a USB storage-device"*; and the USB-kills-UART warning in Section 4.6 |
| **BetterDuino Firmware V4** `src/MDuinoSound.cpp`, `include/MDuinoSoundDYPlayer.h` | read from source | A complete astromech DY-SV5W driver: the same `sendCommand` algorithm, the `delay(100)` with its own comment *"Delay needed between successive commands"*, the 9x25 bank arithmetic, and the volume ladder (max 30, mid 15, min 5, off 0) |
| **Padawan360 (DY-SV5W port)** | read from source, both sketches | A line-for-line MP3 Trigger to DY-SV5W port with the track numbers unchanged -- and a volume control that runs backwards (Section 10.2) |
| ShadowMD, AstroPixelsPlus, CHIRP | read from source | Negative results: none of them drive a DY-SV5W. Section 10.6 |
| r2d2-astromech-simulator | read from source | The simulator's own DY-SV5W option note, and the contradiction it prints (Section 10.6) |
| Bench measurements of a DY-SV5W | the module's query replies and the host's serial log, over its UART | The FLASH drive report, the unsolicited bytes, the drive-select failure, the track-gap mis-play, and the start-up and inter-command timing that has worked (Sections 6.5, 8.2, 9.1-9.3) |
| Envistia Mall DY-SV5W guide; `playfultechnology/arduino-audio`; reseller listings | fetched | Corroboration of the mode table, the DIP-versus-resistor difference against the DY-SV17F, temperature range, availability |

**How the sources rank against each other.** Computation beats any printed byte
sequence, this sheet's included. The datasheet's **tables** beat its **prose**:
every contradiction found is prose-versus-table or prose-versus-prose, and no two
tables disagree. Where the datasheet contradicts itself, nothing is asserted and
the bench test is named (`BUSY`, Section 4.5). On this specific module,
`Lenoirio/dy-sv5w` outranks `SnijderC/dyplayer`: the Rust crate was written
against a DY-SV5W with the datasheet in the repository, and dyplayer's own header
says it is *"an abstraction of basic features of the DY-SV17F"* (Section 10.3).

## 4. Electrical

### 4.1 The pinout, from the datasheet's own table

All twelve pins are on one 2.54 mm header. **Function depends on the mode**
(Section 5); the UART-mode column is what a serial host uses.

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
| 11 | `BUSY` | *"Output low level signal(0V) when playing and output high(3.3V) after playing."* | **see Section 4.5** |
| 12 | `GND` | *"Ground."* | ground |

Separately, not on that header: the two `Speaker` pads, the 3.5 mm jack, the
micro-USB socket, the TF slot, the volume trimmer and the DIP switch.

### 4.2 Supply and logic level, and why an ESP32 needs no level shifter

| Parameter | Value | Source |
| --- | --- | --- |
| Supply | **DC 5 V** | Datasheet pin table (`5V+` / `5V-`); every reseller spec |
| Logic level on every I/O pin, **UART included** | **3.3 V** | Datasheet `BUSY` row (*"output high(3.3V)"*); `Lenoirio/dy-sv5w` README: *"Although the module needs +5V, the level for I/O-pins (including the UART) is 3.3 V"* |
| Operating temperature | **-20 to +85 C** | Reseller specification sheets, consistently |
| Dimensions | **40 x 40 x 9 mm** | Datasheet page 4, dimensioned photograph |
| Idle current | `UNKNOWN` | Not in any document found. Open Item 5 |
| Playing current | `UNKNOWN` measured; **~0.7-1.0 A at 5 V** is the arithmetic (Section 4.4) | Open Item 5 |

> [!IMPORTANT]
> **The 5 V supply and the 3.3 V signalling are the combination an ESP32 wants.**
> Feed the module 5 V for amplifier headroom and wire `TXD`/`RXD` straight to the
> ESP32 with **no level shifting in either direction**: the module's TX idles and
> drives at 3.3 V, which the ESP32 reads natively, and the ESP32's 3.3 V TX is a
> valid high at the module's 3.3 V input.
>
> This is the opposite of the DFPlayer's trade-off (`dfplayer-mini-sound.md`,
> 'Electrical'), where running the module at 3.3 V keeps the levels compatible
> **at the cost of amplifier power**, and running it at 5 V for volume forces a
> divider on its TX. Here you get both. It is the single best electrical property
> of this module.

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

### 4.3 Three ways the volume is set, and only one of them is on the wire

| Control | Where | Range | Who sets it |
| --- | --- | --- | --- |
| Digital volume | `0x13` over UART | 0-30, **default 20** | The host, over the UART |
| Analogue trimmer | Blue potentiometer on the board | mechanical | The builder, once, with a screwdriver |
| Amplifier vs line out | Which output you wire | -- | The builder, at build time |

The trimmer is in the analogue path and the UART volume is in the digital path,
so **they multiply**. A module that is too quiet at UART volume 30 has its trimmer
down; one that distorts at UART volume 10 has its trimmer up. Neither is visible
over the UART and neither is reported anywhere, so a host's 0-30 volume setting
is never the whole story.

**The two audio outputs are not interchangeable:**

- The **`Speaker` pads** are the Class-D amplifier's **bridge-tied** output for
  a *"4ohm 3~5W Speaker"*. Neither leg is ground-referenced. **Never wire either
  pad to an amplifier input or to ground** -- same hazard as the DFPlayer's
  `SPK1`/`SPK2`.
- The **3.5 mm jack** is the line-level DAC output for an external amplifier,
  and is the correct connection for a droid that already has one.

### 4.4 The "5 W" is a supply-rail impossibility, and the honest number is about 3 W

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
  problem, not a module fault. Derived, not measured -- Open Item 5.
- **A droid that needs to be heard across a hall needs the 3.5 mm jack and a real
  amplifier.** The on-board amplifier is a complete solution for a bench and a
  quiet room, and that is the same conclusion the DFPlayer sheet reaches about
  its own 3 W stage.

### 4.5 `BUSY`: the datasheet states both polarities, four times, in four pages

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

A host on the UART does not need the pin: `0x01` reports play state over the
serial link. It matters only to a host that wants playback state without serial
traffic. Open Item 1 is the one-wire, one-scope measurement that settles it.

### 4.6 The micro-USB port will stop UART mode, and probably is not a mass-storage device either

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
firmware-revision or board-revision difference. `UNKNOWN` -- Open Item 3. A card
prepared in a card reader sidesteps the question.

## 5. Mode selection: the DIP switch

### 5.1 The table, transcribed from the datasheet

Columns are printed **CON3, CON2, CON1** -- in that order, which is Section 5.2's
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

### 5.2 The ordering trap: the datasheet counts down, the switch counts up

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
> their setup as *"UART mode (0-0-1)"* -- the UART setting, written in the other
> order. **Always say which order you are quoting.**

Section 12 states both forms.

### 5.3 UART mode and One-Line mode have the same DIP setting, and the datasheet does not resolve it

> [!CAUTION]
> **Both rows print `1 0 0`.** Verified against the rendered page -- the
> `UART Mode` row and the `One-Line Mode` row carry identical CON3/CON2/CON1
> values, and differ only in which I/O column is populated: UART fills IO1/IO0
> with `RXD`/`TXD`, One-Line fills IO4 with `TXD`.

The reading that makes the table consistent is that **`1 0 0` selects "serial
control" and the pin you wire chooses the dialect** -- drive IO0/IO1 and it is
UART; drive IO4 and it is One-Line. That is a hypothesis, not a vendor statement.

The ecosystem cannot resolve it either. A search returned the straightforward
answer for UART (`CON3=1, CON2=0, CON1=0`, agreed everywhere) and, for One-Line,
*"the exact DIP mapping for ONE_Line vs Standard MP3 mode depends on the PCB
printing, so you should use the table printed on your board or datasheet to
confirm"* -- which is a way of saying nobody knows. Envistia's reproduction of
the mode table **omits the One-Line row entirely**.

A host that speaks UART on IO0/IO1 and never asserts IO4 is unaffected. It
matters only to a builder who wants the single-wire mode, and Open Item 2 names
the experiment.

### 5.4 One-Line mode

Not implemented by `SnijderC/dyplayer` (*"the library does not support the
ONE_Line protocol"*) and not implemented by `Lenoirio/dy-sv5w`. Recorded here so
the research does not have to be done twice.

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
the module's internal index (Section 9.3). They are not the same addressing model
and a card that works under one is not guaranteed to behave identically under the
other.

> [!NOTE]
> `0x17` and `0x18` are both printed *"SD card selection"*. One of them is almost
> certainly a typo -- `UNKNOWN` which, and there is nothing to check it against.
> There is also **a `System sleep` command (`0x1B`) with no UART equivalent** in
> any of the three command tables, which is the only capability One-Line appears
> to have that UART does not.

## 6. The serial protocol (normative)

### 6.1 The frame

**9600 baud, 8 data bits, 1 stop bit, no parity, full duplex.** Verbatim from the
datasheet: *"Adopt full duplex serial port communication. Baud rate 9600, data
bits 8, stop bit 1, check bit N."* No command changes the baud rate; 9600 is
fixed, so a software UART TX with a fixed 104 us bit time is enough to drive it.

```
  byte   0      1       2        3 .. 3+n-1     3+n
        0xAA   CMD    LEN (n)    DATA[0..n-1]    SM
```

| Field | Value | Note |
| --- | --- | --- |
| Start | `0xAA` | *"Command Code: fixed to 0xAA."* |
| CMD | `0x01`-`0x1F` | Section 7 |
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

### 6.2 The checksum, and the fact that every printed example verifies

`SM = (sum of all preceding bytes, including 0xAA) & 0xFF`. A two-line loop, and
identical in all three implementations read -- `dyplayer`, `Lenoirio` and
BetterDuino:

```c
uint8_t sum = 0;
for (uint8_t i = 0; i < len; i++) sum = (uint8_t)(sum + payload[i]);
```

**All 17 worked byte sequences printed in the datasheet's control and query
tables were recomputed. Zero mismatches.**

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
> stating plainly.** The DFPlayer sheet (`dfplayer-mini-sound.md`, 'Every worked
> example in the official datasheet has a wrong checksum') found that *"all three
> worked examples in the datasheet have wrong checksums"* -- every byte sequence
> a developer would copy to bootstrap a driver failed the datasheet's own rule.
> Here, 17 for 17 are right, and `SnijderC/dyplayer`'s precomputed constants
> (`0xab`, `0xac`, `0xad`, `0xae`, `0xaf`, `0xb0`, `0xb4`, `0xb6`, `0xb7`,
> `0xb8`, `0xb9`, `0xba`, `0xbb`, `0xbc`, `0xbe`, `0xbf`, `0xc6`) agree with both.
>
> **Compute anyway.** The rule survives contact with a document that gets it
> right, and a code comment or a forum post that prints a byte can still be
> wrong.

**`0xAB` is not a footer.** `AA 01 00 AB` ends in `0xAB` because
`0xAA + 0x01 + 0x00 = 0xAB`; every other frame's final byte is the same
arithmetic. There is no end-marker dialect of this protocol, and a driver that
appends `0xAB` to every frame sends a wrong checksum on all but one of them.

### 6.3 A sum is not a CRC, and two real frames collide

The checksum is a plain modulo-256 sum, so it detects a single corrupted byte but
**cannot detect a transposition**. Two real play frames prove it:

```
play track 1     AA 07 02 00 01 B4
play track 256   AA 07 02 01 00 B4     <-- same SM
```

A transmitter that swapped two payload bytes would produce a **checksum-valid
frame that plays the wrong sound**. There is no framing help either: no end
marker, no escape, no length-independent resync. A receiver that loses byte
alignment recovers only by timing out and waiting for silence.

This is not a reason to distrust the protocol. It is the reason a bit-banged TX
has to protect its bit timing from interrupts: a stretched bit is a corrupted
byte, and a corrupted frame is not always a rejected one.

### 6.4 Nothing acknowledges anything

**Control and setting commands return nothing at all.** The datasheet's `Return`
column reads `None` for all ten control commands and all eleven setting commands;
only the seven query commands reply. `Lenoirio/dy-sv5w` says it in the README:

> *"Most of the commands are fire-and-forget commands. This means there is no
> ack/nack sent from the module. Thus, it's not possible to provide the API caller
> with success information."*

So there is **no way to know a play command was accepted** except by asking
afterwards (`0x01` play state, `0x0D` current song) or by watching `BUSY`. A host
that reports "playing" because it *sent* a play command is reporting what it
sent, not what the module did.

### 6.5 Timing the datasheet does not state

No vendor document states a boot time, a minimum gap between commands, or a reply
latency. What exists is practice and measurement:

| Interval | Value that works | Source |
| --- | --- | --- |
| Power-on to first command | **1.5 s**, then drain the RX buffer | Measured on a bench module: after a 1.5 s wait and a drain, the first queries answer. The module may send bytes during boot (Section 8.2) |
| Between commands | **100 ms** after every frame | BetterDuino's `sendCommand()` ends in `delay(100)` under the comment *"Delay needed between successive commands"*, and its `init()` puts 100 ms between init frames. Measured on a bench module: 100 ms after every frame has worked |
| Query reply | arrives inside a **300 ms** wait | Measured on a bench module. The real latency is `UNKNOWN` |

Worst case for a host that runs four start-up queries and three commands with
these numbers: 1500 ms + 4 x 300 ms + 3 x 100 ms, about **2.7 s**. Open Item 7.

## 7. Commands

Every `SM` below was computed. Frames with a variable payload show the rule and a
worked example.

### 7.1 Control commands (no reply, ever)

| Command | Code | Frame | Note |
| --- | --- | --- | --- |
| Play / resume | `0x02` | `AA 02 00 AC` | Resumes the **selected** track. Not "play track N" |
| Pause | `0x03` | `AA 03 00 AD` | |
| **Stop** | `0x04` | `AA 04 00 AE` | **The stop.** `0x03` then `0x02` is pause then resume, and cancels itself out |
| Previous | `0x05` | `AA 05 00 AF` | |
| Next | `0x06` | `AA 06 00 B0` | **Not play.** Sent to play a sound, it steps through the card and plays whatever is next |
| Previous file | `0x0E` | `AA 0E 00 B8` | Datasheet wording. `dyplayer` calls it *previous directory, last sound* -- Open Item 4 |
| Next file | `0x0F` | `AA 0F 00 B9` | Datasheet wording. `dyplayer` calls it *previous directory, first sound* -- Open Item 4 |
| Stop playing | `0x10` | `AA 10 00 BA` | A **second** stop. `dyplayer` names it `stopInterlude()`; BetterDuino sends it for `Quiet()`. Open Item 4 |
| Volume + | `0x14` | `AA 14 00 BE` | One step |
| Volume - | `0x15` | `AA 15 00 BF` | One step |

### 7.2 Query commands (always reply)

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
can see, **play** is what it is currently reading from. Asking `0x09` before a
drive select and `0x0A` after it confirms the select took.

### 7.3 Setting commands (no reply)

| Command | Code | Frame | Payload | Worked example |
| --- | --- | --- | --- | --- |
| **Specified song** | `0x07` | `AA 07 02 <SN_H> <SN_L> SM` | 16-bit big-endian index | track 1 -> `AA 07 02 00 01 B4` |
| Specified path | `0x08` | `AA 08 <len> <drive> <path...> SM` | see Section 9.6 | -- |
| **Switch drive** | `0x0B` | `AA 0B 01 <drive> SM` | `00` USB / `01` SD / `02` FLASH | FLASH -> `AA 0B 01 02 B8` |
| **Set volume** | `0x13` | `AA 13 01 <vol> SM` | 0-30 | 15 -> `AA 13 01 0F CD` |
| Interlude by number | `0x16` | `AA 16 03 <drive> <SN_H> <SN_L> SM` | drive + index | SD, track 5 -> `AA 16 03 01 00 05 C9` |
| Interlude by path | `0x17` | `AA 17 <len> <drive> <path...> SM` | see Section 9.6 | -- |
| Set loop mode | `0x18` | `AA 18 01 <mode> SM` | 0-7, Section 7.5 | single-stop -> `AA 18 01 02 C5` |
| Set cycle times | `0x19` | `AA 19 02 <H> <L> SM` | 16-bit repeat count | 3 -> `AA 19 02 00 03 C8` |
| **Set EQ** | `0x1A` | `AA 1A 01 <eq> SM` | 0-4, Section 7.5 | NORMAL -> `AA 1A 01 00 C5` |
| Combination play | `0x1B` | `AA 1B <2*k> <name pairs...> SM` | k two-character file names | Section 9.7 |
| End combination play | `0x1C` | `AA 1C 00 C6` | -- | |
| Select, do not play | `0x1F` | `AA 1F 02 <SN_H> <SN_L> SM` | 16-bit index | track 1 -> `AA 1F 02 00 01 CC` |

**Interlude is not mixing.** `0x16` plays a sound *over* the current one and then
returns to it. The vendor's note, quoted through `dyplayer`, says *"'Music
interlude' only has level 1"*: one interlude at a time, covering the previous
one, with no volume of its own. The module plays one stream.

**Precomputed tables for the two payload commands a host sends most:**

| Volume | Frame | | Volume | Frame |
| --- | --- | --- | --- | --- |
| 0 (silent) | `AA 13 01 00 BE` | | 20 (module default) | `AA 13 01 14 D2` |
| 5 | `AA 13 01 05 C3` | | 25 | `AA 13 01 19 D7` |
| 10 | `AA 13 01 0A C8` | | 30 (max) | `AA 13 01 1E DC` |
| 15 | `AA 13 01 0F CD` | | | |

| Track | Frame | | Track | Frame |
| --- | --- | --- | --- | --- |
| 1 | `AA 07 02 00 01 B4` | | 151 | `AA 07 02 00 97 4A` |
| 2 | `AA 07 02 00 02 B5` | | 177 | `AA 07 02 00 B1 64` |
| 21 | `AA 07 02 00 15 C8` | | 255 | `AA 07 02 00 FF B2` |
| 52 | `AA 07 02 00 34 E7` | | 256 | `AA 07 02 01 00 B4` |
| 53 | `AA 07 02 00 35 E8` | | 999 | `AA 07 02 03 E7 9D` |
| 126 | `AA 07 02 00 7E 31` | | 65535 | `AA 07 02 FF FF B1` |

### 7.4 Volume: 0-30 ascending, default 20

Datasheet, communication protocol section C, verbatim:

> *"Volume: the volume is 31grades, 0-30.The default is 20grade."*

Corroborated by `SnijderC/dyplayer` (*"Set the playback volume between 0 and 30.
Default volume if not set: 20."*), by `Lenoirio/dy-sv5w` (*"volume to 10 (max
value is 30)"*), and by BetterDuino's ladder -- `VolumeMax()` = 30,
`VolumeMid()` = 15, `VolumeMin()` = 5, `VolumeOff()` = 0.

> [!IMPORTANT]
> **0 is silent and 30 is loudest.** A host that keeps its own volume on a 0-30
> ascending scale puts the number in the frame unchanged.
>
> That is not universal among the hobby's sound boards and the difference bites.
> The MP3 Trigger's VS1063 volume register is **inverted** (`0x00` loudest), and
> CHIRP takes 0-99. Section 10.2 is a live example of an astromech project
> carrying an MP3 Trigger volume comment into DY-SV5W code and getting the
> direction backwards.

The setting table's `Remark` column says `VOL: 0x00-0xFF`, which contradicts the
protocol section's 0-30. **Believe 0-30**: three implementations clamp there, and
what the module does with 31-255 is untested. `UNKNOWN`, recorded rather than
explored.

### 7.5 Play modes and EQ

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

A host that wants one-shot playback need not send `0x18`: the power-on default is
already single-stop. A host that switches to mode `0x07` to play the whole card
has to send mode `0x02` (`AA 18 01 02 C5`) to get back.

**EQ** (`0x1A`), default NORMAL:

| Value | EQ | Frame |
| --- | --- | --- |
| `0x00` | NORMAL | `AA 1A 01 00 C5` |
| `0x01` | POP | `AA 1A 01 01 C6` |
| `0x02` | ROCK | `AA 1A 01 02 C7` |
| `0x03` | JAZZ | `AA 1A 01 03 C8` |
| `0x04` | CLASSIC | `AA 1A 01 04 C9` |

No query reads EQ back, and a previous owner or a Standard MP3 Mode button press
may have left it elsewhere. A host that wants a known EQ sends `0x1A` at start-up
(Open Item 6).

## 8. What the module sends back

### 8.1 Replies

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

Reply latency is not stated by any vendor document. Replies have arrived inside a
300 ms wait on a bench module (Section 6.5, Open Item 7).

### 8.2 The module also volunteers bytes

There is no documented unsolicited frame -- the datasheet has no
power-on-report equivalent to the DFPlayer's `0x3F`, and no track-finished push.
But the module does send bytes nobody asked for. Measured on a bench module:
**bytes arrive during playback, and at power-on**. A reader that takes whatever
comes next after a query as the reply picks those bytes up, passes a length
check, and reads garbage as the drive or the play state.

**What a reader must therefore do:**

```c
if (n >= 4 && rsp[0] == 0xAA && rsp[1] == 0x09 && rsp[2] == 0x01) { ... }
```

Start byte, **command byte and length byte**, all three, before believing a
reply, and drain the RX buffer before sending a query. `Lenoirio/dy-sv5w`
arrived at exactly the same three checks in `receive_answer()` -- and reads the
trailing checksum byte and throws it away (`let _ = self.serial.read_byte().await;
// ignore CRC for now`). `SnijderC/dyplayer` is the only one of the three
implementations that validates the reply checksum.

The same bytes are the reason to be wary of querying the module while it plays:
a query sent mid-track is the moment those bytes are most likely to land in the
reply window.

## 9. Storage, and the card contract

### 9.1 Some boards run from on-board FLASH

Measured on a bench module: **`0x09` reports `0x02` = FLASH**, and after a drive
select to it, `0x0A` reports FLASH as the play drive.

`SnijderC/dyplayer`'s device enum explains what that is: *"Flash = 0x02, Onboard
flash chip (usually winbond 32, 64Mbit flash)"* -- 4 to 8 MB of SPI flash on the
board, loaded through the micro-USB socket (Section 4.6), independent of the TF
slot.

> [!IMPORTANT]
> **Do not assume a DY-SV5W plays from the card.** Whether a given board has a
> flash chip populated is a board-revision fact, and the module reports it
> truthfully on `0x09`. **Ask, never assume** -- Section 9.2 is what assuming
> costs.

### 9.2 Selecting a drive the module does not have silences its replies

Measured on a bench module that reports FLASH on `0x09`: a `0x0B` drive select
to **SD** (`AA 0B 01 01 B7`) left the module in a state where **it stopped
answering every query** -- zero reply bytes, on every call -- while **play, stop
and volume commands still worked**. The audio sounded normal; only the replies
were gone.

> [!IMPORTANT]
> **"Queries return nothing" plus "playback works" means a confused module, not a
> broken RX wire.** A dead RX path gives you silence on the wire whatever you
> send; a bad drive select gives you a module that plays and will not talk.

The rule that follows: select the drive `0x09` reported, and if `0x09` did not
answer, **send no `0x0B` at all** rather than guessing one.

### 9.3 The index is the filesystem's order, not the file's name

This is the single most consequential behaviour of the module, and three
independent sources say it:

- `SnijderC/dyplayer` README: *"The module will look for the first sound file
  found in the filesystem. It's not using the file name, neither does it order by
  file name."*
- `Lenoirio/dy-sv5w` README: *"don't expect that the file called 00001.mp3 is
  always the song that you address with number 1. It looks like the module rather
  counts the number in the FAT directory structure."*
- Measured on a bench module: with files `001.mp3`, `002.mp3` and `004.mp3`
  present, **requesting track 3 can play `004.mp3`**. A missing number does not
  leave a hole in the index; later files move down to fill it.

`0x07` addresses the **n-th file the module enumerated**. The file name is a
convention the builder maintains, not an address the module honours. The only
play-by-name escape in UART mode is `0x08` play-by-path (Section 9.6).

> [!NOTE]
> **The DFPlayer has an escape from this and the DY-SV5W does not**, which is the
> one place that module is genuinely better -- the DFPlayer sheet
> (`dfplayer-mini-sound.md`, 'Three addressing modes, and only two of them are
> stable') calls choosing `0x12` play-by-filename *"the single most valuable
> decision in this sheet"*. For the DY-SV5W the only defence other than
> play-by-path is the discipline in Section 9.5.

### 9.4 The card itself

| Property | Value | Source |
| --- | --- | --- |
| Card type | microSD / TF | Datasheet board annotation |
| Maximum size | **32 GB** | Datasheet annotation *"TF Card 32G Bit"*; every reseller listing |
| Filesystem | FAT32 (FAT16 accepted) | Ecosystem consensus. exFAT `UNKNOWN`, presumed unsupported -- Open Item 8 |
| Formats | **MP3 and WAV** | Reseller specifications, consistently |
| Maximum tracks | 65535 by frame width; **255** in the I/O modes | `0x07` payload is 16-bit; the I/O mode tables stop at `00255.mp3` |

> [!WARNING]
> **A card bought today will very likely arrive exFAT.** Anything above 32 GB is
> exFAT by default on Windows and macOS and a 64 GB card is now the cheapest on
> the shelf. Specify **FAT32, 32 GB or under**. This is the identical hazard the
> DFPlayer sheet flags, and the identical failure: the module reports no device
> online and a driver correctly concludes there is no card.

### 9.5 Practical rules for a DY-SV5W card

1. **Copy the files onto an empty card in playing order, one at a time or in a
   single ordered batch.** The order they land in the directory is the order the
   module will number them. This is the rule that actually protects you; naming is
   only a reminder of it.
2. Name them `001.mp3` .. `NNN.mp3`, **strictly contiguous, no gaps**
   (Section 9.3). The datasheet's I/O-mode tables use five digits (`00001.mp3`);
   three works and is what the astromech packs ship. Either is fine, consistently.
3. **Prepare the card on Linux or Windows, or clean it afterwards.** A card
   prepared on a Mac carries a `._001.mp3` resource fork beside every track and a
   `.Spotlight-V100` directory, and under an enumeration-order index **every one
   of those is a file in the count**.
4. **Never delete a track from the middle.** Replace it with silence of the same
   name rather than removing it, or re-copy the whole card. Deleting file 3 does
   not free number 3; it renumbers everything after it.
5. **Read back `0x0C` at start-up and compare it with the number of tracks you
   expect.** It is the cheapest possible detection of a card that was rebuilt
   wrong.

### 9.6 Play-by-path (`0x08` / `0x17`)

`AA 08 <len> <drive> <path...> SM`, where the path is **transformed**:

- every `.` becomes `*`
- every `/` except the leading one gets a `*` **inserted before it**
- the whole path is upper-cased

so `/SONGS1/FILE1.MP3` goes on the wire as `/SONGS1*/FILE1*MP3`. The encoder is
`byPathCommand()` in `SnijderC/dyplayer`; the datasheet only gives the frame
shape (`AA 08 Length Drive Path SM`). Names are limited to **8 characters per
directory and 8 per file name**, and `dyplayer` caps whole paths at 36 bytes by
default.

The trade-off: play-by-path gives name-stable addressing -- immunity to
Section 9.3 -- at the cost of a variable-length frame, an 8.3 naming constraint
on the card, and a break with the numbered-card convention every astromech sound
pack uses (Section 10).

### 9.7 Combination play (`0x1B` / `0x1C`)

Queues several short files to play back to back -- the vendor's use case is
building a spoken number out of samples. Files must have **two-character names**
(`01.mp3`) and live in a specific directory: the datasheet says `XY`,
`SnijderC/dyplayer`'s documentation says *"a directory that can be called `DY`,
`ZH` or `XY` ... most modules use `XY` despite documentation suggesting `DY`"*.

Frame: `AA 1B <2*k> <pair1> <pair2> ... SM`, then `AA 1C 00 C6` to end it.

It is a hardware sequence of sounds with no host in the loop. The two-character
naming does not fit a numbered card laid out by Section 9.5.

## 10. How the hobby drives this module (non-normative)

### 10.1 BetterDuino's `MDuinoSoundDYPlayer`

BetterDuino Firmware V4, `MDuinoSoundDYPlayer`, behind
`#define INCLUDE_DY_PLAYER // DY-SV5W audio board`. Its `sendCommand()` computes
the sum over the frame, writes it, and ends in `delay(100)` (Section 6.5).

Two things worth knowing:

- **The 9x25 bank model in arithmetic:** `CalcSoundNr = (BankNr - 1) * 25 +
  SoundNr`, which is MarcDuino's convention (bank 1 = 1-25, bank 2 = 26-50, ...)
  computed on the host and sent as a flat index.
- **`Quiet()` sends `AA 10 00`** -- the `0x10` "stop playing" opcode, not `0x04`.
  Worth noting because its `Quiet(const bool on)` parameter is **never read**: the
  function sends the same frame whichever way you call it. A small, real defect in
  a widely used reference, and a reminder that "the reference does X" is not the
  same as "X is right".

Its volume ladder is the clearest corroboration of the ascending scale: `Max = 30`,
`Mid = 15`, `Min = 5`, `Off = 0`, with `VolumeUp()` **adding** 2.

### 10.2 Padawan360: the same numbers survive a change of module, and the volume does not

The Padawan360 DY-SV5W port exists to swap the sound module -- *"This sketch is
to beta test the DY-SV5W audio player instead of sparkfun mp3"* -- and every
call site keeps the old one commented directly above:

```cpp
      //mp3Trigger.play(21);
    player.playSpecified(21);
```

**The track numbers did not change when the module did.** That is the ecosystem's
own statement that a sound *number* is a property of the droid's card, not of the
module, and it is why Section 9.3's enumeration-order index is a real operator
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
> **Volume semantics do not survive a module swap, and a stale comment is how the
> old scale gets carried forward.**

Its observable vocabulary: named one-shots at 1-12 (5 = Leia), 21, 52/53 as
drive-enable/disable chirps, and random ranges `random(13,17)`, `random(17,25)`,
`random(32,52)`.

### 10.3 `SnijderC/dyplayer`: the library everyone uses, written for a different module

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
where the 1 kOhm and 10 kOhm resistor advice in Section 4.2 comes from.

### 10.4 ... and one of its opcodes is provably wrong

Current `master`:

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
> **No caller would find out from the wire.** The library is popular, actively
> maintained, correct in 20-odd other opcodes, and wrong in this one -- and the
> command returns nothing (Section 6.4). A unit test asserting the byte against
> the datasheet's printed frame catches it in a second.

### 10.5 `Lenoirio/dy-sv5w`: the one written for this module

A Rust `no-std` crate with the datasheet committed alongside it. Independent
value, beyond corroborating every opcode:

- It validates replies the way Section 8.2 describes -- check `0xAA`, check the
  command byte, check the length byte, read the payload, and ignore the trailing
  checksum (`// ignore CRC for now`).
- It states the 3.3 V I/O level plainly (Section 4.2).
- It carries the USB-kills-UART warning (Section 4.6).
- It confirms the enumeration-order index in the author's own words (Section 9.3).
- It is honest about `0x19`: *"The effect of this configuration is unclear for
  now"*, which is a better answer than a guess.

### 10.6 Negative results across the ecosystem

Recorded so nobody searches twice:

- **ShadowMD**: no sound hardware at all. It delegates to MarcDuino and speaks
  MP3 Trigger file numbers.
- **AstroPixelsPlus**: drives a **DFPlayer** through `MarcduinoSound.h`. Nothing
  DY.
- **CHIRP**: a different kind of board entirely (Section 11).
- **MarcDuino** itself: MP3 Trigger and DFPlayer. The DY-SV5W is not in its
  vocabulary; what it contributes is the **9x25 bank convention** that every
  project numbers cards by.
- **r2d2-astromech-simulator**: offers `DY-SV5W` as a Sound answer whose wiring
  note reads *"Serial0 via DYPlayerArduino. 30 is loudest. Watch out: the sketch's
  own `Serial.println()` shares this UART."* -- and then prints the contradiction
  *"sound -- this sketch drives a MD-YX5300"* because the bundled sketch does.
  *"30 is loudest"* is independent third-party agreement on the scale direction.

## 11. How the DY-SV5W, MP3 Trigger, CHIRP and DFPlayer Mini differ

| | **DY-SV5W** | MP3 Trigger | CHIRP | DFPlayer Mini |
| --- | --- | --- | --- | --- |
| Transport | **binary, 9600 fixed** | binary, 9600 (38400 factory) | ASCII, configurable | binary, 9600 |
| Frame | **`AA CMD LEN .. SM`, 4+n B** | 2 bytes | ASCII lines | fixed 10 B |
| Checksum | **sum, 1 byte** | none | none | 16-bit negated sum |
| Volume native | **0-30 ascending** | 0-255 inverted; audible 0-64 | 0-99 | 0-30 |
| Simultaneous streams | **1** | 1 | **3+, mixed** | 1 |
| Decoding | **hardware** | VS1063 | software, RP2350 | hardware |
| Addressing | **index, enumeration order** | file-name prefix | catalog + bank/page | index *or* filename |
| Play-state query | **yes (`0x01`)** | **no query** -- follows `'X'`/`'x'`/`'E'` | yes | yes |
| Device-type query | **yes (`0x09`/`0x0A`)** | no | yes | yes |
| Safe to query while playing | **not advised**: volunteers bytes mid-track (Section 8.2) | no | **yes** | untested |
| Track-finished event | no | no | no | **pushed** |
| Hardware busy pin | **yes, polarity unresolved** | no | no | yes |
| On-board amplifier | **yes, ~3 W real** | no | no | yes, <3 W |
| On-board storage | **often, 4-8 MB flash** | no | **yes, flash bank** | some clones |
| Catalog support | no | no | **yes** | no |
| Board identity | **one board, many resellers** | fixed vendor part | fixed vendor part | **unreliable** |

**What sets the DY-SV5W apart:** 5 V power with 3.3 V logic, so an ESP32 needs no
level shifting; an amplifier on board; queries for play state, drive, track count
and current track; vendor command tables that are arithmetically correct; and
storage that can be on the board itself.

**What it does not have:** name-stable addressing (Section 9.3), a play state that
can be asked mid-track without care (Section 8.2), and mixing. Music under speech
needs a board that mixes.

> [!NOTE]
> **What the DFPlayer has that this module does not is a pushed track-finished
> event.** Without polling, the DY-SV5W's only way to say a sound has ended is the
> `BUSY` pin (Section 4.5, Open Item 1).

## 12. Quick Reference

- Field: Baud. Value: **9600, 8-N-1, full duplex, fixed.** No command changes it.
- Field: Frame. Value: **`0xAA CMD LEN DATA.. SM`**, total `4 + LEN` bytes. No start-of-frame escape, **no end marker**.
- Field: Checksum. Value: **`sum(all preceding bytes) & 0xFF`**, including the `0xAA`. Compute it; never copy a printed byte.
- Field: `0xAB`. Value: **not a footer.** It is `0xAA + 0x01 + 0x00`, the checksum of the play-state query.
- Field: Play a numbered track. Value: **`0x07`**, `AA 07 02 <hi> <lo> SM`, 16-bit big-endian, 1-based. Track 1 = `AA 07 02 00 01 B4`.
- Field: Play opcode to avoid. Value: **`0x06` is NEXT TRACK**, not play. Sent to play a sound, it gives random playback.
- Field: Stop. Value: **`0x04`** -> `AA 04 00 AE`. `0x03` is pause, `0x02` is resume; sending both is a no-op.
- Field: Second stop opcode. Value: `0x10` -> `AA 10 00 BA` ("stop playing" / stop interlude).
- Field: Volume. Value: **`0x13`, range 0-30 ASCENDING, 0 = silent, module default 20.**
- Field: Volume 15. Value: `AA 13 01 0F` **`CD`**.
- Field: EQ. Value: `0x1A`, 0 NORMAL / 1 POP / 2 ROCK / 3 JAZZ / 4 CLASSIC. NORMAL = `AA 1A 01 00 C5`.
- Field: Play mode. Value: `0x18`, 0-7. **Power-on default is `02` single-stop**, which is what a droid wants.
- Field: Switch drive. Value: `0x0B`, `AA 0B 01 <drive> SM`. **Use the value `0x09` reported. Never hardcode. If detection failed, send nothing.**
- Field: Device codes. Value: **`00` USB, `01` SD/TF, `02` FLASH, `FF` none.**
- Field: Play-state codes. Value: **`00` stop, `01` play, `02` pause.**
- Field: Query play state. Value: `AA 01 00 AB` -> `AA 01 01 <state> SM` (5 bytes).
- Field: Query online drive. Value: `AA 09 00 B3` -> `AA 09 01 <drive> SM` (5 bytes).
- Field: Query play drive. Value: `AA 0A 00 B4` -> `AA 0A 01 <drive> SM` (5 bytes).
- Field: Query track count. Value: `AA 0C 00 B6` -> `AA 0C 02 <hi> <lo> SM` (6 bytes).
- Field: Query current track. Value: `AA 0D 00 B7` -> `AA 0D 02 <hi> <lo> SM` (6 bytes).
- Field: Reply validation. Value: **check `0xAA`, the command byte AND the length byte.** The module volunteers bytes during playback and at power-on.
- Field: Queries silent, playback fine. Value: **a confused module, not a dead RX wire** -- usually a drive select to storage it does not have.
- Field: Acknowledgement. Value: **none exists.** Control and setting commands return nothing, ever.
- Field: Inter-command delay. Value: **100 ms** after every frame (ecosystem practice, and has worked on a bench module; no vendor figure).
- Field: Power-on delay. Value: **1500 ms** before the first command, then drain RX (has worked on a bench module; no vendor figure).
- Field: Reply latency. Value: unspecified by any vendor document; replies arrived inside a **300 ms** wait on a bench module.
- Field: DIP for UART mode. Value: **`CON3=1, CON2=0, CON1=0`** -- on the physical switch, **`1`=OFF, `2`=OFF, `3`=ON**. Latched at power-on only.
- Field: Supply. Value: **5 V**. Logic on every pin including UART: **3.3 V**. No level shifter needed for an ESP32.
- Field: Amplifier. Value: marked 5 W; **~3 W is the arithmetic ceiling** into 4 ohm from 5 V, bridge-tied. Speaker pads are BTL -- never into an amplifier input; use the 3.5 mm jack for line level.
- Field: `BUSY` polarity. Value: **UNRESOLVED.** Pin table says LOW-while-playing; four mode blocks say HIGH-while-playing. Do not assert one (Open Item 1).
- Field: Track addressing. Value: **the module's enumeration order, not the file name.** Copy files onto an empty card in order; keep numbering contiguous.
- Field: Card. Value: **FAT32, 32 GB maximum**, MP3 or WAV, no hidden files. Some boards play from **on-board flash instead** -- ask with `0x09`.

## 13. Open Items

| # | Item | How to settle it |
| --- | --- | --- |
| 1 | **`BUSY` polarity, and its assertion latency** (Section 4.5) | One wire to a spare GPIO or a scope probe; play a track of known length and log the transitions. Settles a contradiction no document can. Matters to a host that wants play state without serial traffic |
| 2 | **One-Line mode's real DIP setting** (Section 5.3) | Set `1 0 0`, wire IO4 only, send `0x01 0x0B 0x11` as one-line bits, hear whether track 1 plays. Matters only for the single-wire mode |
| 3 | **Does the micro-USB port enumerate as storage?** (Section 4.6) | Plug a module into a PC and look. Board-revision dependent; affects card-preparation advice only |
| 4 | **`0x0E` / `0x0F` / `0x10` semantics** (Section 7.1) | Datasheet says "previous file / next file / stop playing"; `dyplayer` says "previous directory first/last sound / stop interlude". Send each with a multi-folder card and observe |
| 5 | **Idle and playing current draw** (Section 4.2) | Inline ammeter at 5 V, silent and at volume 30 into the fitted speaker. The ~0.7-1.0 A figure is derived, not measured, and a shared 5 V rail is a real droid design input |
| 6 | **Does EQ persist across power cycles?** (Section 7.5) | No query exists to read it back. Set ROCK, power-cycle, listen. Decides whether sending `0x1A` at start-up is needed |
| 7 | **Boot time, minimum inter-command gap, reply latency** (Section 6.5) | None is vendor-stated. 1.5 s, 100 ms and a 300 ms reply wait have worked on a bench module. Measure one if a symptom points at it |
| 8 | **exFAT** (Section 9.4) | Format a 32 GB card exFAT and see whether `0x09` reports a device. Expected: no. Worth one test because "no card" and "module missing" look the same |

## 14. Sources

**Primary -- the module**

- **DY-SV5W Voice Playback Module Datasheet**, 4 pages, undated content, PDF
  created 2019-06-06 in Microsoft Word by a third party (Section 2.1). **No text
  layer** -- rendered with `pdftoppm -r 150 -png` and read as images. Served by
  GroboTronics and committed into `Lenoirio/dy-sv5w`.
- Bench measurements of a DY-SV5W over its UART: the FLASH drive report, the
  unsolicited bytes, the drive-select failure, the track-gap mis-play, and the
  start-up and inter-command timing (Sections 6.5, 8.2, 9.1-9.3).

**Primary -- source code, read in full**

- `SnijderC/dyplayer` -- `src/DYPlayer.h`, `src/DYPlayer.cpp` (master), plus the
  README and issue #1. Every opcode, the precomputed checksums, the device enum,
  the by-path encoder, and the wrong `0x16` (Section 10.4).
  https://github.com/SnijderC/dyplayer
- `Lenoirio/dy-sv5w` -- `src/lib.rs` and README. The DY-SV5W-specific Rust crate.
  https://github.com/Lenoirio/dy-sv5w
- BetterDuino Firmware V4 -- `src/MDuinoSound.cpp`, `include/MDuinoSoundDYPlayer.h`,
  `include/config.h`.
- Padawan360 DY-SV5W port -- both sketches (`..._PWM.ino` and `..._BETA.ino`).

**Astromech projects, read from source**

- ShadowMD, AstroPixelsPlus, CHIRP -- negative results (Section 10.6).
- r2d2-astromech-simulator -- the DY-SV5W option note and the contradiction it
  prints.

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
> (`grobotronics.com`, `shop.cpu.com.tw`) returned HTTP 403 to a scripted client.
> No clone-behaviour database of the `DFPlayerAnalyzer` kind exists for the DY
> family, and none appears to be needed. `SnijderC/dyplayer`'s README is **not** a
> DY-SV5W authority despite listing it as tested -- the library is written for the
> DY-SV17F and says so in its own header.
