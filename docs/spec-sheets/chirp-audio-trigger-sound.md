# CHIRP Audio Trigger Spec Sheet

The **CHIRP Audio Trigger** is an open-hardware RP2350 sound board by joymonkey,
built for astromech droids: it mixes several streams at once, plays WAV, MP3 and
AAC from a microSD card and from on-board flash, answers a host with a list of
the sounds it holds, and speaks an ASCII line protocol with an MP3 Trigger
compatibility layer beside it.

Research date 2026-09-12. Unusually for this family, **the vendor document is
the source code**: there is no datasheet, no user guide and no product page.
Every command, response, default, pin and limit below was read from joymonkey's
own firmware in [`github.com/joymonkey/CHIRP`](https://github.com/joymonkey/CHIRP)
(upstream `3adcc0d`, 2026-03-08), from the RevB board render and the PlatformIO
board variant written for that firmware, from a deployed SD card image and its
`CHIRP.INI`, or from the astromech projects that drive sound boards. Claims that
could not be sourced are marked `UNKNOWN` with the artefact or bench test that
would settle them.

> [!IMPORTANT]
> **"CHIRP" names two different products by the same author, and this sheet is
> about only one of them.** Write **CHIRP Audio Trigger** in full, every time.
>
> | | What it is |
> | --- | --- |
> | **CHIRP Audio Trigger** | an RP2350 sound player -- **this sheet** |
> | **CHIRP Droid Control** | an RP2350 + ExpressLRS **body controller**, evolved from ShadyRC dEvolution |
>
> This is not a hypothetical collision. Both live in the same repository:
> upstream `origin/main` carries `CHIRP_Audio_Trigger/` and
> `CHIRP_Droid_Control/` side by side, and the repository README gives each its
> own heading. A forum post that says "CHIRP" alone could mean either.

> [!CAUTION]
> **Send ASCII commands in UPPER CASE only. Lower-case `p`, `t` and `v` are
> eaten by the MP3 Trigger compatibility layer before the ASCII parser ever
> sees them.**
>
> `processSerialCommands()` offers every byte to `checkAndHandleMp3Command()`
> first, whenever a command is not already part-built. That shim claims `'O'`,
> `'F'`, `'R'`, `'T'`, `'t'`, `'v'` and `'p'`. So `play:3` does not return
> `ERR:UNKNOWN` -- its leading `p` is consumed as *"play the track at directory
> index 3"* and a **different sound plays**. Section 6.5.
>
> The same fact closes the grammar: no future CHIRP Audio Trigger command may
> begin with any of those seven letters. `PLAY`, `STOP`, `VOL`, `CHRP`, `CCRC`,
> `GMAN`, `GNME`, `LIST`, `STAT`, `BAUD`, `BPAGE`, `MUSB` and `PVOICE` are all
> safe because none of them starts with one.

> [!NOTE]
> **You cannot buy one.** This is a one-person open-hardware project with no
> retail channel found (Section 2.4). The other sound boards the hobby uses are
> purchasable parts; this one is a PCB you have made. That is the reason this
> sheet leans on firmware source rather than a datasheet.

## 1. Scope

Covers the board and what is on it, the two-product name collision and its
evidence, availability, the electrical and resource contract, the SD-card bank
and page model, the build contract and its cold-boot fix, the serial protocol in
full including the MP3 Trigger compatibility shim, the response queue and its
ordering and loss behaviour, and how this product relates to the rest of the
hobby.

Does not cover: the RP2350 itself beyond what the board uses, the Helix decoders'
internals, the MP4/M4A parser, or CHIRP Droid Control (a body controller, out of
scope by definition -- Section 2.5).

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

No retail channel, kit, group buy or shared PCBWay project was found. What
exists is:

- the **GPL-3.0** source and hardware repository at `github.com/joymonkey/CHIRP`;
- five prebuilt UF2 images in `CHIRP_Audio_Trigger/Firmware/` (RevA `260117`,
  RevA `260117b`, RevB `260120`, RevB `260308`, plus a breadboard `proto`);
- Rev A prototypes the author had assembled by PCBWay, with acknowledged
  component-selection errors worked around in firmware and fixed in Rev B.

The author's stated BOM cost is **under USD 20**, and the RP2350 is documented by
Raspberry Pi as in production until **January 2045**. Neither is independently
verified here.

**Status: `UNVERIFIED`.** Settled by asking the author directly, or by a public
release.

### 2.5 The other product sharing the name, stated once

**CHIRP Droid Control** is a body controller: an RP2350 taking ExpressLRS/CRSF
input and driving a droid's motion, sound and lighting, with an EdgeTX Lua
script on the transmitter. Upstream's README states its goals in its own words,
including *"Send system status and audio file details to the operators radio
transmitter via ExpressLRS telemetry packets"* and *"Shouldn't require the end
user (droid wrangler) to know how to code or compile"*.

It is a droid control system, not a sound board, and it appears in this sheet
only so that a reader who meets the word "CHIRP" in a forum thread can tell which
product is being discussed.

The relationship is also why the Audio Trigger's manifest protocol looks the way
it does: `GMAN`, `GNME` and `MSUM` exist so that a droid controller can mirror a
sound list onto a transmitter screen. Any host can use the same interface.

## 3. Sources Checked

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
| `variants/chirp_revb/pins_arduino.h`, `platformio.ini` | the RevB pin map, the PSRAM arrangement and the build contract. Both were written for a PlatformIO build of the firmware during the cold-boot investigation (Section 5.3) and are not in the upstream tree at `3adcc0d` |
| the cold-boot investigation notes kept with that PlatformIO build | the build contract and the cold-boot bug (Section 5.3) |
| `system_audio_data.cpp` (table only) | the 246-clip embedded voice vocabulary |
| `docs/chirp-board-revb.png` | connectors, headers and silkscreen |
| a deployed SD card image and its `CHIRP.INI` | a card layout that runs in a droid (Section 4.5) |
| AstroPixelsPlus `MarcduinoSound.h`, BetterDuino `MDuinoSound.cpp` | the MP3 Trigger convention the shim emulates |

**Not available:** any vendor datasheet, user guide, schematic or product page --
none exists. Any astromech.net forum thread; searched and not found, so community
reception is `UNKNOWN`.

## 4. The SD card contract

This is where the product differs most from the rest of the family. The other
sound boards address a flat number; this one addresses a **bank, a page and an
index**, and the directory names on the card are part of the protocol.

### 4.1 Banks and pages

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
not PSRAM. At about 300 sounds the array alone would take some 245 KB of the
RP2350A's 520 KB SRAM, which is where raising it starts to risk exhaustion.

### 4.2 Variant groups, which is the feature the hobby has wanted

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
> rewriting a file can renumber every entry after it, and a host binding that
> stores a bank/page/index then addresses the wrong sound. Bank 1 is sorted only
> in the weak sense that grouping happens in enumeration order too. The module's
> own defence is `MSUM` (Section 6.3): a host that stores it beside its bindings
> can tell that the card changed.
>
> The legacy root-track list **is** sorted, alphabetically and case-insensitively
> (`file_management.cpp:1111-1125`). The bank lists are not.

### 4.3 Flash sync, and the 14 MB ceiling

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

A card can keep the sync for a small Bank 1 page and leave the bulk of its sounds
on Banks 2-6, which are never synced -- the layout in Section 4.5 does exactly
that.

### 4.4 Formats and sample rates

`getAudioFormat()` recognises `.wav`, `.mp3`, `.aac`, `.m4a` and `.ogg` by
extension. `.ogg` is **recognised but not decodable** -- there is no Vorbis
decoder, so it is enumerated into a bank and then fails at `startStream()`.

The engine runs a fixed **44.1 kHz** I2S output. Other rates are handled by
nearest-neighbour upsampling on the decode path, and the author's own warning is
in the sketch header: a 48 kHz file *"will be slowed ~92% and not sound good"*.
Mono is upconverted to stereo. The practical rule is **resample everything to
44.1 kHz before it goes on the card**; mono WAV for Bank 1, stereo MP3 for music.

### 4.5 A working card layout

A card deployed in a droid that plays from this board, with its `CHIRP.INI`:

| Directory | Bank/Page | Contents |
| --- | --- | --- |
| `1A_general/` | 1A | 24 general beeps (`.mp3`), **flash-synced** |
| `2A_music/` .. `2L_whistle/` | 2A-2L | 12 pages: music, alert, chatty, happy, processing, sad, sentimental, humming, scream, surprised, snarky, whistle |
| `3A_system/` | 3A | 7 system voice announcements |

`#BAUD_RATE 9600`, `#BANK1_PAGE A`, `#USE_FLASH_BANK1 1`. That is **1 Bank 1
directory + 13 Bank 2-6 directories = 14 banks**, and that number is what makes
the `GMAN` queue limit bite (Section 6.4).

Note the emergent convention: this card uses **Bank 2's pages as emotional
categories**. The firmware does not know that -- it is a card layout, not a
protocol feature -- but a host can read the categories back from the directory
names in the manifest.

## 5. Getting the wire to work

### 5.1 The link

| Property | Value |
| --- | --- |
| Signal | UART, 8N1, **3.3 V logic** |
| Module pins | `TX RX +5V G` header -- **TX GPIO4, RX GPIO5** |
| Module default baud | **115200** in the stock firmware |
| Allowed baud values | 2400, 9600, 19200, 38400, 57600, 115200 |

Cross the pair: the module's TX goes to the host's RX, the module's RX to the
host's TX, and the grounds must be common. A host that talks anything other than
115200 gets no answer from a stock module until the rate is changed by one of
the three ways in Section 5.2.

### 5.2 Three ways to set the baud rate

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

### 5.3 The build contract, and the cold-boot bug that is worth reading

For anyone rebuilding this firmware with PlatformIO, an investigation carried
out with the author reached a conclusion that is not guessable. Roughly 8 cold
power-ons in 10 came up dead -- power LED only, no NeoPixels, no USB, no UART --
while the board was completely stable once running. SD card, clock speed and
power supply were each tested and ruled out. The author could reproduce it under
PlatformIO but **not** under the Arduino IDE with the same source, which
isolated it to the build configuration. Two settings fixed it:

```ini
board = rpipico2                     ; not pimoroni_pico_plus_2
board_upload.psram_length = 8388608  ; the critical one
```

Without `psram_length`, the linker omits the PSRAM region entirely: `psram_init()`
still runs, but the TLSF heap is never set up, **every `pmalloc()` returns
`nullptr`**, and the firmware dies in audio init before TinyUSB can enumerate --
which looks exactly like a BOOTROM-level boot failure. The wrong `board` value
embeds a boot2 tuned for a different flash chip.

Also load-bearing, and all in that build's `platformio.ini` and variant:

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

## 6. The serial protocol (normative)

Read from `serial_commands.cpp`, `mp3_compat.cpp` and `serial_queue.cpp`. There
is no other specification.

### 6.1 Framing

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

### 6.2 The command table

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

**A bare `STOP` stops every stream**, a Background Track included; `STOP:<n>`
stops one. `PLAY:` takes no stream argument: the module allocates one with
`getNextAvailableStream()` -- the first inactive stream, and when all are busy it
**steals stream 0**, the stream the system voice uses. A host that wants a sound
to survive other sounds has to keep the stream count in mind.

`CHRP:` synthesises a frequency sweep in software -- a real beep with no file
behind it, so it is the one sound that plays with an empty card.

`PVOICE:` plays one clip from the **246-word voice vocabulary embedded in the
firmware** (`system_audio_data.cpp`), not from the card. The vocabulary is built
for spoken diagnostics -- `sd_card`, `not`, `detected`, `baud_rate`, `setting`,
`reboot_required`, `memory`, `voltage`, `signal`, `lost`, the numbers `0000`-`0100`
and the letters `_a`-`_z`. The board can therefore **say what is wrong out loud
inside a closed droid**, which is a genuinely unusual diagnostic channel and is
how the boot sequence reports a missing SD card.

### 6.3 The four query responses

**`GMAN`** -- the manifest summary:

```
MDAT:<bankCount>                  <- number of BANK lines that follow
BANK:<bank>,<dirName>,<count>     <- once for Bank 1, then once per Bank 2-6 dir
MSUM:<crc32>                      <- CRC32 over every filename on the card
MEND                              <- terminator
```

There is **no page field** in a `BANK:` line. A host derives the page from the
directory name: skip the leading digit, take the next alphabetic character.
`BANK:1` is the only line that carries Bank 1's sound count.

`MSUM` is a CRC32 accumulated over every Bank 1 variant filename and every Bank
2-6 filename (`CHIRP_Audio.ino`, after the bank scans). **It is the card's
fingerprint**, and exists precisely so a host can cache a catalog and notice
when the card changed.

**`GNME:<bank>,<page>,<index>`** -- one name:

```
NAME:<bank>,<page>,<index>,<filename>     <- Banks 2-6
NAME:1,,<index>,<basename>.wav            <- Bank 1: EMPTY page, forced .wav
NAME:<bank>,<page>,<index>,INVALID        <- out of range, Banks 2-6 only
```

Three traps live in those three lines:

- **Bank 1 ignores the requested page.** `handleGnme()` answers from the active
  Bank 1 page whatever page was asked for, and the reply's page field is empty.
  A host that treats an empty page as page `A` and checks the reply against its
  question rejects every Bank 1 name whenever the active page is not `A`.
- **Every Bank 1 name claims to be a `.wav`.** The reply is formatted as
  `"NAME:1,,%d,%s.wav"`: the extension is a **literal**, appended to a basename
  whose real extension was already stripped. A Bank 1 page of `.mp3` files
  reports `general01.mp3` as `general01.wav`. Nothing that plays by index
  notices; anything that matches a catalog name against a filename does.
- **A Banks 2-6 reply with a zero page is malformed.** Banks 2-6 replies are
  formatted `"NAME:%d,%c,%d,%s"` with `page == 0 ? ',' : page` as the `%c`. For
  a bare `2_Label/` directory with no page letter, which `scanSDBanks()`
  accepts, the character emitted **is a comma**: `NAME:2,,,3,file`, an extra
  empty field that shifts everything right. A parser that reads the Bank 1
  empty-page form first can bind the name to `,3,file`. A card whose every
  directory carries a page letter never produces it.

An out-of-range **Bank 1** index produces **no reply at all** -- the handler
returns silently -- so a host must rely on its own timeout.

**`STAT:<stream>`** -- one stream's state, written **directly** to the port:

```
STAT:playing,<filename>,<volume 0-99>
STAT:idle,,0
```

`STAT:` answers during playback without disturbing it, so a host can poll it
while sound plays.

**`LIST`** -- a human block, also written directly, and **not** a full listing
despite the README's description: it prints the Bank 1 count (a `Sounds: <n>`
line), then at most the **first ten** entries with a `... and N more` line, then
one line per Bank 2-6 directory. It is a console convenience, not a machine
interface. Use `GMAN` and `GNME`.

