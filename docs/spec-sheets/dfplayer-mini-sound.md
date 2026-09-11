# DFPlayer Mini Spec Sheet (DFPlayer serial)

Working spec for the **DFPlayer Mini** MP3 module as a **Sound** lineup member
([#305](https://github.com/mattiasbrandt/protoArtoo/issues/305), minted from
[#303](https://github.com/mattiasbrandt/protoArtoo/issues/303)), reached over the
**DFPlayer serial** Component Protocol that #303 assigned it.

Research date 2026-09-11. Every frame byte, command code, electrical value and
default below was read this session from DFRobot's own datasheet (DFR0299, a
**scanned** PDF read page by page as images), from DFRobot's own library and SDK
source, or from the astromech projects on this disk. Checksums were **computed,
not copied**. Claims that could not be sourced are marked `UNKNOWN` with the
artefact or bench test that would settle them.

> [!CAUTION]
> **The vendor documentation is not trustworthy byte-for-byte, and this is the
> central finding of the sheet.** Verified this session:
>
> - **All three worked examples in the datasheet have wrong checksums** -- every
>   byte sequence a developer would copy to bootstrap a driver fails the
>   datasheet's own stated rule (Section 6.2).
> - **The `BUSY` pin polarity is stated both ways**, ten pages apart, in the same
>   document (Section 5.3).
> - **The device numbering for `0x09` contradicts the module's own `0x3F` reply**
>   (Section 7.1), and the query commands `0x47`/`0x48` are assigned to opposite
>   devices by the datasheet and by DFRobot's own library (Section 7.2).
>
> This project has already paid for this exact class of defect once: the DY-SV5W
> driver shipped an end-marker dialect that turned out to be a checksum
> coincidence, and `test_audio_frames.cpp` carries the correction. **Derive every
> constant from the rule, assert it in a native test, and never trust a printed
> byte sequence.**

> [!NOTE]
> **The good news is unusually good, and it is structural.**
>
> **The seam already exists.** Sound is the one family with a proven interface
> across three shipping members, and `AUDIO_DFPLAYER = 2` has been reserved in
> `include/audio_driver.h:36` since before this research. Unlike the PCA9685 and
> the Maestro -- both of which had to *create* a seam -- this part adds a member to
> a finished one (Section 14.1).
>
> **It is the only roadmap part that can be verified without a droid.**
> `AudioSerialIO` is an injectable five-function-pointer transport, and
> `test/test_native/` already holds eighteen audio suites including one that
> exists to *"verify the payload bytes and checksum computation are correct before
> any hardware connection is tested"*. Given the CAUTION above, that is the main
> defence rather than a nicety (Section 14.2).
>
> **And we already run one.** Our own AstroPixelsPlus dome fork drives a DFPlayer
> through `MarcduinoSound.h`. Section 10 is what that has already taught us,
> including one real inconsistency in it.

> [!WARNING]
> **"A DFPlayer Mini" is a form factor, not a part.** Several unrelated silicon
> vendors fill it and they do not behave identically -- checksum handling, ACK
> behaviour, busy polarity and folder rules all vary. This is the only member of
> the Sound family whose identity is unreliable, and Section 2 is what a builder
> and a driver each have to do about it.

## Where this sits in the lineup

The **Sound** category holds four products, and a builder picks one:

| Product | What it is | Status |
| --- | --- | --- |
| DY-SV5W | binary-frame voice module | `supported` |
| MP3 Trigger | SparkFun/Robertsonics VS1053 board | `supported` |
| CHIRP Audio Trigger | RP2350 multi-stream mixer, astromech-specific | `supported` |
| **DFPlayer Mini** | hardware-decoded single-stream player with an amplifier | `roadmap` |

These are peers with different shapes, not a ladder -- Section 13 sets out the
differences in kind. The DFPlayer is the cheapest and the only one with an
on-board amplifier, a hardware busy pin and a pushed track-finished event; CHIRP
is the only one that mixes. Which a droid carries is a fact about what the builder
bought and what the droid has to do.

## 0. Authority Contract

This document is an implementation authority for the DFPlayer serial protocol and
for the module's behaviour as protoArtoo would use it.

Authority order for agent decisions:

1. **Computed truth** -- the checksum rule, applied. The datasheet's printed
   examples are wrong (Section 6.2) and lose to arithmetic.
2. **DFRobot's own library and SDK source** for command codes and device
   numbering, where the datasheet is ambiguous or self-contradicting.
3. **The DFR0299 datasheet** for everything else -- the pinout, the protocol
   shape, the returned frames, the timing.
4. This document.
5. Our own `AstroPixelsPlus` fork -- evidence of practice, **not** of correctness.
   Section 10.2 names a place it is wrong.
6. Other astromech project source (Section 11).

If references conflict:

- Prefer computation over any printed byte sequence.
- Prefer the library over the datasheet on **device numbering**, because the
  module's own `0x3F` reply corroborates the library.
- Prefer the datasheet over the library on **what the hardware does** -- pin
  functions, timing, unsolicited frames.
- Where the datasheet contradicts *itself*, assert nothing: mark `UNKNOWN` and
  name the bench test. `BUSY` polarity is the live example.

Agent requirements when using this document:

- MUST NOT copy a byte sequence from the vendor datasheet. Compute the checksum.
- MUST NOT use `0x03` to play a numbered sound. Use `0x12` (Section 9.1).
- MUST NOT pass the module's device code into `AudioModuleState.device`. The
  enumerations differ (Section 14.4).
- MUST NOT assume a reply to a query is the next frame on the wire. The module
  sends unsolicited frames (Section 8.2).
- MUST NOT assert a `BUSY` polarity until it is measured (Section 5.3).
- MUST wait for the `0x3F` power-on report before sending commands, and allow
  1.5-3 s for it (Section 8.4).
- MUST override `classifyRxStatus()` on artoo-esp32, where the RX line is shared
  with DomeLink (Section 14.3).

## 1. Scope

Covers the module and its clone variants, the electrical contract and pinout, the
serial frame and checksum, the full command and query set, the frames the module
sends unprompted, the SD card addressing contract, what our own dome deployment
has proven, how the astromech hobby drives sound modules generally, the host
libraries, how the four Sound members differ, and what protoArtoo would have to
build.

Does not cover: audio file encoding and bitrate selection, the module's USB
device mode, the ADKEY resistor-ladder input mode, the `0x08` playback modes
beyond naming them (a droid plays one-shots), or the DFPlayer Pro / DF1201S,
which is a different part with a different protocol.

## 2. What you are actually buying

> [!CAUTION]
> **"DFPlayer Mini" is a 16-pin form factor, not a part.** At least eight silicon
> families ship in it, and they are not drop-in compatible at the protocol level.
> This is the only member of the Sound family whose identity is unreliable, and it
> is the dominant risk in adopting it.

| Chip marking | Lineage | How to spot it | What differs |
| --- | --- | --- | --- |
| **`DFROBOT|LISP3`** | genuine DFR0299 | blue LED, all pads populated, amp `YX8002D` | the reference. Answers `0x3F`; **2** track-end callbacks |
| **`YX5200-24SS`** | Yue Xin, the original silicon | 24-pin QSOP marking | reference behaviour; validates checksums |
| **`GD3200A` / `MH2024K-24SS`** | GuoDian, sold explicitly as a YX5200 replacement | often on boards silked `MP3-TF-16P V3.0` | **`0x3F` is a reserved/invalid command** -- never answers an init query. `0x42` status codes differ. `0x1A` is MUTE, not DAC. **1** callback |
| **`GD3200B` / `MH2024K-16SS`** | same, SOP16 | boards silked **`HW-247A`**, red LED | the worst field reports: volume ignored *when sent with a checksum*; BUSY sometimes disagrees with reality; needs ~350 ms between commands |
| **`GD3200D`** | GuoDian, adds SPI flash | red LED, some pads unpopulated | line output is **mono, left channel only** |
| **`TD5580A`** | Tuda | `HW-247A v0.5.1` | **exFAT and 64 GB cards** (unique). **Ignores `0x03` entirely** |
| **`JL AAxxxx` / `ABxxxx`** | Jieli; the original die went EOL, which started all of this | e.g. `AA19HFF859-94`, `AB23A795249` | mixed: some never answer `0x3F`; BUSY needs ~350 ms to settle; `playMp3Folder` broken on some |
| **`MH3028M-24SS`** | MH-ET LIVE | marking | SD only, no USB, no `0x3F` |

### 2.1 The checksum is optional, and on some clones it must be omitted

This is the fork that matters most, and it is **vendor-documented, not a hack**.
The Flyron FN-M16P datasheet -- same silicon family, and the document whose
checksums are *correct* (Section 6.2) -- prints a **two-column table**, verified in
its own text this session:

```
      Commands                   Serial Commands                 Serial Commands
      Description                [with checksum]                [without checksum]
       Play Next           7E FF 06 01 00 00 00 FE FA EF     7E FF 06 01 00 00 00 EF
     Play Previous         7E FF 06 02 00 00 00 FE F9 EF     7E FF 06 02 00 00 00 EF
```

So an **8-byte frame with the two checksum bytes simply removed** is a documented
alternative encoding. The TD5580A datasheet prints *every* command in that form.

The practical rule, assembled from the vendor documents and from library source:

- Every variant accepts the 10-byte checksummed frame **in principle**, and a
  genuine YX5200 **validates** it and answers `0x40 / 0x02` on mismatch.
- **`MH2024K-16SS` / `GD3200B` in the field frequently reject checksummed frames**
  for commands beyond play -- volume in particular. The fix that works is to stop
  sending the two checksum bytes. `Makuna/DFMiniMp3` ships this as a chip type
  (`Mp3ChipMH2024K16SS`, `static const bool SendCheckSum = false;`).
- `TD5580A` accepts both.

> [!IMPORTANT]
> **For protoArtoo this is a driver design requirement, not trivia.** The frame
> builder must be able to emit **with or without** the checksum bytes, selected by
> a setting, because there is no way to know from the outside which module a
> builder soldered in. That is two lines in a `sendCommand()` and it is the
> difference between "works with the module I tested" and "works with what arrives
> in the post."

### 2.2 The genuine part, and its availability

DFRobot **SKU DFR0299**, list **USD 5.90**. The chip DFRobot ships is marked
`DFROBOT|LISP3` with a `YX8002D` amplifier -- **DFRobot does not name the chip in
any of its own documentation**, and has published nothing about the clone problem
on the product page, either wiki page, or its 2025 module selection guide
(searched; negative result).

> [!NOTE]
> **Availability, checked 2026-09-11: DFR0299 is out of stock at DFRobot**, showing
> `Back Order` and `Notify Me` with no Add to Cart.
>
> **And the page's `schema.org` markup says `InStock`, which is wrong.** DFRobot's
> own data payload carries the two fields side by side and names them:
> `stockText:"Out Of Stock"` and `stockTextSeo:"InStock"`. The SEO field is a
> constant, not a state.
>
> **This inverts the rule recorded in
> [`pololu-maestro-servo-controller.md`](pololu-maestro-servo-controller.md).**
> There, the rendered page was hidden boilerplate and the JSON-LD was correct;
> here the JSON-LD is decorative and the rendered page is correct. The durable
> rule is neither: **find the field the site computes for the human, and check
> what it is named.** DFRobot literally spells the difference in the field names.
>
> For this part it matters far less than it did for the Maestro: a commodity form
> factor is available from a hundred sellers. **That is the same fact as Section
> 2's risk, seen from the other side** -- the thing that makes it always purchasable
> is the thing that makes it unidentifiable.

**Successor, and not a drop-in:** DFPlayer Pro (DFR0768, chip `DF1201S`) has
128 MB of onboard flash and speaks **AT commands over UART** -- a completely
different protocol. It is a different part, not a newer DFPlayer Mini. There is no
DFRobot product called "DFPlayer Mini Lite"; listings using that name are
third-party inventions.


## 3. Project Integration

- **[`include/audio_driver.h`](../../include/audio_driver.h)** -- the seam.
  `AUDIO_DFPLAYER = 2` at `:36`; the **0-30 normalised volume** contract; the
  `AudioModuleState` fields this module answers one-for-one;
  `classifyRxStatus()` and its warning about the shared dome UART.
- **[`include/audio_serial_io.h`](../../include/audio_serial_io.h)** -- the
  injectable transport (`writeByte`, `rxAvailable`, `rxRead`, `delayMs`,
  `millisNow`) a DFPlayer driver reuses unchanged, and the reason native tests
  are possible.
- **[`src/drivers/audio_dy_sv5w.cpp`](../../src/drivers/audio_dy_sv5w.cpp)** --
  387 lines, the closest structural template: `sendCommand()`, `sendQuery()` with
  a bounded 300 ms timeout, a `begin()` that runs device/state/track queries.
- **[`src/tasks/audio_task.cpp`](../../src/tasks/audio_task.cpp)** -- `:60-61`,
  the `#error "AUDIO_DFPLAYER driver not yet implemented - see T15 / T16"` that a
  driver replaces. (#305 already notes that pointer is stale.)
- **[`src/drivers/audio_soft_uart_tx.h`](../../src/drivers/audio_soft_uart_tx.h)**
  -- the bit-banged 9600 TX that matches this module's fixed baud, and its
  measured *"~1.04 ms per byte"* Core 0 cost.
- **[`include/config.h`](../../include/config.h)** -- `PIN_AUDIO_TX` / `PIN_AUDIO_RX`
  on both targets, and `PA_CAP_DEDICATED_AUDIO_UART` (`:71`, `:76`), which is why
  the read path differs per board.
- **[`docs/sound_playback.md`](../sound_playback.md)** -- the audio system
  reference. Already lists `AUDIO_DFPLAYER` as *"Binary frames, 9600 baud -- Not
  yet implemented"*; carries the `$` command mapping this module must serve, the
  MP3 Trigger's inverted volume scaling, and the DY-SV5W contiguous-numbering
  finding that Section 9.2 builds on.
- **`CONTEXT.md`** -- **Audio Step Core**, **Audio Config Map**, **Component
  Toggle**. The init-retry lifecycle a driver's `begin()` should lean on lives in
  the Step Core.
- **[ADR 0042](../adr/0042-component-families-are-selected-at-runtime-where-the-board-offers-a-choice.md)**
  -- the Component Member rule, and the sound family's build-time-versus-runtime
  question this part sharpens.
- **`~/Documents/GitHub/AstroPixelsPlus`** -- our own dome fork, which already
  drives one (Section 10).

## 4. Sources Checked

| Source | How it was taken | What it gave |
| --- | --- | --- |
| **DFR0299 datasheet V1.0** -- https://dfimg.dfrobot.com/wiki/20532/DFR0299_mp3-player-module_datasheet_V1.0.pdf | `curl`, then **rendered to PNG and read as images** -- it is a scan with no text layer | Pinout, frame format, command tables, returned frames, error codes, power-on timing. And the three wrong checksums, the `BUSY` contradiction and the device-numbering conflict |
| DFRobot wiki, DFR0299 | fetched, parsed for the datasheet link | The only machine-readable route to the datasheet |
| **`DFRobot/DFRobotDFPlayerMini`** | fetched raw and read in full | The checksum algorithm, every command code, the device constants, `begin()`'s 2.2 s reset path, and an unreachable branch in its `0x3F` handler |
| **`~/Documents/GitHub/AstroPixelsPlus/MarcduinoSound.h`** | read on disk | Our own deployment: the 9x25 bank model, `play()` versus its own documented `/mp3/` layout, the hard-fail `begin()` |
| `AstroPixelsPlus` `docs/SETUP.md`, `docs/HARDWARE_WIRING.md` | read on disk | The documented card layout, the wiring, and the dome's own UART conflict |
| **`~/Documents/GitHub/Padawan360_mega_maestro_DYSV5W`** | read on disk | A line-by-line MP3 Trigger to DY-SV5W port with the track numbers unchanged -- the evidence that the hobby's numbering is module-independent (Section 11) |
| **`~/Documents/GitHub/ShadowMD`** | read on disk, grep validated | No sound hardware at all: MarcDuino function codes, MP3 Trigger file numbers |
| `~/Documents/GitHub/CHIRP` | read on disk | What the multi-stream tier actually is |
| `SnijderC/dyplayer` | fetched | DY-SV5W volume is **0-30 ascending, default 20** -- which makes a Padawan360 comment provably stale |
| protoArtoo `origin/main` | read, branch-parity checked | The seam, the drivers, the tests, the pin budget, the `$` mapping |

## 5. Electrical

### 5.1 The pinout, from the datasheet's own table

| No | Pin | Description | Note (verbatim) |
| --- | --- | --- | --- |
| 1 | VCC | Input Voltage | **DC3.2~5.0V; Type: DC4.2V** |
| 2 | RX | UART serial input | |
| 3 | TX | UART serial output | |
| 4 | DAC_R | Audio output right channel | *"Drive earphone and amplifier"* |
| 5 | DAC_L | Audio output left channel | *"Drive earphone and amplifier"* |
| 6 | SPK2 | Speaker- | *"Drive speaker less than 3W"* |
| 7 | GND | Ground | |
| 8 | SPK1 | Speaker+ | *"Drive speaker less than 3W"* |
| 9 | IO1 | Trigger port 1 | short press = play previous, long press = volume down |
| 10 | GND | Ground | |
| 11 | IO2 | Trigger port 2 | short press = play next, long press = volume up |
| 12 | ADKEY1 | AD Port 1 | *"Trigger play first segment"* |
| 13 | ADKEY2 | AD Port 2 | *"Trigger play fifth segment"* |
| 14 | USB+ | USB+ DP | |
| 15 | USB- | USB- DM | |
| 16 | BUSY | Playing Status | see Section 5.3 |

> [!NOTE]
> **VCC is specified 3.2-5.0 V, typical 4.2 V -- so 3.3 V is inside the
> specification.** That is the opposite of the Maestro's situation
> ([`pololu-maestro-servo-controller.md`](pololu-maestro-servo-controller.md)
> Section 5.3) and it means an ESP32 build can run the module from the same 3.3 V
> rail with **no level shifting in either direction**. The cost is amplifier
> headroom: SPK1/SPK2 is an internal bridge amp and its output scales with supply,
> so a 3.3 V droid is a quieter droid. A builder who wants volume runs it at 4.2-5 V
> and then **does** need to divide the module's 5 V TX down to the ESP32.
>
> The widely-copied hobby wiring adds a **1 kohm series resistor in the host's TX
> line to the module's RX**, attributed to noise. That is community practice; the
> datasheet does not specify it (Open Item).

### 5.2 Audio out: two different outputs, one of which is a trap

`SPK1`/`SPK2` is a **bridge-tied amplifier for a speaker under 3 W** -- it is not
a line output and must never be connected to an amplifier input, because neither
leg is ground-referenced. `DAC_L`/`DAC_R` is the line-level pair for driving an
external amplifier.

For a droid that already has an amplifier, `DAC_L`/`DAC_R` is the correct
connection and the on-board amp goes unused. For a droid with a bare speaker,
SPK1/SPK2 is a complete solution at low volume and is a meaningful part of why
this module costs what it does.

### 5.3 `BUSY`: the datasheet contradicts itself, on the pin we most want

> [!CAUTION]
> **The official datasheet states both polarities, ten pages apart.**
>
> Pin table, Section 2.2, pin 16 `BUSY`:
> > *"Playing Status -- Low means playing \ High means no"*
>
> Protocol section 3.3.2, referring to the same pin:
> > *"we opened a dedicated I/O as decoding and pausing status indication. See Pin
> > 16, Busy. 1). Output high level at playback status; 2). Output low level at
> > pause status and module sleep"*
>
> These are exact opposites. Hobby practice overwhelmingly treats `BUSY` as
> **active-low while playing**, which agrees with the pin table, but this sheet
> will not assert a polarity DFRobot's own document contradicts.
>
> **Bench test, five minutes:** wire `BUSY` to a GPIO, play a track, log the
> level. Settle it before any code depends on it (Open Item 1).

The polarity matters more than it looks. `BUSY` gives **playback state on one
GPIO with no serial traffic at all**, which is exactly what protoArtoo's
`AudioModuleState.playState` wants -- and on artoo-esp32 it is the only way to get
it while DomeLink owns the shared UART (Section 14.2). It is the same shape as the
Maestro's `ERR` pin: a single input that turns a silent module into an observable
one.

## 6. The serial protocol (normative)

### 6.1 The frame

9600 baud, 8-N-1, default and effectively fixed. The datasheet's own wording is
*"serial communication baud rate can set as your own, the default baud rate is
9600"*, but **no serial command changes it** (Section 7) -- so 9600 is what
firmware gets.

```
  byte  0     1     2     3     4       5      6      7       8       9
       0x7E  VER   Len   CMD  Feedback para1  para2  cksHi   cksLo   0xEF
       0x7E  0xFF  0x06                                              0xEF
```

| Field | Value | Note |
| --- | --- | --- |
| Start | `0x7E` | |
| VER | `0xFF` | *"Version Information"* |
| Len | `0x06` | *"the number of bytes after 'Len'"*, but the datasheet's own example counts **VER through para2** -- *"Data length is 6, which are 6 bytes [FF 06 09 00 00 04]"*. It is a constant; treat it as one |
| CMD | -- | Section 7 |
| Feedback | `0x00` / `0x01` | *"0x01: need answering, 0x00: do not need to return the response"* |
| para1/para2 | -- | 16-bit parameter, **high byte first** |
| Checksum | -- | 16-bit, high byte first. See below |
| End | `0xEF` | |

**Checksum = `-(sum of bytes 1..6)`**, i.e. the two's-complement negation of the
sum of VER, Len, CMD, Feedback, para1 and para2, sent high byte first. The
datasheet describes it only as *"Accumulation and verification [not include start
bit $]"*; DFRobot's own library states it exactly:

