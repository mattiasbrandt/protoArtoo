# MP3 Trigger Spec Sheet (SparkFun WIG-13720)

The **SparkFun MP3 Trigger** is a microSD MP3 player board designed by
Robertsonics, with eighteen trigger inputs and a one- and two-byte serial command
set. The astromech hobby standardised on it: MarcDuino is built around it, and
the 25-track sound banks every R2 sound pack ships in are its convention.

Research date 2026-09-12. Every command byte, electrical value and default below
was read from the **MP3 Trigger v2 User Guide PDF** (downloaded from
robertsonics.com and read in full, all nine pages), from **SparkFun's own Eagle
schematic** (cloned from `github.com/sparkfun/MP3_Trigger`), from the SparkFun
v2.4 hookup guide, or from the astromech projects that drive it. Claims that
could not be sourced are marked `UNKNOWN` with the artefact or bench test that
would settle them.

> [!WARNING]
> **The chip is a VS1063.** SparkFun's schematic names part `U7` as
> `deviceset="VS1063" device="SMD" value="VS1063"`, annotated *"VLSI VS1063 audio
> codec IC"*. Several secondary sources say VS1053. Nothing functional depends on
> the distinction -- the inverted volume register is the same across the VS10xx
> family -- but a reader chasing a datasheet should chase the right one
> (Section 2.2).

> [!IMPORTANT]
> **The SparkFun SKU is WIG-13720, not DEV-13720.** SparkFun's own repository
> README links `sparkfun.com/products/13720` as *"MP3 Trigger (WIG-13720)"*. A
> wrong part number is how a builder buys the wrong board.

## 1. Scope

Covers the board and what is on it, the electrical contract, the serial protocol
in full, the SD-card and file-naming contract, the initialization file, the
trigger-pin hardware path, and how the astromech hobby uses the board.

Does not cover: the VS1063's own SCI register set below the `'v'` command, the
PSoC bootloader beyond Section 3.4, the Qwiic MP3 Trigger (a different product --
I2C, WT2003S decoder, and discontinued), the WAV Trigger and Tsunami successors
beyond Section 2.4, or MarcDuino's own serial protocol.

## 2. What you are actually buying

### 2.1 The board