### 6.4 The response queue, and why ordering is not what you expect

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
   host timeout must be generous.
3. **A long reply can lose its own beginning.** `handleGman()` emits
   `sdBankCount + 4` messages -- `MDAT`, one `BANK:` for Bank 1, one per Bank 2-6
   directory, `MSUM`, `MEND` -- in one uninterrupted burst before `loop()` gets
   a chance to drain any of them. Past 15 the oldest are silently discarded, and
   `MDAT` and `BANK:1` are the first two to go. The threshold is **12 Bank 2-6
   directories**: a card with 13, like the one in Section 4.5, emits 17 messages
   and loses both. The reply then carries no Bank 1 count at all. `LIST` is
   written straight to the UART, so its `Sounds: <n>` line still carries the
   count.

The third rule is reasoned from the firmware source; it has not been captured
off the wire (Open Item 1). The fix at the source is a drain inside the `GMAN`
loop or a larger `SERIAL2_QUEUE_SIZE`; on the card side it is fewer Bank 2-6
directories.

### 6.5 The MP3 Trigger compatibility layer

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
never answers. A builder debugging a mixed setup will meet it.

> [!CAUTION]
> **The shim consumes its argument byte even when it rejects the command.**
> `'T'` followed by a non-digit returns false, so the `'T'` is appended to the
> ASCII command buffer -- but the second byte was already read and thrown away.
> A host that sends `TEST\n` loses the `E`, and the module then tries to parse
> `TST`.