```cpp
uint16_t DFRobotDFPlayerMini::calculateCheckSum(uint8_t *buffer){
  uint16_t sum = 0;
  for (int i=Stack_Version; i<Stack_CheckSum; i++) {
    sum += buffer[i];
  }
  return -sum;
}
```

Worked, and verified by computation this session:

```
play track 1        7E FF 06 03 00 00 01 FE F7 EF
play /mp3/0001.mp3  7E FF 06 12 00 00 01 FE E8 EF
set volume 15       7E FF 06 06 00 00 0F FE E6 EF
stop                7E FF 06 16 00 00 00 FE E5 EF
query status        7E FF 06 42 00 00 00 FE B9 EF
```

### 6.2 Every worked example in the official datasheet has a wrong checksum

> [!CAUTION]
> **All three byte sequences the datasheet prints as copyable examples fail its
> own checksum rule.** Computed this session against the rule DFRobot's own
> library implements:
>
> | Datasheet says | Printed cks | Correct cks |
> | --- | --- | --- |
> | *"specify play NORFLASH"* `7E FF 06 09 00 00 04 FF DD EF` | `FF DD` | **`FE EE`** |
> | *"select the first song played"* `7E FF 06 03 00 00 01 FF E6 EF` | `FF E6` | **`FE F7`** |
> | *"sending the playback command"* `7E FF 06 0D 00 00 00 FF EE EF` | `FF EE` | **`FE EE`** |
>
> Three for three. These are exactly the sequences a developer copies to bootstrap
> a driver, and the last one is annotated field by field -- *"FF --- Checksum high
> byte, E6 --- Checksum low byte"* -- so it is not a typo in one digit but a wrong
> value presented as authoritative.
>
> **Why this has gone unnoticed for a decade:** most modules do not validate the
> checksum at all (Section 2). Code derived from these examples works until it
> meets a module that does -- which is the `0x40 / 0x02` *"Verification error"*
> return in Section 8.3.
>
> **This project has been burned by exactly this shape before.** The DY-SV5W
> driver originally implemented an end-marker dialect, and
> `test/test_native/test_audio_frames/test_audio_frames.cpp` records the
> correction: *"the 0xAB in query frame AA 01 00 AB is just a checksum
> coincidence... not a protocol footer byte."* **Derive the frame from the rule
> and assert it in a native test; never copy a vendor's printed bytes.**

