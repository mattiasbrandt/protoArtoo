# DFPlayer Mini Spec Sheet (DFRobot DFR0299)

The **DFPlayer Mini** is a 16-pin MP3 player module: microSD (and USB) playback
decoded in hardware, a 10-byte binary serial protocol at 9600 baud, a hardware
`BUSY` pin, and an on-board bridge amplifier for a speaker under 3 W. DFRobot
sells it as SKU DFR0299, and many unrelated vendors fill the same form factor.

Research date 2026-09-11. Every frame byte, command code, electrical value and
default below was read from DFRobot's own datasheet (DFR0299, a **scanned** PDF
read page by page as images), from DFRobot's own library and SDK source, or from
the astromech projects that command the module. Checksums were **computed, not
copied**. Claims that could not be sourced are marked `UNKNOWN` with the artefact
or bench test that would settle them.

> [!CAUTION]
> **The vendor documentation is not trustworthy byte-for-byte, and this is the
> central finding of the sheet.** Verified by reading and computing:
>
> - **All three worked examples in the datasheet have wrong checksums** -- every
>   byte sequence a developer would copy to bootstrap a driver fails the
>   datasheet's own stated rule (Section 5.2).
> - **The `BUSY` pin polarity is stated both ways**, ten pages apart, in the same
>   document (Section 4.3).
> - **The device numbering for `0x09` contradicts the module's own `0x3F` reply**
>   (Section 6.1), and the query commands `0x47`/`0x48` are assigned to opposite
>   devices by the datasheet and by DFRobot's own library (Section 6.2).
>
> **Derive every constant from the rule, assert it in a test, and never trust a
> printed byte sequence.**

> [!WARNING]
> **"A DFPlayer Mini" is a form factor, not a part.** Several unrelated silicon
> vendors fill it and they do not behave identically -- checksum handling, ACK
> behaviour, busy polarity and folder rules all vary. Section 2 is what a builder
> and a driver each have to do about it.

## 1. Scope

Covers the module and its clone variants, the electrical contract and pinout, the
serial frame and checksum, the full command and query set, the frames the module
sends unprompted, the SD card addressing contract, how the astromech hobby commands
sound modules generally, the host libraries, and how the module compares with
three other serial sound modules the hobby uses.

Does not cover: audio file encoding and bitrate selection, the module's USB
device mode, the ADKEY resistor-ladder input mode, the `0x08` playback modes
beyond naming them (a droid plays one-shots), or the DFPlayer Pro / DF1201S,
which is a different part with a different protocol.

## 2. What you are actually buying

> [!CAUTION]
> **"DFPlayer Mini" is a 16-pin form factor, not a part.** At least eight silicon
> families ship in it, and they are not drop-in compatible at the protocol level.
> It is the dominant risk in adopting it.

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
**tabulated** checksums are correct (Section 5.2) -- prints a **two-column
table**, verified in its own text:

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
> **For a host driver this is a design requirement, not trivia.** The frame
> builder must be able to emit **with or without** the checksum bytes, selected by
> a setting, because there is no way to know from the outside which module a
> builder soldered in. That is two lines in a send routine and it is the
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
> constant, not a state. To read DFRobot stock, read the field the site computes
> for the human.
>
> For this part stock matters little: a commodity form factor is available from a
> hundred sellers. **That is the same fact as Section 2's risk, seen from the other
> side** -- the thing that makes it always purchasable is the thing that makes it
> unidentifiable.

**Successor, and not a drop-in:** DFPlayer Pro (DFR0768, chip `DF1201S`) has
128 MB of onboard flash and speaks **AT commands over UART** -- a completely
different protocol. It is a different part, not a newer DFPlayer Mini. There is no
DFRobot product called "DFPlayer Mini Lite"; listings using that name are
third-party inventions.

## 3. Sources Checked