### 6.6 Boot behaviour and timing

`setup()` runs a fixed **1500 ms settle** before touching anything, then: LEDs,
`Serial2`, SPI1, SD (three attempts, 25 MHz then 4 MHz each, with a CS pulse and
500 ms between attempts), LittleFS (three attempts), `CHIRP.INI`, a `Serial2`
re-open at the configured baud, audio allocation, decoder allocation, Bank 1
scan, flash sync, Bank 2-6 scan, checksum, root-track scan, then unmute and a
100 ms DMA prime.

Boot is therefore **at least ~1.7 s and realistically several seconds**, longer
on the first boot after a card change because the flash sync runs and narrates
itself. A host should wait before its first query, and longer after a card
change.

Failure behaviour differs by subsystem, and the distinction matters:

| Failure | Behaviour |
| --- | --- |
| SD card missing | error chirps, speaks *"SD card not detected"*, **then continues** in flash-only mode |
| LittleFS mount failure | error sequence, then **halts forever** with red LEDs |
| PSRAM allocation failure | logs, leaves that decoder null, continues with fewer streams |
| Firmware version changed | speaks the new version aloud at boot |

**A silent board with three red LEDs flashing is a halted flash mount, not a
crash.** The firmware flashes them deliberately so the freeze is legible.

### 6.7 The LED vocabulary