| Field | Value |
| --- | --- |
| Vendor / designer | SparkFun Electronics, designed by Robertsonics (Jamie Robertson) |
| Current SKU | **WIG-13720** |
| Earlier SKUs | WIG-09356 (named in the v2 guide's own trademark line), WIG-11029 (retired) |
| MCU | Cypress PSoC **CY8C29466-24** |
| Audio decoder | **VLSI VS1063** (Section 2.2) |
| Storage | microSD, SDSC and SDHC, FAT16 or FAT32 |
| Audio out | 1/8" stereo jack, up to 192 kbps stereo |
| Trigger inputs | **18** (TRIG01-TRIG18), active low, internal pull-ups, 3.3-5.0 V |
| Serial | full duplex, 8 bits, 1 start, 1 stop, no parity, no flow control, **3.3-5 V TTL** |
| Default baud | **38400** |
| Input voltage | 4.5-12.0 V DC, or regulated 3.3 V (jumper selectable) |
| Current | **about 45 mA idle, 85 mA playing** |
| Local control | on-board navigation switch (left / right / centre) |
| Firmware update | microSD bootloader, no programmer needed (Section 3.4) |

Board dimensions, weight and mounting-hole pattern: `UNKNOWN`. Neither the user
guide nor the hookup guide states them, and no distributor page read carried a
mechanical drawing. Settled by measuring a board, or by opening the `.brd` file
in `github.com/sparkfun/MP3_Trigger/Hardware`.

### 2.2 The decoder is a VS1063

SparkFun's own Eagle schematic is the source:

```xml
<part name="U7" library="SparkFun-DigitalIC" deviceset="VS1063" device="SMD" value="VS1063"/>
```

and the sheet carries the annotation *"VLSI VS1063 audio codec IC"*. (`VS1033D`
also appears in that file, but only as the name of a reused library *symbol*, not
as a fitted part.) Five secondary sources, reseller listings and community code
comments among them, say VS1053.

> [!NOTE]
> **Nothing functional turns on this.** The `'v'` command's semantics -- one byte,
> `0x00` loudest, ascending toward silence -- are the VS10xx `SCI_VOL` convention
> and identical across VS1033, VS1053 and VS1063. It matters because a reader
> chasing a datasheet should chase the right one.

### 2.3 Availability

WIG-13720 is still listed by SparkFun and carried by Digi-Key, Mouser and the
usual distributors as of this research date, at roughly **USD 50**. Two related
SparkFun products are **not** this board and should not be bought by mistake:

- **Qwiic MP3 Trigger** -- I2C, WT2003S decoder, on-board amplifier, and
  **discontinued**. A different protocol entirely; nothing in this sheet applies.
- **WAV Trigger / Tsunami** -- the Robertsonics successors (Section 2.4).

### 2.4 The successors

Robertsonics' own line moved on, and SparkFun points polyphonic users at the
newer boards:

| Board | Simultaneous tracks | Format | Note |
| --- | --- | --- | --- |
| MP3 Trigger | **1** | MP3, <= 192 kbps | this sheet |
| WAV Trigger | up to **14** stereo | uncompressed WAV | ~8 ms trigger latency |
| Tsunami | **32** mono / 18 stereo | uncompressed WAV | 8 output channels |

The MP3 Trigger plays one track at a time. Music under another sound needs a
board that mixes, such as a WAV Trigger or a Tsunami.

## 3. The hardware paths beside the serial port

### 3.1 Eighteen trigger pins

The board's original purpose is **not** serial control. Eighteen inputs, active
low with internal pull-ups, each start the track whose filename begins with the
matching three digits: TRIG01 starts `001xxxx.MP3`, TRIG18 starts `018xxxx.MP3`.
A switch to ground is a complete installation with no microcontroller at all.

Shunt jumpers turn it into a sequencer: *"when a triggered track reaches the end,
the MP3 Trigger v2 looks to see if any trigger inputs are active, and will
automatically start another track if so ... the MP3 Trigger v2 will always start
the next higher trigger track, wrapping back to 1 after 18."*

A board under serial control may still be wired for its trigger pins, and a
jumpered trigger starts tracks the controller never asked for. Section 8.3's
false-trigger problem is the same wiring, misbehaving.

### 3.2 Quiet Mode, which reports the pins over serial

`'Q'` + `'1'` decouples the trigger pins from playback: instead of starting
tracks, an activated trigger makes the board send `'M'` followed by a **3-byte
bitmask** (TRIG01-08, TRIG09-16, TRIG17-18). *"Quiet Mode is off by default and is
not preserved through a power cycle."*

It turns the board into an 18-input expander reporting over the same wire, and
it puts binary `'M'` frames into a response stream that is otherwise ASCII
(Section 6.4).

### 3.3 The navigation switch

Left is previous track, right is next, centre is start/stop -- and the `'R'`,
`'F'` and `'O'` serial commands are documented as doing *"the same function"*.
Useful for testing a card without a controller attached.

### 3.4 The bootloader

Firmware updates come off the card: rename the hex file `MP3TRIGR.HEX`, hold the
centre nav switch while powering on, wait for a solid status LED, power cycle. The
guide is emphatic that the bootloader lives in protected flash and that using a
hardware programmer on anything but the bootloader image **erases it** -- *"Don't
do it!"*

### 3.5 Status LED, which is the only diagnostic before the wire works

| Blink pattern | Meaning |
| --- | --- |
| 1 long | no formatted microSD media found |
| 1 long, then 1 short | media found, **no MP3 files located** |
| constant short blinks | hardware problem with the MP3 decoder |
| **3 short** | media found, at least one MP3 file -- ready |

A builder debugging silence should read the LED before reading a log. Three short
blinks and no sound is a wiring or baud problem; anything else is a card problem.

## 4. Sources Checked

| Source | How it was taken | What it gave |
| --- | --- | --- |
| **MP3 Trigger v2 User Guide, 2012.02.01** | `curl` from `robertsonics.com`, `pdftotext -layout`, **all nine pages read** | **The protocol.** Every command in Section 6, the initialization file grammar, the LED codes, the trigger-pin behaviour, the bootloader, the electricals, and the 18-byte version string that Open Item 1 is about |
| **SparkFun MP3 Trigger hardware repository** | `git clone github.com/sparkfun/MP3_Trigger`, Eagle `.sch` parsed | **The decoder question, settled.** `value="VS1063"` and *"VLSI VS1063 audio codec IC"* against five secondary sources saying VS1053. Also confirmed WIG-13720 from the README |
| SparkFun v2.4 Hookup Guide | fetched and read | Confirmed the command list, the 3.3-5 V TTL level, the trigger pins, the LED codes and `MP3TRIGR.INI` against the guide -- two independent readings, no contradictions |
| **Printed Droid knowledge base** | fetched and read | **The two field problems no vendor document mentions**: false triggers on long cables and the 1 kOhm fix, and the audio output's DC offset (Section 8.3) |
| CHIRP `mp3_compat.cpp` | read from source | **CHIRP speaks this protocol.** Its parser, its 254 stop track, and the two commands it does *not* implement (Section 8.1) |
| AstroPixelsPlus `MarcduinoSound.h` | read from source | A blank track of **252**, and the measured audibility floor of **100** that Section 6.6 turns on |
| ShadowMD, Padawan360 (DY-SV5W port) | read from source | Neither drives this board directly today: ShadowMD delegates to MarcDuino over `$8x`, and the Padawan360 port moved **away** from the MP3 Trigger with its calls left commented out |

**What did not survive checking.** A web research pass attributed to the hookup
guide a *"~80 ms MP3 startup latency"*, a *"~100-500 ms gap between sequential
tracks"* and a claim that *"SDXC cards may not mount"*. **None of those appear in
either the user guide or the hookup guide.** They may be true -- MP3 frame
padding is real, and the guide does bound the card at SDSC and SDHC -- but they
are not sourced and are not stated as facts here. Open Item 4 names the
measurement.

The same pass reported the decoder as a VS1063 citing a reseller listing, which
turned out to be **right**. It also reported the board as *"currently
available"* while marking one of its own SKUs retired; the SKU history in
Section 2.1 is what the documents actually say, and no attempt is made here to
reconcile SparkFun's retired-product pages into a clean timeline.

## 5. Getting the wire to work

### 5.1 The factory baud is 38400

A board out of the box talks **38400**. A controller that talks anything else
gets no answer until the card says otherwise. The fix is a file on the card, not
a firmware setting. From the user guide:

> The initialization file must be named **"MP3TRIGR.INI"** and must, like all the
> mp3 files, be in the root directory. The file is optional. If it does not
> exist, then the MP3 Trigger v2 defaults to normal operation at 38.4K baud.

For 9600 the file is one line:

```
#BAUD 9600
```

Supported values are exactly **2400, 9600, 19200, 31250, 38400** -- the guide
enumerates them and nothing else is accepted. Parsing rules that bite:

- only the **first 512 bytes** are examined
- the first `*` character ends the command section; everything after it is free
  comment text
- every command starts with `#` **followed by a space**
- comments are *not* allowed before the `*`

The init file's other commands change what a board does on its own: `#RAND N`
excludes the first N tracks from the random-trigger function, and
`#TRIG N, F, L` repurposes a trigger pin (Section 3.1). `#VOLM N` sets a power-on
volume, and the guide's note on it is the one Section 6.6 turns on: *"Default is
full volume = 0. Useful range is 0 to 64, with values above 64 being
inaudible."*

### 5.2 The card contract

| Rule | Value |
| --- | --- |
| Card types | SDSC and SDHC microSD |
| File system | FAT16 or FAT32 |
| Location | **root directory**, no subdirectories |
| Naming | `NNNxxxx.MP3` -- three digits with leading zeros, then anything |
| Addressable range | **1-255** over serial (`'t'`); 18 over the trigger pins |
| Format | MP3, up to **192 kbps stereo** |
| Hot swap | **not supported** |

The hot-swap rule is an operator fact, not a footnote. From the guide: *"the
microSD media is only initialized during power up. So whenever the card is
changed or updated, be sure to power cycle the MP3 Trigger v2 after installing
the card."* **Changing a droid's sounds means power-cycling the sound module**,
and a module powered from the same rail as everything else means power-cycling
the droid.

### 5.3 Levels, and the one thing this sheet cannot tell you

The serial port is *"full duplex 3.3-5V serial TTL"* by the guide's own words, and
the trigger inputs *"support voltage levels of either 5V or 3.3V"*. So the
**module accepts 3.3 V on its RX**, and a 3.3 V controller can talk to it
directly.

The direction that matters is the other one: the module's **TX** into the
controller's RX. Whether this board's TX idles at 3.3 V or at 5 V depends on
which supply the jumper selects, and **no document read states the output
swing**.

> [!WARNING]
> **`UNKNOWN`, and it is the one unknown on this sheet that could damage
> hardware.** An ESP32 pin is not 5 V tolerant, and this module can be run from
> a 5 V rail. **Measure the module's TX idle voltage before connecting it to a
> 3.3 V controller's RX**, or power the module from the jumper-selected
> regulated 3.3 V and take the level question off the table. Open Item 2.

## 6. The serial protocol (normative)

Full duplex, **8 bits, 1 start, 1 stop, no parity, no flow control**. Commands are
never echoed. The whole command set is one or two bytes:

> 1-byte commands are upper case ASCII characters. 2-byte commands start with an
> ASCII character. Those starting with an **upper case** character use an ASCII
> value ('0'-'9') as the second byte. 2-byte commands starting with a **lower
> case** character require a **binary** value (0-255) as the second byte.

That rule is the whole grammar, and it is worth internalising: **case tells you
how to encode the argument.**

### 6.1 The command table

| Command | Bytes | First | Second | Effect |
| --- | --- | --- | --- | --- |
| Start / Stop | 1 | `'O'` `0x4F` | -- | toggles: stops if playing, restarts from the beginning if stopped |
| Forward | 1 | `'F'` `0x46` | -- | next track in the directory |
| Reverse | 1 | `'R'` `0x52` | -- | previous track in the directory |
| Trigger (ASCII) | 2 | `'T'` `0x54` | `'1'`-`'9'` | plays `00Nxxxx.MP3` |
| **Trigger (binary)** | 2 | **`'t'` `0x74`** | **1-255 binary** | **plays `NNNxxxx.MP3`** |
| Play by index | 2 | `'p'` `0x70` | 0-255 binary | plays the *n*th track in directory order |
| **Set volume** | 2 | **`'v'` `0x76`** | **0-255 binary** | **`0x00` loudest, ascending toward silence** |
| **Status request** | 2 | **`'S'` `0x53`** | `'0'` or `'1'` | version string, or total track count |
| Quiet mode | 2 | `'Q'` `0x51` | `'0'` or `'1'` | trigger pins report over serial instead of playing |

Bolded rows are the four a serial controller uses: play, volume and the two
status queries. There is **no checksum, no framing byte and no acknowledgement**
-- a play command is two bytes and silence. Nothing in the user guide requires a
gap between commands, but a SparkFun forum thread reports the board needing
10-100 ms to settle after an `'X'` (Open Item 3).

The `'t'` argument is one byte: a track number above 255 cannot be sent, and a
controller that casts 256 to a byte sends `0x00`.

### 6.2 `'t'` and `'p'` address different things

This distinction is easy to miss and it is the module's best feature:

- **`'t'` addresses the filename.** Track 42 is the file whose name starts `042`.
- **`'p'` addresses the directory position.** Track 42 is whatever the card
  enumerates 42nd.

Section 7.1 is why that matters.

### 6.3 The two status responses, and the terminator that may not exist

| Query | Response | Example |
| --- | --- | --- |
| `'S'` `'0'` | version string, `'='`-prefixed | `=MP3 Trigger v2.50` |
| `'S'` `'1'` | total track count in ASCII, `'='`-prefixed | `=14` |

The guide describes the first as *"an 18-byte version string: e.g. `=MP3 Trigger
v2.50`"*. **That example is exactly 18 characters.** If the count is literal,
the response carries **no CR, no LF and no terminator of any kind**, and a
reader that waits for a line ending waits until its own timeout. Community
implementations assume `"=MP3 Trigger v2.NN\r\n"` and `"=NNN\r\n"`, sourced from
each other rather than from the guide. `UNKNOWN` which is right on real
firmware; Open Item 1 is a capture, and it is ten minutes with a USB-serial
adapter and a terminal.

### 6.4 What the module says when nobody asked

| Byte | Meaning |
| --- | --- |
| `'X'` `0x58` | the currently playing track **finished** |
| `'x'` `0x78` | the currently playing track was **cancelled by a new command** |
| `'E'` `0x45` | **a requested track does not exist** |
| `'M'` `0x4D` + 3 bytes | Quiet Mode only: a trigger-pin bitmask (Section 3.2) |

> [!CAUTION]
> **`'E'` means the track is missing, not that the hardware is broken.** The
> guide: *"'E': When a requested track doesn't exist (error)."* The distinction
> is *"your SD card does not have track 126"* versus *"your sound board has
> failed"*.

**These arrive at any time, including between a query and its reply.** A reader
that takes the next bytes on the wire as the reply to its query fails on a track
that happens to finish in that window; skip leading `'X'` / `'x'` / `'E'` until
the `'='`.

The protocol has **no play-state query and no current-track query**. These
three bytes are the only play-state the module ever reports, and the last track
sent is the only current track a controller can know.

### 6.5 There is no stop command, and the community does not agree on the blank track

`'O'` toggles, which means its effect depends on a play state this protocol
cannot report (Section 6.4). So every implementation stops by playing a silent
file -- and picks a different one:

| Implementation | Blank track | Source |
| --- | --- | --- |
| CHIRP compat layer | 254 | `mp3_compat.cpp` |
| AstroPixelsPlus / Reeltwo | **252** | `MP3_EMPTY_SOUND`, `MarcduinoSound.h` |

> [!IMPORTANT]
> **There is no single community standard.** Both numbers are right for their own
> card. What matters operationally is only this: **the blank track the
> controller plays must exist in the root of the card**, or the stop produces an
> `'E'` and whatever was playing keeps playing. `254XXXX.MP3` ships in the common
> R2 sound packs, and a builder who assembled a card by hand may not have it.

### 6.6 Volume: the register is 0-255, the audible span is not

`'v'` takes one binary byte, inverted: `0x00` is loudest. The guide's `#VOLM`
note puts the useful range at **0-64**, with values above 64 inaudible.
AstroPixelsPlus, on the strength of its own measurement, maps its volume onto
**0-100** instead. It is the one project that tested the audible floor and wrote
the number down. Either way most of the register is silent, and a controller that
maps a volume control onto the full 0-255 spends most of its travel on nothing.

## 7. Track numbering, which is this module's real advantage

### 7.1 It addresses the filename

`'t'` plays the file whose name begins with the three digits you sent. Not the
*n*th file, not the file the card happened to be written first -- the file that
says `126` on the front.

**Copying the files again in any order changes nothing.** The card is
self-describing, the numbers are visible in a file browser, and a builder can
edit one sound without disturbing the rest. For a droid whose sound library is
curated over years, that is the single most useful property this board has. A
module that addresses by filesystem enumeration order renumbers every sound when
a card is rebuilt in a different order.

The costs of the same decision: **255 tracks, hard**, and no directories.

### 7.2 The community's 25-track banks

MarcDuino and the R2 sound packs number the card in banks of 25:

| Tracks | Category |
| --- | --- |
| 001-025 | general |
| 026-050 | chatty |
| 051-075 | happy |
| 076-100 | sad |
| 101-125 | whistle |
| 126-150 | scream |
| 151-175 | Leia |
| 176-200 | music |
| 201-225 | music |
| **254** | **silent blank** (Section 6.5) |
| **255** | **startup** |

A controller that plays track 254 or 255 from a random range plays the blank or
the startup sound.

## 8. How the hobby drives this module (non-normative)

Everything in this section is evidence of practice. None of it is normative.

### 8.1 CHIRP implements this protocol on purpose

CHIRP's `mp3_compat.cpp` is a **deliberate MP3 Trigger compatibility layer** on a
2025-era RP2350 board: `checkAndHandleMp3Command()` intercepts `'O'`, `'F'`,
`'R'`, `'T'`, `'t'`, `'v'` and `'p'` before CHIRP's own ASCII parser sees them,
converts `'v'`'s inverted byte to a float gain (`1.0f - sfVol / 255.0f`) and
applies it to every stream, and uses **track 254** as its blank.

A newer module shipped a shim so that droids already wired for an MP3 Trigger
could drop it in. **Two commands are not in that shim** -- `'S'` status and
`'Q'` quiet mode -- so a controller pointed at a CHIRP while configured as an MP3
Trigger plays tracks correctly and reports a dead link forever. A builder
debugging a mixed setup should know it.

### 8.2 The others have moved on, and one of them left a trail

- **AstroPixelsPlus / Reeltwo** (`MarcduinoSound.h`) abstracts three modules
  (`kMP3Trigger`, `kDFMini`, `kHCR`) behind a `Stream&`, uses a
  `(range - vol)`-style inverted volume formula, and -- as Section 6.6 sets out
  -- **maps to 0-100 rather than 0-255**, on the strength of its own
  measurement.
- **ShadowMD** does not drive a sound module at all. It delegates to MarcDuino
  over `$8x\r`, so the MP3 Trigger is behind that board, not behind ShadowMD.
- **A Padawan360 port to the DY-SV5W** keeps its `mp3Trigger.play()` calls as
  comments beside the DY-SV5W calls that replaced them -- and got the volume
  direction wrong in the move, because the two modules invert against each other.
- **MarcDuino** itself is the reason the numbering exists. Its firmware and the
  R2 Touch app hardcode the 25-track banks, which is why the sound packs are
  shaped that way and why changing a filename breaks a droid.

### 8.3 Two field problems no vendor document mentions

From Printed Droid's knowledge base, which is builders writing for builders:

- **False triggers on long cables.** *"Long cables cause false triggering and
  crosstalk between the inputs"* because of *"inadequate pull-up resistors on the
  inputs, so they have a tendency to 'float'."* The recommendation is *"short
  cables (like 2ft or less)"*; the fix is a **1 kOhm pull-up** added between
  *"pin 3 of the micro-controller and the large tab (3.3v) of the nearby voltage
  regulator"*, which reportedly *"fixed the false triggers on all inputs, even
  with 25ft cables"*. A board wired for its trigger pins in a droid starts tracks
  on its own, and the symptom is indistinguishable from a controller bug.
- **The audio output is not line out.** *"The included audio output has a DC
  offset with respect to the power supply ground, so it's only safe for driving
  headphones. Also, the static electricity from a long cable might fry the MP3
  decoder chip."* A droid runs a long cable to an amplifier in the body, which is
  exactly the case being warned about. **AC-couple it.**

## 9. Quick Reference

- Field: Vendor part. Value: **WIG-13720** (SparkFun). **Not DEV-13720.**
- Field: Decoder. Value: **VLSI VS1063**, from SparkFun's schematic. **Not VS1053.**
- Field: Serial format. Value: **8 bits, 1 start, 1 stop, no parity, no flow control, full duplex, 3.3-5 V TTL**.
- Field: Baud. Value: the factory default is **38400**; any other rate requires `MP3TRIGR.INI` on the card.
- Field: Init file. Value: **`MP3TRIGR.INI`** in the card root, e.g. `#BAUD 9600`, `#` then a space, first 512 bytes only, `*` ends the command section.
- Field: Allowed baud values. Value: **2400, 9600, 19200, 31250, 38400**. Nothing else.
- Field: Command grammar. Value: 1 or 2 bytes. **Upper-case first byte takes an ASCII digit argument; lower-case takes a binary byte.**
- Field: Play a track. Value: **`'t'` + binary 1-255**, addressing the file whose name starts with those three digits.
- Field: `'p'` versus `'t'`. Value: **`'p'` is directory position, `'t'` is filename prefix.**
- Field: Track ceiling. Value: **255**. The argument is one byte; 256 cast to a byte is `0x00`.
- Field: Stop. Value: **no stop command**; play a blank track. **254** in CHIRP, **252** in AstroPixelsPlus/Reeltwo. The blank must exist in the card root.
- Field: Volume command. Value: **`'v'` + binary 0-255, inverted** -- `0x00` loudest.
- Field: Volume audibility. Value: the vendor says **above 64 is inaudible**; a builder's measurement says **above 100**. Either way most of the register is silent.
- Field: Status queries. Value: **`'S'`+`'0'`** version, **`'S'`+`'1'`** track count, both replies `'='`-prefixed.
- Field: Response terminator. Status: **UNKNOWN** -- the guide describes an 18-byte version string that is exactly 18 visible characters, implying no CR/LF (Open Item 1).
- Field: Unsolicited bytes. Value: **`'X'` finished, `'x'` cancelled, `'E'` requested track does not exist.** `'E'` is **not** a hardware error.
- Field: Play-state and current-track queries. Value: **none.** Only the unsolicited bytes report play-state.
- Field: Quiet Mode. Value: `'Q'`+`'1'` makes trigger pins report `'M'` plus a 3-byte bitmask instead of playing. Off by default, not preserved across power cycles.
- Field: Card. Value: **microSD, SDSC or SDHC, FAT16 or FAT32, root directory only, `NNNxxxx.MP3`**, MP3 up to 192 kbps stereo.
- Field: Hot swap. Value: **not supported.** The card is read only at power-on.
- Field: Power. Value: **4.5-12 V DC or regulated 3.3 V (jumper)**, about **45 mA idle / 85 mA playing**.
- Field: Audio output. Value: **line level with a DC offset**, no on-board amplifier. AC-couple before a long cable.
- Field: Trigger pins. Value: **18**, active low, internally pulled up, 3.3-5 V.
- Field: Status LED. Value: **3 short blinks = ready.** 1 long = no card; 1 long + 1 short = card but no MP3s; constant short = decoder fault.
- Field: TX level. Status: **UNKNOWN** -- the module's TX swing is not stated in any document read (Open Item 2).

## 10. Open Items

| # | Item | How to settle it |
| --- | --- | --- |
| 1 | **Do the `'S'` replies end with CR/LF?** Section 6.3. | Ten minutes: USB-serial adapter, 9600 baud, send `S0`, capture raw bytes, count them. |
| 2 | **What voltage does the module's TX idle at?** Section 5.3. | Meter on the module's TX pin with the board powered from 5 V, then from the 3.3 V jumper position. **Do this before wiring one to a 3.3 V controller.** |
| 3 | **Can back-to-back commands be dropped?** Section 6.1. A SparkFun forum thread reports a 10-100 ms settling window after `'X'`. | Send a stop immediately followed by a play twenty times and count how many play. |
| 4 | **Is there a start-up latency worth designing around?** A research pass claimed about 80 ms of MP3 parser delay and 100-500 ms gaps between sequential tracks; **neither is in any document read** (Section 4). | Trigger a known track against a scope or a phone recording and measure command-to-first-sample. |

## 11. Sources

**Primary -- vendor**

- **MP3 Trigger v2 User Guide, 2012.02.01** -- https://www.robertsonics.com/s/MP3TriggerV2UserGuide_2012-02-04.pdf
  (downloaded and read in full; the source for Sections 3, 5 and 6)
- **SparkFun MP3 Trigger hardware repository** -- https://github.com/sparkfun/MP3_Trigger
  (cloned; the Eagle schematic settled the decoder and the SKU)
- SparkFun MP3 Trigger Hookup Guide v2.4 -- https://learn.sparkfun.com/tutorials/mp3-trigger-hookup-guide-v24/all
- SparkFun product page -- https://www.sparkfun.com/products/13720
- Robertsonics product pages -- https://www.robertsonics.com/mp3-trigger,
  https://www.robertsonics.com/wav-trigger, https://www.robertsonics.com/tsunami
- `MP3TRIGR.INI` sample, mirrored by Pololu -- https://www.pololu.com/file/0J531/mp3trigr.ini

**Primary -- community source code**

- CHIRP Audio Trigger, `CHIRP_Audio_Trigger/Arduino_Sketches/CHIRP_Audio/mp3_compat.cpp`
- AstroPixelsPlus, `MarcduinoSound.h`
- ShadowMD, `src/Shadow_MD_DualController_Template.ino`
- Padawan360 DY-SV5W port, `Padawan360_body_mega_maestro_DY5_audioplayer_BETA.ino`

**Builder documentation (primary source, non-normative)**

- Printed Droid, SparkFun MP3 Trigger board -- https://www.printed-droid.com/kb/sparkfun-mp3-trigger-board/
  (the false-trigger fix and the DC-offset warning in Section 8.3)
- CuriousMarc, MP3 Trigger sound system -- https://www.curiousmarc.com/r2-d2/mp3-trigger-sound-system
  (the MarcDuino bank convention)

> [!NOTE]
> **Negative results, recorded so nobody repeats them.** The SparkFun hardware
> repository carries **no firmware** -- only Eagle files and a production panel --
> so the command set could not be read from source and the user guide is the
> authority for it. No mechanical drawing was found for the board in any vendor
> document. No document read states the module's TX output swing, which is Open
> Item 2. The astromech projects that once drove this module have moved off it:
> ShadowMD to MarcDuino, a Padawan360 port to the DY-SV5W, AstroPixelsPlus to an
> abstraction that still carries it. **CHIRP's compatibility layer is the live
> MP3 Trigger code in the stable.**