### 6.3 Framing has no resynchronisation help

Unlike the Maestro's protocol, **there is no high-bit convention separating
command bytes from data bytes** -- every field is a full byte and `0x7E` or
`0xEF` can appear anywhere in a parameter or a checksum. A decoder must
therefore hunt for `0x7E`, take exactly ten bytes, and **validate both the
`0xEF` terminator and the checksum** before acting, rather than trusting the
start byte alone. On a shared or noisy line a dropped byte resynchronises only
by luck.

That matters more here than it looks, because the module sends **unsolicited**
frames (Section 8.2) into the same stream as query replies.

## 7. Commands

### 7.1 Control commands (no reply unless Feedback is set)

| CMD | Function | Parameter |
| --- | --- | --- |
| `0x01` | Next | |
| `0x02` | Previous | |
| `0x03` | **Specify track (NUM)** | 0-2999, **physical index** (Section 9) |
| `0x04` | Increase volume | |
| `0x05` | Decrease volume | |
| `0x06` | **Specify volume** | **0-30** |
| `0x07` | Specify EQ | 0-5 = Normal/Pop/Rock/Jazz/Classic/Bass |
| `0x08` | Specify playback mode | 0-3 = Repeat / folder repeat / single repeat / random |
| `0x09` | Specify playback source | see the warning below |
| `0x0A` | Enter standby, low power | |
| `0x0B` | Normal working | |
| `0x0C` | Reset module | |
| `0x0D` | Playback (resume) | |
| `0x0E` | Pause | |
| `0x0F` | **Specify folder to playback** | folder 1-10, file -- *"need to set by user"* |
| `0x10` | Volume adjust set | `DH=1` open adjust, `DL` gain 0-31 |
| `0x11` | Repeat play | 1 = start, 0 = stop |
| `0x12` | **Play from `/mp3/NNNN.mp3`** | 1-9999, **by filename** |
| `0x13` | Advertise (interrupt with `/advert/NNNN.mp3`) | |
| `0x14` | Play large folder | folder in high 4 bits, file in low 12 |
| `0x16` | **Stop** | |

`0x12`, `0x13` and `0x14` are absent from the datasheet's own command tables and
are read here from DFRobot's library (`playMp3Folder`, `advertise`,
`playLargeFolder`). They are universally supported and universally undocumented
by the vendor -- **which is itself a finding**, because `0x12` is the command a
correct integration should be built on (Section 10.2).

> [!WARNING]
> **The datasheet's device numbering for `0x09` is wrong, and the module's own
> reply proves it.** The command table reads *"Specify playback
> source(0/1/2/3/4) -- U/TF/AUX/SLEEP/FLASH"*, which would make TF card `1`.
>
> But the power-on report `0x3F` returns a **bitmask**, and the datasheet lists it
> explicitly: U-Disk `0x01`, TF Card `0x02`, PC `0x04`, FLASH `0x08`. DFRobot's
> library agrees (`DFPLAYER_DEVICE_U_DISK 1`, `SD 2`, `AUX 3`, `SLEEP 4`,
> `FLASH 5`) and parses that bitmask at `:165-175` as bit 0 = USB, bit 1 = card.
>
> So **TF card is `2`, not `1`**, and the parenthetical `(0/1/2/3/4)` is a
> translation artefact. Reasoned from the bitmask rather than looked up; the bench
> test is to send `0x09` with `1` and with `2` and see which one makes an SD-only
> module play.
>
> Note also that the datasheet has a **PC** device at `0x04` where the library has
> `AUX=3`. The two device enumerations are not the same list, and nothing in
> either document reconciles them.

### 7.2 Query commands (always reply)