| Source | How it was taken | What it gave |
| --- | --- | --- |
| **DFR0299 datasheet V1.0** -- https://dfimg.dfrobot.com/wiki/20532/DFR0299_mp3-player-module_datasheet_V1.0.pdf | `curl`, then **rendered to PNG and read as images** -- it is a scan with no text layer | Pinout, frame format, command tables, returned frames, error codes, power-on timing. And the three wrong checksums, the `BUSY` contradiction and the device-numbering conflict |
| DFRobot wiki, DFR0299 | fetched, parsed for the datasheet link | The only machine-readable route to the datasheet |
| **`DFRobot/DFRobotDFPlayerMini`** | fetched raw and read in full | The checksum algorithm, every command code, the device constants, `begin()`'s 2.2 s reset path, and an unreachable branch in its `0x3F` handler |
| **AstroPixelsPlus `MarcduinoSound.h`** | read from source | A dome controller's DFPlayer integration: the 9x25 bank model, `play()` versus its own documented `/mp3/` layout, the hard-fail `begin()` (Section 9.6) |
| AstroPixelsPlus `docs/SETUP.md`, `docs/HARDWARE_WIRING.md` | read from source | The documented card layout, the wiring, and the dome's own UART conflict |
| **Padawan360 DY-SV5W port** | read from source | A line-by-line MP3 Trigger to DY-SV5W port with the track numbers unchanged -- the evidence that the hobby's numbering is module-independent (Section 9) |
| **ShadowMD** | read from source, grep validated | No sound hardware at all: MarcDuino function codes, MP3 Trigger file numbers |
| CHIRP | read from source | What the multi-stream tier actually is |
| `SnijderC/dyplayer` | fetched | DY-SV5W volume is **0-30 ascending, default 20** -- which makes a Padawan360 comment provably stale |

## 4. Electrical

### 4.1 The pinout, from the datasheet's own table

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
| 16 | BUSY | Playing Status | see Section 4.3 |

> [!NOTE]
> **VCC is specified 3.2-5.0 V, typical 4.2 V -- so 3.3 V is inside the
> specification.** That means an ESP32 host can run the module from the same
> 3.3 V rail with **no level shifting in either direction**. The cost is amplifier
> headroom: SPK1/SPK2 is an internal bridge amp and its output scales with supply,
> so a 3.3 V droid is a quieter droid. A builder who wants volume runs it at 4.2-5 V
> and then **does** need to divide the module's 5 V TX down to the ESP32.
>
> The widely-copied hobby wiring adds a **1 kohm series resistor in the host's TX
> line to the module's RX**, attributed to noise. That is community practice; the
> datasheet does not specify it (Open Item 6).

### 4.2 Audio out: two different outputs, one of which is a trap

`SPK1`/`SPK2` is a **bridge-tied amplifier for a speaker under 3 W** -- it is not
a line output and must never be connected to an amplifier input, because neither
leg is ground-referenced. `DAC_L`/`DAC_R` is the line-level pair for an external
amplifier.

For a droid that already has an amplifier, `DAC_L`/`DAC_R` is the correct
connection and the on-board amp goes unused. For a droid with a bare speaker,
SPK1/SPK2 is a complete solution at low volume and is a meaningful part of why
this module costs what it does.

### 4.3 `BUSY`: the datasheet contradicts itself, and a second vendor settles it

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
> These are exact opposites.

**Resolved: `BUSY` is LOW while playing.** A second vendor's datasheet for the
same silicon settles it without a bench run. Flyron's FN-M16P, pin 16, read
directly:

> "Low level when working, and high level when standby"

Two independent vendor documents -- DFRobot's own pin table and Flyron's -- against
one self-contradicting section, and hobby practice agrees with both. **Section
3.3.2 of the DFRobot manual is simply wrong**, and it is worth naming because the
error has been copied downstream into at least one widely-circulated third-party
rewrite.

> [!NOTE]
> **Assertion latency is the part that still needs measuring**, and it is chip
> dependent: some clones need a settling time of roughly 350 ms after a play
> command before `BUSY` reflects reality, and on `MH2024K-16SS` it is reported not
> to track play state reliably at all. **Do not use `BUSY` as a play-started
> edge.** Gate it behind a fixed hold after each command and use it only as a
> play-finished indication (Open Item 1).

The polarity matters more than it looks. `BUSY` gives **playback state on one
GPIO with no serial traffic at all**, so a host whose RX line is shared, absent or
busy can still see whether a sound is playing. It is the same shape as the Pololu
Maestro's `ERR` pin: a single input that turns a silent module into an observable
one.

## 5. The serial protocol (normative)

### 5.1 The frame

9600 baud, 8-N-1, default and effectively fixed. The datasheet's own wording is
*"serial communication baud rate can set as your own, the default baud rate is
9600"*, but **no serial command changes it** (Section 6) -- so 9600 is what
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
| CMD | -- | Section 6 |
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

Worked, and verified by computation:

```
play track 1        7E FF 06 03 00 00 01 FE F7 EF
play /mp3/0001.mp3  7E FF 06 12 00 00 01 FE E8 EF
set volume 15       7E FF 06 06 00 00 0F FE E6 EF
stop                7E FF 06 16 00 00 00 FE E5 EF
query status        7E FF 06 42 00 00 00 FE B9 EF
```