Three NeoPixels at brightness 20, run from `blinkies.cpp`:

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

### 6.8 `CHIRP.INI`, and two defaults that are documented wrongly

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
> triggers that mix instead of interrupting. **State both keys explicitly rather
> than relying on either default.**

`#STREAM_BUFFER_SIZE` is rounded **down to a power of two** (minimum 32 KB,
capped at 4096 KB) because the ring buffer wraps with a bitmask.

If `#BANK1_PAGE` is missing, `parseIniFile()` **rewrites the whole file** with
the values currently in memory -- so a hand-written minimal INI comes back
expanded with the code defaults baked in. `#VERSION` is the firmware's own
record of what booted last; changing it is what triggers the spoken
firmware-update announcement.

## 7. How the hobby got here (non-normative)

Everything in this section is evidence of practice. None of it is normative.

### 7.1 It exists because of five specific complaints

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
answer. Point 3 is answered by a ~USD 1 MCU doing the decoding in software --
the author credits an earlier collaborator for showing that an ESP32 could decode
MP3 with a DAC and no codec chip.

### 7.2 The shim is a statement about the ecosystem

`mp3_compat.cpp` is a deliberate MP3 Trigger emulator on 2026-era hardware. The
reason is compatibility with the installed base: **PADAWAN, SHADOW and ShadyRC
all speak that protocol**, so a droid already wired for one can drop this board
in and keep its host firmware.