| CMD | Query | Note |
| --- | --- | --- |
| `0x3F` | Send initialization parameters | bitmask of online devices |
| `0x42` | **Current status** | play state |
| `0x43` | Current volume | |
| `0x44` | Current EQ | |
| `0x45` | Current playback mode | |
| `0x46` | Current software version | |
| `0x47` / `0x48` / `0x49` | **Total file count** | per device -- see below |
| `0x4B` / `0x4C` / `0x4D` | **Current track** | per device -- see below |
| `0x4E` | File count in folder | |
| `0x4F` | Folder count | not in the datasheet table; from the library |

> [!WARNING]
> **The datasheet and DFRobot's own library disagree on which device each query
> addresses.**
>
> | | Datasheet says | Library sends |
> | --- | --- | --- |
> | `0x47` | *"total number of TF card files"* | U-disk |
> | `0x48` | *"total number of U-disk files"* | **SD/TF** |
> | `0x49` | flash | flash |
> | `0x4B` | *"current track of TF card"* | U-disk |
> | `0x4C` | *"current track of U-Disk"* | **SD/TF** |
>
> The library's ordering (U, TF, flash) is consistent with the `0x3F` bitmask and
> with the device numbering resolved above, so **the library is probably right and
> the datasheet's device labels are another translation artefact** -- the same
> defect class as `0x09`. But this is inference from a pattern, not a primary
> statement, and it decides which byte a driver sends to read the SD track count.
>
> **Bench test:** with only an SD card inserted, send `0x47` and `0x48` and see
> which returns a plausible count (Open Item 2).

## 8. What the module sends back

Three different kinds of frame arrive on the same wire, and a decoder must handle
all of them.

### 8.1 Replies to queries

A query returns the same `CMD` with the value in `para1`/`para2`. Straightforward.

### 8.2 Unsolicited frames -- the ones that break a naive driver

| CMD | Meaning | Parameter |
| --- | --- | --- |
| `0x3A` | **Device inserted** | `0x01` U-disk, `0x02` TF card |
| `0x3B` | **Device removed** | `0x01` U-disk, `0x02` TF card |
| `0x3C` | U-disk finished a track | track number |
| `0x3D` | **TF card finished a track** | track number |
| `0x3E` | Flash finished a track | track number |
| `0x3F` | Power-on device report | bitmask |

**These arrive whenever the module feels like it**, interleaved with query
replies. A driver that issues a query and reads the next ten bytes as the answer
will eventually read a track-finished notification instead. Match on `CMD`, do not
assume ordering.

> [!NOTE]
> **`0x3D` is genuinely useful to us.** It is a track-completion event, pushed, at
> no polling cost -- which is what ADR 0043-style "schedule from arrival" logic
> wants for audio, and what `AUDIO_CAP_QUERY_SAFE_PLAYING` exists to avoid needing.
> The catch is Section 14.2: on artoo-esp32 the RX line is only ours when DomeLink
> is not using it, so pushed events can be missed entirely.
>
> Note also the datasheet's red annotation under this very table calls `3D` a
> *"U-disk command"* while its own table two lines above assigns `3D` to the TF
> card. Another internal contradiction, in the same document, on the same page.

### 8.3 Error returns

| Frame | Meaning |
| --- | --- |
| `0x40` param `0x00` | Module is busy |
| `0x40` param `0x01` | *"A frame data are not all received"* |
| `0x40` param `0x02` | **Verification (checksum) error** |

*"The module returns busy, basically when module power-on initialization will
return, because the modules need to initialize the file system."*

The `0x02` return is the one that matters: **a module that reports it is a module
that validates checksums**, and therefore one that rejects code derived from the
datasheet's worked examples (Section 6.2).

### 8.4 Power-on behaviour, and a droid that squawks when you touch the card

Two documented behaviours with real consequences:

- **Initialisation takes 1.5-3 s.** *"The module power on, require a certain of
  the time initialization, this time is determined by U-disk, TF card, flash, etc.
  device's file numbers, general situation in the 1.5 ~ 3Sec."* And explicitly:
  *"MCU will not send corresponding control commands until module initialization
  sending commands or the module will not process the commands sent by MCU, and
  will also affect the normal initialization of the module."* **Wait for `0x3F`
  before sending anything.** The time scales with file count, so a 225-sound droid
  card is at the slow end.
- **Inserting a card auto-plays track 1.** *"When push-in device, we default
  playback the first track of device root directory as audition, if users do not
  need this feature, you can wait 100ms after receiving the message of push-in
  serial device, and then send pause command."* So hot-swapping an SD card makes
  the droid talk, and suppressing it requires firmware to watch for `0x3A` and
  answer with a pause.

One more, useful rather than dangerous: *"The module will enter into pause status
automatically after being specified playing"* -- a specified track plays once and
stops, rather than rolling into the next file. That is the behaviour a droid wants
and it needs no configuration.

## 9. The SD card contract, which is where this module actually bites

### 9.1 Three addressing modes, and only two of them are stable

| Command | Addresses by | Path convention | Stable across a card rebuild? |
| --- | --- | --- | --- |
| `0x03` | **physical index in FAT allocation order** | anywhere | **No** |
| `0x12` | filename | `/mp3/NNNN.mp3`, 1-9999 | **Yes** |
| `0x0F` | folder + filename | `/NN/NNN.mp3`, folders 1-10; `0x14` packs folder into the high 4 bits and file into the low 12 | **Yes** |

> [!CAUTION]
> **`0x03` is the command every tutorial uses and the one nobody should use.**
> It plays *"the Nth file the filesystem happens to list"*, which is the order the
> files were **written**, not the order they are named. Copy a folder of sounds
> onto a card and the two usually agree; re-copy one file, delete and replace one,
> drag them in a different order, or let a file manager parallelise the copy, and
> they silently diverge. Every sound then shifts and nothing reports an error.
>
> The hobby's standard workarounds are all about forcing write order -- copying
> files one at a time in numeric order, or running `fatsort` over the card
> afterwards. Both are instructions to a human that must be repeated perfectly
> every time the card changes.
>
> **`0x12` removes the problem instead of managing it**, and it is what
> `AudioDriver::playTrack()`'s own contract asks for -- *"1-based index (maps
> directly to SD card file number)"*. Our own dome fork uses `0x03` while
> documenting `0x12`'s layout (Section 10.2); **protoArtoo should use `0x12`.**

### 9.2 This project already knows this failure mode from a different module

`docs/sound_playback.md` records it for the DY-SV5W that ships today:

> "DY-SV5W expects contiguous numbering with no gaps in the sequence. If a number
> is missing, the module's internal track index does not align with filename
> intent and later files can be addressed as if they were the missing number."
>
> Present: `001.mp3`, `002.mp3`, `004.mp3` -- requesting track `003` may play
> `004.mp3`.

Same class of defect: an index the module derives for itself drifting away from
the number the builder typed. The DY-SV5W has no escape from it -- the
recommendation is the discipline, *"keep the root directory as strict NNN.mp3
contiguous files with no skipped numbers"*. **The DFPlayer does have an escape,
and taking it is the single most valuable decision in this sheet.**

The same section also carries the hidden-file warning, which applies identically
here: *"avoid hidden files (macOS `._` files cause issues)"*. A card prepared on a
Mac carries a `._0001.mp3` resource fork beside every track and a
`.Spotlight-V100` directory; under `0x03` each of those is **a file in the index**.

### 9.3 Practical rules for a protoArtoo card

1. Use `/mp3/NNNN.mp3`, four digits, zero-padded, and address with `0x12`.
2. Prepare the card on Linux or Windows, or clean it afterwards
   (`dot_clean`, or delete `._*` and `.Spotlight-V100`).
3. Keep numbering contiguous anyway -- it costs nothing and it keeps the card
   portable to a DY-SV5W or an MP3 Trigger droid, which is a real thing builders do
   (Section 11).
4. Read back `0x48` (total files) at `begin()` and compare it to what the
   configuration expects. A mismatch is the cheapest possible detection of a
   card that was rebuilt wrong.


## 10. What our own dome has already proven

> [!IMPORTANT]
> **We already ship a DFPlayer integration.** `~/Documents/GitHub/AstroPixelsPlus`
> -- our own fork, the droid's dome controller -- drives a DFPlayer Mini through
> `MarcduinoSound.h`, pinned to `DFRobotDFPlayerMini#V1.0.6` in its
> `platformio.ini`. This section is what that deployment already teaches, and like
> the PCA9685 sheet's Section 10 it is the most valuable material here, because it
> is ours and it is real.

### 10.1 The bank model, and where protoArtoo's differs

`MarcduinoSound.h:35-53` implements the community bank convention:

```cpp
#define MP3_MAX_BANKS                9  // nine banks
#define MP3_MAX_SOUNDS_PER_BANK     25  // no more than 25 sound in each
#define MP3_BANK_CUTOFF              4  // cutoff for banks that play "next" sound on $x
```

with a flat file number derived at `:156`:

```cpp
filenum = (bank - 1) * MP3_MAX_SOUNDS_PER_BANK + sound;
```

Nine banks of twenty-five is **225 files**, named by role: gen, chat, happy, sad,
whistle, scream, Leia, sing, mus. Banks 1-4 advance to the *next* sound on a bare
`$x`; banks 5-9 always replay the first.

**protoArtoo does not use this model.** `docs/sound_playback.md` Section 3 maps
`$nnn` straight to `playTrack(nnn)` with named slots (`snd_scream`, `snd_leia`,
`snd_cantina_s` ...) held in NVS and configurable without a rebuild. That is a
deliberate and better design -- the bank arithmetic is a fixed 25-wide grid that
wastes slots in sparse banks, and our own fork's own comments admit the sparsity
(`MP3_BANK4_SOUNDS 4`, `MP3_BANK5_SOUNDS 3`). **But it means a builder moving an
SD card from a MarcDuino-family droid to protoArtoo keeps the file numbers and
loses the bank semantics**, which is a documentation problem rather than a code
one.