### 5.2 Every worked example in the official datasheet has a wrong checksum

> [!CAUTION]
> **All three byte sequences the datasheet prints as copyable examples fail its
> own checksum rule.** Computed against the rule DFRobot's own library
> implements:
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
> return in Section 7.3.
>
> **The pattern is sharper than "DFRobot's document is wrong", and it generalises.**
> Checked against Flyron's FN-M16P for the same silicon: its **command tables are
> correct** (`7E FF 06 01 00 00 00 FE FA EF` and the rest verify exactly), while
> its **prose example is wrong** in the same way DFRobot's are -- section 3.3.2
> gives volume 15 as `7E FF 06 06 00 00 0F FF D5 EF`, where the correct checksum is
> `FE E6`.
>
> So across **two independent vendors**, the tabulated frames are right and the
> hand-written prose examples are wrong. The tables were evidently generated; the
> prose was typed. **Trust a vendor's table over a vendor's sentence, and compute
> either way.**
>
> **Derive the frame from the rule and assert it in a test; never copy a vendor's
> printed bytes.**

### 5.3 Framing has no resynchronisation help

Unlike the Pololu Maestro's protocol, **there is no high-bit convention separating
command bytes from data bytes** -- every field is a full byte and `0x7E` or
`0xEF` can appear anywhere in a parameter or a checksum. A decoder must
therefore hunt for `0x7E`, take exactly ten bytes, and **validate both the
`0xEF` terminator and the checksum** before acting, rather than trusting the
start byte alone. On a shared or noisy line a dropped byte resynchronises only
by luck.

That matters more here than it looks, because the module sends **unsolicited**
frames (Section 7.2) into the same stream as query replies.

## 6. Commands

### 6.1 Control commands (no reply unless Feedback is set)

| CMD | Function | Parameter |
| --- | --- | --- |
| `0x01` | Next | |
| `0x02` | Previous | |
| `0x03` | **Specify track (NUM)** | 0-2999, **physical index** (Section 8) |
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
by the vendor -- **which is itself a finding**, because `0x12` is the command that
addresses a sound by its filename (Section 8.1).

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
> module play (Open Item 3).
>
> Note also that the datasheet has a **PC** device at `0x04` where the library has
> `AUX=3`. The two device enumerations are not the same list, and nothing in
> either document reconciles them.

### 6.2 Query commands (always reply)

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

## 7. What the module sends back

Three different kinds of frame arrive on the same wire, and a decoder must handle
all of them.

### 7.1 Replies to queries

A query returns the same `CMD` with the value in `para1`/`para2`. Straightforward.

### 7.2 Unsolicited frames -- the ones that break a naive driver

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
> **`0x3D` is genuinely useful.** It is a track-completion event, pushed, at no
> polling cost. The catch is that it is pushed once: a host that is not listening
> on RX when it arrives has missed it, and nothing repeats it.
>
> Note also the datasheet's red annotation under this very table calls `3D` a
> *"U-disk command"* while its own table two lines above assigns `3D` to the TF
> card. Another internal contradiction, in the same document, on the same page.

### 7.3 Error returns

| Frame | Meaning |
| --- | --- |
| `0x40` param `0x00` | Module is busy |
| `0x40` param `0x01` | *"A frame data are not all received"* |
| `0x40` param `0x02` | **Verification (checksum) error** |

*"The module returns busy, basically when module power-on initialization will
return, because the modules need to initialize the file system."*

The `0x02` return is the one that matters: **a module that reports it is a module
that validates checksums**, and therefore one that rejects code derived from the
datasheet's worked examples (Section 5.2).

### 7.4 Power-on behaviour, and a droid that squawks when you touch the card

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

> [!CAUTION]
> **Power-on volume is maximum, and it is a vendor statement.** Flyron's FN-M16P
> section 3.3.2: *"Our system power-on default volume is level 30, if you want to
> set the volume, then directly send the corresponding commands."* Level 30 is the
> top of the scale.
>
> **A droid that powers up and plays a startup sound before setting volume plays it
> at full volume**, through an amplifier, next to whoever is standing at the droid.
> **Send `0x06` before the first `0x12`**, and never rely on the module's own
> default.

**Two audible artefacts worth designing around**, both reported consistently and
neither in the datasheet:

- **A reset thump.** `0x0C` (reset) produces an audible pop. DFRobot's library
  issues one inside `begin()` by default, which is one more reason Section 10.1
  warns about that library. A driver that needs a reset should do it once, before
  unmuting, not on every init retry.
