# CHIRP Audio Trigger Spec Sheet (registry token `chirp_ascii_uart`)

Working spec for the **CHIRP Audio Trigger**, the **Sound** lineup member that
does what none of the others can ([#387](https://github.com/mattiasbrandt/protoArtoo/issues/387),
minted from [#303](https://github.com/mattiasbrandt/protoArtoo/issues/303) and
[#316](https://github.com/mattiasbrandt/protoArtoo/issues/316)), reached over the
Component Protocol the registry calls `chirp_ascii_uart`.

> [!NOTE]
> **Line references to protoArtoo source are against `epic/operator-experience`
> at `40e37ace`**, the branch this product's work lives on -- that is where they
> were read. Two cited files are not on `main` at all:
> `include/component_registry.inc` (the Component Registry, including this
> product's row) and `src/tasks/audio_sound_member.cpp` (the driver binding).
> Line numbers in files that *are* on `main` -- `data/sound.js`,
> `include/audio_driver.h`, `include/audio_chirp.h` -- have drifted between the
> two branches; the quoted text is what to search for. References to the CHIRP
> Audio Trigger's own firmware are against the local clone described below.

Research date 2026-09-12. Unusually for this family, **the vendor document is
the source code**: there is no datasheet, no user guide and no product page.
Every command, response, default, pin and limit below was read this session from
joymonkey's own firmware in the local clone of
[`github.com/joymonkey/CHIRP`](https://github.com/joymonkey/CHIRP)
(`~/Documents/GitHub/CHIRP`, upstream `3adcc0d`, 2026-03-08), from the RevB board
render and custom board variant in that tree, from this repository's driver,
registry and web surface, from the deployed SD card image in
`~/Dropbox/R2-CHIRP/CHIRP-SD.zip`, or from the astromech projects on this disk.
Claims that could not be sourced are marked `UNKNOWN` with the artefact or bench
test that would settle them.

> [!IMPORTANT]
> **"CHIRP" names two different products by the same author, and this sheet is
> about only one of them.** Write **CHIRP Audio Trigger** in full, every time.
>
> | | What it is | Relationship to protoArtoo |
> | --- | --- | --- |
> | **CHIRP Audio Trigger** | an RP2350 sound player -- **this sheet** | a **Sound** member protoArtoo drives |
> | **CHIRP Droid Control** | an RP2350 + ExpressLRS **body controller**, evolved from ShadyRC dEvolution | a **peer**; a builder picks it *instead of* protoArtoo |
>
> This is not a hypothetical collision. Both live in the same repository:
> upstream `origin/main` carries `CHIRP_Audio_Trigger/` and
> `CHIRP_Droid_Control/` side by side, and the repository README gives each its
> own heading. CONTEXT.md's Flagged Ambiguities records the resolution
> (operator, 2026-09-08): **always qualify the audio module in operator copy and
> in any lineup, never bare.** Internal identifiers (`chirpVol`, `audio_chirp`,
> `make ota-chirp`, the `chr_*` NVS keys) keep the short form -- they are
> unambiguous inside their own subject.

> [!CAUTION]
> **Send ASCII commands in UPPER CASE only. Lower-case `p`, `t` and `v` are
> eaten by the MP3 Trigger compatibility layer before the ASCII parser ever
> sees them.**
>
> `processSerialCommands()` offers every byte to `checkAndHandleMp3Command()`
> first, whenever a command is not already part-built. That shim claims `'O'`,
> `'F'`, `'R'`, `'T'`, `'t'`, `'v'` and `'p'`. So `play:3` does not return
> `ERR:UNKNOWN` -- its leading `p` is consumed as *"play the track at directory
> index 3"* and a **different sound plays**. Section 7.5.
>
> The same fact closes the grammar: no future CHIRP Audio Trigger command may
> begin with any of those seven letters. `PLAY`, `STOP`, `VOL`, `CHRP`, `CCRC`,
> `GMAN`, `GNME`, `LIST`, `STAT`, `BAUD`, `BPAGE`, `MUSB` and `PVOICE` are all
> safe because none of them starts with one.

> [!WARNING]
> **Two of the six capability bits this product declares are not answered by the
> module. The driver makes them up, and the Sound page shows one of them as a
> number the API's own enumeration does not define.**
>
> `include/component_registry.inc:172-176` declares `AUDIO_CAP_DEVICE_TYPE` and
> `AUDIO_CAP_CURRENT_TRACK`. `data/sound.js:343-345` therefore **shows** the
> Device and Current-track rows. But `queryModuleState()` hardcodes
> `out.device = 0x03` (`src/drivers/audio_chirp.cpp:628`, and again at `:702`
> for the cached path) -- and `include/audio_driver.h:81` documents that field as
> `0=USB 1=SD/TF 2=FLASH 0xFF=unknown`, in which **`3` means nothing**.
> `data/sound.js:384` prints it raw, so the operator reads a bare **"3"**.
> Current track is `m_lastTrack`, an echo of the last index *we sent*, which
> `stop()` never clears and which cannot know which of three streams is live or
> which random variant played. Section 10.2 and Finding 13.1.

> [!NOTE]
> **You cannot buy one.** This is a one-person open-hardware project with no
> retail channel found (Section 2.4). Every other Sound member is a purchasable
> part; this one is a PCB you have made. That is not a reason against it -- it
> is the reason this sheet leans on firmware source rather than a datasheet, and
> the reason Open Item 1 asks what the Component Picker card should say.

## Where this sits in the lineup

The **Sound** category holds four products, and a builder picks one:

| Product | What it is | Status | Registry value |
| --- | --- | --- | --- |
| DY-SV5W | binary-frame voice module with an amplifier | `supported`, **default** | 18 |
| MP3 Trigger | SparkFun/Robertsonics VS1063 board, 18 trigger pins | `supported` | 19 |
| **CHIRP Audio Trigger** | **RP2350 multi-stream mixer, astromech-specific** | **`supported`** | **20** |
| DFPlayer Mini | hardware-decoded single-stream player with an amplifier | `roadmap` | 21 |

[`dy-sv5w-sound.md`](dy-sv5w-sound.md) Section 16 carries the full comparison
across all four and this sheet does not repeat it; Section 11 below lists only
the rows where the CHIRP Audio Trigger is the outlier.

**It is the only member that was designed for this hobby, and the only one that
can answer "what sounds do you have?"** The DY-SV5W, the MP3 Trigger and the
DFPlayer Mini are general-purpose parts a droid happens to use; a host addresses
them by a number and learns nothing back. The CHIRP Audio Trigger was designed
by an astromech builder to fix the four things the hobby had complained about
for a decade -- one sound at a time, no idea what is playing, no file list, and
a price that kept climbing.

That is why it holds `CATALOG`, why it holds `QUERY_SAFE_PLAYING`, and why it is
the widest capability word in the family (`0x3F`). It is also why it is the only
member whose *absence* would cost protoArtoo a feature: the Sound page's catalog
workspace, the `chr_*` slot bindings and the category-range mapping all exist
because this module can be asked.

**What it costs** is a board you have to make yourself, a protocol with no
specification outside its own source, a firmware that is still moving, and a
response path that this sheet shows can silently drop the answer to the one
query protoArtoo most depends on (Finding 13.2).

## 0. Authority Contract

This document is an implementation authority for the CHIRP Audio Trigger serial
protocol and for the board's behaviour as protoArtoo uses it.

Authority order for agent decisions:

1. **The CHIRP Audio Trigger firmware source** for everything the module does.
   There is no datasheet and no user guide; `serial_commands.cpp`,
   `mp3_compat.cpp`, `file_management.cpp`, `serial_queue.cpp` and `config.h`
   **are** the specification. Every command in Section 7 was read from them this
   session.
2. **The board variant and `platformio.ini`** in that tree for what is on the
   PCB and how it is built -- they carry the RevB pin map and the PSRAM
   arrangement that no prose states.
3. This document.
4. **`docs/sound_playback.md`** for operator procedure. Where it and this sheet
   disagree about behaviour, this sheet wins and the drift is a Finding
   (Section 13 names two).
5. **protoArtoo's own driver** as evidence of practice, not of the module's
   contract. The driver is a client; several of its assumptions are recorded
   here as findings rather than facts.

If references conflict:

- Prefer the **firmware source** over any README for what the board does. The
  upstream README is a feature summary written ahead of the code in places --
  it advertises `LIST` as *"List sounds stored in Sound Bank 1"* when `LIST`
  prints only the **first ten** (Section 7.3).
- Prefer the **local clone's source** over the upstream README for what the
  board in this droid does, and say which. The local tree is **modified and
  ahead of upstream in places and behind it in others** (Section 2.5).
- Prefer a **measurement** over source reading where one exists. Nothing in
  Sections 7 to 9 has been captured off the wire this session; it is all read
  from both ends of the protocol.
- If still unresolved, mark `UNKNOWN` and stop dependent work.

Agent requirements when using this document:

- MUST write **CHIRP Audio Trigger** in full in operator copy, docs and any
  lineup. Never bare "CHIRP" outside a code identifier.
- MUST NOT send a lower-case ASCII command. `p`, `t` and `v` are intercepted
  (Section 7.5).
- MUST NOT assume a response arrives in the order the commands were sent.
  `STAT`, `LIST` and every `ERR:` line are written **straight to the UART**
  while `PACK:`, `BANK:`, `NAME:`, `MSUM:` and `MEND` go through a queue
  (Section 7.4).
- MUST NOT assume every `GMAN` line arrives. The outgoing queue holds **15**
  messages and `GMAN` on this droid's own card emits **17**, oldest dropped
  first (Finding 13.2).
- MUST NOT assume `GNME` honours the page argument for Bank 1. It does not, and
  on any Bank 1 page other than `A` every name degrades to `index_N`
  (Finding 13.3).
- MUST NOT trust a Bank 1 file extension from `GNME`. It is **always** reported
  as `.wav`, whatever the file is (Finding 13.4).
- MUST NOT treat `device = 3` as a device type. It is not in the enumeration
  (Finding 13.1).
- MUST NOT assume `STOP` stops one sound. It stops **every stream**, including a
  music bed (Section 9.3).
- MUST state that the module is proven on this droid but that **no line of the
  protocol in Section 7 has been captured off the wire**.

## 1. Scope

Covers the board and what is on it, the two-product name collision and its
evidence, availability, the electrical and resource contract, the SD-card bank
and page model, the serial protocol in full including the MP3 Trigger
compatibility shim, the response queue and its ordering and loss behaviour, what
protoArtoo's driver actually sends, the transport and its UART arbitration, the
capability word and what the operator sees, how this product relates to the rest
of the hobby, and the defects this research found on both sides of the wire.

Does not cover: the RP2350 itself beyond what the board uses, the Helix decoders'
internals, the MP4/M4A parser, CHIRP Droid Control (a peer control system, out of
scope by definition -- Section 2.6), or protoArtoo's Sound page UI behaviour
(that is `docs/sound_playback.md`).

## 2. What you are actually buying

### 2.1 The board

A 2-layer PCB carrying a **Raspberry Pi RP2350A**, 16 MB QSPI flash, 8 MB PSRAM,
a microSD socket and an I2S DAC. There is no audio decoder chip: the MCU decodes,
mixes and outputs everything in software. The author's stated design goals were a
drop-in MP3 Trigger replacement that adds simultaneous streams, a queryable file
manifest, and a bill of materials under USD 20.

From the RevB board render (`CHIRP_Audio_Trigger/docs/chirp-board-revb.png`,
silkscreen `REV.B 20260101`) and the firmware's pin definitions:

| Feature | Detail | Source |
| --- | --- | --- |
| MCU | RP2350A, 30 GPIO, QFN-60 | `variants/chirp_revb/pins_arduino.h` |
| Clock | **250 MHz** (overclocked from 150 MHz stock) | `platformio.ini` `board_build.f_cpu` |
| Flash | 16 MB; **2 MB sketch + 14 MB LittleFS** | `board_build.filesystem_size = 14m` |
| PSRAM | **8 MB** on QMI CS1 = **GPIO0**, max 109 MHz | `pins_arduino.h`, `RP2350_PSRAM_CS` |
| Storage | microSD on SPI1 -- CS 13, MISO 12, MOSI 15, SCK 14 | `config.h:21-24` |
| DAC | **PCM5102A** I2S, BCLK 9, LRCK 10, DATA 11, mute 8 | `config.h:34-36`, `CHIRP_Audio.ino:138` |
| Audio out | 3.5 mm stereo jack **plus** LEFT/GND/RIGHT pads | RevB render |
| Host UART | `Serial2`, **TX GPIO4 / RX GPIO5**, on the `TX RX +5V G` header | `config.h:41-42` |
| Buttons | Prev 18, Play/Stop 17, Next 16, plus RESET and BOOT | `config.h:45-47` |
| LEDs | **3 NeoPixels on GPIO19**, plus a power LED | `config.h:37`, `blinkies.cpp` |
| MSC trigger | **GPIO7 pulled low** enables USB mass storage | `config.h:38` |
| Expansion | QWIIC/I2C connector; headers breaking out GPIO 20-29 | RevB render |
| Power | USB-C **or** +5 V screw pads, selected by a `POWER SELECT` jumper | RevB render |

### 2.2 There is no amplifier, and that is a wiring decision

The output stage is a **PCM5102A**, a line-level stereo I2S DAC. Nothing on the
board amplifies it. A droid therefore needs an external amplifier between this
board and the speakers -- the same arrangement the MP3 Trigger needs, and the
opposite of the DY-SV5W and the DFPlayer Mini, which carry their own.

The firmware drives the DAC's `XSMT` soft-mute line on **GPIO8** and treats HIGH
as unmuted (`CHIRP_Audio.ino:138`). It mutes deliberately during flash writes so
the SD-to-flash sync does not click through the speakers, and applies a **50 ms
fade-in** on every stream start to prevent pops (`audio_playback.cpp:567-572`).
This is a board that has had someone listen to it.

> [!NOTE]
> **Rev A and Rev B differ in a way that changes the firmware.** On Rev A,
> **GPIO8 is the PSRAM chip select**; on Rev B it is the DAC mute and **GPIO0**
> is the PSRAM CS. Rev A also carries only **2 MB** of PSRAM against Rev B's
> 8 MB, which is the difference between 3 streams and 13. `config.h:26-29`
> selects between them with `#define BOARD_REV_B`, and on Rev A the mute is done
> in software by stopping the I2S clocks instead. **Compiling a Rev B image for
> a Rev A board drives the PSRAM chip select as an output pin.**

### 2.3 PSRAM is the resource that sets the stream count

Every stream needs a ring buffer, and each decoder needs a slab:

| Consumer | Cost | Where |
| --- | --- | --- |
| Stream 0 ring buffer | **64 KB, internal SRAM** (not PSRAM) | `audio_playback.cpp:126-131` |
| Streams 1..n ring buffers | `#STREAM_BUFFER_SIZE` each, default **512 KB** | `audio_playback.cpp:142` |
| MP3 decoder (Helix) | ~25 KB each, one per stream | `CHIRP_Audio.ino` header |
| AAC decoder (Helix) | ~70 KB each, one per stream | `CHIRP_Audio.ino` header |

The author's own arithmetic: Rev A's 2 MB allows `3 * (512 + 25 + 70)` KB;
Rev B's 8 MB would allow about 13 streams, *"more than the CPU can handle"*.
The shipped default is **3 streams** (`DEFAULT_MAX_STREAMS`), and `#MAX_STREAMS`
accepts 1-10.

Stream 0 is special twice over: it is the only buffer in internal RAM, and it is
the stream the embedded system voice plays on. `playVoiceFeedback()` **blocks
Core 0** and pumps the audio itself until the clip finishes
(`file_management.cpp:445-473`) -- so while the board is speaking, it is not
reading serial commands.

### 2.4 Availability: there is no product to buy

No retail channel, kit, group buy or shared PCBWay project was found this
session. What exists is:

- the **GPL-3.0** source and hardware repository at `github.com/joymonkey/CHIRP`;
- five prebuilt UF2 images in `CHIRP_Audio_Trigger/Firmware/` (RevA `260117`,
  RevA `260117b`, RevB `260120`, RevB `260308`, plus a breadboard `proto`);
- Rev A prototypes the author had assembled by PCBWay, with acknowledged
  component-selection errors worked around in firmware and fixed in Rev B.

The author's stated BOM cost is **under USD 20**, and the RP2350 is documented by
Raspberry Pi as in production until **January 2045** -- both relevant to a
lineup decision, and neither independently verified here.

**Status: `UNVERIFIED`.** Settled by asking the author directly, or by a public
release. Open Item 1 is what the Component Picker card should say in the
meantime, because "buy one" is not available advice.

### 2.5 The tree in this droid is a fork, in both directions

`~/Documents/GitHub/CHIRP` is **not** a clean checkout of upstream, and any agent
reading it must know which way each difference runs.

**Local, not upstream** -- uncommitted work in the clone:

- `platformio.ini`, `variants/chirp_revb/pins_arduino.h`,
  `protoArtoo-config.md`, `platformio-psram-issue.md` and
  `sound-bank-page-detection.md` are untracked additions;
- `CHIRP_Audio.ino` and `config.h` are modified.

The substance of those changes -- boot hardening, the PSRAM linker fix, the
9600-baud default, `pmalloc()` null guards -- is recorded in
`protoArtoo-config.md` and summarised in Section 6.3.

**Upstream, not local** -- `origin/main` has moved on and now carries
`CHIRP_Droid_Control/` (firmware plus an EdgeTX Lua script) and its own
`platformio.ini` and `main.cpp`, which the local `HEAD` predates.

> [!WARNING]
> `~/Dropbox/R2-CHIRP/protoArtoo-chirp-changes.md` is an **older copy** of
> `protoArtoo-config.md` and is stale where they disagree. It states
> `board = pimoroni_pico_plus_2`; the in-tree `platformio.ini` uses
> `board = rpipico2`, and Section 6.3 explains why that change was the
> cold-boot fix. **Read the in-tree copy.**

### 2.6 The other product sharing the name, stated once

**CHIRP Droid Control** is a body controller: an RP2350 taking ExpressLRS/CRSF
input and driving a droid's motion, sound and lighting, with an EdgeTX Lua
script on the transmitter. Upstream's README states its goals in its own words,
including *"Send system status and audio file details to the operators radio
transmitter via ExpressLRS telemetry packets"* and *"Shouldn't require the end
user (droid wrangler) to know how to code or compile"*.

Those are protoArtoo's goals. **CHIRP Droid Control is a peer product in the
same category as protoArtoo, not a component of it**, and the two would never be
fitted together. It appears in this sheet only so that a reader who meets the
word "CHIRP" in a forum thread can tell which product is being discussed.

The relationship is also why the Audio Trigger's manifest protocol looks the way
it does: `GMAN`, `GNME` and `MSUM` exist so that *a* droid controller can mirror
a sound list onto a transmitter screen. protoArtoo is a second consumer of an
interface designed for its competitor -- which is a compliment to the interface.

## 3. Project Integration

| Field | Value | Source |
| --- | --- | --- |
| Registry value | **20** | `include/component_registry.inc:172` |
| Registry id | `chirp` | same |
| Operator-visible name | **CHIRP Audio Trigger** | same |
| Category | `COMPONENT_CATEGORY_SOUND` | same |
| Component Protocol | **`chirp_ascii_uart`** | same |
| Lineup status | `COMPONENT_STATUS_SUPPORTED` | same |
| Capability word | **`0x3F`** -- all six bits | same, Section 10 |
| Board capability gate | `nullptr` -- **no board narrows it** | same |
| Carried in every image | yes (`included = 1`) | same |
| Driver | `src/drivers/audio_chirp.cpp`, `include/audio_chirp.h` | -- |
| Build default token | `PA_AUDIO_DRIVER = AUDIO_CHIRP` (value 4) | `include/audio_driver.h:50` |
| NVS member value | `chirp` under key `snd_member` | ADR 0042 |
| Driver binding | `src/tasks/audio_sound_member.cpp:38-52` | -- |
| PlatformIO envs | `artoo_esp32_chirp`, `..._chirp_ota`, `..._chirp_check` | `platformio.ini` |
| Make targets | `flash-chirp`, `flash-chirp-monitor`, `ota-chirp`, `check-chirp` | `Makefile:213-256` |
| Operator doc | `docs/sound_playback.md` Section 2.2 | -- |

The sound member is chosen **at runtime and staged at reboot** (ADR 0042). Every
image carries all three built Sound drivers, so `PA_AUDIO_DRIVER` now only names
which module a controller that has never been told starts with. The driver reads
its own capability word back out of the registry row rather than restating it:

```cpp
uint8_t capabilities() const override {
    return componentPartCapabilities("chirp");
}
static_assert(componentPartExists("chirp"), ...);
```

That `static_assert` is the reason a typo in the id cannot ship as "a module
that can be asked nothing" (`include/audio_chirp.h:62-68`).

## 4. Sources Checked

| Source | What it settled |
| --- | --- |
| `serial_commands.cpp` (548 lines, read in full) | every ASCII command, argument default and response |
| `mp3_compat.cpp` (205 lines, read in full) | the legacy shim and its interception order |
| `file_management.cpp` (1124 lines) | `CHIRP.INI` keys, bank scanning, variant grouping, flash sync, voice feedback |
| `serial_queue.cpp` (96 lines, read in full) | the outgoing queue, its depth and its drop rule |
| `config.h` (430 lines) | pins, limits, struct sizes, stream types |
| `CHIRP_Audio.ino` (809 lines) | boot order, button combos, the documented `CHIRP.INI` reference |
| `audio_playback.cpp` (selected) | buffer allocation, mixing, `playChirp()`, format detection |
| `blinkies.cpp` (289 lines, read in full) | the LED vocabulary |
| `variants/chirp_revb/pins_arduino.h` | the RevB pin map and PSRAM arrangement |
| `platformio.ini` + `protoArtoo-config.md` | the build contract and the cold-boot investigation |
| `system_audio_data.cpp` (table only) | the 246-clip embedded voice vocabulary |
| `docs/chirp-board-revb.png` | connectors, headers and silkscreen |
| `~/Dropbox/R2-CHIRP/CHIRP-SD.zip` + its `CHIRP.INI` | the card layout actually deployed in this droid |
| `src/drivers/audio_chirp.cpp` (720 lines, read in full) | what protoArtoo sends and parses |
| `include/component_registry.inc`, `include/chirp_binding_keys.h`, `data/sound.js` | the capability word, the NVS keys, the operator surface |
| AstroPixelsPlus `MarcduinoSound.h`, protoArtoo's MP3 Trigger driver | the convention the shim emulates |

**Not available:** any vendor datasheet, user guide, schematic or product page --
none exists. Any astromech.net forum thread; searched and not found this
session, so community reception is `UNKNOWN`.

## 5. The SD card contract

This is where the product differs most from the rest of the family. The other
three address a flat number; this one addresses a **bank, a page and an index**,
and the directory names on the card are part of the protocol.

### 5.1 Banks and pages

| Level | Rule | Limit |
| --- | --- | --- |
| Bank | first character of the directory name, `1`-`6` | 6 banks |
| Page | second character, `A`-`Z`, then `_` | 26 pages per bank |
| Label | everything after the `_`, free text | dir name <= 63 chars |

So `2B_StarWarsClips/` is **Bank 2, Page B**. Bank 1 is reserved for the droid's
primary vocals; Banks 2-6 are whatever the builder wants. `scanSDBanks()` accepts
`[2-6][A-Z]_Label` and also bare `[2-6]_Label` (no page); `scanValidBank1Pages()`
requires the `1[A-Z]_` form exactly.

Firmware ceilings (`config.h:60-62`):

| Constant | Value | What it bounds |
| --- | --- | --- |
| `MAX_SOUNDS` | **100** | grouped sounds in the active Bank 1 page |
| `MAX_SD_BANKS` | **20** | bank/page directories for Banks 2-6 total |
| `MAX_FILES_PER_BANK` | **100** | files in any one Bank 2-6 directory |
| `MAX_ROOT_TRACKS` | **255** | legacy root-directory tracks |

`MAX_SOUNDS` is not free: each `SoundFile` carries a 16-byte basename and **25
variant slots of 32 bytes**, so the array is roughly 82 KB of **internal SRAM**,
not PSRAM. `protoArtoo-config.md` records the reasoning: raising it beyond ~300
risks SRAM exhaustion on the RP2350A.

### 5.2 Variant groups, which is the feature the hobby has wanted

In **Bank 1 only**, files whose name is `basename` + `_` + a digit are collapsed
into one addressable sound, and playing it picks a variant at random:

```
1A_R2D2/happy_01.wav  |
1A_R2D2/happy_02.wav  +--> one sound, "happy", index n
1A_R2D2/happy_03.wav  |
1A_R2D2/disagree.wav  ---> one sound, "disagree", index n+1
```

The rule is exactly `strchr(filename, '_')` followed by `isdigit()`
(`file_management.cpp:341-367`): the basename is everything before the **first**
underscore. `happy_long_01.wav` therefore groups under `happy`, not
`happy_long`. Selection avoids an immediate repeat -- if the random pick equals
`lastVariantPlayed` it advances by one (`serial_commands.cpp:130-138`). Up to 25
variants per group; extras beyond that are silently dropped.

**Banks 2-6 do not group.** Every file is its own index, in directory
enumeration order, filtered to known audio extensions.

> [!IMPORTANT]
> **An index is a position, not a name -- and in Banks 2-6 not even a sorted
> one.** `scanSDBanks()` appends files in `openNext()` order with no sort, so a
> Bank 2 index is the filesystem's enumeration order. Adding, deleting or
> rewriting a file can renumber every entry after it, and every `chr_*` slot
> binding pointing past that file then addresses the wrong sound. Bank 1 is
> sorted only in the weak sense that grouping happens in enumeration order too.
> The **only** defence is `MSUM` (Section 7.3) -- and protoArtoo ignores it
> (Finding 13.5).
>
> The legacy root-track list **is** sorted, alphabetically and case-insensitively
> (`file_management.cpp:1111-1125`). The bank lists are not.

### 5.3 Flash sync, and the 14 MB ceiling

With `#USE_FLASH_BANK1 1`, the active Bank 1 page is copied to the 14 MB LittleFS
partition at boot and played from there afterwards, so the droid's primary vocals
never wait on an SD seek. The sync:

- creates `/flash`, then **prunes** any file no longer in the card's Bank 1;
- copies in 512-byte chunks, muting the DAC for each write;
- **skips a file whose flash copy is already the same size** -- so the second
  boot is fast, and *size* is the only equality test (a same-size replacement is
  never re-copied);
- announces progress by voice, or by two chirps per file when the voice set is
  absent;
- is capped by `DEV_MODE`/`DEV_SYNC_LIMIT` at **100 files** in the shipped
  configuration (`config.h:50-51`).

`CCRC` empties `/flash` and forces a re-sync at the next boot; the firmware says
so and requires the reboot.

This droid's card **disables** the sync in the general case for size reasons and
enables it for the small page it actually uses -- see Section 5.5.

### 5.4 Formats and sample rates

`getAudioFormat()` recognises `.wav`, `.mp3`, `.aac`, `.m4a` and `.ogg` by
extension. `.ogg` is **recognised but not decodable** -- there is no Vorbis
decoder, so it is enumerated into a bank and then fails at `startStream()`.

The engine runs a fixed **44.1 kHz** I2S output. Other rates are handled by
nearest-neighbour upsampling on the decode path, and the author's own warning is
in the sketch header: a 48 kHz file *"will be slowed ~92% and not sound good"*.
Mono is upconverted to stereo. The practical rule is **resample everything to
44.1 kHz before it goes on the card**; mono WAV for Bank 1, stereo MP3 for music.

### 5.5 The card in this droid

From `CHIRP-SD.zip` and its `CHIRP.INI`:

| Directory | Bank/Page | Contents |
| --- | --- | --- |
| `1A_general/` | 1A | 24 general beeps, **flash-synced** |
| `2A_music/` .. `2L_whistle/` | 2A-2L | 12 pages: music, alert, chatty, happy, processing, sad, sentimental, humming, scream, surprised, snarky, whistle |
| `3A_system/` | 3A | 7 system voice announcements |

`#BAUD_RATE 9600`, `#BANK1_PAGE A`, `#USE_FLASH_BANK1 1`. That is **1 Bank 1
directory + 13 Bank 2-6 directories = 14 banks**, and that number is what makes
Finding 13.2 bite.

Note the emergent convention: this droid uses **Bank 2's pages as emotional
categories**, which maps cleanly onto protoArtoo's twelve `chr_cat_*` category
bindings. The firmware does not know that -- it is a card layout, not a protocol
feature -- but the Sound page's *"Apply suggestions"* action infers categories
from exactly these directory names.

## 6. Getting the wire to work

### 6.1 The link

| Property | Value |
| --- | --- |
| Signal | UART, 8N1, 3.3 V logic |
| Module pins | `TX RX +5V G` header -- **TX GPIO4, RX GPIO5** |
| Module default baud | **115200** upstream; **9600** in this droid's build |
| protoArtoo baud | **9600**, not negotiable (soft UART, Section 9) |
| Allowed baud values | 2400, 9600, 19200, 38400, 57600, 115200 |

Cross the pair: the module's TX goes to `PIN_AUDIO_RX`, the module's RX to
`PIN_AUDIO_TX`, and the grounds must be common. The driver's own diagnostic says
so when a query gets nothing back:

> *"No CHIRP RX bytes during GMAN query. Verify CHIRP TX -> PIN_AUDIO_RX, common
> GND, and baud=9600."*

### 6.2 Three ways to set the baud rate

1. **`CHIRP.INI`** in the card root: `#BAUD_RATE 9600`. Parsed at boot, and the
   firmware re-opens `Serial2` afterwards so it takes effect on the same boot.
2. **The `BAUD:` command**, which also rewrites `CHIRP.INI` and speaks the new
   rate aloud before switching.
3. **The buttons, with no card edit and no host**: hold **Prev**, press
   **Play/Stop**, and the rate cycles `115200 -> 9600 -> 2400 -> 115200`. The
   board says the new rate out loud, so this is usable inside a closed droid.

The second button combo -- hold **Prev**, press **Next** -- cycles the Bank 1
page across the pages that actually exist on the card, and says the new one
aloud. It needs a reboot to take effect, and the firmware says that too.

> [!NOTE]
> **Bank 1 page changes are the one setting that can silently cost a
> re-sync.** Changing the page changes which directory Bank 1 means, so the next
> boot prunes the old page out of flash and copies the new one in. On a large
> page that is a long, loud boot.

### 6.3 The build contract, and the cold-boot bug that is worth reading

For anyone rebuilding this firmware, `protoArtoo-config.md` records an
investigation whose conclusion is not guessable. Roughly 8 cold power-ons in 10
came up dead -- power LED only, no NeoPixels, no USB, no UART -- while the board
was completely stable once running. SD card, clock speed and power supply were
each tested and ruled out. The author could reproduce it under PlatformIO but
**not** under the Arduino IDE with the same source, which isolated it to the
build configuration. Two settings fixed it:

```ini
board = rpipico2                     ; not pimoroni_pico_plus_2
board_upload.psram_length = 8388608  ; the critical one
```

Without `psram_length`, the linker omits the PSRAM region entirely: `psram_init()`
still runs, but the TLSF heap is never set up, **every `pmalloc()` returns
`nullptr`**, and the firmware dies in audio init before TinyUSB can enumerate --
which looks exactly like a BOOTROM-level boot failure. The wrong `board` value
embeds a boot2 tuned for a different flash chip.

Also load-bearing, and all in the in-tree `platformio.ini` and variant:

- `RP2350_PSRAM_CS = 0` and `PICO_RP2350A 1` -- the stock Pimoroni variant sets
  CS to GPIO47, which does not exist on an RP2350A, and PSRAM reports 0 KB;
- `-Ofast` (unflagging `-Os`) and `f_cpu = 250 MHz`, to match the Arduino IDE
  build the firmware was tested against;
- `framework-arduinopico` pinned to **5.5.1**;
- `arduino-libhelix` pinned to **v0.9.2**;
- Adafruit TinyUSB deliberately **not** in `lib_deps` -- adding it pulls
  Adafruit's SdFat fork in transitively and collides with the core's SdFat.

Two environments exist: `chirp_rp2350` (production, USB CDC compiled out) and
`chirp_rp2350_debug` (`-D DEBUG`, USB serial and verbose boot). **The production
build never starts USB serial at all** -- deliberately, because initialising
TinyUSB with no host attached disturbed SD and flash init timing during boot.

## 7. The serial protocol (normative)

Read from `serial_commands.cpp`, `mp3_compat.cpp` and `serial_queue.cpp` this
session. There is no other specification.

### 7.1 Framing

**Host to module:** ASCII, one command per line, terminated by `\n` or `\r`
(either alone ends a command; `\r\n` is safe because the second byte finds an
empty buffer and is discarded). Command and arguments are separated by `:`,
arguments by `,`. The input buffer is **128 bytes** per port; a longer line is
truncated by dropping the overflow, not by erroring.

**Module to host:** ASCII lines terminated by `\r\n` (`println`). There is no
checksum and no sequence number in either direction.

There are **two independent command buffers**, one for USB CDC and one for the
UART, so a debug session cannot corrupt a command arriving from the droid.

**Parsing is prefix-based and case-sensitive.** `strncmp` against the literal
uppercase token; anything unmatched answers `ERR:UNKNOWN`.

> [!CAUTION]
> **Arguments are positional and omitting one in the middle is legal**, because
> `parseArgInt()` returns a default when it meets an immediate comma. `PLAY:5,,B`
> is *"index 5, default bank 1, page B"*. This means a malformed command often
> plays **something** rather than failing.

### 7.2 The command table

| Command | Arguments | Defaults | Queued reply | Direct reply |
| --- | --- | --- | --- | --- |
| `PLAY:` | `index[,bank[,page[,vol]]]` | bank `1`, page `A`, vol `-1` (unchanged) | `PACK:PLAY`, `S:<stream>,ply,<vol>` | `ERR:*` on failure |
| `STOP` | none, or `:<stream>`, or `:*` | all streams | `PACK:STOP`, `S:<n>,idle,,0` per stream | `ERR:PARAM` |
| `VOL:` | `<0-99>` or `<stream>,<0-99>` | -- | `PACK:SVOL` | `ERR:PARAM` |
| `CHRP:` | `startHz,endHz,ms[,vol]` | vol `128` | `PACK:CHRP` | -- |
| `GMAN` | none | -- | `MDAT:`, `BANK:`xN, `MSUM:`, `MEND` | -- |
| `GNME:` | `bank,page,index` | -- | `NAME:...` | -- |
| `LIST` | none | -- | -- | **human-readable block** |
| `STAT:` | `<stream>` | -- | -- | **`STAT:...`** |
| `CCRC` | none | -- | `PACK:CCRC` | progress text |
| `BAUD:` | `<rate>` | -- | `PACK:BAUD`, `BAUD:<rate>` | `ERR:PARAM` |
| `BPAGE:` | `<A-Z>` | -- | `PACK:BPAGE`, `BPAGE:<page>` | note text |
| `MUSB` | none (toggle), or `:0` / `:1` | toggle | `PACK:MUSB`, `MUSB:<0\|1>` | -- |
| `PVOICE:` | `<clip name>` | -- | `PACK:PVOICE` | `ERR:PARAM` |
| *(unmatched)* | -- | -- | -- | `ERR:UNKNOWN` |

Volume is **0-99 ascending**, 99 loudest -- the opposite direction to the MP3
Trigger's register and a different span to the DY-SV5W's 0-30. It is stored as a
float gain per stream (`vol / 99.0f`).

`CHRP:` synthesises a frequency sweep in software -- a real beep with no file
behind it. Nothing in protoArtoo sends it today; Open Item 4.

`PVOICE:` plays one clip from the **246-word voice vocabulary embedded in the
firmware** (`system_audio_data.cpp`), not from the card. The vocabulary is built
for spoken diagnostics -- `sd_card`, `not`, `detected`, `baud_rate`, `setting`,
`reboot_required`, `memory`, `voltage`, `signal`, `lost`, the numbers `0000`-`0100`
and the letters `_a`-`_z`. The board can therefore **say what is wrong out loud
inside a closed droid**, which is a genuinely unusual diagnostic channel and is
how the boot sequence reports a missing SD card.

### 7.3 The four query responses

**`GMAN`** -- the manifest summary:

```
MDAT:<bankCount>                  <- number of BANK lines that follow
BANK:<bank>,<dirName>,<count>     <- once for Bank 1, then once per Bank 2-6 dir
MSUM:<crc32>                      <- CRC32 over every filename on the card
MEND                              <- terminator
```

There is **no page field** in a `BANK:` line. The page has to be derived from
the directory name, which is what protoArtoo does (`derivePageFromDirName()`:
skip leading digits, take the next alphabetic character).

`MSUM` is a CRC32 accumulated over every Bank 1 variant filename and every Bank
2-6 filename (`CHIRP_Audio.ino`, after the bank scans). **It is the card's
fingerprint**, and exists precisely so a host can cache a catalog and notice
when the card changed. protoArtoo parses the line as a valid frame marker and
then discards the value (Finding 13.5).

**`GNME:<bank>,<page>,<index>`** -- one name:

```
NAME:<bank>,<page>,<index>,<filename>     <- Banks 2-6
NAME:1,,<index>,<basename>.wav            <- Bank 1: EMPTY page, forced .wav
NAME:<bank>,<page>,<index>,INVALID        <- out of range, Banks 2-6 only
```

Three traps live in those three lines, and all three are Findings: Bank 1 ignores
the requested page (13.3), Bank 1 always claims `.wav` (13.4), and a Banks 2-6
reply with a zero page emits a stray comma that shifts every field right (13.6).
An out-of-range **Bank 1** index produces **no reply at all** -- the handler
returns silently -- so a host must rely on its own timeout.

**`STAT:<stream>`** -- one stream's state, written **directly** to the port:

```
STAT:playing,<filename>,<volume 0-99>
STAT:idle,,0
```

**`LIST`** -- a human block, also written directly, and **not** a full listing
despite the README's description: it prints the Bank 1 count, then at most the
**first ten** entries with a `... and N more` line, then one line per Bank 2-6
directory. It is a console convenience, not a machine interface. Use `GMAN` and
`GNME`.

### 7.4 The response queue, and why ordering is not what you expect

This is the single most important host-side fact in the protocol, and nothing
outside the source states it.

`sendSerialResponse()` branches on the port (`serial_commands.cpp:7-16`):

- **USB CDC** -> written immediately;
- **UART (`Serial2`)** -> **pushed onto a 16-slot ring queue**.

But large parts of the firmware bypass that helper and call `serial.println()` or
`serial.printf()` directly -- **every `ERR:` line**, the whole of `STAT`, the
whole of `LIST`, and `CCRC`'s progress text. Those go out immediately, on the
same UART, while queued messages are still waiting.

The queue is drained in `loop()` by `trySendQueuedMessages(5)`:

| Property | Value | Consequence |
| --- | --- | --- |
| Depth | `SERIAL2_QUEUE_SIZE = 16`, ring reserves one -> **15 usable** | a burst longer than 15 loses messages |
| Overflow rule | **drop the OLDEST**, bump `messagesDropped` | the *front* of a reply disappears, not the tail |
| Drain rate | **5 messages per `loop()`** | |
| Drain gate | **skipped entirely while `isCpuBusy()`** | |
| `isCpuBusy()` | true when any `STREAM_TYPE_MP3_SD` ring buffer is **below 25%** | replies stall while an MP3 is streaming from SD |

Three rules follow for any host implementation:

1. **A direct reply can overtake a queued one.** Send `PLAY:` then `STAT:0`
   quickly and the `STAT:` line may arrive before `PACK:PLAY`. Match on the line
   prefix; never on arrival order.
2. **Replies stall under audio load.** A busy decoder starves the queue by
   design -- audio wins, and that is the right trade for a sound board, but a
   host timeout must be generous. protoArtoo allows 1500 ms at boot, 2500 ms on
   refresh, 450 ms per name and 300 ms for status.
3. **A long reply can lose its own beginning.** `GMAN` emits `sdBankCount + 4`
   messages in one uninterrupted burst before `loop()` gets a chance to drain
   any of them. Past 15 banks-plus-four the oldest are silently discarded, and
   `MDAT` and `BANK:1` are the first two to go. Finding 13.2.

### 7.5 The MP3 Trigger compatibility layer

`checkAndHandleMp3Command()` is offered **every inbound byte whenever the command
buffer is empty**, before the ASCII parser. It implements the SparkFun MP3
Trigger protocol so that a droid already wired for one can drop this board in
without a firmware change on the host:

| Byte | Argument | Action here |
| --- | --- | --- |
| `'O'` | -- | toggle play/stop of the last root track |
| `'F'` | -- | next root track (wraps) |
| `'R'` | -- | previous root track (wraps) |
| `'T'` | ASCII `'0'`-`'9'` | play root track by number |
| `'t'` | binary 0-255 | play root track by number |
| `'p'` | binary 0-255 | play root track by **directory index** |
| `'v'` | binary 0-255 | volume, **inverted**: `gain = 1.0 - v/255`, applied to every stream |

Root tracks are the audio files in the **card root**, sorted alphabetically,
matched by a leading three-digit prefix (`001`), falling back to a bare `N.`
form. `#LEGACY_MONOPHONIC` decides whether a legacy trigger stops the previous
sound (`1`, the documented default) or mixes (`0`).

Two commands of the MP3 Trigger protocol are **not** implemented: `'S'` status
and `'Q'` quiet mode. A host configured as an MP3 Trigger would therefore play
tracks correctly and report a **dead link forever**, because its version query
never answers. protoArtoo never does this -- it has a native driver -- but a
builder debugging a mixed setup will meet it.

> [!CAUTION]
> **The shim consumes its argument byte even when it rejects the command.**
> `'T'` followed by a non-digit returns false, so the `'T'` is appended to the
> ASCII command buffer -- but the second byte was already read and thrown away.
> A host that sends `TEST\n` loses the `E`, and the module then tries to parse
> `TST`.

### 7.6 Boot behaviour and timing

`setup()` runs a fixed **1500 ms settle** before touching anything, then: LEDs,
`Serial2`, SPI1, SD (three attempts, 25 MHz then 4 MHz each, with a CS pulse and
500 ms between attempts), LittleFS (three attempts), `CHIRP.INI`, a `Serial2`
re-open at the configured baud, audio allocation, decoder allocation, Bank 1
scan, flash sync, Bank 2-6 scan, checksum, root-track scan, then unmute and a
100 ms DMA prime.

Boot is therefore **at least ~1.7 s and realistically several seconds**, longer
on the first boot after a card change because the flash sync runs and narrates
itself. protoArtoo's driver waits **2000 ms** in `begin()` before its first
query, with a comment saying a first boot after an SD change may need more.

Failure behaviour differs by subsystem, and the distinction matters:

| Failure | Behaviour |
| --- | --- |
| SD card missing | error chirps, speaks *"SD card not detected"*, **then continues** in flash-only mode |
| LittleFS mount failure | error sequence, then **halts forever** with red LEDs |
| PSRAM allocation failure | logs, leaves that decoder null, continues with fewer streams |
| Firmware version changed | speaks the new version aloud at boot |

**A silent board with three red LEDs flashing is a halted flash mount, not a
crash.** The firmware flashes them deliberately so the freeze is legible.

### 7.7 The LED vocabulary

Three NeoPixels at brightness 20, driven from `blinkies.cpp`:

| State | Appearance |
| --- | --- |
| Boot | each LED turns green in turn, then off |
| Idle | blue/purple/pink Cylon scanner |
| Playing | one LED per stream -- **blue = WAV, green = MP3, orange = AAC/M4A** |
| Flash sync | LED 2 heartbeat at 500 ms, LEDs 0 and 1 blink per file copied |
| USB MSC active | faster green/yellow scanner |
| Fatal | all red, flashing at 1 Hz forever |

LED updates are skipped while `isCpuBusy()`, so **the lights stop moving when the
decoder is under pressure**. That is a diagnostic, not a fault.

### 7.8 `CHIRP.INI`, and two defaults that are documented wrongly

The file lives in the card root. Keys begin with `#`, then the name, then a
space, then the value; anything else on a line is ignored. Parsing is
case-insensitive on the key.

| Key | Accepted | Documented default | **Code default** |
| --- | --- | --- | --- |
| `#BANK1_PAGE` | `A`-`Z` | `A` | `A` |
| `#BANK1_VARIANT` | `A`-`Z` | -- | legacy alias for the above |
| `#BAUD_RATE` | 2400, 9600, 19200, 38400, 57600, 115200 | 115200 | 115200 |
| `#USE_FLASH_BANK1` | `0` / `1` | **`1`** | **`0`** |
| `#LEGACY_MONOPHONIC` | `0` / `1` | **`1`** | **`0`** |
| `#MAX_STREAMS` | 1-10 | 3 | 3 |
| `#STREAM_BUFFER_SIZE` | `SMALL` 128 / `MEDIUM` 256 / `LARGE` 512 / number in KB | LARGE | LARGE |
| `#VERSION` | written by the firmware | -- | -- |

> [!WARNING]
> **Two documented defaults do not match the code.** The sketch header states
> `#USE_FLASH_BANK1 [Default: 1]` and `#LEGACY_MONOPHONIC [Default: 1]`, but
> `globals.cpp:45` and `CHIRP_Audio.ino:105` both initialise `false`. If the key
> is **absent** from `CHIRP.INI` you get `0` -- no flash sync, and legacy
> triggers that mix instead of interrupting. Finding 13.7. **State both keys
> explicitly rather than relying on either default.**

`#STREAM_BUFFER_SIZE` is rounded **down to a power of two** (minimum 32 KB,
capped at 4096 KB) because the ring buffer wraps with a bitmask.

If `#BANK1_PAGE` is missing, `parseIniFile()` **rewrites the whole file** with
the values currently in memory -- so a hand-written minimal INI comes back
expanded with the code defaults baked in. `#VERSION` is the firmware's own
record of what booted last; changing it is what triggers the spoken
firmware-update announcement.

## 8. What protoArtoo's driver actually sends

`src/drivers/audio_chirp.cpp`, read in full. The driver implements the six
`AudioDriver` methods plus the catalog interface.

### 8.1 The command side

| `AudioDriver` call | Bytes on the wire | Notes |
| --- | --- | --- |
| `begin(vol)` | `VOL:<n>\n`, then optionally `GMAN\n` | after a **2000 ms** settle |
| `playTrack(n)` | `PLAY:<n>,1,A\n` | delegates to `playTrackBanked(n, 1, 'A')` |
| `playTrackBanked(i,b,p)` | `PLAY:<i>,<b>,<p>\n` | page normalised to uppercase, non-alpha becomes `A` |
| `stop()` | `STOP\n` | **all streams** |
| `setVolume(v)` | `VOL:<v*99/30>\n` | 0-30 in, 0-99 out, ascending both ways |
| `queryModuleState()` | `STAT:0\n` | stream 0 only |
| `refreshCatalog()` | `GMAN\n`, then `GNME:<b>,<p>,<i>\n` per entry | |

Track index `0` is dropped silently; bank `0` is coerced to `1`. The driver
**never sends a volume argument on `PLAY:`**, so per-sound volume is not used --
every sound plays at the global level.

### 8.2 The read side, and its timeouts

| Operation | Budget | Constant |
| --- | --- | --- |
| Boot manifest | **1500 ms** total | `begin()` |
| Catalog manifest | **2500 ms** total | `refreshCatalog()` |
| Per-name wait | **450 ms** | `CHIRP_GNME_WAIT_MS` |
| Per-line read inside that | **120 ms** | `CHIRP_GNME_READLINE_MS` |
| Status query | **300 ms** total, 40 ms per line | `queryModuleState()` |

`readLine()` accumulates until `\n`, discarding `\r`, and yields 1 ms whenever no
byte is ready, so a long catalog walk does not starve WiFi, OTA or SSE. Every
query **drains the RX buffer first**, in 32-byte chunks with a yield between, so
a stale unsolicited line cannot be mistaken for the answer.

Response matching is by content, not order -- `loadManifestBanks()` accepts any
line beginning `MDAT:`, `BANK:`, `MSUM:` or `MEND` and stops on `MEND`;
`refreshCatalog()` requires the parsed `NAME:` line's bank, page and index to
**equal what it asked for** before accepting it. That is the right design given
Section 7.4, and it is why Finding 13.3 shows up as a timeout rather than as
wrong data.

A name that never arrives becomes a synthetic entry named `index_<n>` and the
walk continues; the count of those is logged at the end.

### 8.3 `stop()` stops everything, and that is a real limit

The module's strength is simultaneity, and protoArtoo's `stop()` sends bare
`STOP`, which the firmware expands to *"stop every stream"*. There is no way for
the current interface to stop a beep while leaving music playing, even though
the module supports `STOP:<stream>` exactly for that.

The driver also cannot choose a stream: `PLAY:` takes no stream argument, and the
module allocates one with `getNextAvailableStream()` -- first inactive, and when
all are busy it **steals stream 0**, which is the one the system voice uses. So
under load a droid's third simultaneous sound will interrupt whatever stream 0
is doing.

This is the gap ADR 0054 (*sound gains a second lane*) exists to close, and this
module is the only member that could implement it. Open Item 3.

## 9. The transport

### 9.1 Two directions, two mechanisms

On **artoo-esp32**, the board protoArtoo runs today:

| Direction | Mechanism | Pin |
| --- | --- | --- |
| Host -> module | **bit-banged software UART**, 9600 8N1 | `PIN_AUDIO_TX` = **GPIO26** |
| Module -> host | **hardware UART2**, shared with the dome link | `PIN_AUDIO_RX` = **GPIO35** |

There is no spare hardware UART TX on this board, which is the whole reason for
the soft UART. `softUartTxByte()` wraps **each byte** in a `portMUX` critical
section -- `delayMicroseconds()` is not interrupt-safe, and the 1 ms FreeRTOS
tick or a WiFi ISR would otherwise stretch a bit period and corrupt the frame.
At **104 us per bit** that is **~1.04 ms per byte**, taken in per-byte slices
rather than one block. Core 1's real-time control loops are untouched; this is a
Core 0 cost only, and it is why the baud rate is not negotiable upward without a
hardware UART.

> [!NOTE]
> **This module's commands cost three to four times what the soft UART was
> budgeted for.** The header's own figure is *"~5 ms per 4-byte audio command"*,
> which is a DY-SV5W binary frame. An ASCII `PLAY:12,2,C` is 12 bytes plus the
> newline -- about **13.5 ms** -- and `GNME:2,C,12` plus a 300 ms status query
> is a different shape of load again. Nothing measured says this is a problem
> (audio commands are at most a few per second), but the sizing note in
> `audio_soft_uart_tx.h:22-23` predates this backend and should not be read as
> covering it. Open Item 5.

GPIO35 is **input-only** on the ESP32, which is why the assignment cannot simply
be reversed.

On **firebeetle2-P4** (`PA_CAP_DEDICATED_AUDIO_UART`), `PIN_AUDIO_TX` = 34 and
`PIN_AUDIO_RX` = 36 are a real UART3 pair. **No P4 environment selects this
backend today**, so that path has never run; the driver's file header says so and
deliberately leaves the dome-UART guards in rather than adding an untested
capability branch (#254).

### 9.2 The shared UART, and how the driver behaves when it loses

`UART_PORT_AUDIO` on artoo-esp32 *is* the dome link's controller. The driver
therefore arbitrates rather than assuming:

- `begin()` calls `domeUartAcquire(DOME_UART_AUDIO)`; if the dome holds it, the
  manifest fetch is **skipped** and logged as *"playback commands remain
  available"* -- because TX is a separate soft UART and never blocked.
- `queryModuleState()` checks `domeUartOwnedBy(DOME_UART_DOME)` and returns the
  **cached** state untouched rather than a false "no response".
- `refreshCatalog()` refuses outright when the dome owns the bus.
- `classifyRxStatus()` distinguishes `AUDIO_RX_BLOCKED_BY_DOME_UART` from
  `AUDIO_RX_NO_RESPONSE`, and the Sound page prints *"Status unavailable:
  protoR2link is using UART"* rather than an error.

**Sound always plays; only the answers can be unavailable.** That asymmetry is
the design, and it is correct. CHANGELOG records both halves being fixed on real
hardware: *"CHIRP audio and the dome serial link now coexist reliably on a shared
UART"* and *"CHIRP audio could silently fail to initialize when the dome serial
link held the UART"*.

### 9.3 Memory, which this product has already cost once

The catalog is the largest per-module allocation in the audio subsystem, and it
is split deliberately:

| Allocation | Size | When |
| --- | --- | --- |
| Bank summary | 64 x `AudioCatalogBank` = **~2.3 KB** | first discovery; then held |
| Entry array | actual track count x ~52 B, **capped ~15.6 KB** at 300 entries | **only** on `refreshCatalog()` |

The entry array was once allocated on the boot and link path, and the result is
in the CHANGELOG: *"A heap-exhaustion crash (OOM) affecting CHIRP and Learned
Sequences"*, fixed by *"Sequence and CHIRP catalog memory is now allocated based
on actual data size"*. `m_catalogCount` is zeroed **before** the array is
swapped so a concurrent `/api/audio/catalog` reader sees an empty catalog rather
than freed memory.

Anything further belongs on the headroom register (#381), not here.

## 10. Capabilities, status, and what the operator sees

### 10.1 The capability word is `0x3F`, the widest in the family

| Bit | Value | Declared | Honestly answered by the module? |
| --- | --- | --- | --- |
| `AUDIO_CAP_STATUS_QUERY` | `0x01` | yes | **yes** -- `STAT:` |
| `AUDIO_CAP_DEVICE_TYPE` | `0x02` | yes | **no** -- hardcoded `3` (Finding 13.1) |
| `AUDIO_CAP_TRACK_COUNT` | `0x04` | yes | **yes** -- Bank 1 count from `GMAN` |
| `AUDIO_CAP_CURRENT_TRACK` | `0x08` | yes | **no** -- echo of the last index sent |
| `AUDIO_CAP_QUERY_SAFE_PLAYING` | `0x10` | yes | **yes** -- `STAT:` is safe during playback |
| `AUDIO_CAP_CATALOG` | `0x20` | yes | **yes** -- `GMAN` + `GNME` |

`QUERY_SAFE_PLAYING` is the bit no other member has, and it changes operator
behaviour rather than just data: the Sound page **auto-refreshes every 10
seconds** instead of showing a manual *Poll* button, because polling this module
mid-playback does not disturb it. `data/sound.js:315` computes
`showManualPoll = supportsStatusQuery && !supportsSafePlayingQuery`, so declaring
the bit is what removes the button.

`CATALOG` is the other unique bit, and it unlocks the whole catalog workspace on
the Sound page: live entry listing, bulk multi-select, per-row mapping to named
and system slots, category-range mapping, and the *Apply suggestions* action
that infers categories from bank directory names.

### 10.2 What the operator actually reads, and the two rows that lie

Because all six bits are declared, all four status rows are shown:

| Row | Source | Truth |
| --- | --- | --- |
| Driver | `"CHIRP"` | fine |
| Link | did `STAT:` answer | fine |
| Device | **hardcoded `3`** | **not in the enumeration** -- reads as a bare "3" |
| Play state | parsed from `STAT:` / `S:` | fine |
| Total tracks | Bank 1 count from `GMAN` | fine when `GMAN` survives (Finding 13.2) |
| Current track | **last index we sent** | wrong after `stop()`, after a legacy trigger, after a button press, and blind to which variant or stream played |

`docs/sound_playback.md` claims *"device type and current track are not
applicable for CHIRP and are hidden"*. **They are declared, so they are shown.**
Finding 13.1.

### 10.3 The binding layer, and a name baked into the API

Because this module addresses `bank/page/index` rather than a flat number,
protoArtoo stores a per-slot binding. `include/chirp_binding_keys.h` is the one
declaration of those keys:

- **27 named/system slots** -- `chr_scream`, `chr_leia`, `chr_cantina_s`,
  `chr_imp_march`, `chr_startup`, `chr_sys_boot`, `chr_sys_mode_n`,
  `chr_sys_netdown` and so on;
- **12 category ranges** -- `chr_cat_gen`, `chr_cat_chat`, `chr_cat_hap`,
  `chr_cat_proc`, `chr_cat_sad`, `chr_cat_sent`, `chr_cat_hum`, `chr_cat_scrm`,
  `chr_cat_ooh`, `chr_cat_alrm`, `chr_cat_snrk`, `chr_cat_whis`.

`GET /api/audio/tracks` exposes them as `chirp_bindings` and
`chirp_category_bindings`, omitted when nothing valid is saved.

> [!NOTE]
> **The product name is baked into public JSON and NVS keys where a capability
> belongs.** `chirp_bindings` and `chr_*` describe *"the binding a catalog-capable
> module needs"*, not *"the binding the CHIRP Audio Trigger needs"* -- any future
> member declaring `AUDIO_CAP_CATALOG` would inherit a competitor's product name
> in its own config. This is recorded on
> [#175](https://github.com/mattiasbrandt/protoArtoo/issues/175) and explicitly
> **not ticketed**, pending a decision on whether the compatibility cost of
> renaming stored NVS keys and a public API field is worth paying. Noted here so
> the next reader knows it is a known choice and not an oversight.
>
> One key is also shortened against a platform limit: `sys_net_down` maps to
> `chr_sys_netdown`, because 15 characters is the ESP-IDF Preferences key
> ceiling (#189).

## 11. How the four Sound members differ

[`dy-sv5w-sound.md`](dy-sv5w-sound.md) Section 16 carries the full comparison
and this sheet does not duplicate it. The rows where the CHIRP Audio Trigger is
the outlier:

| Row | CHIRP Audio Trigger | Why it matters |
| --- | --- | --- |
| Addressing | **bank / page / index, with names** | the only member a host can enumerate |
| Simultaneous streams | **3 by default, up to 10** | the only member that can mix at all |
| Decoding | **software, on a 250 MHz RP2350** | no codec chip to go end-of-life |
| Formats | **WAV, MP3, AAC, M4A** | the widest; `.ogg` is recognised but undecodable |
| Catalog | **yes -- `GMAN` / `GNME` / `MSUM`** | unique; the whole Sound page workspace depends on it |
| Safe to query while playing | **yes** | unique; it is why the page auto-refreshes |
| Variant groups | **yes, random non-repeating** | unique; one "happy" that never sounds the same twice |
| Synthesised tones | **yes -- `CHRP:`** | unique; a beep with no file behind it |
| Spoken diagnostics | **246-clip vocabulary in firmware** | unique; the board says what is wrong out loud |
| Protocol | **ASCII lines, human-typable** | the only member you can drive from a terminal by hand |
| Legacy compatibility | **an MP3 Trigger shim, on purpose** | the only member that emulates another member |
| On-board amplifier | **no** | needs an external amp, like the MP3 Trigger |
| On-board storage | **yes, 14 MB flash** for Bank 1 | fastest start of any member |
| USB mass storage | **yes** -- card editable over USB-C | unique; no card removal to change sounds |
| Vendor documentation | **none -- the source is the spec** | unique, and the reason this sheet is long |
| Purchasable | **no** | unique, and the reason Open Item 1 exists |
| Proven on our hardware | **yes** | shares this only with the DY-SV5W |

**What it uniquely brings** is the answer to "what sounds do you have, and what
are they called?" -- which is the question every other member refuses. Add
mixing, variant groups, format breadth, flash-backed vocals, USB card editing
and spoken diagnostics, and it is not really the same class of device as the
other three.

**What it uniquely costs** is that you cannot buy one, its specification is a
moving Git repository rather than a document, its response path can silently
drop the front of a long reply (Finding 13.2), and every operator-facing name it
touches carries a competitor's product name (Section 10.3).

## 12. How the hobby got here (non-normative)

Everything in this section is evidence of practice. None of it is normative.

### 12.1 It exists because of five specific complaints

The author set them out in the project's own announcement, and they read as a
decade of accumulated annoyance with the board everyone used:

1. **One sound at a time.** *"My droid might be getting the crowd dancing at an
   event... then I'll accidentally make him fire off a beep-boop and the music
   comes to a sudden embarrassing stop."* Two MP3 Triggers or a dual-player
   shield had been tried and *"always seemed overcomplicated"*.
2. **No feedback.** *"The MP3 Trigger receives a play command and plays, but
   can't provide feedback on what files its playing."* The concrete pain was a
   transmitter Lua jukebox that had to be hand-edited to match the card.
3. **Price.** *"MP3 Triggers are currently over $60"*, attributed to the
   proprietary VLSI decoder and its MCU.
4. **255 sounds.** Acknowledged as *"actually a lot of sounds for a droid"*.
5. **DIY.** *"I just like going my own way."*

Points 1 and 2 are precisely what `MAX_STREAMS`, `GMAN`, `GNME` and `MSUM`
answer, and point 2 is why protoArtoo has a catalog workspace at all. Point 3 is
answered by a ~USD 1 MCU doing the decoding in software -- the author credits an
earlier collaborator for showing that an ESP32 could decode MP3 with a DAC and
no codec chip.

### 12.2 The shim is a statement about the ecosystem

`mp3_compat.cpp` is a deliberate MP3 Trigger emulator on 2026-era hardware. The
reason is compatibility with the installed base: **PADAWAN, SHADOW and ShadyRC
all speak that protocol**, so a droid already wired for one can drop this board
in and keep its host firmware.

The convention it emulates is worth stating because it is what the rest of the
family assumes:

- **`'t'` + a binary byte** plays the file whose name starts with those three
  digits. Verified in BetterDuino's `MDuinoSound.cpp`, in protoArtoo's own MP3
  Trigger driver, and in commented-out Padawan360 code.
- **`'v'` + a binary byte is inverted** -- `0` loudest, `255` silent -- because
  it is a VS10xx register. This board converts it with
  `gain = 1.0f - sfVol / 255.0f` and applies it to every stream.
- **Sound numbers are banked in 25s.** AstroPixelsPlus's `MarcduinoSound.h`
  states the canonical table: *"Bank 1: gen sounds, numbered 001 to 025; Bank 2:
  chat sounds, numbered 026 to 050..."* through nine banks. Every R2 sound pack
  ships in that shape, which is why changing a filename breaks a droid.
- **There is no stop command**, so the hobby stops sound by playing a silent
  track. This board and protoArtoo both use **254**; AstroPixelsPlus uses
  **252** (`MP3_EMPTY_SOUND`). Do not assume either is universal.

Note what the bank convention and this product's bank model are **not**: the
MarcDuino bank is a *number range inside a flat 255*, while a CHIRP Audio
Trigger bank is a *directory*. They share a word and nothing else. This is why
`docs/sound_playback.md` warns that protoArtoo's named-track defaults
(`scream=126`, `leia=151`) are DY-SV5W/community pack numbers and **must be
re-mapped** through the Sound page when this module is fitted.

### 12.3 The rest of the field, briefly

- **ShadowMD** drives no sound module directly; it delegates to a MarcDuino over
  `$8x\r`, so the player sits behind that board.
- **Padawan360** on this disk has already migrated *away* from the MP3 Trigger
  to a DY-SV5W, with the old calls surviving as comments.
- **AstroPixelsPlus / Reeltwo** abstracts three players -- MP3 Trigger, DFPlayer
  Mini and the HCR Vocalizer -- behind one `Stream&`.
- **None of them queries the player for a track count, a current track or a file
  list.** That is the gap this product was built to fill, and it is the single
  clearest justification for the `CATALOG` capability existing in our registry
  at all.

### 12.4 The change request that went the other way

`sound-bank-page-detection.md`, in the local clone, is a **firmware change
request written by this project for the upstream author**. It asks for three
things, all of which are live limits today:

1. `GMAN` should report **all** valid Bank 1 pages, not only the active one;
2. `GNME:1,<page>,<index>` should honour the requested page;
3. `PLAY:<index>,1,<page>` should play from the requested page.

Today `scanBank1()` only ever populates the active `#BANK1_PAGE`, so a card with
`1A_R2D2` and `1B_R5D4` exposes only one of them, and switching needs an INI
edit or a button combo **and a reboot**. Item 2 is the direct cause of
Finding 13.3.

This is worth recording as a relationship as much as a technical note: the
upstream author is reachable and has acted on this project's findings before --
`protoArtoo-config.md` documents a cold-boot bug diagnosed jointly, where the
author reproduced the failure under PlatformIO and not under the Arduino IDE,
which is what isolated it to the build configuration.

## 13. Findings against the shipping implementation

Eight, found this session by reading both ends of the protocol against each
other. **None is fixed here** -- #387 is a record, not a change request -- so
all eight are reported. Findings 13.1 to 13.6 and 13.8 are reasoned from source
on both sides; none has been captured off the wire.

### 13.1 REPORTED -- two declared capabilities are fabricated, and one prints an undefined value

`include/component_registry.inc:172-176` declares `AUDIO_CAP_DEVICE_TYPE` and
`AUDIO_CAP_CURRENT_TRACK`. Neither is answered by the module:

- `src/drivers/audio_chirp.cpp:628` and `:702` set `out.device = 0x03`.
  `include/audio_driver.h:81` defines the field as
  `0=USB 1=SD/TF 2=FLASH 0xFF=unknown/none`. **`3` is not a member of that
  set.** `data/sound.js:384` renders it raw, so the operator reads "3".
- `out.currentTrack = m_lastTrack`, the last index protoArtoo *sent*. `stop()`
  does not clear it; a legacy trigger, a front-panel button press or a variant
  pick never touch it; and it cannot express which of three streams is playing.

Both rows are **shown** on the Sound page because both bits are declared
(`data/sound.js:343,345`). `docs/sound_playback.md` says the opposite -- *"device
type and current track are not applicable for CHIRP and are hidden"* -- which is
stale.

Three coherent options, and choosing between them is an operator decision:
drop both bits from the registry row and let the rows hide; keep the bits and
make the values real (`device` = `2` when Bank 1 is flash-backed, `1` otherwise;
`currentTrack` parsed from `STAT:`'s filename against the catalog); or keep them
and define `3` in the enumeration as "mixed". **Whichever is chosen, the doc
sentence is wrong today.**

### 13.2 REPORTED -- `GMAN` on this droid's own card overflows the module's reply queue

`handleGman()` emits `sdBankCount + 4` messages -- `MDAT`, one `BANK:` for Bank
1, one per Bank 2-6 directory, `MSUM`, `MEND` -- in a single uninterrupted
burst, with no drain between them. The queue is `SERIAL2_QUEUE_SIZE = 16` with
one slot reserved, so **15 usable**, and overflow **drops the oldest**.

This droid's card carries 13 Bank 2-6 directories (Section 5.5), so `GMAN`
emits **17** messages and the first **two** -- `MDAT` and `BANK:1` -- are
discarded before the drain ever runs.

`BANK:1` is the line that sets `bank1Count` and therefore `m_totalTracks`. If it
is dropped, **Total tracks reads 0 on a perfectly healthy link**, and the
boot-time `m_totalTracks` stays 0 because `begin()` passes
`keepTotalTracks = false`. The catalog walk also loses Bank 1 entirely, because
`refreshCatalog()` iterates the banks `loadManifestBanks()` captured.

The threshold is **12 Bank 2-6 directories**; this card has 13.

*Settled by:* a UART capture of `GMAN` on this card, counting `BANK:` lines
against the 14 directories. Ten minutes with the board on a bench. If confirmed,
the fix is upstream (drain inside the `GMAN` loop, or raise
`SERIAL2_QUEUE_SIZE`) and the workaround here is fewer bank directories.

### 13.3 REPORTED -- Bank 1 names degrade to `index_N` on any page but `A`

`refreshCatalog()` always sends an explicit page: `GNME:1,A,5`. For `bank == 1`,
`handleGnme()` **ignores the page argument** and answers from the active page
with an **empty** page field: `NAME:1,,5,happy.wav`. protoArtoo's
`parseNameLine()` handles the empty-page form by forcing `*pageOut = 'A'`.

That agrees only while the active Bank 1 page *is* `A`. Set `#BANK1_PAGE B` and:

1. `GMAN` reports `BANK:1,1B_R5D4,N`, so `derivePageFromDirName()` gives `'B'`;
2. protoArtoo asks `GNME:1,B,<i>` and requires `respPage == 'B'`;
3. the module answers with an empty page, parsed as `'A'`;
4. the match check rejects every line, the loop spins the full **450 ms**, and
   the entry becomes `index_<i>`.

So a builder who switches Bank 1 to a second droid personality loses every Bank
1 name and pays ~11 s of dead time for 24 sounds. Upstream change request item 2
(Section 12.4) fixes it at the source; a one-line host-side tolerance -- accept
an empty page as "the bank's page" rather than literally `'A'` -- fixes it here.

### 13.4 REPORTED -- every Bank 1 name claims to be a `.wav`

`handleGnme()` formats Bank 1 replies as `"NAME:1,,%d,%s.wav"` -- the extension
is a **literal**, appended to a basename that has already had its real extension
stripped. This droid's `1A_general/` holds **`.mp3`** files, so the catalog shows
`general01.wav` for a file named `general01.mp3`.

Nothing breaks today, because protoArtoo plays by index and never by name. It
breaks the moment anything matches a catalog name against a filename -- a card
audit, an export, a builder reading the Sound page and going looking for the
file. Cosmetic, cheap to fix upstream, worth knowing before someone trusts it.

### 13.5 REPORTED -- `MSUM` is parsed and thrown away

The module computes a CRC32 over every filename on the card and sends it as
`MSUM:<value>` in every manifest, for the express purpose of letting a host
notice that the card changed. `loadManifestBanks()` accepts `MSUM:` as a valid
frame line and **never reads the value**.

This matters because of Section 5.2: a Banks 2-6 index is filesystem
enumeration order, so adding one file can renumber everything after it and
silently repoint every `chr_*` binding past that point. `MSUM` is exactly the
signal that would catch it -- store it beside the bindings, compare on boot,
and warn the operator that the card changed rather than playing the wrong sound.

*Settled by:* a decision, not a measurement. It is a real feature the module
offers and we decline.

### 13.6 REPORTED -- a Banks 2-6 `INVALID`/zero-page reply is malformed

`handleGnme()` formats Banks 2-6 replies with `"NAME:%d,%c,%d,%s"` and passes
`page == 0 ? ',' : page` as the `%c`. When the page is zero -- a bare
`2_Label/` directory with no page letter, which `scanSDBanks()` accepts -- the
character emitted **is a comma**, producing `NAME:2,,,3,file`: an extra empty
field that shifts everything right.

protoArtoo's parser tries the empty-page form first via
`sscanf("NAME:%lu,,%lu,%47[^\r\n]")`, which on `NAME:2,,,3,file` binds the name
to `,3,file` rather than `file`. The entry would be accepted with a corrupted
name. It cannot happen on this droid's card (every directory has a page letter),
so this is a latent trap rather than a live defect.

### 13.7 REPORTED -- two `CHIRP.INI` defaults are documented backwards

The sketch header documents `#USE_FLASH_BANK1 [Default: 1]` and
`#LEGACY_MONOPHONIC [Default: 1]`. The code initialises **both to `false`**
(`globals.cpp:45`, `CHIRP_Audio.ino:105`). An absent key therefore gives the
opposite of the documented behaviour. Section 7.8. Upstream doc fix; the
practical rule is to state both keys explicitly.

### 13.8 REPORTED -- the soft UART's cost note predates this backend

`src/drivers/audio_soft_uart_tx.h:22-23` budgets *"~5 ms per 4-byte audio
command"*, which describes a DY-SV5W binary frame. This module's ASCII commands
are 3-4x longer -- a `PLAY:` is 12-17 bytes, about 13-18 ms of Core 0 in
per-byte critical sections. No measurement says this is harmful, and audio
commands are rare, but the comment should not be read as covering this backend.
Section 9.1, Open Item 5.

## 14. Agent Lookup Quick Reference

- Field: Product name. Required value: **CHIRP Audio Trigger**, always in full in operator copy, docs and any lineup. **Never bare "CHIRP"** -- that also names **CHIRP Droid Control**, a peer body controller by the same author.
- Field: Component Protocol. Required value: **`chirp_ascii_uart`**.
- Field: protoArtoo identifiers. Required value: registry value **20**, id `chirp`, name `CHIRP Audio Trigger`, build token `PA_AUDIO_DRIVER = AUDIO_CHIRP` (4), NVS member value `chirp` under key `snd_member`.
- Field: Capability word. Required value: **`0x3F`** -- all six bits. Widest in the Sound family; the only member with `CATALOG` or `QUERY_SAFE_PLAYING`.
- Field: MCU. Required value: **RP2350A** at **250 MHz**, 16 MB flash (2 MB sketch + 14 MB LittleFS), **8 MB PSRAM on GPIO0** (Rev B). Rev A is **2 MB** PSRAM on **GPIO8**.
- Field: DAC. Required value: **PCM5102A**, line level. **No on-board amplifier.**
- Field: Host link. Required value: UART **8N1, 3.3 V**, module **TX GPIO4 / RX GPIO5** on the `TX RX +5V G` header.
- Field: Baud. Required value: **9600 for protoArtoo**, set by `#BAUD_RATE 9600` in `CHIRP.INI`. The firmware default is **115200**.
- Field: Allowed baud values. Required value: **2400, 9600, 19200, 38400, 57600, 115200**. Nothing else.
- Field: Command framing. Required value: ASCII, **UPPER CASE**, `:` before arguments, `,` between them, `\n` terminated, 128-byte buffer.
- Field: Case sensitivity. Required value: **commands are case-sensitive and lower-case `p`, `t`, `v` are intercepted by the MP3 Trigger shim** before the parser. `play:3` plays a sound.
- Field: Reserved first bytes. Required value: **`O` `F` `R` `T` `t` `v` `p`** belong to the legacy shim. No command may start with one.
- Field: Play. Required value: **`PLAY:<index>[,<bank>[,<page>[,<vol>]]]`**, bank default 1, page default `A`, vol default unchanged.
- Field: Stop. Required value: **`STOP`** stops **every stream**. `STOP:<n>` stops one; protoArtoo never sends that form.
- Field: Volume. Required value: **0-99 ascending, 99 loudest.** protoArtoo maps `0-30 -> vol * 99 / 30`.
- Field: Manifest. Required value: **`GMAN`** -> `MDAT:<n>`, `BANK:<bank>,<dir>,<count>` xN, `MSUM:<crc32>`, `MEND`. **No page field in a `BANK:` line** -- derive it from the directory name.
- Field: Name lookup. Required value: **`GNME:<bank>,<page>,<index>`** -> `NAME:<bank>,<page>,<index>,<file>`. **Bank 1 answers with an EMPTY page field, ignores the requested page, and always appends `.wav`.**
- Field: Status. Required value: **`STAT:<stream>`** -> `STAT:playing,<file>,<vol>` or `STAT:idle,,0`. **Written directly, bypassing the reply queue.**
- Field: Reply ordering. Required value: **not guaranteed.** `ERR:`, `STAT:` and `LIST` go straight out; `PACK:`, `BANK:`, `NAME:`, `MSUM:`, `MEND` are queued. Match on prefix, never on order.
- Field: Reply queue depth. Required value: **15 usable** of `SERIAL2_QUEUE_SIZE = 16`. Overflow **drops the oldest**. `GMAN` emits `sdBankCount + 4`.
- Field: Bank directory limit before `GMAN` truncates. Required value: **12** Bank 2-6 directories. This droid has **13** (Finding 13.2).
- Field: Card layout. Required value: **`<bank><page>_<Label>/`**, bank `1`-`6`, page `A`-`Z`. Bank 1 is primary vocals.
- Field: Variant grouping. Required value: **Bank 1 only** -- `basename_NN.ext` groups under the text before the **first** underscore, max 25 variants, played at random without immediate repeat.
- Field: Banks 2-6 ordering. Required value: **filesystem enumeration order, unsorted.** Adding a file can renumber everything after it.
- Field: Firmware limits. Required value: `MAX_SOUNDS` **100** (Bank 1 grouped), `MAX_SD_BANKS` **20**, `MAX_FILES_PER_BANK` **100**, `MAX_ROOT_TRACKS` **255**.
- Field: Formats. Required value: **WAV, MP3, AAC, M4A** at **44.1 kHz**. `.ogg` is enumerated but **cannot be decoded**. 48 kHz plays slow.
- Field: Streams. Required value: **3 by default**, `#MAX_STREAMS` 1-10. **Stream 0 is the system-voice stream and is stolen when all are busy.**
- Field: Config file. Required value: **`CHIRP.INI`** in the card root, `#KEY value`. **`#USE_FLASH_BANK1` and `#LEGACY_MONOPHONIC` default to `0` in code despite the docs saying `1`** -- state them explicitly.
- Field: Flash sync. Required value: active Bank 1 page copied to the **14 MB** LittleFS partition; skip test is **file size only**. `CCRC` clears it and needs a reboot.
- Field: USB mass storage. Required value: **`MUSB`**, or pull **GPIO7** low. SD streams are stopped while active.
- Field: Button combos. Required value: **Prev + Play/Stop** cycles baud `115200 -> 9600 -> 2400`; **Prev + Next** cycles the Bank 1 page (reboot required). Both are spoken aloud.
- Field: Fatal indication. Required value: **three red LEDs flashing at 1 Hz = halted LittleFS mount**, not a crash. A missing SD card chirps, speaks, and **continues** in flash-only mode.
- Field: Transport on artoo-esp32. Required value: **bit-banged TX on GPIO26**, **hardware RX on GPIO35 (UART2, shared with the dome link)**. ~**1.04 ms per byte** of Core 0, per-byte critical sections.
- Field: UART arbitration. Required value: **TX always works; RX yields to the dome link.** A blocked read is `AUDIO_RX_BLOCKED_BY_DOME_UART`, never "no response".
- Field: Catalog memory. Required value: bank summary **~2.3 KB** held after first discovery; entry array **only** on `refreshCatalog()`, right-sized, capped at **300 entries / ~15.6 KB**.
- Field: Binding keys. Required value: **27 named/system slots (`chr_*`) and 12 category ranges (`chr_cat_*`)**, declared once in `include/chirp_binding_keys.h`. `sys_net_down` -> **`chr_sys_netdown`** (15-char NVS ceiling).
- Field: Device type reported. Required value: **`3`, which is not in the API's enumeration** (Finding 13.1). Do not interpret it.
- Field: Current track reported. Required value: **the last index protoArtoo sent**, not module state (Finding 13.1).
- Field: Availability. Status: **UNVERIFIED / not purchasable.** Open-hardware, GPL-3.0, no retail channel found.
- Field: Upstream. Required value: **https://github.com/joymonkey/CHIRP**, subdirectory `CHIRP_Audio_Trigger/`. Local clone `~/Documents/GitHub/CHIRP` is a **modified fork** (Section 2.5).
- Field: Hardware verification. Required value: **yes** -- sleep behaviour, shared-UART coexistence and heap behaviour are all recorded against real hardware. **But no line of Section 7 has been captured off the wire.**

If a required value cannot be proven for the board in hand, status is `UNKNOWN`
and dependent work stops.

## 15. Open Items

| # | Item | How to settle it |
| --- | --- | --- |
| 1 | **What should the Component Picker card say about a part nobody can buy?** Every other card names a purchasable product; this one is a PCB a builder has to have made. A greyed `roadmap` treatment would be wrong -- it ships and works. | An operator decision, plus #316's photograph. The card needs one honest line about provenance; this also decides what #387's image-provenance record says. |
| 2 | **Does `GMAN` actually lose its first lines on this card?** Finding 13.2, reasoned from source at both ends and never observed. It decides whether "Total tracks" can read 0 on a healthy link. | Bench, ~10 min: board on a USB-serial adapter at 9600, send `GMAN`, count `BANK:` lines against the 14 directories on the card. |
| 3 | **Should `stop()` stop one stream instead of all?** Section 8.3. The module supports `STOP:<n>`; the `AudioDriver` interface has no way to express it, so a beep kills the music bed. | A design decision tied to ADR 0054. It needs a stream concept in the interface, which only this member could use -- the reason it has not been done. |
| 4 | **Is `CHRP:` worth exposing?** A synthesised sweep needs no card, no catalog and no binding, so it is the one sound that always works -- a natural boot or error signal even on an empty card. | A product decision. Cheap to try: `CHRP:500,100,500,50` from the Controller Console. |
| 5 | **Is the soft UART's cost note stale for this backend?** Finding 13.8. Commands are 3-4x the budgeted length. | Measure with a scope or a GPIO toggle around `sendCommand()` during a catalog walk, which is the worst case. Then correct the comment, or the design. |
| 6 | **Should `MSUM` be stored and compared?** Finding 13.5. It is the module's own answer to "the card changed and your bindings now point at the wrong sounds". | A decision, then a small change: persist it beside the bindings and warn on mismatch. |
| 7 | **Should the `chr_*` / `chirp_bindings` names be capability-shaped?** Section 10.3, recorded on #175 and deliberately not ticketed. | An operator decision about whether breaking stored NVS keys and a public API field is worth paying now or never. |
| 8 | **Which Bank 1 page does this droid actually want?** Finding 13.3 only bites away from page `A`, and today the card uses `A`. A second droid personality would trip it. | Not a measurement -- a question of whether multi-personality Bank 1 is wanted. If it is, upstream change request item 2 (Section 12.4) is the fix. |

## 16. Sources

**Primary -- the product**

- **`github.com/joymonkey/CHIRP`** -- source, hardware and prebuilt firmware, GPL-3.0.
  Read this session from the local clone at `~/Documents/GitHub/CHIRP`
  (upstream `3adcc0d`, 2026-03-08; see Section 2.5 on how the clone differs).
  - `CHIRP_Audio_Trigger/Arduino_Sketches/CHIRP_Audio/serial_commands.cpp` -- the protocol
  - `.../mp3_compat.cpp` -- the MP3 Trigger shim
  - `.../file_management.cpp` -- `CHIRP.INI`, banks, variants, flash sync, voice
  - `.../serial_queue.cpp` -- the reply queue
  - `.../config.h`, `.../CHIRP_Audio.ino` -- pins, limits, boot order, the `CHIRP.INI` reference
  - `.../audio_playback.cpp`, `.../blinkies.cpp`, `.../msc_interface.cpp`
  - `.../system_audio_data.cpp` -- the 246-clip voice vocabulary
  - `.../variants/chirp_revb/pins_arduino.h`, `.../platformio.ini`
  - `CHIRP_Audio_Trigger/README.md`, repository `README.md` -- feature summary and the two-product split
  - `CHIRP_Audio_Trigger/docs/chirp-board-revb.png` -- the RevB render
- **`protoArtoo-config.md`** and **`platformio-psram-issue.md`** (local, in the clone) --
  the build contract and the cold-boot investigation
- **`sound-bank-page-detection.md`** (local, in the clone) -- this project's change
  request to the upstream author
- **The author's project announcement**, quoted on
  [#387](https://github.com/mattiasbrandt/protoArtoo/issues/387) -- the five
  design complaints, the BOM target and the RP2350 rationale

**Primary -- this project**

- `src/drivers/audio_chirp.cpp`, `include/audio_chirp.h`
- `src/drivers/audio_soft_uart_tx.h`, `src/tasks/audio_sound_member.cpp`
- `include/component_registry.inc`, `include/audio_driver.h`, `include/chirp_binding_keys.h`
- `include/config.h`, `docs/pin_map.md`
- `data/sound.js`, `src/web/api_audio.cpp`
- `docs/sound_playback.md` Section 2.2, `docs/action-registry.yaml`
- `CONTEXT.md` Flagged Ambiguities (2026-09-08) -- the naming resolution
- `CHANGELOG.md` -- the shared-UART and heap-exhaustion history
- ADR 0042 (runtime Component Member selection), ADR 0054 (a second sound lane)
- `~/Dropbox/R2-CHIRP/CHIRP-SD.zip` and its `CHIRP.INI` -- the deployed card

**Secondary -- ecosystem**

- [`dy-sv5w-sound.md`](dy-sv5w-sound.md) Section 16 -- the canonical four-member comparison
- [`mp3-trigger-sound.md`](mp3-trigger-sound.md) Sections 7-9, 13.1 -- the protocol the shim emulates
- `~/Documents/GitHub/AstroPixelsPlus/MarcduinoSound.h` -- the 25-track bank convention
- `~/Documents/Astromech/BetterDuinoFirmwareV4/src/MDuinoSound.cpp` -- the reference MP3 Trigger client
- `~/Documents/GitHub/Padawan360_mega_maestro_DYSV5W` -- a migration away from the MP3 Trigger
- `~/Documents/GitHub/ShadowMD` -- delegates sound to MarcDuino, drives no module directly

**Not found this session**

- Any vendor datasheet, user guide, schematic or product page -- **none exists**
- Any astromech.net forum thread for this product -- searched, not found
- Any retail or group-buy channel -- see Section 2.4