### 10.2 The fork calls `play()`, and its own documentation describes `playMp3Folder()`

> [!WARNING]
> **A real inconsistency in our own deployment.**
>
> `MarcduinoSound.h:191` plays with:
> ```cpp
> fDFMini.play(filenum);
> ```
> which the library maps to **`0x03`** -- and `0x03` selects a track **by physical
> index in the card's FAT allocation order**, not by filename.
>
> But `docs/SETUP.md:1050` tells the builder:
> > *"SD card is inserted in DFPlayer with correct folder structure
> > (`/mp3/0001.mp3`)"*
>
> `/mp3/NNNN.mp3` is the addressing scheme of **`0x12`** (`playMp3Folder`), which
> *is* filename-based. The fork documents one contract and implements another.
>
> It works today only because a card written once, in order, into a single folder
> happens to make physical index and filename agree. Re-copy one file, or let the
> operating system write them in a different order, and every sound shifts.

**For protoArtoo this settles a design decision rather than merely warning about
one.** `AudioDriver::playTrack()` is documented as *"Play a specific track by
1-based index (maps directly to SD card file number)"*. Only `0x12` delivers that
sentence. **A protoArtoo DFPlayer driver should use `0x12` with `/mp3/NNNN.mp3`,
not `0x03`** -- and should say so in builder documentation, because it is the one
choice that makes a sound's identity survive a card being rebuilt.

### 10.3 `begin()` can block for ~2.2 s, and our fork treats failure as fatal

`MarcduinoSound.h:419` is:

```cpp
if (!fDFMini.begin(stream))
{
    DEBUG_PRINTLN("Unable to begin:");
    ...
    return false;
}
```

Called with library defaults, `begin()` is `begin(stream, isACK=true, doReset=true)`,
which runs:

```cpp
if (doReset) {
    reset();
    waitAvailable(2000);
    delay(200);
}
```

So a missing or silent module costs **2.2 seconds at boot** and then disables
sound entirely for that session. `waitAvailable()` spins on `delay(0)`, which
yields on ESP32 rather than hard-blocking -- better behaved than the Maestro
library's `while (available() < N);` -- but 2.2 s is still a long time inside a
boot path.

protoArtoo's contract is friendlier and a driver should use it:
`AudioDriver::begin()` *"Returns true when command-side initialisation completed;
false only for a transient failure that should be retried by AudioTask"*, and the
Audio Step Core already owns an **init-retry lifecycle**. **A DFPlayer driver
should bound its own wait and return false to be retried, not adopt the library's
2 s reset-and-hope.**

### 10.4 The dome's pin conflict is a lesson about UART scarcity, not about this module

`docs/HARDWARE_WIRING.md:1177` records that on the dome the DFPlayer occupies
`Serial1` (AUX4/AUX5) and therefore **excludes FireStrip and BadMotivator**, with
the builder choosing one or the other in `platformio.ini`. That is the same
scarce-UART story protoArtoo has on artoo-esp32 (Section 14.2), reached
independently on a different board. It is worth citing to a builder as evidence
that the constraint is structural rather than a protoArtoo quirk.

## 11. How the hobby actually drives sound (non-normative)

Sound is the one subsystem every astromech project has, which makes this the best
cross-project comparison available anywhere in the lineup. Everything in this
section was read as code, on this disk or cloned this session.

### 11.1 The shape almost everyone has: the host owns numbers, the module owns files

Across every project read, the host firmware holds **a bare integer per sound**
and the mapping from integer to audio file lives on the SD card. No project reads
a sound's name, duration or existence back from its module. The interesting
differences are only in **how the integer is computed** and **who owns the
vocabulary**.

| Project | Sound hardware | Addressing | Who owns the vocabulary |
| --- | --- | --- | --- |
| **ShadowMD** | **none** | -- | MarcDuino, by function code |
| **MarcDuino** | its own | file number | itself |
| **Padawan360** (DY-SV5W fork) | DY-SV5W | flat track number | the sketch, inline |
| **AstroPixelsPlus** (ours) | **DFPlayer Mini** | `(bank-1)*25 + sound` | the `$` command |
| **CHIRP** | itself (RP2350) | bank + page + index, with a catalog | a manifest on the card |
| **protoArtoo** | pluggable | flat track number, **named slots in NVS** | `docs/sound_playback.md` |

**protoArtoo and CHIRP are the only two that treat the vocabulary as data rather
than as code**, and protoArtoo is the only one where a builder can rename what
`$L` plays without a rebuild.

### 11.2 ShadowMD: no sound hardware, and an MP3 Trigger namespace anyway

`src/Shadow_MD_DualController_Template.ino` owns no sound module -- grep
validated, zero hits for `servo` or any player library, and its header says
*"SHADOW_MD: Small Handheld Arduino Droid Operating Wand + MarcDuino"* with
*"BODY PANEL OPTIONS ASSUME SECOND MARCDUINO MASTER BOARD ON MEGA ADK SERIAL #3"*.

It commands sound as **numbered MarcDuino functions**:

```
//     2 = Scream - all panels open
//     6 = Beep cantina - w/ marching ants panel action
//     8 = Cantina Dance - orchestral, rhythmic panel dance
//     9 = Leia message
//    26 = Volume Up      27 = Volume Down
//    28 = Volume Max     29 = Volume Mid
```

and where a button plays a *specific* sound, the field is explicitly an MP3
Trigger file number, repeated on every custom button block:

```
// CUSTOM SOUND SETTING: Enter the file # prefix on the MP3 trigger card of the
// sound to play (0 = NO SOUND)
```

Two things follow. **The SHADOW family's namespace is filename-based** -- an
MP3 Trigger plays `NNN-name.mp3` by its numeric prefix -- so a DFPlayer driven by
`0x03` (physical order) does not provide the namespace this ecosystem assumes.
And **its volume vocabulary is up/down/max/mid**, which is exactly protoArtoo's
`$+ $- $f $m`, so that mapping is already aligned.

### 11.3 Padawan360: the same numbers survive a change of module

`~/Documents/GitHub/Padawan360_mega_maestro_DYSV5W` is a fork whose whole purpose
is swapping the sound module -- its header says *"This sketch is to beta test the
DY-SV5W audio player instead of sparkfun mp3"*. Every call site is preserved with
the old one commented out directly above:

```cpp
      //mp3Trigger.play(21);
    player.playSpecified(21);
...
       // mp3Trigger.play(random(32, 52));
        player.playSpecified(random(32, 52));
...
      //mp3Trigger.play(5);     // leia message L1+X
       player.playSpecified(5);
```

> [!IMPORTANT]
> **The track numbers did not change when the module did.** That is the single
> most useful fact in this section. The hobby treats the sound *number* as a
> property of the droid's SD card, portable between modules -- so a builder moving
> from an MP3 Trigger or a DY-SV5W to a DFPlayer expects sound 21 to stay sound 21.
>
> `0x03` breaks that expectation silently, because it addresses by write order
> rather than by name. `0x12` preserves it. This is the ecosystem's own argument
> for Section 9.1's decision, arrived at independently of protoArtoo's interface
> contract.

Its observable vocabulary, extracted in full: named one-shots at 1-12 (5 is the
Leia message), 21, and 52/53 as drive-enable and drive-disable chirps, with random
ranges `random(13,17)`, `random(17,25)` and `random(32,52)`.

**Two defects worth recording**, both from reading the code rather than the
README:

- At `:563-564` the comment reads `//mp3Trigger.play(random(25, 32));` while the
  live line is `player.playSpecified(random(32, 52));`. The port changed the range
  and left the old comment. One category of sound is now unreachable from that
  button.
- `:99` declares `// Default sound volume at startup / 0 = full volume, 255 off`
  with `byte vol = 25;` -- and then passes `vol` to `player.setVolume()`.
  `SnijderC/dyplayer`'s header says *"Set the playback volume between 0 and 30.
  Default volume if not set: 20."* **The comment describes the MP3 Trigger's
  inverted 0-255 scale while the call takes the DY-SV5W's ascending 0-30 scale.**
  `25` happens to be loud on both readings, which is why nobody noticed.