- **Continuous idle hiss.** The on-board amplifier's shutdown pin is hard-grounded
  on the usual boards, so the amp is always on and the speaker hisses whenever
  nothing is playing. The boards carry a **solder bridge** that reroutes that pin
  to follow `BUSY`, which silences the idle at the cost of a click at the start and
  end of every file (the amplifier's wake-up time is about 100 ms). **Which
  trade-off is right is a builder's decision, not a firmware one** -- but a droid
  that sits quietly for hours mostly wants the bridge moved.

## 8. The SD card contract, which is where this module actually bites

### 8.1 Three addressing modes, and only two of them are stable

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
> **`0x12` removes the problem instead of managing it.** It is the one addressing
> mode that makes a sound's identity survive a card being rebuilt. AstroPixelsPlus
> plays with `0x03` while documenting `0x12`'s layout (Section 9.6).

Other modules have the same class of defect -- an index the module derives for
itself drifting away from the number the builder typed -- and not all of them
offer a filename mode to escape to. The DFPlayer does have the escape.

### 8.2 Hidden files on the card

A card prepared on a Mac carries a `._0001.mp3` resource fork beside every track
and a `.Spotlight-V100` directory; under `0x03` each of those is **a file in the
index**, and every sound after it shifts.

### 8.3 The card itself: FAT32, and nothing above 32 GB

Vendor-stated for the original silicon (FN-M16P section 1.2): *"Supports FAT16 and
FAT32 file system"* and *"Supports maximum 32GB micro SD card and 32GB USB flash
drive."*

> [!WARNING]
> **A card bought today will very likely arrive formatted exFAT, which this module
> cannot read.** Anything above 32 GB is exFAT by default under both Windows and
> macOS, and a 64 GB card is now the cheapest thing on the shelf. The failure is
> silent in the worst way: the module simply reports no device online, emits no
> `0x3F`, and a driver correctly concludes there is no card.
>
> Only one clone family -- `TD5580A` -- supports exFAT and 64 GB (Section 2), which
> means **a card that works in one builder's module can be unreadable in another's
> with the same firmware**. Specify FAT32 and 32 GB or under, and have the driver
> say *"no card"* rather than *"module missing"* when `0x3F` never arrives.

### 8.4 Practical rules for a card

1. Use `/mp3/NNNN.mp3`, four digits, zero-padded, and address with `0x12`.
2. Prepare the card on Linux or Windows, or clean it afterwards
   (`dot_clean`, or delete `._*` and `.Spotlight-V100`).
3. Keep numbering contiguous anyway -- it costs nothing and it keeps the card
   portable to a DY-SV5W or an MP3 Trigger droid, which is a real thing builders do
   (Section 9).
4. Read back the total file count at start-up (`0x48` per the library;
   Open Item 2) and compare it to what the host expects. A mismatch is the
   cheapest possible detection of a card that was rebuilt wrong.

## 9. How the hobby actually drives sound (non-normative)

Sound is the one subsystem every astromech project has, which makes this the best
cross-project comparison available. Everything in this section was read as code.

### 9.1 The shape almost everyone has: the host owns numbers, the module owns files

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
| **AstroPixelsPlus** | **DFPlayer Mini** | `(bank-1)*25 + sound` | the `$` command |
| **CHIRP** | itself (RP2350) | bank + page + index, with a catalog | a manifest on the card |

**CHIRP is the only one that treats the vocabulary as data on the card rather
than as code.**

### 9.2 ShadowMD: no sound hardware, and an MP3 Trigger namespace anyway

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
MP3 Trigger plays `NNN-name.mp3` by its numeric prefix -- so a DFPlayer addressed with
`0x03` (physical order) does not provide the namespace this ecosystem assumes.
And **its volume vocabulary is up/down/max/mid**, the same four as MarcDuino's
`$+ $- $f $m`.

### 9.3 Padawan360: the same numbers survive a change of module

The Padawan360 DY-SV5W port is a fork whose whole purpose is swapping the sound
module -- its header says *"This sketch is to beta test the DY-SV5W audio player
instead of sparkfun mp3"*. Every call site is preserved with the old one
commented out directly above:

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
> for filename addressing (Section 8.1).

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

The second is the general lesson: **volume semantics do not survive a module
swap, and a stale comment is how the old scale gets carried forward.** The
DFPlayer's `0x06` takes the same ascending 0-30 as the DY-SV5W; the MP3 Trigger's
register is inverted.

### 9.4 CHIRP is a different tier, and says so

CHIRP's README describes *"an advanced MP3 and WAV file decoder/mixer/player,
heavily inspired by the Sparkfun/Robertsonics MP3 Trigger that has been used
across droid control systems since the original Padawan PS2 days"*, running
*"3+ Independent Audio Streams (WAV, MP3 or AAC)"* on an RP2350 with an I2S DAC
and a `CHIRP.INI` on the card.

**Nothing about the DFPlayer competes with that.** They answer different
questions: CHIRP is what a droid with layered audio needs; a DFPlayer is a
complete one-shot sound system for the price of a coffee, with the amplifier
included.

### 9.5 The convention is MarcDuino's, and three projects implement it differently

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
-- identical to AstroPixelsPlus's arithmetic, which inherited it through Reeltwo.

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
> converted a write-order-proof scheme into a write-order-dependent one**, and
> AstroPixelsPlus inherited that (Section 9.6).
>
> This is independent confirmation of Section 8.1 from the direction that matters
> most: **the ecosystem's reference implementation already uses `0x12`.**

### 9.6 AstroPixelsPlus: a dome controller that commands a DFPlayer

AstroPixelsPlus commands a DFPlayer Mini through `MarcduinoSound.h`, pinned to
`DFRobotDFPlayerMini#V1.0.6` in its `platformio.ini`. Four facts from its source
and its own documentation.

**The bank model.** `MarcduinoSound.h:35-53` implements MarcDuino's convention:

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
`$x`; banks 5-9 always replay the first. The grid is fixed at 25 wide, and the
source's own counts show the sparsity (`MP3_BANK4_SOUNDS 4`, `MP3_BANK5_SOUNDS 3`).

**It calls `play()`, and its own documentation describes `playMp3Folder()`.**

> [!WARNING]
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
> *is* filename-based. The firmware documents one contract and implements another.
>
> It works only because a card written once, in order, into a single folder
> happens to make physical index and filename agree. Re-copy one file, or let the
> operating system write them in a different order, and every sound shifts.

**`begin()` can block for about 2.2 s, and failure is fatal.** `MarcduinoSound.h:419`
is:

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
reschedules on ESP32 rather than hard-blocking (Section 10.2 for what that does to
the idle task), but 2.2 s is still a long time inside a boot path. A host that
wants a bounded start-up wait and a later retry has to bound the wait itself.

**The DFPlayer costs the dome a UART.** `docs/HARDWARE_WIRING.md:1177` records
that on the dome the DFPlayer occupies `Serial1` (AUX4/AUX5) and therefore
**excludes FireStrip and BadMotivator**, with the builder choosing one or the other
in `platformio.ini`.

### 9.7 MarcDuino's checksum drifts, and the stop idiom poisons the next sound

Worth recording because it explains why the whole ecosystem tolerates bad
checksums.

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

Transcribed verbatim and executed, in call order:

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

The lesson for a driver author is not "MarcDuino is broken" -- it is that **a
frame builder must be a pure function with no static state, and its output must
be asserted in a test.**

### 9.8 Negative results across the ecosystem

| Project | DFPlayer? | Note |
| --- | --- | --- |
| `dankraus/padawan360` (upstream) | **none** | MP3 Trigger only, and its numbering is **flat 1-53, not MarcDuino's banks** -- the two card layouts are not interchangeable |
| `reeltwo/Reeltwo` (the library) | **none** | `MarcduinoSound.h` is copy-pasted per sketch; `ReeltwoAudio` is ESP32 I2S -- the R-Series direction is **no external module at all** |
| `joymonkey/dEvolution` (ShadyRC) | **none** | MP3 Trigger, TX-only soft serial, but **uses MarcDuino's global numbering** |
| `PrintedDroid/AstroCan-X-System` | **none** | current generation is **onboard I2S from SD**; no sound module |
| `RealNobser/AstroCommsFirmware` | **none** | MP3 board shares the dome TX line |
| CHIRP | **none** | it *is* a sound module (Section 9.4) |
| `r2d2-astromech-simulator` | **none** | names DY-SV5W and MD-YX5300 |

Two structural observations follow. **Penumbra's runtime default is HCR**, not
DFPlayer and not MP3 Trigger -- so the family AstroPixelsPlus descends from has
already moved toward a speech-synthesis module for new builds. And **the newest
commercial designs have dropped external sound modules entirely** in favour of I2S
from an SD card on the main board. The DFPlayer is the cheap, simple,
universally-available option -- not the direction the hobby's high end is
travelling.

## 10. Libraries

| Library | Last commit | Maintained | Blocking reads | Clone handling |
| --- | --- | --- | --- | --- |
| `DFRobot/DFRobotDFPlayerMini` | 2023-06-26 (V1.0.6) | effectively no -- *"Is this library still maintained?"* open and unanswered since 2024 | yes, 500 ms -- **and one path with no timeout at all** | none |
| `PowerBroker2/DFPlayerMini_Fast` | 2021-08-25 | no | yes, busy-spins with no `yield()` | none |
| **`Makuna/DFMiniMp3`** | 2024-02-06 | yes | 900 ms ack, 3 retries | **yes -- three chip classes** |

`Makuna/DFMiniMp3` is the only library that models the problem in Section 2.1,
via template chip types (`Mp3ChipOriginal`, `Mp3ChipIncongruousNoAck`,
`Mp3ChipMH2024K16SS`).

### 10.1 The official library can hang forever

> [!CAUTION]
> **`DFRobotDFPlayerMini::sendStack()` contains an unbounded loop, and a droid is
> the case that triggers it.** Traced end to end in V1.0.6 source:
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
> AstroPixelsPlus is on this path: `MarcduinoSound.h:419` calls
> `fDFMini.begin(stream)` with library defaults, which means `isACK=true`, and
> nothing in it ever drains the notification queue.
>
> A host that cannot accept an unbounded wait has to own its read loop, with a
> bounded timeout, rather than use this library in ACK mode.

Other defects in the same library, verified in source: `begin()` returns `true`
unconditionally when ACK is disabled (`|| !isACK` at `:118`); the
`DFPlayerCardUSBOnline` branch at `:172` is unreachable, because any value with
bit 0 or bit 1 set is caught earlier; and replies are never correlated to
requests, so a track-finished notification is readily consumed as a query answer.

### 10.2 `delay(0)` is not a yield to the idle task

The library's spin loops are built on `delay(0)` -- at `:36`, `:93` and `:230`. On
an ESP32 that is not the harmless yield it looks like.

Verified from the Arduino-ESP32 and ESP-IDF sources:

```c
// framework-arduinoespressif32/cores/esp32/esp32-hal-misc.c:212
void delay(uint32_t ms) {
  vTaskDelay(ms / portTICK_PERIOD_MS);
}
```

```c
// framework-espidf/components/freertos/FreeRTOS-Kernel/tasks.c:1583
/* A delay time of zero just forces a reschedule. */
if( xTicksToDelay > ( TickType_t ) 0U ) { ... }
```

So `vTaskDelay(0)` takes the else branch: it **reschedules without blocking**. The
calling task is never moved to the blocked list, so the scheduler only ever picks
among tasks of **equal or higher** priority. The **idle task is the lowest
priority on the core** and therefore never runs.

> [!CAUTION]
> **The idle task is what feeds the ESP-IDF Task Watchdog.** A spin that starves
> it does not merely stall audio: the watchdog fires and the controller resets.
>
> Combined with Section 10.1's unbounded `while (_isSending)` loop, the failure
> mode is complete: a track finishes, the notification is never drained, the next
> command spins on `delay(0)` forever, the idle task never runs, the watchdog
> fires, and the controller reboots **because a sound ended**.
>
> A driver that waits on this module must block for at least one tick
> (`vTaskDelay(1)`) inside the wait, never `delay(0)`.

## 11. How it compares with three other serial sound modules

| | DY-SV5W | MP3 Trigger | CHIRP | **DFPlayer Mini** |
| --- | --- | --- | --- | --- |
| Transport | binary, 9600 | binary, 9600 | ASCII, configurable | **binary, 9600** |
| Frame | `0xAA CMD LEN .. SM` (4+ B) | short binary | ASCII lines | **fixed 10 B** |
| Volume native | 0-30 | **0-255, inverted** | -- | **0-30** |
| Simultaneous streams | 1 | 1 | **3+, mixed** | 1 |
| Decoding | hardware | VS1063 | software, RP2350 | hardware |
| Addressing | index, **contiguous required** | file number | catalog + bank/page | **index *or* filename** |
| Play-state query | yes | **no query** -- unsolicited `'X'`/`'x'`/`'E'` bytes | yes | yes (`0x42`) |
| Track-finished event | -- | -- | -- | **pushed (`0x3D`)** |
| Hardware busy pin | -- | -- | -- | **yes (`BUSY`)** |
| On-board amplifier | -- | -- | -- | **yes, <3 W** |
| Catalog on the card | no | no | **yes** | no |
| Board identity | fixed vendor part | fixed vendor part | fixed vendor part | **unreliable (Section 2)** |

**These are peers with genuinely different shapes.**

**CHIRP is a different tier of thing, not a better DFPlayer.** Its own README
describes it as *"an advanced MP3 and WAV file decoder/mixer/player, heavily
inspired by the Sparkfun/Robertsonics MP3 Trigger"*, running three or more
simultaneous streams on an RP2350 with an I2S DAC. Music under speech needs
mixing; a DFPlayer plays one file at a time and always will.

**What the DFPlayer brings that the other three do not** is the bottom of the
price range with an amplifier included, plus two observability features: a
**pushed track-finished event** and a **hardware busy pin**. The MP3 Trigger, at
the other end, has no play-state query -- a host follows the board's unsolicited
`'X'` / `'x'` / `'E'` bytes instead.

**What it costs** is identity. The other three are specific boards from specific
vendors. "A DFPlayer Mini" is a form factor that several unrelated silicon
vendors fill, and Section 2 is the consequence.

## 12. Quick Reference

- Field: Baud. Value: **9600**, 8-N-1, effectively fixed (no serial command changes it).
- Field: Frame length. Value: **exactly 10 bytes**, `0x7E` ... `0xEF` (8 bytes with the checksum omitted, Section 2.1).
- Field: Frame layout. Value: `7E VER LEN CMD FB par1 par2 cksHi cksLo EF`, VER=`0xFF`, LEN=`0x06`.
- Field: Checksum. Value: **`-(sum of bytes 1..6)`**, 16-bit, high byte first. **Never copy the datasheet's examples.**
- Field: Play by filename. Value: **`0x12`** with `/mp3/NNNN.mp3`. *Not* `0x03`.
- Field: Play by physical index. Value: `0x03` -- addressed by FAT write order. Avoid.
- Field: Play from folder. Value: `0x0F` (folder 1-10, file), `/NN/NNN.mp3`.
- Field: Volume. Value: **`0x06`, range 0-30**, ascending.
- Field: Stop. Value: `0x16`.
- Field: Pause / resume. Value: `0x0E` / `0x0D`.
- Field: Reset. Value: `0x0C`.
- Field: Query play state. Value: `0x42`.
- Field: Query software version. Value: `0x46`.
- Field: Query total SD files. Value: `0x48` **per the library**; the datasheet says `0x47`. Unresolved -- Open Item 2.
- Field: Query current SD track. Value: `0x4C` per the library; datasheet says `0x4B`. Same conflict.
- Field: Power-on device report. Value: `0x3F`, **bitmask** -- U-disk `0x01`, TF `0x02`, PC `0x04`, FLASH `0x08`.
- Field: Device code for TF card in `0x09`. Value: **`2`**, per the bitmask and the library. The datasheet's `(0/1/2/3/4)` list is wrong.
- Field: Unsolicited frames. Value: `0x3A` insert, `0x3B` remove, `0x3C`/`0x3D`/`0x3E` track finished, `0x3F` power-on.
- Field: Error returns. Value: `0x40` with `0x00` busy, `0x01` incomplete frame, `0x02` checksum failure.
- Field: Power-on init time. Value: **1.5-3 s**; wait for `0x3F` before sending anything.
- Field: Card-insert behaviour. Value: **auto-plays root track 1**; suppress with a pause ~100 ms after `0x3A`.
- Field: Track range. Value: 0-2999 for `0x03`; 1-9999 for `0x12`.
- Field: VCC. Value: **3.2-5.0 V, typical 4.2 V** -- 3.3 V is in specification.
- Field: BUSY polarity. Value: **LOW while playing** (DFRobot's pin table and Flyron agree; DFRobot section 3.3.2 is wrong). Latency to assert is chip-dependent -- do not use it as a play-started edge.
- Field: Power-on volume. Value: **30, maximum.** Send `0x06` before the first play, always.
- Field: Filesystem. Value: **FAT16 or FAT32 only, 32 GB maximum.** exFAT is unreadable on the original silicon.
- Field: Speaker output. Value: SPK1/SPK2, **bridge-tied, under 3 W**, never into an amplifier input. Use DAC_L/DAC_R for line level.

## 13. Open Items

| # | Item | How to settle it |
| --- | --- | --- |
| 1 | **`BUSY` assertion latency** (polarity itself is **resolved** -- Section 4.3) | Scope or log the pin against a known track length; some clones need ~350 ms to settle and one is reported not to track state at all |
| 2 | **`0x47` vs `0x48`** for the SD file count, and `0x4B` vs `0x4C` for current track | With only an SD card present, send both and see which returns a plausible value (Section 6.2) |
| 3 | **`0x09` device code for TF** | Send `0x09` with `1` and with `2`; see which makes an SD-only module play (Section 6.1) |
| 4 | **Which chipset a given module carries** | Read the chip marking; read `0x46` (software version) at start-up and record it with the marking (Section 2) |
| 5 | **Does the module answer queries correctly during playback** | Query `0x42` and `0x4C` while a track plays and check the replies and the playback for disturbance; repeat per chipset |
| 6 | **The 1 kohm series resistor on RX** | Community practice, not in the datasheet. Determine whether it is needed at 3.3 V |
| 7 | **Does a given module validate checksums** | Send a deliberately wrong checksum and watch for `0x40 / 0x02` (Section 7.3) |
| 8 | **Does the idle-hiss solder bridge exist on a given board** | Inspect; choose hiss or click (Section 7.4) |

## 14. Sources

**Primary -- vendor**

- **DFR0299 datasheet V1.0**, DFRobot -- https://dfimg.dfrobot.com/wiki/20532/DFR0299_mp3-player-module_datasheet_V1.0.pdf. A **scanned PDF with no text layer**; rendered with `pdftoppm` and read as images.
- **FN-M16P Embedded MP3 Audio Module Datasheet**, Flyron -- same silicon family, **correct checksums in its command tables** (its prose examples are wrong, like DFRobot's -- Section 5.2), the two-column with/without-checksum table, the power-on volume statement, and the filesystem limits.
- DFRobot product page DFR0299 -- https://www.dfrobot.com/product-1121.html (price, stock, and the `stockText` / `stockTextSeo` discrepancy).
- DFRobot wiki -- https://wiki.dfrobot.com/DFPlayer_Mini_SKU_DFR0299.
- GD3200A/B + MH2024K datasheet (GuoDian); TD5580A User Manual V1.3; YX5200-24SS Chip Manual V1.6 -- the clone families in Section 2.

**Primary -- library source code**

- `DFRobot/DFRobotDFPlayerMini` V1.0.6 -- read in full: the checksum algorithm, the command map, the device constants, and the unbounded `sendStack()` loop.
- `nhutchison/MarcDuinoMain` `MP3sound.c` / `MP3sound.h` -- the bank convention's origin, its `0x12` play command, and the checksum drift reproduced in Section 9.7.
- `Makuna/DFMiniMp3` -- the only library that models clone chipsets as types.
- `SnijderC/dyplayer` -- DY-SV5W volume range, for the comparison in Section 9.3.
- Arduino-ESP32 `esp32-hal-misc.c` and ESP-IDF FreeRTOS `tasks.c` -- the `delay(0)` behaviour in Section 10.2.

**Primary -- community source code**

- AstroPixelsPlus -- `MarcduinoSound.h`, `docs/SETUP.md`, `docs/HARDWARE_WIRING.md`, `platformio.ini`.
- Padawan360 DY-SV5W port -- the MP3 Trigger to DY-SV5W port.
- ShadowMD -- MarcDuino delegation, grep validated.
- CHIRP -- the multi-stream tier.
- `r2d2-astromech-simulator` -- negative result.

**Third party, ecosystem**

- `reeltwo/PenumbraShadowMD`, `reeltwo/Reeltwo`, `reeltwo/ReeltwoAudio`
- `dankraus/padawan360`, `joymonkey/dEvolution`
- `PrintedDroid/ShadowMD-AstroComms`, `PrintedDroid/AstroCan-X-System`, `RealNobser/AstroCommsFirmware`
- `dpoulson/r2_control` -- independent corroboration of the HCR vocabulary
- `ghmartin77/DFPlayerAnalyzer` issues #1-#25 -- a crowdsourced database of clone behaviour, and the best compatibility matrix that exists
- DFRobot library issues #51, #54, #63, #67, #69, #71; `Makuna/DFMiniMp3` issues #131, #139, #146, #148
- Printed Droid knowledge base -- the DFPlayer guide and the R2D2 sound packs

> [!NOTE]
> **Negative results, recorded so nobody repeats them.** DFRobot has published
> nothing about the clone problem on the product page, either wiki page, or its
> 2025 module selection guide. `astromech.net` is login-walled, so every claim
> sourced to the forums here is second-hand through project repositories and
> vendor knowledge bases. There is no repository named `ShadowRC`; the code that
> exists is `joymonkey/dEvolution` (ShadyRC). `ghmartin77/DFPlayerMini` does not
> exist -- the useful repository of that author is `DFPlayerAnalyzer`. Kyber and
> Stealth are closed-source and their sound modules are `UNKNOWN`.