The convention it emulates is worth stating because it is what the rest of the
family assumes:

- **`'t'` + a binary byte** plays the file whose name starts with those three
  digits. Verified in BetterDuino's `MDuinoSound.cpp` and in commented-out
  Padawan360 code.
- **`'v'` + a binary byte is inverted** -- `0` loudest, `255` silent -- because
  it is a VS10xx register. This board converts it with
  `gain = 1.0f - sfVol / 255.0f` and applies it to every stream.
- **Sound numbers are banked in 25s.** AstroPixelsPlus's `MarcduinoSound.h`
  states the canonical table: *"Bank 1: gen sounds, numbered 001 to 025; Bank 2:
  chat sounds, numbered 026 to 050..."* through nine banks. Every R2 sound pack
  ships in that shape, which is why changing a filename breaks a droid.
- **There is no stop command**, so the hobby stops sound by playing a silent
  track. This board uses **254**; AstroPixelsPlus uses **252**
  (`MP3_EMPTY_SOUND`). Do not assume either is universal.

Note what the bank convention and this product's bank model are **not**: the
MarcDuino bank is a *number range inside a flat 255*, while a CHIRP Audio
Trigger bank is a *directory*. They share a word and nothing else. A host whose
sound numbers follow the MarcDuino banks (scream 126, Leia 151) has to map each
of them to a bank, page and index before this board plays the same sound.

### 7.3 The rest of the field, briefly

- **ShadowMD** drives no sound module directly; it delegates to a MarcDuino over
  `$8x\r`, so the player sits behind that board.
- **A Padawan360 port** has migrated *away* from the MP3 Trigger to a DY-SV5W,
  with the old calls surviving as comments.
- **AstroPixelsPlus / Reeltwo** abstracts three players -- MP3 Trigger, DFPlayer
  Mini and the HCR Vocalizer -- behind one `Stream&`.
- **None of them queries the player for a track count, a current track or a file
  list.** That is the gap this product was built to fill.

## 8. Quick Reference