That second one is a warning for us, not a criticism of them: **volume semantics
do not survive a module swap, and a stale comment is how the old scale gets
carried forward.** protoArtoo's answer already exists -- the interface normalises
to 0-30 and `docs/sound_playback.md` documents the MP3 Trigger's inversion
explicitly (*"vol=0 -> nativeVol=255 (silent) ... vol=30 -> nativeVol=0
(maximum)"*). The DFPlayer needs no conversion at all, which is one fewer place to
get this wrong.

### 11.4 CHIRP is a different tier, and says so

`~/Documents/GitHub/CHIRP`'s README describes *"an advanced MP3 and WAV file
decoder/mixer/player, heavily inspired by the Sparkfun/Robertsonics MP3 Trigger
that has been used across droid control systems since the original Padawan PS2
days"*, running *"3+ Independent Audio Streams (WAV, MP3 or AAC)"* on an RP2350
with an I2S DAC and a `CHIRP.INI` on the card.

protoArtoo's CHIRP backend is correspondingly the richest of the three shipping
ones -- `AUDIO_CAP_CATALOG`, bank/page addressing, a manifest query at boot, and
capabilities `0x1F` meaning **safe to query during playback**.

**Nothing about the DFPlayer competes with that**, and the sheet should not
pretend otherwise. They answer different questions: CHIRP is what a droid with
layered audio needs; a DFPlayer is a complete one-shot sound system for the price
of a coffee, with the amplifier included.

### 11.5 The convention is MarcDuino's, and three projects implement it differently

The 9-bank model is **MarcDuino's**, and its header is the text every other
project copies (`nhutchison/MarcDuinoMain`, `MP3sound.h:8-21`):

```
 *       Bank 1: gen sounds, numbered 001 to 025
 *       Bank 2: chat sounds, numbered 026 to 050
 *       Bank 3: happy sounds, numbered 051 to 075
 *       Bank 4: sad sounds, numbered 076 to 100
 *       Bank 5: whistle sounds, numbered 101 to 125
 *       Bank 6: scream sounds, numbered 126 to 150
 *       Bank 7: Leia sounds, numbered 151 to 175
 *       Bank 8: sing sounds (deprecated, not used by R2 Touch)
 *       Bank 9: mus sounds, numbered 201 t0 225
```

with `filenum = (bank-1)*MP3_MAX_SOUNDS_PER_BANK + sound;` at `MP3sound.c:391`
-- identical to our dome fork's arithmetic, because our fork inherited it.

> [!IMPORTANT]
> **Three projects implement the same bank model over three different DFPlayer
> commands, and only one of them is write-order-proof.**
>
> | Project | Command | Addressing | Survives a card rebuild? |
> | --- | --- | --- | --- |
> | **MarcDuino** `MP3sound.c:611` | **`0x12`** | `/MP3/NNNN.mp3` by filename | **yes** |
> | **Penumbra / AstroPixelsPlus** `MarcduinoSound.h:192` | `0x03` | flat index = SD write order | **no** |
> | **Printed Droid** DFPlayer sketch | `0x14` | play-in-large-folder | folder 0, unclear |
>
> MarcDuino -- the origin of the convention -- got it right, and its own comment
> says so: `// CMD (12 play mp3 folder) 03 - root`. **The Reeltwo port silently
> converted a write-order-proof scheme into a write-order-dependent one**, and our
> dome fork inherited that (Section 10.2).
>
> This is independent confirmation of Section 9.1's decision from the direction
> that matters most: **the ecosystem's reference implementation already uses
> `0x12`.** protoArtoo should copy MarcDuino here, not Reeltwo.

### 11.6 MarcDuino's checksum drifts, and the stop idiom poisons the next sound

Worth recording because protoArtoo interoperates with MarcDuino's `$` vocabulary,
and because it explains why the whole ecosystem tolerates bad checksums.

`MP3sound.c:585-591`:

```c
void sendDFP (uint8_t *cmd) {
  uint16_t checksum = 0;
  for (int i=2; i<8; i++) {
    checksum += cmd[i];
  }
  cmd[7] = (uint8_t)~(checksum >> 8);
  cmd[8] = (uint8_t)~(checksum);
```

Two departures from the specification: the sum runs over bytes **2..7** rather
than 1..6 -- omitting the `0xFF` version byte and **including `cmd[7]`, the
previous call's checksum high byte** -- and it takes a one's complement where the
rule is a two's complement. Those two errors cancel *exactly* while `cmd[7]` holds
`0xFE`. And `play_cmd` is declared `static` (`:611`), so that byte persists
between calls.

Transcribed verbatim and executed this session, in call order:

```
track    1  marcduino=0xfee8  spec=0xfee8  ok     (stale cmd[7] now 0xfe)
track  233  marcduino=0xfe00  spec=0xfe00  ok     (stale cmd[7] now 0xfe)
track  234  marcduino=0xfdff  spec=0xfdff  ok     (stale cmd[7] now 0xfd)
track  235  marcduino=0xfdff  spec=0xfdfe  *** MISMATCH ***
track  252  marcduino=0xfdee  spec=0xfded  *** MISMATCH ***
track   10  marcduino=0xfee0  spec=0xfedf  *** MISMATCH ***
track    1  marcduino=0xfee8  spec=0xfee8  ok
```

Above track 233 the sum crosses into `0x02xx`, `cmd[7]` becomes `0xFD`, the
cancellation stops working, and **the corruption is sticky**: track 10 -- an
entirely ordinary low number -- comes out wrong purely because track 252 played
before it. `MP3_EMPTY_SOUND` is **252** and is the idiom used to stop playback,
so *stopping a sound poisons the next one*.

It has been in the field for years without anyone noticing, which is the clearest
possible evidence for Section 2.1: **most modules do not check the checksum.**

**For protoArtoo the lesson is not "MarcDuino is broken" -- it is that a frame
builder must be a pure function with no static state, and its output must be
asserted in a native test.** `test_audio_frames.cpp` already does exactly that for
the DY-SV5W.

### 11.7 Negative results across the ecosystem

| Project | DFPlayer? | Note |
| --- | --- | --- |
| `dankraus/padawan360` (upstream) | **none** | MP3 Trigger only, and its numbering is **flat 1-53, not MarcDuino's banks** -- the two card layouts are not interchangeable |
| `reeltwo/Reeltwo` (the library) | **none** | `MarcduinoSound.h` is copy-pasted per sketch; `ReeltwoAudio` is ESP32 I2S -- the R-Series direction is **no external module at all** |
| `joymonkey/dEvolution` (ShadyRC) | **none** | MP3 Trigger, TX-only soft serial, but **uses MarcDuino's global numbering** |
| `PrintedDroid/AstroCan-X-System` | **none** | current generation is **onboard I2S from SD**; no sound module |
| `RealNobser/AstroCommsFirmware` | **none** | MP3 board shares the dome TX line |
| CHIRP | **none** | it *is* a sound module (Section 11.4) |
| `r2d2-astromech-simulator` | **none** | names DY-SV5W and MD-YX5300 |

Two structural observations follow. **Penumbra's runtime default is HCR**, not
DFPlayer and not MP3 Trigger -- so the family protoArtoo's dome fork descends from
has already moved toward a speech-synthesis module for new builds. And **the
newest commercial designs have dropped external sound modules entirely** in favour
of I2S from an SD card on the main board. The DFPlayer's place on the lineup is
the cheap, simple, universally-available option -- not the direction the hobby's
high end is travelling.
## 12. Libraries

| Library | Last commit | Maintained | Blocking reads | Clone handling |
| --- | --- | --- | --- | --- |
| `DFRobot/DFRobotDFPlayerMini` | 2023-06-26 (V1.0.6) | effectively no -- *"Is this library still maintained?"* open and unanswered since 2024 | yes, 500 ms -- **and one path with no timeout at all** | none |
| `PowerBroker2/DFPlayerMini_Fast` | 2021-08-25 | no | yes, busy-spins with no `yield()` | none |
| **`Makuna/DFMiniMp3`** | 2024-02-06 | yes | 900 ms ack, 3 retries | **yes -- three chip classes** |

`Makuna/DFMiniMp3` is the only library that models the problem in Section 2.1,
via template chip types (`Mp3ChipOriginal`, `Mp3ChipIncongruousNoAck`,
`Mp3ChipMH2024K16SS`). If protoArtoo ever wanted a library rather than 300 lines
of its own, that is the one -- but see Section 14.1 for why it should not.

### 12.1 The official library can hang forever, and our dome fork is on that path

> [!CAUTION]
> **`DFRobotDFPlayerMini::sendStack()` contains an unbounded loop, and a droid is
> the case that triggers it.** Traced end to end in V1.0.6 source this session:
>
> ```cpp
> void DFRobotDFPlayerMini::sendStack(){
>   if (_sending[Stack_ACK]) {
>     while (_isSending) { delay(0); waitAvailable(); }   // no timeout here
>   }
> ```
>
> 1. `_isSending` is set true whenever ACK mode is on (`:52`), and is cleared in
>    exactly one place -- `parseStack()` on receiving a `0x41` ACK (`:153`).
> 2. `available()` returns `_isAvailable` (`:285`), a **sticky** flag set by
>    `handleMessage()` (`:135`) and cleared only by `readType()` / `read()`.
> 3. `waitAvailable()` returns `true` the moment `available()` is true -- so its
>    own `millis()` timeout is never reached.
> 4. An **unsolicited** `0x3C`/`0x3D` track-finished frame routes to
>    `handleMessage()`, which sets `_isAvailable` and **does not** clear
>    `_isSending`.
>
> So: play a sound, let it finish, do not drain the notification, send another
> command -- and `sendStack()` spins forever. **A droid plays sounds and lets them
> finish.** The smoking gun that the guard was removed rather than never present:
> `_timeOutTimer` is declared, assigned once at `:51`, and **read nowhere in the
> library** -- a dead field where the timeout used to be.
>
> **Our own AstroPixelsPlus fork is on this path.** `MarcduinoSound.h:419` calls
> `fDFMini.begin(stream)` with library defaults, which means `isACK=true`, and
> nothing in it ever drains the notification queue.
>
> This is decisive for protoArtoo: **do not use this library.** A driver must own
> its own bounded read loop, exactly as `audio_dy_sv5w.cpp` already does with its
> 300 ms `sendQuery()` timeout.

Other defects in the same library, verified in source: `begin()` returns `true`
unconditionally when ACK is disabled (`|| !isACK` at `:118`); the
`DFPlayerCardUSBOnline` branch at `:172` is unreachable, because any value with
bit 0 or bit 1 set is caught earlier; and replies are never correlated to
requests, so a track-finished notification is readily consumed as a query answer.
## 13. How the four Sound members differ

| | DY-SV5W | MP3 Trigger | CHIRP | **DFPlayer Mini** |
| --- | --- | --- | --- | --- |
| Status | `supported` | `supported` | `supported` | **`roadmap`** |
| Transport | binary, 9600 | binary, 9600 | ASCII, configurable | **binary, 9600** |
| Frame | `0xAA CMD LEN .. SM` (4+ B) | short binary | ASCII lines | **fixed 10 B** |
| Volume native | 0-30 | **0-255, inverted** | -- | **0-30** |
| Simultaneous streams | 1 | 1 | **3+, mixed** | 1 |
| Decoding | hardware | VS1053 | software, RP2350 | hardware |
| Addressing | index, **contiguous required** | file number | catalog + bank/page | **index *or* filename** |
| Play-state query | yes | **no** -- *"always shows unknown"* | yes | yes (`0x42`) |
| Track-finished event | -- | -- | -- | **pushed (`0x3D`)** |
| Hardware busy pin | -- | -- | -- | **yes (`BUSY`)** |
| On-board amplifier | -- | -- | -- | **yes, <3 W** |
| Catalog support | no | no | **yes** (`AUDIO_CAP_CATALOG`) | no |
| Board identity | fixed vendor part | fixed vendor part | fixed vendor part | **unreliable (Section 2)** |

**These are peers with genuinely different shapes, and the lineup is right to hold
all four.**

**CHIRP is a different tier of thing, not a better DFPlayer.** Its own README
describes it as *"an advanced MP3 and WAV file decoder/mixer/player, heavily
inspired by the Sparkfun/Robertsonics MP3 Trigger"*, running three or more
simultaneous streams on an RP2350 with an I2S DAC. A droid that wants an ambient
bed under speech needs mixing and therefore needs CHIRP. A DFPlayer plays one file
at a time and always will.

**What the DFPlayer uniquely brings to this family** is the bottom of the price
range with an amplifier included, plus two observability features no current
member has: a **pushed track-finished event** and a **hardware busy pin**. The
MP3 Trigger, at the other end, cannot report play state at all -- protoArtoo's own
documentation says its indicator *"always shows `unknown`"*.

**What it uniquely costs** is identity. The other three are specific boards from
specific vendors. "A DFPlayer Mini" is a form factor that several unrelated
silicon vendors fill, and Section 2 is the consequence.

## 14. What protoArtoo would have to do

### 14.1 The seam already exists, and it appears to have been shaped around this module

> [!IMPORTANT]
> **This is the only part on the whole roadmap that adds a member to a finished
> family instead of creating one.** The PCA9685 sheet's headline was *"this part
> does not slot into an existing seam -- it creates one"*, and the Maestro's was the
> same. Here the opposite is true, and the fit is closer than "it fits".

Four pieces of evidence that the `AudioDriver` interface was drawn with a
DFPlayer in view, even though no driver was ever written:

| Interface fact | DFPlayer fact |
| --- | --- |
| `AUDIO_DFPLAYER = 2` reserved in `include/audio_driver.h:36` | -- |
| *"Volume range is normalised **0-30** at the interface level; concrete drivers scale to their module's native range if different"* | `0x06` takes **0-30**. **No scaling needed** |
| `playTrack()` -- *"1-based index (maps directly to SD card file number)"* | `0x12` does exactly that (Section 10.2) |
| `AudioModuleState { playState, device, totalTracks, currentTrack }` | `0x42`, `0x3F`, `0x48`, `0x4C` -- **one query each, nothing left over** |

The DY-SV5W, which is what ships today, is 0-30 too, so the volume range is not
proof on its own -- but `AudioModuleState`'s four fields mapping one-to-one onto
four DFPlayer queries, with no field unserved and no query wasted, is hard to read
as coincidence.

**What actually has to be written** is one file pair, `include/audio_dfplayer.h`
and `src/drivers/audio_dfplayer.cpp`, following `audio_dy_sv5w.cpp` (387 lines)
almost structurally: a `sendCommand()`, a `sendQuery()` with a bounded timeout, a
`begin()` that runs a couple of queries, and `queryModuleState()`. Plus one line
in `src/tasks/audio_task.cpp` replacing the `#error`.

**It reuses `AudioSerialIO` unchanged** -- the five function pointers
(`writeByte`, `rxAvailable`, `rxRead`, `delayMs`, `millisNow`) that already
separate framing from transport. Which leads directly to the next point.

### 14.2 It is the only roadmap part that can be verified without hardware

`test/test_native/` already carries eighteen audio suites, including
`test_audio_frames` (*"verify the payload bytes and checksum computation are
correct before any hardware connection is tested"*), `test_audio_io_seam`,
`test_audio_driver` and `test_audio_uart_claim`.

> [!NOTE]
> **This corrects this ticket's own fourth checkbox.** #305 says *"Verification --
> this needs a signal on a pin, so it is droid-gate work, not Bench-Mode."* That is
> true of the **audible** half and false of the **protocol** half. Frame bytes,
> checksums, the `0x12`-versus-`0x03` decision, unsolicited-frame handling, reply
> matching and the device-code translation are all natively testable with an
> injected `AudioSerialIO`, on a laptop, with no droid. Given Section 6.2 --
> a vendor datasheet whose every worked example is wrong -- that is not a
> convenience, it is the main defence.

### 14.3 The board budget, and why the soft UART is fine here

| | artoo-esp32 | firebeetle2 |
| --- | --- | --- |
| Audio TX | `PIN_AUDIO_TX = 26`, **bit-banged** soft UART at 9600 | `PIN_AUDIO_TX = 34`, hardware `UART_PORT_AUDIO = 3` |
| Audio RX | `PIN_AUDIO_RX = 35`, **shares UART2 with DomeLink** via `domeUartAcquire()` | `PIN_AUDIO_RX = 36`, dedicated |
| `PA_CAP_DEDICATED_AUDIO_UART` | `0` | `1` |

**The DFPlayer's fixed 9600 baud is exactly what the existing soft UART does**, so
no new transport is needed on either board. The cost, from
`src/drivers/audio_soft_uart_tx.h`'s own measured header (*"~1.04 ms per byte"* in
a Core 0 `portMUX` critical section):

| Command | Bytes | Core 0 blocked |
| --- | --- | --- |
| DY-SV5W play (today) | 4-5 | ~5 ms |
| **DFPlayer play** | **10** | **~10.4 ms** |

Roughly double, and the header's justification still holds -- *"Audio commands are
infrequent (at most a few per second)"* -- so a few plays per second costs around
3 % of Core 0. **Unlike the Maestro, nothing here wants to stream**, so the
soft-UART route is sufficient rather than merely tolerable. Contrast
[`pololu-maestro-servo-controller.md`](pololu-maestro-servo-controller.md)
Section 14.3, where the same transport was disqualified.

> [!WARNING]
> **On artoo-esp32 the read path is not reliably ours.** The RX line shares UART2
> with the dome link, and `AudioDriver::classifyRxStatus()` exists precisely for
> this -- its own comment warns that *"forgetting to override in such a driver
> causes false 'No module response' errors in the UI"*. A DFPlayer driver **must**
> override it to return `BLOCKED_BY_DOME_UART`.
>
> Two consequences specific to this module: the **unsolicited** `0x3D`
> track-finished and `0x3A`/`0x3B` card events (Section 8.2) will be **missed**
> whenever DomeLink holds the bus, so nothing may depend on receiving them; and
> the `BUSY` pin (Section 5.3) becomes the only always-available playback
> indication on that board. If a spare GPIO exists, wiring `BUSY` is worth more
> here than any query.

### 14.4 The device-code translation, which is a one-line bug waiting to happen

`AudioModuleState.device` is documented `0=USB 1=SD/TF 2=FLASH 0xFF=unknown`.
The DFPlayer's are `1=U-disk 2=SD 5=FLASH` (library) over a `0x3F` bitmask of
`0x01 / 0x02 / 0x08`.

**Nothing matches.** A driver that passes the module's byte straight through
reports an SD card as `FLASH` and a USB stick as `SD/TF`. Translate explicitly,
and assert the translation in a native test.

### 14.5 Registry, Gate, Member

**Component Protocol.** #303's **DFPlayer serial** stands. It changes the driver
-- a different frame, a different checksum, a different addressing model -- so it
passes `CONTEXT.md`'s test.

**Board Capability Gate.** **Probably none needed**, and this is the one category
where that answer is easy: every Sound member already wires to the same audio TX
pin on each board, which is the condition ADR 0042 names for the sound family
(*"all three wire to `PIN_AUDIO_TX = 26`"*). A DFPlayer is a fourth device on the
same pin at the same baud. If it wants `BUSY`, that is a spare GPIO and an
optional capability, not a gate.

**Component Member.** Sound is the family that **already has** a member, with
three entries. This adds a fourth. Under ADR 0042 the remaining question is
build-time versus runtime, and #303 already flags it: the three current members
are chosen by which firmware you flash (`artoo_esp32_chirp`,
`artoo_esp32_mp3trigger_check`), which is exactly what `not-in-this-build` exists
to describe. **Adding a DFPlayer does not change that question, but it does make
it more pressing**, because four build variants for one pin is the point at which
the build matrix argues for a runtime member.

### 14.6 Costs to state plainly

- **A cheap module with an unreliable identity.** Section 2 is the risk: what
  arrives in the post may not behave like what was tested. A driver should read
  `0x46` (software version) at `begin()` and log it, so a field report carries
  the one identifying number available.
- **The vendor documentation cannot be trusted byte-for-byte.** Three wrong
  worked examples, a self-contradicting `BUSY` polarity, and two device
  enumerations that disagree with the vendor's own library. Every constant in a
  driver needs a native test asserting it.
- **Unsolicited frames must be tolerated**, and on artoo-esp32 must not be relied
  upon.
- **No status is free on artoo-esp32** without a `BUSY` wire.
- **Sound gains a fourth build variant** unless the member becomes runtime.
- **It is the cheapest sound member by a wide margin**, single-stream, with a
  3 W amplifier included -- which is the whole reason it is on the lineup.

## 15. Agent Lookup Quick Reference

- Field: Baud. Required value: **9600**, 8-N-1, effectively fixed (no serial command changes it).
- Field: Frame length. Required value: **exactly 10 bytes**, `0x7E` ... `0xEF`.
- Field: Frame layout. Required value: `7E VER LEN CMD FB par1 par2 cksHi cksLo EF`, VER=`0xFF`, LEN=`0x06`.
- Field: Checksum. Required value: **`-(sum of bytes 1..6)`**, 16-bit, high byte first. **Never copy the datasheet's examples.**
- Field: Play by filename. Required value: **`0x12`** with `/mp3/NNNN.mp3`. *Not* `0x03`.
- Field: Play by physical index. Required value: `0x03` -- addressed by FAT write order. Avoid.
- Field: Play from folder. Required value: `0x0F` (folder 1-10, file), `/NN/NNN.mp3`.
- Field: Volume. Required value: **`0x06`, range 0-30** -- identical to `AudioDriver`'s normalised range, no scaling.
- Field: Stop. Required value: `0x16`.
- Field: Pause / resume. Required value: `0x0E` / `0x0D`.
- Field: Reset. Required value: `0x0C`.
- Field: Query play state. Required value: `0x42`.
- Field: Query total SD files. Required value: `0x48` **per the library**; the datasheet says `0x47`. Unresolved -- Open Item 2.
- Field: Query current SD track. Required value: `0x4C` per the library; datasheet says `0x4B`. Same conflict.
- Field: Power-on device report. Required value: `0x3F`, **bitmask** -- U-disk `0x01`, TF `0x02`, PC `0x04`, FLASH `0x08`.
- Field: Device code for TF card in `0x09`. Required value: **`2`**, per the bitmask and the library. The datasheet's `(0/1/2/3/4)` list is wrong.
- Field: Unsolicited frames. Required value: `0x3A` insert, `0x3B` remove, `0x3C`/`0x3D`/`0x3E` track finished, `0x3F` power-on.
- Field: Error returns. Required value: `0x40` with `0x00` busy, `0x01` incomplete frame, `0x02` checksum failure.
- Field: Power-on init time. Required value: **1.5-3 s**; wait for `0x3F` before sending anything.
- Field: Card-insert behaviour. Required value: **auto-plays root track 1**; suppress with a pause ~100 ms after `0x3A`.
- Field: Track range. Required value: 0-2999 for `0x03`; 1-9999 for `0x12`.
- Field: VCC. Required value: **3.2-5.0 V, typical 4.2 V** -- 3.3 V is in specification.
- Field: BUSY polarity. Required value: **`UNKNOWN`** -- the datasheet states both. Measure before use.
- Field: Speaker output. Required value: SPK1/SPK2, **bridge-tied, under 3 W**, never into an amplifier input. Use DAC_L/DAC_R for line level.
- Field: protoArtoo driver constant. Required value: `AUDIO_DFPLAYER = 2` (`include/audio_driver.h:36`).

## 16. Open Items

| # | Item | How to settle it |
| --- | --- | --- |
| 1 | **`BUSY` polarity** -- the datasheet states both | Wire it to a GPIO, play a track, log the level. Five minutes (Section 5.3) |
| 2 | **`0x47` vs `0x48`** for the SD file count, and `0x4B` vs `0x4C` for current track | With only an SD card present, send both and see which returns a plausible value (Section 7.2) |
| 3 | **`0x09` device code for TF** | Send `0x09` with `1` and with `2`; see which makes an SD-only module play (Section 7.1) |
| 4 | **Which chipset is in the module we buy** | Read the chip marking; log `0x46` (software version) at `begin()` (Section 2) |
| 5 | **Does this module accept queries during playback** | Set `AUDIO_CAP_QUERY_SAFE_PLAYING` only after testing. CHIRP does (`0x1F`), DY-SV5W does not |
| 6 | **The 1 kohm series resistor on RX** | Community practice, not in the datasheet. Determine whether it is needed at 3.3 V |
| 7 | **Does the module we get validate checksums** | Send a deliberately wrong checksum and watch for `0x40 / 0x02` |
| 8 | **Audible verification** | Genuinely droid-gate: a speaker, a card, and a listener. The protocol half is native (Section 14.2) |

## 17. Sources

**Primary -- vendor**

- **DFR0299 datasheet V1.0**, DFRobot -- https://dfimg.dfrobot.com/wiki/20532/DFR0299_mp3-player-module_datasheet_V1.0.pdf. A **scanned PDF with no text layer**; rendered with `pdftoppm` and read as images.
- **FN-M16P Embedded MP3 Audio Module Datasheet**, Flyron -- same silicon family, **correct checksums**, and the two-column with/without-checksum command table.
- DFRobot product page DFR0299 -- https://www.dfrobot.com/product-1121.html (price, stock, and the `stockText` / `stockTextSeo` discrepancy).
- DFRobot wiki -- https://wiki.dfrobot.com/DFPlayer_Mini_SKU_DFR0299.
- GD3200A/B + MH2024K datasheet (GuoDian); TD5580A User Manual V1.3; YX5200-24SS Chip Manual V1.6 -- the clone families in Section 2.

**Primary -- source code**

- `DFRobot/DFRobotDFPlayerMini` V1.0.6 -- read in full: the checksum algorithm, the command map, the device constants, and the unbounded `sendStack()` loop.
- `nhutchison/MarcDuinoMain` `MP3sound.c` / `MP3sound.h` -- the bank convention's origin, its `0x12` play command, and the checksum drift reproduced in Section 11.6.
- `Makuna/DFMiniMp3` -- the only library that models clone chipsets as types.
- `SnijderC/dyplayer` -- DY-SV5W volume range, for the comparison in Section 11.3.

**Read locally, on this disk**

- `~/Documents/GitHub/AstroPixelsPlus` -- **our own dome fork**: `MarcduinoSound.h`, `docs/SETUP.md`, `docs/HARDWARE_WIRING.md`, `platformio.ini`.
- `~/Documents/GitHub/Padawan360_mega_maestro_DYSV5W` -- the MP3 Trigger to DY-SV5W port.
- `~/Documents/GitHub/ShadowMD` -- MarcDuino delegation, grep validated.
- `~/Documents/GitHub/CHIRP` -- the multi-stream tier.
- `~/Documents/GitHub/r2d2-astromech-simulator` -- negative result.

**Third party, ecosystem**

- `reeltwo/PenumbraShadowMD`, `reeltwo/Reeltwo`, `reeltwo/ReeltwoAudio`
- `dankraus/padawan360`, `joymonkey/dEvolution`
- `PrintedDroid/ShadowMD-AstroComms`, `PrintedDroid/AstroCan-X-System`, `RealNobser/AstroCommsFirmware`
- `dpoulson/r2_control` -- independent corroboration of the HCR vocabulary
- `ghmartin77/DFPlayerAnalyzer` issues #1-#25 -- a crowdsourced database of clone behaviour, and the best compatibility matrix that exists
- DFRobot library issues #51, #54, #63, #67, #69, #71; `Makuna/DFMiniMp3` issues #131, #139, #146, #148
- Printed Droid knowledge base -- the DFPlayer guide and the R2D2 sound packs

**protoArtoo**

- `include/audio_driver.h`, `include/audio_serial_io.h`, `src/drivers/audio_dy_sv5w.cpp`, `src/tasks/audio_task.cpp`, `src/drivers/audio_soft_uart_tx.h`, `include/config.h`
- `docs/sound_playback.md`, `docs/pin_map.md`, `CONTEXT.md`, ADR 0042, ADR 0043
- `test/test_native/test_audio_frames/`, `test_audio_io_seam/`, `test_audio_driver/`
- [`pololu-maestro-servo-controller.md`](pololu-maestro-servo-controller.md), [`pca9685-servo-expander.md`](pca9685-servo-expander.md)

> [!NOTE]
> **Negative results, recorded so nobody repeats them.** DFRobot has published
> nothing about the clone problem on the product page, either wiki page, or its
> 2025 module selection guide. `astromech.net` is login-walled, so every claim
> sourced to the forums here is second-hand through project repositories and
> vendor knowledge bases. There is no repository named `ShadowRC`; the code that
> exists is `joymonkey/dEvolution` (ShadyRC). `ghmartin77/DFPlayerMini` does not
> exist -- the useful repository of that author is `DFPlayerAnalyzer`. Kyber and
> Stealth are closed-source and their sound modules are `UNKNOWN`.
