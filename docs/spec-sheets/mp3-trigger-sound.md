# MP3 Trigger Spec Sheet (SparkFun WIG-13720, registry token `mp3trigger_serial`)

Working spec for the **SparkFun MP3 Trigger**, the **Sound** lineup member that
the astromech hobby standardised on ([#392](https://github.com/mattiasbrandt/protoArtoo/issues/392),
minted from [#303](https://github.com/mattiasbrandt/protoArtoo/issues/303) and
[#316](https://github.com/mattiasbrandt/protoArtoo/issues/316)), reached over the
Component Protocol the registry calls `mp3trigger_serial`.

Research date 2026-09-12. Every command byte, electrical value and default below
was read this session from the **MP3 Trigger v2 User Guide PDF** (downloaded from
robertsonics.com and read in full, all nine pages), from **SparkFun's own Eagle
schematic** (cloned from `github.com/sparkfun/MP3_Trigger`), from the SparkFun
v2.4 hookup guide, from this repository's driver, tests and defaults, or from the
astromech projects on this disk. Claims that could not be sourced are marked
`UNKNOWN` with the artefact or bench test that would settle them.

> [!CAUTION]
> **The operator's volume slider is silent across most of its travel, and the
> shipped default sits at or below the edge of audibility.**
>
> `setVolume()` maps the normalised 0-30 range onto the module's whole 0-255
> register: `nativeVol = (30 - vol) * 255 / 30`. But the module is **not audible
> across that whole register**. The vendor's own guide says *"values much above
> 0x40 are too low to be audible"*, and the one astromech project that measured
> it puts the practical floor at **100** (*"doc says anything below 64 is
> inaudible, not true, 100 is"*).
>
> | Threshold | Native limit | Lowest audible `vol` | Usable slider positions |
> | --- | --- | --- | --- |
> | Vendor guide | 64 | **23** | 8 of 31 |
> | Field measurement | 100 | **19** | 12 of 31 |
>
> `src/config_store.cpp:166` ships `audioVolume = 20`, which is **native 85** --
> past the vendor's threshold and barely inside the field one. Section 8.3 does
> the arithmetic; Open Item 1 is what to do about it. **The driver's own comment
> already knows this** and maps to 0-255 anyway.

> [!WARNING]
> **The chip is a VS1063. Five comments in our source say VS1053.**
> SparkFun's schematic for this board names part `U7` as
> `deviceset="VS1063" device="SMD" value="VS1063"`, annotated *"VLSI VS1063 audio
> codec IC"*. Nothing functional depends on it -- the inverted 0-255 volume
> register is the same across the VS10xx family -- but it is a fact stated wrongly
> in the place a developer would copy it from. Section 2.2, fixed in the change
> that carries this sheet (Section 14.1).

> [!IMPORTANT]
> **Our part number is wrong too.** `include/audio_mp3trigger.h:4` and
> `docs/sound_playback.md:242` both say **DEV-13720**. The SparkFun SKU is
> **WIG-13720** -- SparkFun's own repository README links
> `sparkfun.com/products/13720` as *"MP3 Trigger (WIG-13720)"*. Also fixed here.

> [!NOTE]
> **This part ships in every image and has never been run on our hardware.**
> `docs/sound_playback.md:56` is the honest line -- *"Implemented -- hardware
> validation pending"* -- and it is the difference between this sheet and
> [`dy-sv5w-sound.md`](dy-sv5w-sound.md), which has a validation record going back
> to 2026-03-22. Everything in Sections 11-13 is verified on a laptop through the
> `AudioSerialIO` seam. Nothing here has made a sound.

## Where this sits in the lineup

The **Sound** category holds four products, and a builder picks one:

| Product | What it is | Status | Registry value |
| --- | --- | --- | --- |
| DY-SV5W | binary-frame voice module with a 5 W amplifier | `supported`, **default** | 18 |
| **MP3 Trigger** | **SparkFun/Robertsonics VS1063 board, 18 trigger pins** | **`supported`** | **19** |
| CHIRP Audio Trigger | RP2350 multi-stream mixer, astromech-specific | `supported` | 20 |
| DFPlayer Mini | hardware-decoded single-stream player with an amplifier | `roadmap` | 21 |

[`dy-sv5w-sound.md`](dy-sv5w-sound.md) Section 16 sets out how the four differ in
kind, and this sheet does not repeat that table. What it adds is the reason this
particular row exists at all.

**The MP3 Trigger is the hobby's common denominator, and that is its whole
argument.** `docs/goal.md` records it in the landscape survey as the audio module
of **three** other projects -- Padawan360, ShadowMD and ShadowRC (`:133-135`) --
and MarcDuino, the dominant R2 control system, is built around it. The 25-track
sound banks every R2 sound pack ships in (Section 9.1) are *its* convention. A
builder arriving from any of those systems already owns the board, already owns
the card, and already knows the numbers.

So the DY-SV5W is the module protoArtoo recommends and the MP3 Trigger is the
module a builder **already has**. Supporting it is a migration path, not a
preference.

## 0. Authority Contract

This document is an implementation authority for the MP3 Trigger serial protocol
and for the board's behaviour as protoArtoo uses it.

Authority order for agent decisions:

1. **The MP3 Trigger v2 User Guide** for the protocol, the initialization file,
   the electricals and the card contract. It is Robertsonics' own document, it is
   the source SparkFun's hookup guide reproduces, and every command in Section 7
   was read from it this session.
2. **SparkFun's published schematic** for what is on the board. It settled the
   decoder question against four secondary sources (Section 2.2).
3. This document.
4. **Other implementations** -- CHIRP's `mp3_compat.cpp`, AstroPixelsPlus's
   `MarcduinoSound.h`, and our own driver -- as evidence of practice. Section 13
   names two places they disagree with each other.
5. Community documentation (Section 13.3), for the operator-visible conventions
   the vendor never wrote down.

If references conflict:

- Prefer the User Guide over any implementation for **what the board does**.
- Prefer the schematic over any prose for **what is on the board**.
- Prefer a **measurement** over the User Guide where one exists and is
  attributed. The volume-audibility floor is the live example: the guide says 64,
  a builder who measured says 100, and Section 8.3 reports both rather than
  choosing.
- If still unresolved, mark `UNKNOWN` and stop dependent work.

Agent requirements when using this document:

- MUST NOT widen the volume map to the full 0-255 register without reading
  Section 8.3 first. Most of it is inaudible.
- MUST NOT treat `'E'` as a hardware error. It means **the requested track does
  not exist** (Section 7.4). Our own comment says otherwise and is wrong.
- MUST NOT assume a status response is terminated. The guide describes an
  **18-byte version string** with no terminator, and `readLine()` waits for a
  `\n` that may never arrive (Section 7.3, Open Item 2).
- MUST NOT assume the next bytes on the wire are the reply. `'X'`, `'x'` and
  `'E'` arrive unsolicited, at any time (Section 7.4).
- MUST NOT send a track number above 255. The `'t'` argument is one byte and
  `256` casts to `0x00` (Section 8.2).
- MUST NOT assume the blank stop track is 254 everywhere. It is 254 here and in
  CHIRP, and **252** in AstroPixelsPlus (Section 8.4).
- MUST NOT change the baud rate expectation without the card. 9600 is **not** the
  factory default; it requires `MP3TRIGR.INI` in the card root (Section 6).
- MUST state that nothing in this sheet is hardware-verified on our droid.

## 1. Scope

Covers the board and what is on it, the electrical contract, the serial protocol
in full, the SD-card and file-naming contract, the initialization file, the
trigger-pin hardware path we do not use, what protoArtoo's driver actually sends,
the capability word and what the operator sees, how the astromech hobby uses the
board, and the defects this research found in our own source.

Does not cover: the VS1063's own SCI register set below the `'v'` command, the
PSoC bootloader beyond Section 3.4, the Qwiic MP3 Trigger (a different product --
I2C, WT2003S decoder, and discontinued), the WAV Trigger and Tsunami successors
beyond Section 2.4, or MarcDuino's own serial protocol (that is
`docs/marcduino_commands.md`).

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
guide nor the hookup guide states them, and no distributor page read this session
carried a mechanical drawing. Settled by measuring a board, or by opening the
`.brd` file in `github.com/sparkfun/MP3_Trigger/Hardware`.

### 2.2 The decoder is a VS1063, and this repository says VS1053 five times

`src/drivers/audio_mp3trigger.cpp` and `include/audio_mp3trigger.h` name the
**VS1053** in five places, including the one a developer would copy -- the volume
comment. SparkFun's own Eagle schematic disagrees:

```xml
<part name="U7" library="SparkFun-DigitalIC" deviceset="VS1063" device="SMD" value="VS1063"/>
```

and the sheet carries the annotation *"VLSI VS1063 audio codec IC"*. (`VS1033D`
also appears in that file, but only as the name of a reused library *symbol*, not
as a fitted part.)

> [!NOTE]
> **Nothing functional turns on this.** The `'v'` command's semantics -- one byte,
> `0x00` loudest, ascending toward silence -- are the VS10xx `SCI_VOL` convention
> and identical across VS1033, VS1053 and VS1063. The correction matters because
> the sheet's whole method is that a name in a comment is a fact, not a guess, and
> because a reader chasing a datasheet should chase the right one. Fixed in
> Section 14.1.

### 2.3 Availability

WIG-13720 is still listed by SparkFun and carried by Digi-Key, Mouser and the
usual distributors as of this research date, at roughly **USD 50**, which makes it
the most expensive member of the Sound family by an order of magnitude -- the
DY-SV5W is a few pounds. Two related SparkFun products are **not** this board and
should not be bought by mistake:

- **Qwiic MP3 Trigger** -- I2C, WT2003S decoder, on-board amplifier, and
  **discontinued**. A different protocol entirely; nothing in this sheet applies.
- **WAV Trigger / Tsunami** -- the Robertsonics successors (Section 2.4).

### 2.4 The successors, and why a droid might want one

Robertsonics' own line moved on, and SparkFun points polyphonic users at the
newer boards:

| Board | Simultaneous tracks | Format | Note |
| --- | --- | --- | --- |
| MP3 Trigger | **1** | MP3, <= 192 kbps | this sheet |
| WAV Trigger | up to **14** stereo | uncompressed WAV | ~8 ms trigger latency |
| Tsunami | **32** mono / 18 stereo | uncompressed WAV | 8 output channels |

**This is the same axis CHIRP sits on.** `CONTEXT.md:401` defines a music bed as
something that exists *"where the fitted module mixes"*, and names the MP3 Trigger
and the DY-SV5W as *"the single-track modules it contrasts itself with"*. A droid
that wants a bed under a performance needs CHIRP, a WAV Trigger, or a Tsunami --
not this board. Neither Robertsonics successor is in protoArtoo's lineup and
neither is proposed here; they are recorded because a builder asking *"can I have
music under the screams"* has to be told no, and told what would.

## 3. The hardware paths we do not use

### 3.1 Eighteen trigger pins

The board's original purpose is **not** serial control. Eighteen inputs, active
low with internal pull-ups, each start the track whose filename begins with the
matching three digits: TRIG01 starts `001xxxx.MP3`, TRIG18 starts `018xxxx.MP3`.
A switch to ground is a complete installation with no microcontroller at all.

Shunt jumpers turn it into a sequencer: *"when a triggered track reaches the end,
the MP3 Trigger v2 looks to see if any trigger inputs are active, and will
automatically start another track if so ... the MP3 Trigger v2 will always start
the next higher trigger track, wrapping back to 1 after 18."*

protoArtoo uses none of this. **But a builder's board may still be wired for it**,
and a jumpered trigger will start tracks the droid never asked for. Section 13.3's
false-trigger problem is the same wiring, misbehaving.

### 3.2 Quiet Mode, which would let the droid read those pins

`'Q'` + `'1'` decouples the trigger pins from playback: instead of starting
tracks, an activated trigger makes the board send `'M'` followed by a **3-byte
bitmask** (TRIG01-08, TRIG09-16, TRIG17-18). *"Quiet Mode is off by default and is
not preserved through a power cycle."*

Recorded, not implemented. It would turn the sound module into an 18-input GPIO
expander reporting over the same wire -- and it would also inject binary `'M'`
frames into a response stream our parser reads as ASCII lines (Section 7.4).

### 3.3 The navigation switch

Left is previous track, right is next, centre is start/stop -- and the `'R'`,
`'F'` and `'O'` serial commands are documented as doing *"the same function"*.
Useful for an operator testing a card without a droid attached.

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

## 4. Project Integration

- **[`src/drivers/audio_mp3trigger.cpp`](../../src/drivers/audio_mp3trigger.cpp)**
  (279 lines) and
  **[`include/audio_mp3trigger.h`](../../include/audio_mp3trigger.h)** (120
  lines) -- the driver. Section 8 is about these two files.
- **[`include/audio_driver.h`](../../include/audio_driver.h)** -- the seam.
  Lines 92-97 are the capability vocabulary; `:120-122` is the 0-30 volume
  contract Section 8.3 argues we honour too literally.
- **[`src/drivers/audio_soft_uart_tx.h`](../../src/drivers/audio_soft_uart_tx.h)**
  -- the bit-bang TX every sound module shares. `SOFT_UART_BIT_US = 104`.
- **[`include/component_registry.inc`](../../include/component_registry.inc)**
  `:168-171` -- row 19, and the one declaration of this module's capability word.
- **[`src/tasks/audio_sound_member.cpp`](../../src/tasks/audio_sound_member.cpp)**
  -- `kSoundMemberDrivers` binds `"mp3_trigger"` to the driver instance, with a
  `static_assert` that fails the build if a selectable member has no driver.
- **[`src/tasks/audio_task.cpp`](../../src/tasks/audio_task.cpp)** -- the UART
  claim around every query (`:723`, `:744`, `:792`), and the unclaimed boot
  seeding at `:690` that Section 14.5 is about.
- **[`include/config.h`](../../include/config.h)** -- `PIN_AUDIO_TX` /
  `PIN_AUDIO_RX` / `UART_PORT_AUDIO` per Board Variant, and
  `PA_CAP_DEDICATED_AUDIO_UART`.
- **[`include/audio_dollar_parser.h`](../../include/audio_dollar_parser.h)**
  `:42-55` -- the named-track defaults, which are **this module's** community
  numbers (Section 9.2).
- **[`docs/sound_playback.md`](../sound_playback.md)** `:238-320` -- the
  operator-facing version of this sheet's Sections 6-9.
- **[`dy-sv5w-sound.md`](dy-sv5w-sound.md)** -- the peer sheet. Its Section 16
  is the four-member comparison this one does not repeat; its Section 17.5 is a
  defect this module shares (Section 14.5).
- **ADR 0027** (toggles staged at reboot), **ADR 0042** (Component Families
  selected at runtime -- `:45` names this module's pin, `:174-178` is why no
  Board Capability Gate excludes it).

## 5. Sources Checked

| Source | How it was taken | What it gave |
| --- | --- | --- |
| **MP3 Trigger v2 User Guide, 2012.02.01** | `curl` from `robertsonics.com`, `pdftotext -layout`, **all nine pages read** | **The protocol.** Every command in Section 7, the initialization file grammar, the LED codes, the trigger-pin behaviour, the bootloader, the electricals, and the 18-byte version string that Open Item 2 is about |
| **SparkFun MP3 Trigger hardware repository** | `git clone github.com/sparkfun/MP3_Trigger`, Eagle `.sch` parsed | **The decoder question, settled.** `value="VS1063"` and *"VLSI VS1063 audio codec IC"* against five secondary sources saying VS1053. Also confirmed WIG-13720 from the README |
| SparkFun v2.4 Hookup Guide | fetched and read | Confirmed the command list, the 3.3-5 V TTL level, the trigger pins, the LED codes and `MP3TRIGR.INI` against the guide -- two independent readings, no contradictions |
| **Printed Droid knowledge base** | fetched and read | **The two field problems no vendor document mentions**: false triggers on long cables and the 1 kOhm fix, and the audio output's DC offset (Section 13.3) |
| `~/Documents/GitHub/CHIRP/.../mp3_compat.cpp` | read on disk | **CHIRP speaks this protocol.** Its parser, its 254 stop track, and the two commands it does *not* implement (Section 13.1) |
| `~/Documents/GitHub/AstroPixelsPlus/MarcduinoSound.h` | read on disk | The same volume formula we use -- and a different blank track (**252**), and the measured audibility floor of **100** that Section 8.3 turns on |
| `~/Documents/GitHub/ShadowMD`, `~/Documents/GitHub/Padawan360_mega_maestro_DYSV5W` | read on disk | Neither drives this board directly today: ShadowMD delegates to MarcDuino over `$8x`, and the Padawan360 on this disk is a port **away** from the MP3 Trigger with its calls left commented out |
| protoArtoo driver, tests, registry, config defaults | read on disk | Sections 8-11, and the four findings in Section 15 |
| protoArtoo docs | read on disk | `docs/sound_playback.md:238-320`, `docs/status.md:125-126`, `docs/goal.md:133-135`, `CONTEXT.md:401` |

**What did not survive checking.** A web research pass attributed to the hookup
guide a *"~80 ms MP3 startup latency"*, a *"~100-500 ms gap between sequential
tracks"* and a claim that *"SDXC cards may not mount"*. **None of those appear in
either the user guide or the hookup guide** as read this session. They may be
true -- MP3 frame padding is real, and the guide does bound the card at SDSC and
SDHC -- but they are not sourced and are not stated as facts here. Open Item 5
names the measurement.

The same pass reported the decoder as a VS1063 citing a reseller listing, which
turned out to be **right** and to contradict our own source. It also reported the
board as *"currently available"* while marking one of its own SKUs retired; the
SKU history in Section 2.1 is what the documents actually say, and no attempt is
made here to reconcile SparkFun's retired-product pages into a clean timeline.

## 6. Getting the wire to work

### 6.1 The factory baud is 38400 and we speak 9600

This is the first thing that will not work. `docs/sound_playback.md:56` and the
driver's own log message both say it, and it is worth stating as a rule rather
than a note: **a board out of the box will not answer protoArtoo.**

The fix is a file on the card, not a firmware setting. From the user guide:

> The initialization file must be named **"MP3TRIGR.INI"** and must, like all the
> mp3 files, be in the root directory. The file is optional. If it does not
> exist, then the MP3 Trigger v2 defaults to normal operation at 38.4K baud.

The minimum contents for protoArtoo are one line:

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

`docs/sound_playback.md:246-248` currently defers this to *"the SparkFun MP3
Trigger v2.4 Hookup Guide for the exact filename and format"*. It is written out
above, and Section 14.4 puts it in that document.

The init file's other two commands are not protoArtoo's business but change what
a builder's board does on its own: `#RAND N` excludes the first N tracks from the
random-trigger function, and `#TRIG N, F, L` repurposes a trigger pin (Section
3.1). `#VOLM N` sets a power-on volume, and the guide's note on it is the one
Section 8.3 turns on: *"Default is full volume = 0. Useful range is 0 to 64, with
values above 64 being inaudible."*

### 6.2 The card contract

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
and a droid whose module is powered from the same rail as everything else means
power-cycling the droid.

### 6.3 Levels, and the one thing this sheet cannot tell you

The serial port is *"full duplex 3.3-5V serial TTL"* by the guide's own words, and
the trigger inputs *"support voltage levels of either 5V or 3.3V"*. So the
**module accepts 3.3 V from an ESP32 on its RX**, and protoArtoo's bit-bang TX is
fine as it stands.

The direction that matters is the other one. The module's **TX drives our RX**,
and `PIN_AUDIO_RX` on both Board Variants is an ESP32 pin that is **not 5 V
tolerant**. Whether this board's TX idles at 3.3 V or at 5 V depends on which
supply the jumper selects, and **no document read this session states the output
swing**.

> [!WARNING]
> **`UNKNOWN`, and it is the one unknown on this sheet that could damage
> hardware.** Neither `include/config.h` nor `docs/pin_map.md` carries a
> level-shifting note for the audio lane, and this module is the only Sound
> member that can be run from a 5 V rail. The astromech simulator on this disk
> states the constraint in the general case -- *"Nothing may send 5 V back into an
> ESP32 pin -- none of these are 5 V tolerant"* -- without answering it for this
> board. **Measure the module's TX idle voltage before connecting it to
> `PIN_AUDIO_RX`**, or power the module from the jumper-selected regulated 3.3 V
> and take the level question off the table. Open Item 3.

## 7. The serial protocol (normative)

Full duplex, **8 bits, 1 start, 1 stop, no parity, no flow control**. Commands are
never echoed. The whole command set is one or two bytes:

> 1-byte commands are upper case ASCII characters. 2-byte commands start with an
> ASCII character. Those starting with an **upper case** character use an ASCII
> value ('0'-'9') as the second byte. 2-byte commands starting with a **lower
> case** character require a **binary** value (0-255) as the second byte.

That rule is the whole grammar, and it is worth internalising: **case tells you
how to encode the argument.**

### 7.1 The command table

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

Bolded rows are the four protoArtoo uses. There is **no checksum, no framing byte
and no acknowledgement** -- a play command is two bytes and silence.

### 7.2 `'t'` and `'p'` address different things

This distinction is easy to miss and it is the module's best feature:

- **`'t'` addresses the filename.** Track 42 is the file whose name starts `042`.
- **`'p'` addresses the directory position.** Track 42 is whatever the card
  enumerates 42nd.

protoArtoo uses `'t'`. Section 9.1 is why that matters.

### 7.3 The two status responses, and the terminator that may not exist

| Query | Response | Example |
| --- | --- | --- |
| `'S'` `'0'` | version string, `'='`-prefixed | `=MP3 Trigger v2.50` |
| `'S'` `'1'` | total track count in ASCII, `'='`-prefixed | `=14` |

The guide describes the first as *"an 18-byte version string: e.g. `=MP3 Trigger
v2.50`"*. **That example is exactly 18 characters.** If the count is literal,
the response carries **no CR, no LF and no terminator of any kind** -- and
`readLine()` (Section 8.1) waits for a `'\n'` that never arrives, returning only
when its 500 ms timeout expires.

> [!IMPORTANT]
> **If that reading is right, every status query costs its full 500 ms.** The
> parse still succeeds -- the accumulated text is intact and `line[0] == '='` --
> so nothing fails, it is just slow: `begin()` spends about **2 s** (1000 ms boot
> wait plus two timed-out queries) and each `queryModuleState()` about **1 s**,
> which is exactly the *"blocking up to ~1 s"* the driver already documents as
> its worst case. The difference is that it would be the **only** case.
>
> Our driver's comments claim `"=MP3 Trigger v2.NN\r\n"` and `"=NNN\r\n"`,
> sourced from other implementations rather than from the guide. `UNKNOWN` which
> is right on real firmware; Open Item 2 is a capture, and it is ten minutes with
> a USB-serial adapter and a terminal.

### 7.4 What the module says when nobody asked

| Byte | Meaning |
| --- | --- |
| `'X'` `0x58` | the currently playing track **finished** |
| `'x'` `0x78` | the currently playing track was **cancelled by a new command** |
| `'E'` `0x45` | **a requested track does not exist** |
| `'M'` `0x4D` + 3 bytes | Quiet Mode only: a trigger-pin bitmask (Section 3.2) |

> [!CAUTION]
> **`'E'` means the track is missing, not that the hardware is broken.** Both
> `src/drivers/audio_mp3trigger.cpp:37` and `include/audio_mp3trigger.h` call it
> *"hardware error"*. The guide is unambiguous: *"'E': When a requested track
> doesn't exist (error)."* The distinction is the difference between *"your SD
> card does not have track 126"* and *"your sound board has failed"*, and it is
> the first thing an operator would act on. Section 14.3 fixes the comment.

**These arrive at any time, including between a query and its reply.** Our
`sendQuery()` drains RX immediately before writing, which closes most of the
window but not all of it: a track finishing in the microseconds after the drain
puts an `'X'` at the head of the line buffer, `line[0] != '='`, and the query
reports a dead link on a live module. Section 14.6 records it; it is benign
(the next poll recovers) and it is real.

## 8. What protoArtoo's driver actually sends

### 8.1 The four `AudioDriver` methods, and the two helpers under them

| Call | Bytes on the wire | Source |
| --- | --- | --- |
| `playTrack(n)` | `'t'`, `(uint8_t)n` | `audio_mp3trigger.cpp:185-186` |
| `stop()` | `'t'`, `0xFE` | `audio_mp3trigger.cpp:197-198` |
| `setVolume(v)` | `'v'`, `(30 - v) * 255 / 30` | `audio_mp3trigger.cpp:212-215` |
| `begin(v)` | `'S'`,`'0'` then `'S'`,`'1'` then `setVolume(v)` | `audio_mp3trigger.cpp:132`, `:148`, `:160` |
| `queryModuleState()` | `'S'`,`'0'` then `'S'`,`'1'` | `audio_mp3trigger.cpp:243`, `:253` |

Two private helpers carry all the RX work:

- **`readLine()`** (`:79-95`) -- accumulates until `'\n'` or timeout, discards
  `'\r'`, NUL-terminates. Buffer is `char line[48]` at both call sites, so 47
  usable characters. Sleeps 1 ms between polls, which yields Core 0.
- **`sendQuery()`** (`:98-105`) -- drains stale RX, writes the two command bytes,
  reads one line. Every query in the driver goes through it.

The acceptance test is the same four times: `if (n > 1 && line[0] == '=')`. Two
things follow. A one-character response is rejected even if it is `'='`, and
**any** `'='`-prefixed line counts as a live link -- the driver never checks that
an S0 reply actually says *"MP3 Trigger"*.

**There is no inter-command delay.** The DY-SV5W driver posts 100 ms after every
frame and a test asserts it; this driver returns immediately after two
`writeByte()` calls. Nothing in the user guide requires a gap, but a SparkFun
forum thread reports the board needing 10-100 ms to settle after an `'X'`.
`UNKNOWN` whether back-to-back commands can be dropped; Open Item 4.

`begin()` **always returns `true`** (`:163`), link or no link. That is correct
against the interface contract -- *"false only for a transient failure that should
be retried"* -- and it means a missing module is reported through status, never
through a failed init.

### 8.2 The track guard, and why 256 is the interesting number

```c
if (track == 0)   { return; }                 // interface contract: silently ignore
if (track > 255)  { /* log */ return; }       // 't' takes one byte
m_lastTrack = track;
```

The comment at `:170-173` states the reason plainly: `(uint8_t)256` is `0x00`, so
without the guard a track number one past the ceiling would play **a different
track**, silently. `test_audio_mp3trigger.cpp:130` asserts that overflow directly,
and `test_audio_io_seam.cpp:270-271` asserts the real driver emits **zero bytes**
for `playTrack(256)`.

`m_lastTrack` is assigned **before** the bytes go out, and `stop()` does not clear
it. So `currentTrack` keeps naming the last track we asked for, after it has
stopped -- which is what "last played" means and is worth knowing when reading the
Sound page.

### 8.3 Volume: the map spans a register the module cannot use

```c
uint8_t nativeVol = (uint8_t)((uint32_t)(30u - vol) * MP3TRIGGER_VOL_MAX / 30u);
```

The direction is right -- the VS10xx register is inverted, `0x00` is loudest -- and
the arithmetic is pinned by eleven native tests. The problem is the **range**.

| `vol` | native | vendor: audible? | field: audible? |
| --- | --- | --- | --- |
| 30 | 0 | yes (maximum) | yes |
| 25 | 42 | yes | yes |
| **23** | **59** | **yes -- last audible step** | yes |
| 22 | 68 | **no** | yes |
| **20 (shipped default)** | **85** | **no** | yes, quiet |
| **19** | **93** | no | **yes -- last audible step** |
| 18 | 102 | no | **no** |
| 15 | 127 | no | no |
| 10 | 170 | no | no |
| 0 | 255 | no (silent) | no |

Two independent audibility floors exist and neither is ours:

- **The vendor's**, in the guide's own `#VOLM` note: *"Useful range is 0 to 64,
  with values above 64 being inaudible."* -> the operator's usable span is
  **`vol` 23-30, eight of thirty-one positions**.
- **A builder's measurement**, in `~/Documents/GitHub/AstroPixelsPlus/MarcduinoSound.h:64`:
  *"doc says anything below 64 is inaudible, not true, 100 is. 82 is another good
  value."* -> usable span **`vol` 19-30, twelve of thirty-one**.

AstroPixelsPlus acts on its own measurement: it maps the operator's whole range
onto **0-100**, not 0-255 (`MarcduinoSound.h:396`). protoArtoo knows the same
number -- `docs/sound_playback.md:299-300` says *"practical audible range is
approximately 0-100 on the native scale"* -- and maps to 0-255 anyway.

> [!CAUTION]
> **The shipped default volume is at or below the audible floor.**
> `src/config_store.cpp:166` sets `audioVolume = 20`, which is native **85**:
> inaudible by the vendor's number, and the quietest usable step by the field
> one. A builder fitting this module, flashing a default image and pressing play
> hears little or nothing, and the obvious diagnosis -- wiring, baud, card -- is
> the wrong one.
>
> The fix is one line and it is **not** made here, because it changes audible
> behaviour on a module nobody has bench-tested: mapping `vol` 0-30 onto native
> 100-0 instead of 255-0 would put the whole slider inside the audible band and
> make `vol = 20` a genuine two-thirds. Open Item 1 carries it, with the
> measurement that should precede it.

`30u - vol` is **unsigned and unguarded**. The interface says AudioTask clamps to
0-30 before the call and it does; a future caller that does not would underflow
to a large `uint32_t`. Noted, not a live defect.

### 8.4 `stop()` plays a track, and the community does not agree which one

The MP3 Trigger has **no discrete stop command**. `'O'` toggles, which means its
effect depends on a play state this protocol cannot report (Section 11.1). So
every implementation stops by playing a silent file -- and picks a different one:

| Implementation | Blank track | Source |
| --- | --- | --- |
| **protoArtoo** | **254** | `MP3TRIGGER_STOP_TRACK`, `audio_mp3trigger.h:49` |
| CHIRP compat layer | 254 | `mp3_compat.cpp` |
| AstroPixelsPlus / Reeltwo | **252** | `MP3_EMPTY_SOUND`, `MarcduinoSound.h:53` |

> [!IMPORTANT]
> **"Community standard" overstates it.** Our own comments call 254 *"the
> community-standard blank track"* and name BetterDuino and SHADOW_MD; the
> Reeltwo lineage uses 252 for the same purpose. Both are right for their own
> card. What matters operationally is only this: **`254XXXX.MP3` must exist in
> the root of the card in the droid**, or `stop()` produces an `'E'` and whatever
> was playing keeps playing. That file ships in the common R2 sound packs, and a
> builder who assembled a card by hand may not have it.

## 9. Track numbering, which is this module's real advantage

### 9.1 It addresses the filename, and nothing else in the family does

`'t'` plays the file whose name begins with the three digits you sent. Not the
*n*th file, not the file the card happened to be written first -- the file that
says `126` on the front.

That is worth stating as a contrast, because the peer sheets both record the
opposite problem. The DY-SV5W's index is **filesystem enumeration order**
([`dy-sv5w-sound.md`](dy-sv5w-sound.md) Section 10.2), so a card rebuilt in a
different order silently renumbers every sound in the droid. The DFPlayer has
three addressing modes of which only two are stable
([`dfplayer-mini-sound.md`](dfplayer-mini-sound.md) Section 9.1).

**On an MP3 Trigger, copying the files again in any order changes nothing.** The
card is self-describing, the numbers are visible in a file browser, and a builder
can edit one sound without disturbing the rest. For a droid whose sound library
is curated over years, that is the single most useful property this board has.

The costs of the same decision: **255 tracks, hard**, and no directories.

### 9.2 protoArtoo's named tracks *are* this module's numbers

`include/audio_dollar_parser.h:42-55` carries the defaults, and they are the
astromech community's MP3 Trigger banks:

| Tracks | Category | protoArtoo default that lands in it |
| --- | --- | --- |
| 001-025 | general | `AUDIO_TRACK_HAPPY = 3` |
| 026-050 | chatty | -- |
| 051-075 | happy | -- |
| 076-100 | sad | -- |
| 101-125 | whistle | -- |
| 126-150 | scream | `AUDIO_TRACK_SCREAM = 126`, `AUDIO_TRACK_FAINT = 128` |
| 151-175 | Leia | `AUDIO_TRACK_LEIA = 151` |
| 176-200 | music | `AUDIO_TRACK_CANTINA_S = 176`, `SW_THEME = 177`, `IMP_MARCH = 178`, `CANTINA_L = 180` |
| 201-225 | music | -- |
| **254** | **silent blank** | `stop()` |
| **255** | **startup** | `AUDIO_TRACK_STARTUP = 255` |

`docs/sound_playback.md:270` claims *"All protoArtoo named-track NVS defaults
match this layout with no remapping needed"*, and reading the header against the
table this session, they do -- with one thing the table does not flag:
**`AUDIO_TRACK_HAPPY = 3` sits in the general band, not the happy band.** That is
almost certainly deliberate (track 3 is a specific community greeting clip, and
the comment says so), but a reader checking the claim will trip on it.

**This is the strongest argument for keeping this module supported.** protoArtoo's
entire default sound namespace is the MP3 Trigger's convention. On this module the
defaults are correct out of the box; on every other member they are a mapping
exercise.

### 9.3 Three ways the config layer can ask for a track this module cannot play

The random pool and the twelve category ranges are `uint16_t` and validated as
`0 .. 0xFFFF` (`src/config_store.cpp:1186-1190`). Nothing in the config layer
knows this module's ceiling. So an operator can save, and the API will accept:

1. **a range above 255** -- every draw from it is dropped by the driver's guard
   with a `PA_LOG_WARN` and produces silence;
2. **a range spanning 254** -- draws land on the blank stop track, producing a
   silent "sound" at random;
3. **a range spanning 255** -- draws replay the startup sound.

None of these is a crash and all three present to the operator as *"sometimes
nothing happens"*. The clean fix is a member-aware validator, which is a change
to shared config validation for the sake of one module and therefore a decision
rather than a typo. Open Item 6.

## 10. The transport, which is borrowed on artoo-esp32

### 10.1 Two directions, two mechanisms

```c
s_mp3Serial.begin(9600, SERIAL_8N1, PIN_AUDIO_RX, -1);   // RX only, TX pin = -1
softUartTxBegin();                                        // TX: bit-bang
```

**TX is a software UART and RX is a hardware one**, and they are not the same
peripheral. The reason is on the artoo-esp32: three hardware UARTs, and UART0 is
the console, UART1 the drive link, UART2 shared between the dome link and audio's
RX (`include/config.h:141-152`). There is no spare TX, so audio bit-bangs it.

| Board Variant | `PIN_AUDIO_TX` | `PIN_AUDIO_RX` | `UART_PORT_AUDIO` | `PA_CAP_DEDICATED_AUDIO_UART` |
| --- | --- | --- | --- | --- |
| artoo-esp32 | 26 (bit-bang) | 35 (input-only) | 2, **shared with the dome link** | 0 |
| firebeetle2 | 34 | 36 | 3, exclusive | 1 |

> [!NOTE]
> **No build selects this driver on firebeetle2, so its P4 path has never run.**
> `audio_mp3trigger.cpp:10-14` says so and says why it was left alone: *"adding a
> capability branch no build compiles would ship untested code (#254)."* On that
> board the driver would still bit-bang TX on a pin that has a real UART behind
> it -- correct, but wasteful, and untested. The DY-SV5W driver branches on the
> capability; this one does not.

### 10.2 What a command costs Core 0

`softUartTxByte()` wraps each byte in a `portMUX` critical section, because
`delayMicroseconds()` is not interrupt-safe and a stretched bit period corrupts
the frame. At 104 us per bit and ten bit periods per byte that is **about 1.04 ms
per byte, with Core 0 non-preemptible**.

| Command | Bytes | Core 0 blocked |
| --- | --- | --- |
| `playTrack`, `stop`, `setVolume` | 2 | **~2.1 ms** |
| DY-SV5W play frame | 6 | ~6.2 ms |
| DY-SV5W volume frame | 5 | ~5.2 ms |

**The two-byte protocol is the cheapest in the family on this axis** -- a third of
a DY-SV5W play frame. Core 1's real-time loops (drive, RC, dome link) are
unaffected either way; this is Core 0's web and audio work only.

### 10.3 The read path is borrowed, and the claim is AudioTask's

On artoo-esp32 the UART controller this driver reads from belongs to the dome
link. Every query in `audio_task.cpp` is wrapped in `audioUartClaim()` /
`audioUartRelease()`, and a denied claim is reported as
**`AUDIO_RX_BLOCKED_BY_DOME_UART`** -- which the Sound page renders as *"RX
unavailable while protoR2link owns UART2"* rather than as a dead module.

The driver itself does no contention check, and its header is explicit that an
earlier comment claiming otherwise *"was never true on any commit of this file"*.
It also does not override `classifyRxStatus()`, and -- exactly as
[`dy-sv5w-sound.md`](dy-sv5w-sound.md) Section 12.3 works out for its own module
-- it does not need to, because `audio_task.cpp` tests the claim **before** calling
the driver. The one call site that escapes that reasoning is Section 14.5.

## 11. Capabilities, status, and what the operator sees

### 11.1 The capability word is `0x0D`, and one of its three bits is memory

```c
PA_COMPONENT_PART(19, "mp3_trigger", "MP3 Trigger", COMPONENT_CATEGORY_SOUND, "mp3trigger_serial",
                  COMPONENT_STATUS_SUPPORTED,
                  AudioDriver::AUDIO_CAP_STATUS_QUERY | AudioDriver::AUDIO_CAP_TRACK_COUNT |
                  AudioDriver::AUDIO_CAP_CURRENT_TRACK,
                  nullptr, 1)
```

`0x01 | 0x04 | 0x08` = **`0x0D`**, asserted at
`test_component_registry.cpp:88` and again through the driver at
`test_audio_sound_member.cpp:48-51`. The driver does not restate the word; it
returns `componentPartCapabilities("mp3_trigger")`, with a `static_assert` that
the id exists. **This is the narrowest capability word of the three built Sound
members** -- DY-SV5W is `0x0F`, CHIRP is `0x3F`.

What the three bits actually cost the module to honour:

| Bit | Set? | How it is satisfied |
| --- | --- | --- |
| `STATUS_QUERY` `0x01` | yes | `'S'`+`'0'` -- a real round trip |
| `DEVICE_TYPE` `0x02` | **no** | no such command exists; `device` is hardcoded `0xFF` |
| `TRACK_COUNT` `0x04` | yes | `'S'`+`'1'` -- a real round trip |
| `CURRENT_TRACK` `0x08` | yes | **cached `m_lastTrack`, never queried** |
| `QUERY_SAFE_PLAYING` `0x10` | **no** | polling during playback is not claimed safe |
| `CATALOG` `0x20` | **no** | no banks; `playTrackBanked()` falls through to `playTrack()` |

> [!NOTE]
> **`CURRENT_TRACK` here means "the driver can report it", not "the module can be
> asked".** The protocol has no current-track query; the value is what we last
> sent. That is a defensible reading of the bit -- the operator gets a true answer
> -- and it is a different guarantee from the DY-SV5W's, where the same bit is a
> live `0x0D` query. Worth knowing before trusting the field after a track ends
> on its own.

**Play state has no capability bit at all.** It is emitted unconditionally by the
API and is permanently `0xFF` here, which is why `docs/sound_playback.md:316`
says *"Play-state indicator always shows `unknown` for the MP3 Trigger"*. The
`'X'` / `'x'` messages (Section 7.4) are exactly the information that would fix
this, and the driver discards them.

### 11.2 What the Sound page does with `0x0D`

`data/sound.js:307-360` is the one consumer, and its own comment sets the rule:
*"a capability the firmware declares and nothing reads is worse than no
capability, because the page then reports a field the fitted module cannot
actually answer."* For this word:

- Device row: **hidden** (`supportsStatusQuery && supportsDeviceType`)
- Total tracks row: shown
- Current track row: shown
- Status table: shown; the no-query notice: hidden
- **Manual Poll button: shown**, because `showManualPoll` is
  `supportsStatusQuery && !supportsSafePlayingQuery`
- Auto-refresh cadence: **off** (`moduleStatusCadenceWanted` follows
  `QUERY_SAFE_PLAYING`)
- Note text: *"Status is cached from boot. Use Poll to refresh -- only poll when
  not playing."*
- CHIRP catalog card: hidden, with *"Catalog unavailable for this backend."*

The API still emits `"device"` for this module -- it serialises `0xFF` as
`"none"` -- so a client that branches on the JSON field rather than on the
capability bit will happily render a device row saying "none".
`docs/api.md:589-590` already states the rule: *"Clients branch on a bit, never
on `driver`."*

### 11.3 Selecting it

`snd_member` in NVS, value `mp3_trigger`, accepted by `POST /api/config` and
**staged at reboot** like any Component Toggle (ADR 0027, ADR 0042). Validation is
registry-driven: only a value whose row is in `COMPONENT_CATEGORY_SOUND` and is
selectable is accepted, and an unrecognised stored member falls back to
`dy_sv5w`.

Every image carries this driver -- `included` is the literal `1` on the row and
there is no Board Capability Gate, because all three Sound modules bolt to
`PIN_AUDIO_TX` and no board narrows the set (ADR 0042:174-178). `PA_AUDIO_DRIVER
= AUDIO_MP3TRIGGER` no longer selects what the image can drive; it only names
what a controller **that has never been told** starts with. The dedicated envs
(`artoo_esp32_mp3trigger`, `..._ota`, `..._check`) and `make ota-mp3trigger` still
exist and are the way to ship a board that boots straight onto this module.

> [!NOTE]
> `README.md:269` still describes swapping sound modules as something you do
> *"with a reflash"*. Since ADR 0042 it is a setting. Not changed here -- it is
> operator-facing prose in a file this sheet does not own -- but it is stale.

## 12. How the four Sound members differ

[`dy-sv5w-sound.md`](dy-sv5w-sound.md) Section 16 carries the full
twenty-row comparison and this sheet does not duplicate it. The rows where the
MP3 Trigger is the outlier:

| Row | MP3 Trigger | Why it matters |
| --- | --- | --- |
| Frame | **2 bytes, no checksum, no ack** | cheapest command in the family (~2.1 ms of Core 0) |
| Addressing | **filename prefix** | the only member whose numbering survives a card rebuild (Section 9.1) |
| Volume native | **0-255, inverted** | and mostly inaudible (Section 8.3) |
| Play-state query | **none** | permanently `unknown` on the Sound page |
| Device-type query | **none** | the only built member missing this bit |
| Stop | **no stop command** | done by playing a silent track (Section 8.4) |
| On-board amplifier | **no** | needs an external amp; output is line level with a DC offset |
| Trigger inputs | **18** | the only member with a hardware path that bypasses us entirely |
| Price | **~USD 50** | an order of magnitude above the DY-SV5W |
| Proven on our hardware | **no** | the only *supported* member with no bench record |

**What it uniquely brings** is the ecosystem. Filename addressing, the 25-track
bank convention every R2 sound pack ships in, and the fact that protoArtoo's own
named-track defaults already *are* its numbers (Section 9.2). A builder migrating
from MarcDuino, Padawan360 or SHADOW keeps their card and their muscle memory.

**What it uniquely costs** is 255 tracks, no status beyond "the link is up", an
external amplifier, the price, and a volume curve that needs work before the
module sounds like anything (Section 8.3).

## 13. How the hobby drives this module (non-normative)

Everything in this section is evidence of practice. None of it is normative.

### 13.1 CHIRP implements this protocol on purpose

`~/Documents/GitHub/CHIRP/.../mp3_compat.cpp` is a **deliberate MP3 Trigger
compatibility layer** on a 2025-era RP2350 board:
`checkAndHandleMp3Command()` intercepts `'O'`, `'F'`, `'R'`, `'T'`, `'t'`, `'v'`
and `'p'` before CHIRP's own ASCII parser sees them, converts `'v'`'s inverted
byte to a float gain (`1.0f - sfVol / 255.0f`) and applies it to every stream, and
uses **track 254** as its blank.

That is the clearest possible statement of this board's standing: a newer,
better-specified module shipped a shim so that droids already wired for an MP3
Trigger could drop it in. **Two commands are not in that shim** -- `'S'` status
and `'Q'` quiet mode -- so a controller pointed at a CHIRP while configured as an
MP3 Trigger would play tracks correctly and report a dead link forever.
protoArtoo never does this (it has a native CHIRP driver), but a builder
debugging a mixed setup might.

### 13.2 The others have moved on, and one of them left a trail

- **AstroPixelsPlus / Reeltwo** (`MarcduinoSound.h`) abstracts three modules
  (`kMP3Trigger`, `kDFMini`, `kHCR`) behind a `Stream&`, uses **the same volume
  formula we do**, and -- as Section 8.3 sets out -- **maps to 0-100 rather than
  0-255**, on the strength of its own measurement. It is the one project that
  tested the audible floor and wrote the number down.
- **ShadowMD** does not drive a sound module at all. It delegates to MarcDuino
  over `$8x\r`, so the MP3 Trigger is behind that board, not behind ShadowMD.
- **The Padawan360 on this disk is a port away from this module.** Its
  `mp3Trigger.play()` calls survive as comments beside the DY-SV5W calls that
  replaced them -- the migration [`dy-sv5w-sound.md`](dy-sv5w-sound.md) Section
  15.2 shows got the volume direction wrong precisely because the two modules
  invert against each other.
- **MarcDuino** itself is the reason the numbering exists. Its firmware and the
  R2 Touch app hardcode the 25-track banks, which is why the sound packs are
  shaped that way and why changing a filename breaks a droid.

### 13.3 Two field problems no vendor document mentions

From Printed Droid's knowledge base, which is builders writing for builders:

- **False triggers on long cables.** *"Long cables cause false triggering and
  crosstalk between the inputs"* because of *"inadequate pull-up resistors on the
  inputs, so they have a tendency to 'float'."* The recommendation is *"short
  cables (like 2ft or less)"*; the fix is a **1 kOhm pull-up** added between
  *"pin 3 of the micro-controller and the large tab (3.3v) of the nearby voltage
  regulator"*, which reportedly *"fixed the false triggers on all inputs, even
  with 25ft cables"*. protoArtoo does not use the trigger pins -- but a board
  wired for them in a droid will start tracks on its own, and the symptom is
  indistinguishable from a firmware bug.
- **The audio output is not line out.** *"The included audio output has a DC
  offset with respect to the power supply ground, so it's only safe for driving
  headphones. Also, the static electricity from a long cable might fry the MP3
  decoder chip."* A droid runs a long cable to an amplifier in the body, which is
  exactly the case being warned about. **AC-couple it.**

## 14. Findings against the shipping implementation

Six, found by reading the driver against the user guide and the schematic this
session. Three are fixed in the change that carries this sheet; three are
reported.

### 14.1 FIXED -- the decoder is a VS1063, not a VS1053

Five comments across `include/audio_mp3trigger.h` and
`src/drivers/audio_mp3trigger.cpp` name the VS1053, including the volume comment
a developer would copy. SparkFun's schematic says `value="VS1063"`. Nothing
functional turns on it (Section 2.2); the comments are corrected.

### 14.2 FIXED -- the SparkFun part number is WIG-13720, not DEV-13720

`include/audio_mp3trigger.h:4` and `docs/sound_playback.md:242` both say
`DEV-13720`. SparkFun's own repository README links the product as *"MP3 Trigger
(WIG-13720)"*. A wrong part number is how a builder buys the wrong board.

### 14.3 FIXED -- `'E'` means the track is missing, not that the hardware failed

`audio_mp3trigger.cpp:37` and the matching header line document `'E'` as
*"hardware error"*. The user guide: *"'E': When a requested track doesn't exist
(error)."* Section 7.4. The driver does not act on `'E'` either way, so this is a
comment fix -- but it is the comment that would send someone to the wrong
diagnosis.

### 14.4 FIXED -- `docs/sound_playback.md` sends the reader away for a fact we now have

`:246-248` says to *"refer to the SparkFun MP3 Trigger v2.4 Hookup Guide for the
exact filename and format"* of the baud init file. The filename is
`MP3TRIGR.INI`, the line is `#BAUD 9600`, and both are now in that document and
in Section 6.1.

### 14.5 REPORTED -- `begin()`'s queries are not arbitrated

Every query in `audio_task.cpp` is wrapped in `audioUartClaim()` **except the two
inside `begin()`**. On artoo-esp32 the sequence is the same one
[`dy-sv5w-sound.md`](dy-sv5w-sound.md) Section 17.5 reasons out for its module,
with a shorter fuse: `begin()` opens the shared controller RX-only, sleeps
**1000 ms**, and then queries -- while DomeLinkTask, on the other core, may have
re-opened the same controller on the dome's pins and set its owner flag.

If DomeLinkTask wins, S0 gets nothing, `m_linkOk` stays false, S1 is skipped
entirely, `m_totalTracks` stays 0, and the cached state the Sound page seeds from
says the module is dead. The operator's Poll button then recovers it, because
that path **is** arbitrated. Reasoned from code, not measured -- and unlike the
DY-SV5W there is no hardware record to argue against it. Open Item 7.

The same call path is where `classifyRxStatus()` is actually used
(`audio_task.cpp:690`), unclaimed, so a blocked boot query is reported as
`NO_RESPONSE` rather than `BLOCKED_BY_DOME_UART`. The driver does not override
that method and, per Section 10.3, does not need to anywhere else.

### 14.6 REPORTED -- an unsolicited byte can fail a live query

`sendQuery()` drains RX then writes. A track that finishes in the window between
the drain and the reply puts `'X'` at the head of the buffer; `line[0] != '='`,
and the driver reports a dead link on a working module until the next poll. The
guide's `'X'` / `'x'` / `'E'` messages (Section 7.4) are unhandled, so any of the
three can do it.

The peer sheet's rule applies unchanged --
[`dy-sv5w-sound.md`](dy-sv5w-sound.md)'s *"MUST NOT assume a query response is
the next bytes on the wire"* -- and the cheap fix is the same shape: skip leading
bytes that are not `'='` rather than accepting the first line whole. Benign
today; Open Item 8.

### 14.7 REPORTED -- the mirror test suite can pass while the driver is wrong

`test_audio_mp3trigger.cpp` states it plainly at `:9-11`: *"The driver is not
instantiated here ... all logic under test is extracted as standalone functions
that mirror the implementation."* The volume formula, the constants and the
guards are **copied** into the test. Change `setVolume()` in the driver and those
eleven tests still pass.

The saving grace is `test_audio_io_seam.cpp`, which drives the **real**
`AudioDriverMp3Trigger` through the injected IO and asserts the actual TX bytes
for play, stop, volume and `begin()` -- ten tests, including that `playTrack(256)`
emits **zero** bytes and that a link-less `begin(10)` emits exactly
`'S'`,`'0'`,`'v'`,`0xAA`. So the wire is genuinely pinned; the mirror suite is
belt-and-braces that could silently stop matching the belt.

**No test drives `queryModuleState()`.** Its parse path -- the `'='` guard, the
`sscanf`, the S1-gated-on-S0 rule -- is exercised only through `begin()`.

## 15. Agent Lookup Quick Reference

- Field: Component Protocol. Required value: **`mp3trigger_serial`**.
- Field: protoArtoo identifiers. Required value: registry value **19**, id `mp3_trigger`, name `MP3 Trigger`, build default `PA_AUDIO_DRIVER = AUDIO_MP3TRIGGER`, NVS member value `mp3_trigger` under key `snd_member`.
- Field: Capability word. Required value: **`0x0D`** -- STATUS_QUERY, TRACK_COUNT, CURRENT_TRACK. **No `DEVICE_TYPE`, no `QUERY_SAFE_PLAYING`, no `CATALOG`.** Narrowest of the three built Sound members.
- Field: Vendor part. Required value: **WIG-13720** (SparkFun). **Not DEV-13720.**
- Field: Decoder. Required value: **VLSI VS1063**, from SparkFun's schematic. **Not VS1053**, whatever our comments said.
- Field: Serial format. Required value: **8 bits, 1 start, 1 stop, no parity, no flow control, full duplex, 3.3-5 V TTL**.
- Field: Baud. Required value: **9600 for protoArtoo**; the factory default is **38400** and the change requires `MP3TRIGR.INI` on the card.
- Field: Init file. Required value: **`MP3TRIGR.INI`** in the card root, `#BAUD 9600`, `#` then a space, first 512 bytes only, `*` ends the command section.
- Field: Allowed baud values. Required value: **2400, 9600, 19200, 31250, 38400**. Nothing else.
- Field: Command grammar. Required value: 1 or 2 bytes. **Upper-case first byte takes an ASCII digit argument; lower-case takes a binary byte.**
- Field: Play a track. Required value: **`'t'` + binary 1-255**, addressing the file whose name starts with those three digits.
- Field: `'p'` versus `'t'`. Required value: **`'p'` is directory position, `'t'` is filename prefix.** protoArtoo uses `'t'`; never swap them.
- Field: Track ceiling. Required value: **255**. `playTrack(256)` emits no bytes; `(uint8_t)256` would be `0x00`.
- Field: Stop. Required value: **`'t'` + 254** -- there is no stop command. `254XXXX.MP3` must exist in the card root.
- Field: Blank-track number elsewhere. Required value: **252** in AstroPixelsPlus/Reeltwo. Do not assume 254 is universal.
- Field: Volume command. Required value: **`'v'` + binary 0-255, inverted** -- `0x00` loudest.
- Field: Volume audibility. Required value: the vendor says **above 64 is inaudible**; a builder's measurement says **above 100**. Either way most of the register is silent.
- Field: protoArtoo volume map. Required value: `nativeVol = (30 - vol) * 255 / 30`. **`vol` below 19 is inaudible; the shipped default of 20 is native 85.** Section 8.3.
- Field: Status queries. Required value: **`'S'`+`'0'`** version, **`'S'`+`'1'`** track count, both replies `'='`-prefixed.
- Field: Response terminator. Status: **UNKNOWN** -- the guide describes an 18-byte version string that is exactly 18 visible characters, implying no CR/LF. Settled by capturing the bytes (Open Item 2).
- Field: Unsolicited bytes. Required value: **`'X'` finished, `'x'` cancelled, `'E'` requested track does not exist.** `'E'` is **not** a hardware error.
- Field: Quiet Mode. Required value: `'Q'`+`'1'` makes trigger pins report `'M'` plus a 3-byte bitmask instead of playing. Off by default, not preserved across power cycles.
- Field: Card. Required value: **microSD, SDSC or SDHC, FAT16 or FAT32, root directory only, `NNNxxxx.MP3`**, MP3 up to 192 kbps stereo.
- Field: Hot swap. Required value: **not supported.** The card is read only at power-on.
- Field: Power. Required value: **4.5-12 V DC or regulated 3.3 V (jumper)**, about **45 mA idle / 85 mA playing**.
- Field: Audio output. Required value: **line level with a DC offset**, no on-board amplifier. AC-couple before a long cable.
- Field: Trigger pins. Required value: **18**, active low, internally pulled up, 3.3-5 V. protoArtoo uses none of them.
- Field: Status LED. Required value: **3 short blinks = ready.** 1 long = no card; 1 long + 1 short = card but no MP3s; constant short = decoder fault.
- Field: Transport on artoo-esp32. Required value: **bit-bang TX on GPIO 26, hardware RX on GPIO 35 (UART2, shared with the dome link)**. About **2.1 ms of blocked Core 0 per command**.
- Field: Level shifting. Status: **UNKNOWN** -- the module's TX swing is not stated in any document read. `PIN_AUDIO_RX` is not 5 V tolerant (Open Item 3).
- Field: Hardware verification. Required value: **none.** No bench record exists for this module on this project's droid.

If a required value cannot be proven for the board in hand, status is `UNKNOWN`
and dependent work stops.

## 16. Open Items

| # | Item | How to settle it |
| --- | --- | --- |
| 1 | **Should the volume map target 0-100 instead of 0-255?** Section 8.3. Today `vol` 0-18 is inaudible and the shipped default of 20 is at the edge. AstroPixelsPlus maps to 0-100 on its own measurement. | Play a known track at native 40, 64, 85, 100 and 120 through a real amplifier and write down where it stops being usable. Then change one line in `setVolume()` and the eleven mirror tests. **A behaviour change on an unverified module -- measure first.** |
| 2 | **Do the `'S'` replies end with CR/LF?** Section 7.3. If not, every query costs its full 500 ms timeout. | Ten minutes: USB-serial adapter, 9600 baud, send `S0`, capture raw bytes, count them. |
| 3 | **What voltage does the module's TX idle at?** Section 6.3. `PIN_AUDIO_RX` is not 5 V tolerant on either board. | Meter on the module's TX pin with the board powered from 5 V, then from the 3.3 V jumper position. **Do this before wiring one to a controller.** |
| 4 | **Can back-to-back commands be dropped?** Section 8.1. We post no inter-command delay; the DY-SV5W driver posts 100 ms; a SparkFun forum thread reports a 10-100 ms settling window after `'X'`. | Send `stop()` immediately followed by `playTrack(n)` twenty times and count how many play. |
| 5 | **Is there a start-up latency worth designing around?** A research pass claimed about 80 ms of MP3 parser delay and 100-500 ms gaps between sequential tracks; **neither is in any document read this session** (Section 5). | Trigger a known track against a scope or a phone recording and measure command-to-first-sample. |
| 6 | **Should config validation know the fitted module's track ceiling?** Section 9.3. Category ranges validate `0..0xFFFF`; this module drops above 255, plays silence on 254 and the startup sound on 255. | A decision, not a measurement: either a member-aware validator, or a Rehearsal-Warning-style report on the Sound page. It touches shared config validation for one module's sake. |
| 7 | **Do `begin()`'s queries lose the UART race on artoo-esp32?** Section 14.5. Reasoned from code; unlike the DY-SV5W there is no hardware record arguing against it. | One boot with the dome link on serial and an MP3 Trigger fitted: does the log say *"link OK - version:"* or *"no response to S0 query"*? |
| 8 | **Should `sendQuery()` skip leading non-`'='` bytes?** Section 14.6. An `'X'` arriving in the window after the drain fails a query on a live module. | Cheap and natively testable through the existing recording IO: feed `X=MP3 Trigger v2.50` and assert the parse still succeeds. |
| 9 | **Is `AUDIO_TRACK_HAPPY = 3` right?** Section 9.2. It sits in the community's *general* band, not the *happy* band, while every other named default lands where the table says. | An operator decision about which clip should answer `$H`, not a research result. |
| 10 | **Does anything test that the Device row is hidden for a `0x0D` module?** `test_sound_capability_consumers_340.js` pins `TRACK_COUNT` both ways but not `DEVICE_TYPE`. | Add the mirror case to the existing web test; it is four lines beside the ones already there. |

## 17. Sources

**Primary -- vendor**

- **MP3 Trigger v2 User Guide, 2012.02.01** -- https://www.robertsonics.com/s/MP3TriggerV2UserGuide_2012-02-04.pdf
  (downloaded and read in full this session; the source for Sections 3, 6 and 7)
- **SparkFun MP3 Trigger hardware repository** -- https://github.com/sparkfun/MP3_Trigger
  (cloned; the Eagle schematic settled the decoder and the SKU)
- SparkFun MP3 Trigger Hookup Guide v2.4 -- https://learn.sparkfun.com/tutorials/mp3-trigger-hookup-guide-v24/all
- SparkFun product page -- https://www.sparkfun.com/products/13720
- Robertsonics product pages -- https://www.robertsonics.com/mp3-trigger,
  https://www.robertsonics.com/wav-trigger, https://www.robertsonics.com/tsunami
- `MP3TRIGR.INI` sample, mirrored by Pololu -- https://www.pololu.com/file/0J531/mp3trigr.ini

**Primary -- source code, read on this disk**

- `~/Documents/GitHub/CHIRP/CHIRP_Audio_Trigger/Arduino_Sketches/CHIRP_Audio/mp3_compat.cpp`
- `~/Documents/GitHub/AstroPixelsPlus/MarcduinoSound.h`
- `~/Documents/GitHub/ShadowMD/src/Shadow_MD_DualController_Template.ino`
- `~/Documents/GitHub/Padawan360_mega_maestro_DYSV5W/Padawan360_body_mega_maestro_DY5_audioplayer_BETA.ino`

**Builder documentation (primary source, non-normative)**

- Printed Droid, SparkFun MP3 Trigger board -- https://www.printed-droid.com/kb/sparkfun-mp3-trigger-board/
  (the false-trigger fix and the DC-offset warning in Section 13.3)
- CuriousMarc, MP3 Trigger sound system -- https://www.curiousmarc.com/r2-d2/mp3-trigger-sound-system
  (the MarcDuino bank convention)

**protoArtoo**

- `src/drivers/audio_mp3trigger.cpp`, `include/audio_mp3trigger.h`,
  `src/drivers/audio_soft_uart_tx.h`, `include/audio_driver.h`
- `include/component_registry.inc`, `src/tasks/audio_sound_member.cpp`,
  `src/tasks/audio_task.cpp`, `include/config.h`, `src/config_store.cpp`,
  `include/audio_dollar_parser.h`, `data/sound.js`
- `test/test_native/test_audio_mp3trigger/`, `test_audio_io_seam/`,
  `test_audio_sound_member/`, `test_component_registry/`,
  `test/test_web/test_sound_capability_consumers_340.js`
- `docs/sound_playback.md`, `docs/api.md`, `docs/status.md`, `docs/goal.md`,
  `CONTEXT.md`, ADR 0027, ADR 0042
- [`dy-sv5w-sound.md`](dy-sv5w-sound.md), [`dfplayer-mini-sound.md`](dfplayer-mini-sound.md)

> [!NOTE]
> **Negative results, recorded so nobody repeats them.** The SparkFun hardware
> repository carries **no firmware** -- only Eagle files and a production panel --
> so the command set could not be read from source and the user guide is the
> authority for it. No mechanical drawing was found for the board in any vendor
> document. No document read this session states the module's TX output swing,
> which is Open Item 3. And the astromech projects on this disk that once drove
> this module have all moved off it: ShadowMD to MarcDuino, Padawan360 to the
> DY-SV5W, AstroPixelsPlus to an abstraction that still carries it. **CHIRP's
> compatibility layer is the only live MP3 Trigger code in the stable.**