- Field: Product name. Value: **CHIRP Audio Trigger**, in full. **Never bare "CHIRP"** -- that also names **CHIRP Droid Control**, a body controller by the same author.
- Field: MCU. Value: **RP2350A** at **250 MHz**, 16 MB flash (2 MB sketch + 14 MB LittleFS), **8 MB PSRAM on GPIO0** (Rev B). Rev A is **2 MB** PSRAM on **GPIO8**.
- Field: DAC. Value: **PCM5102A**, line level. **No on-board amplifier.**
- Field: Power. Value: **USB-C or +5 V screw pads**, chosen by the `POWER SELECT` jumper.
- Field: Host link. Value: UART **8N1, 3.3 V**, module **TX GPIO4 / RX GPIO5** on the `TX RX +5V G` header.
- Field: Baud. Value: the firmware default is **115200**. Change it with `#BAUD_RATE` in `CHIRP.INI`, the `BAUD:` command, or **Prev + Play/Stop**.
- Field: Allowed baud values. Value: **2400, 9600, 19200, 38400, 57600, 115200**. Nothing else.
- Field: Command framing. Value: ASCII, **UPPER CASE**, `:` before arguments, `,` between them, `\n` terminated, 128-byte buffer.
- Field: Case sensitivity. Value: **commands are case-sensitive and lower-case `p`, `t`, `v` are intercepted by the MP3 Trigger shim** before the parser. `play:3` plays a sound.
- Field: Reserved first bytes. Value: **`O` `F` `R` `T` `t` `v` `p`** belong to the legacy shim. No command may start with one.
- Field: Play. Value: **`PLAY:<index>[,<bank>[,<page>[,<vol>]]]`**, bank default 1, page default `A`, vol default unchanged.
- Field: Stop. Value: **`STOP`** stops **every stream**. `STOP:<n>` stops one.
- Field: Volume. Value: **0-99 ascending, 99 loudest.** `VOL:<v>` for every stream, `VOL:<stream>,<v>` for one.
- Field: Manifest. Value: **`GMAN`** -> `MDAT:<n>`, `BANK:<bank>,<dir>,<count>` xN, `MSUM:<crc32>`, `MEND`. **No page field in a `BANK:` line** -- derive it from the directory name.
- Field: Name lookup. Value: **`GNME:<bank>,<page>,<index>`** -> `NAME:<bank>,<page>,<index>,<file>`. **Bank 1 answers with an EMPTY page field, ignores the requested page, and always appends `.wav`.**
- Field: Status. Value: **`STAT:<stream>`** -> `STAT:playing,<file>,<vol>` or `STAT:idle,,0`. **Written directly, bypassing the reply queue.** Safe to send during playback.
- Field: Reply ordering. Value: **not guaranteed.** `ERR:`, `STAT:` and `LIST` go straight out; `PACK:`, `BANK:`, `NAME:`, `MSUM:`, `MEND` are queued. Match on prefix, never on order.
- Field: Reply queue depth. Value: **15 usable** of `SERIAL2_QUEUE_SIZE = 16`. Overflow **drops the oldest**. `GMAN` emits `sdBankCount + 4`.
- Field: Bank directory limit before `GMAN` truncates. Value: **12** Bank 2-6 directories. Past it, `MDAT` and `BANK:1` are lost first.
- Field: Card fingerprint. Value: **`MSUM`**, a CRC32 over every filename on the card. Changes when a file is added, removed or renamed.
- Field: Card layout. Value: **`<bank><page>_<Label>/`**, bank `1`-`6`, page `A`-`Z`. Bank 1 is primary vocals.
- Field: Variant grouping. Value: **Bank 1 only** -- `basename_NN.ext` groups under the text before the **first** underscore, max 25 variants, played at random without immediate repeat.
- Field: Banks 2-6 ordering. Value: **filesystem enumeration order, unsorted.** Adding a file can renumber everything after it.
- Field: Firmware limits. Value: `MAX_SOUNDS` **100** (Bank 1 grouped), `MAX_SD_BANKS` **20**, `MAX_FILES_PER_BANK` **100**, `MAX_ROOT_TRACKS` **255**.
- Field: Formats. Value: **WAV, MP3, AAC, M4A** at **44.1 kHz**. `.ogg` is enumerated but **cannot be decoded**. 48 kHz plays slow.
- Field: Streams. Value: **3 by default**, `#MAX_STREAMS` 1-10. **Stream 0 is the system-voice stream and is stolen when all are busy.**
- Field: Config file. Value: **`CHIRP.INI`** in the card root, `#KEY value`. **`#USE_FLASH_BANK1` and `#LEGACY_MONOPHONIC` default to `0` in code despite the docs saying `1`** -- state them explicitly.
- Field: Flash sync. Value: active Bank 1 page copied to the **14 MB** LittleFS partition; skip test is **file size only**. `CCRC` clears it and needs a reboot.
- Field: USB mass storage. Value: **`MUSB`**, or pull **GPIO7** low. SD streams are stopped while active.
- Field: Button combos. Value: **Prev + Play/Stop** cycles baud `115200 -> 9600 -> 2400`; **Prev + Next** cycles the Bank 1 page (reboot required). Both are spoken aloud.
- Field: Fatal indication. Value: **three red LEDs flashing at 1 Hz = halted LittleFS mount**, not a crash. A missing SD card chirps, speaks, and **continues** in flash-only mode.
- Field: PlatformIO cold boot. Value: **`board = rpipico2`** and **`board_upload.psram_length = 8388608`**; without the second, every `pmalloc()` returns `nullptr` and roughly 8 cold boots in 10 come up dead.
- Field: Availability. Status: **UNVERIFIED / not purchasable.** Open-hardware, GPL-3.0, no retail channel found.
- Field: Upstream. Value: **https://github.com/joymonkey/CHIRP**, subdirectory `CHIRP_Audio_Trigger/`.
- Field: Wire capture. Status: **UNKNOWN** -- no line of Section 6 has been captured off the wire; it is all read from source.

## 9. Open Items

| # | Item | How to settle it |
| --- | --- | --- |
| 1 | **Does `GMAN` actually lose its first lines on a card with 13 or more Bank 2-6 directories?** Section 6.4, reasoned from source and never observed on the wire. | Bench, ~10 min: board on a USB-serial adapter, send `GMAN`, count `BANK:` lines against the directories on the card. |
| 2 | **Is the board for sale anywhere?** Section 2.4. | Ask the author, or watch the repository for a public release. |

## 10. Sources

**Primary -- the product**

- **`github.com/joymonkey/CHIRP`** -- source, hardware and prebuilt firmware, GPL-3.0,
  read at upstream `3adcc0d` (2026-03-08).
  - `CHIRP_Audio_Trigger/Arduino_Sketches/CHIRP_Audio/serial_commands.cpp` -- the protocol
  - `.../mp3_compat.cpp` -- the MP3 Trigger shim
  - `.../file_management.cpp` -- `CHIRP.INI`, banks, variants, flash sync, voice
  - `.../serial_queue.cpp` -- the reply queue
  - `.../config.h`, `.../CHIRP_Audio.ino` -- pins, limits, boot order, the `CHIRP.INI` reference
  - `.../audio_playback.cpp`, `.../blinkies.cpp`, `.../msc_interface.cpp`
  - `.../system_audio_data.cpp` -- the 246-clip voice vocabulary
  - `CHIRP_Audio_Trigger/README.md`, repository `README.md` -- feature summary and the two-product split
  - `CHIRP_Audio_Trigger/docs/chirp-board-revb.png` -- the RevB render
- **The PlatformIO build of the firmware** -- `platformio.ini`,
  `variants/chirp_revb/pins_arduino.h` and the cold-boot investigation notes kept
  with them (Section 5.3); not in upstream at `3adcc0d`
- **The author's project announcement** -- the five design complaints, the BOM
  target and the RP2350 rationale (Section 7.1)
- **A deployed SD card image and its `CHIRP.INI`** -- the card layout in Section 4.5

**Secondary -- ecosystem**

- [`mp3-trigger-sound.md`](mp3-trigger-sound.md), "The serial protocol (normative)",
  "Track numbering, which is this module's real advantage" and "CHIRP implements
  this protocol on purpose" -- the protocol the shim emulates
- AstroPixelsPlus `MarcduinoSound.h` -- the 25-track bank convention
- BetterDuino `MDuinoSound.cpp` -- the reference MP3 Trigger client
- Padawan360 DY-SV5W port -- a migration away from the MP3 Trigger
- ShadowMD -- delegates sound to MarcDuino, drives no module directly

**Not found**

- Any vendor datasheet, user guide, schematic or product page -- **none exists**
- Any astromech.net forum thread for this product -- searched, not found
- Any retail or group-buy channel -- see Section 2.4
