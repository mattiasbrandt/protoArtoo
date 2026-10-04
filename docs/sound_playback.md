# protoArtoo — Audio System Reference

The body controller is the **sole audio source** for the droid. All audio commands —
from RC input, web API, dome serial `$` RX, or mood presets — route through the
AudioTask queue and are dispatched through a pluggable driver backend. No other
task or subsystem writes to the audio GPIO directly.

## Table of Contents

- [1. Backend Architecture](#1-backend-architecture)
- [2. Backend Details](#2-backend-details)
- [2.1 `AUDIO_SOFT_UART` - DY-SV5W Binary Frame](#21-audio_soft_uart--dy-sv5w-binary-frame)
- [2.2 `AUDIO_CHIRP` - CHIRP Audio Trigger ASCII Backend](#22-audio_chirp---chirp-audio-trigger-ascii-backend)
- [2.3 `AUDIO_MP3TRIGGER` - SparkFun MP3 Trigger](#23-audio_mp3trigger---sparkfun-mp3-trigger)
- [3. MarcDuino `$` Command Mapping](#3-marcduino--command-mapping)
- [4. Random Playback Mode](#4-random-playback-mode)
- [5. Operator Frontend Surfaces](#5-operator-frontend-surfaces)
- [6. Sources](#6-sources)

---

## 1. Backend Architecture

Sound is a **Component Family**: every image carries a driver for every
supported module, and which one runs is the **Component Member** — a runtime
setting a builder changes from the browser, staged at reboot like a component
toggle (ADR 0042). There is no rebuild and no reflash to change sound module.

Each backend implements the `AudioDriver` interface and owns all details of the
wire protocol, command format, and volume scaling for its module. The rest of
the firmware is completely agnostic to which module is physically installed —
it asks the driver what it supports and never which one it is.

`PA_AUDIO_DRIVER` keeps exactly one job: it names the module a controller that
has never been asked starts with. It no longer decides what the image can
drive.

Interface reference:

- `include/audio_driver.h` — the interface
- `include/component_registry.inc` — the one declaration of every module, with
  its operator-visible name, protocol, status and capability bits

Volume is normalised to **0–30** at the interface boundary. Each backend maps
this to its module's native range.

### Available backends

Every row below ships in every image. The three implemented modules are all
selectable at runtime; DFPlayer Mini is present in the lineup as planned, with
no driver behind it.

| Member id | Module | Protocol | Status |
|---|---|---|---|
| `dy_sv5w` | DY-SV5W | Binary frames, 9600 baud | ✅ Implemented |
| `mp3_trigger` | SparkFun MP3 Trigger | Binary, 9600 baud (community standard; factory default 38400) | ✅ Implemented — hardware validation pending |
| `chirp` | CHIRP Audio Trigger | ASCII commands, configurable baud | ✅ Implemented — TX+RX, live status queries |
| `dfplayer_mini` | DFPlayer Mini | Binary frames, 9600 baud | 🔲 Planned — no driver in the image |

To change module: wire up the new one and `POST /api/config` with
`soundMember=<member id>`. The change is saved immediately and takes effect at
the next boot. The lineup itself is `GET /api/identity/components`.

Setup's Audio control is an enable toggle plus the live driver name; it does
not choose which product is fitted. The Component Picker cards that would show
each module's photograph are still to come (#369). The photographs already
ship in the default asset set (`/<id>.webp`, including `mp3_trigger.webp`).

The `PA_AUDIO_DRIVER` values (`AUDIO_SOFT_UART`, `AUDIO_CHIRP`,
`AUDIO_MP3TRIGGER`) still name the same three modules, and still select which
one a freshly flashed controller starts on. `AUDIO_DFPLAYER` is not one of
them: naming an unbuilt module as a build's default is a build error.

---

## 2. Backend Details

### 2.1 `AUDIO_SOFT_UART` — DY-SV5W Binary Frame

**Files:** `src/drivers/audio_dy_sv5w.cpp`, `include/audio_dy_sv5w.h`

**Full protocol reference: [`docs/spec-sheets/dy-sv5w-sound.md`](spec-sheets/dy-sv5w-sound.md)** —
the source of truth for this module: every command and query frame with its
computed checksum, the DIP mode table, the electrical contract, the storage
rules, and what our own hardware has proven.

Binary command frames at 9600 baud 8-N-1, in the format
`0xAA [CMD] [LEN] [DATA...] [SM]`, where `SM` is the low 8 bits of the sum of all
preceding bytes. There is **no end marker**: the `0xAB` that ends the play-state
query `AA 01 00 AB` is that frame's checksum (`0xAA + 0x01 + 0x00`), not a footer.

Transport depends on the board, keyed on `PA_CAP_DEDICATED_AUDIO_UART`. On
artoo-esp32 all three hardware UART controllers are spoken for, so TX is an
interrupt-protected software bit-bang on `PIN_AUDIO_TX` (GPIO 26) and RX borrows
the dome link's controller through `audioUartClaim()`. Where the board has a
spare controller, audio gets it in both directions. The driver is identical on
both: the `AudioSerialIO` seam hides the difference.

Command bytes are source-verified against the module datasheet, the DYPlayer and
BetterDuino references, and 23 native tests (`test_audio_frames`,
`test_audio_io_seam`). Playback, stop and volume are confirmed on hardware
(2026-03-22).

**DY-SV5W SD card layout** (standard R2 community numbering):
files are placed in the SD root numbered sequentially (`001.mp3`, `002.mp3` …).
FAT32-formatted card required; avoid hidden files (macOS `._` files cause issues).

Important module behavior (confirmed): DY-SV5W expects contiguous numbering with
no gaps in the sequence. If a number is missing, the module's internal track
index does not align with filename intent and later files can be addressed as if
they were the missing number.

Example:

- Present: `001.mp3`, `002.mp3`, `004.mp3`
- Missing: `003.mp3`
- Result: requesting track `003` may play the file named `004.mp3`

Recommended rule: keep the root directory as strict `NNN.mp3` contiguous files
(`001`..`N`) with no skipped numbers, and copy them onto an empty card in
playing order — the module numbers tracks by the order it enumerates them, not
by their names.

Some DY-SV5W boards play from **on-board flash** rather than the card and report
device `0x02` instead of `0x01`. The driver asks (`0x09`) and never assumes; see
the spec sheet's storage section.

---

### 2.2 `AUDIO_CHIRP` — CHIRP Audio Trigger ASCII Backend

**File:** `src/drivers/audio_chirp.cpp`

The CHIRP Audio Trigger is an RP2350-based multi-stream audio board that accepts
ASCII text commands over UART. It is a significant capability step up from
single-track binary modules:

- **3+ independent simultaneous streams** (WAV, MP3, AAC)
- **Onboard flash** for Bank 1 sounds — fast access, no SD seek
- **Variant groups** — files sharing a base name are randomly selected, no repeat
- **Configurable baud rate** via `CHIRP.INI` on the SD card
- **Legacy MP3 Trigger command compatibility** built in
- **Synthesised chirp tones** (`CHRP:` command) — unique CHIRP feature

**Protocol:** ASCII commands, `\n` terminated, with ACK responses from the board.

**Baud rate:** CHIRP defaults to 115200 but is configurable. Set `#BAUD_RATE 9600`
in `CHIRP.INI` on the SD root to match the protoArtoo software UART driver. If
a hardware UART becomes available in a future revision, higher baud rates are
possible without code changes beyond the driver.

#### Setup

Required `CHIRP.INI` settings:

| Key | Required value | Purpose |
|---|---|---|
| `#BAUD_RATE` | `9600` | Must match protoArtoo soft-UART rate |
| `#BANK1_PAGE` | `A` (board default) | Selects active page for Bank 1 vocals |
| `#USE_FLASH_BANK1` | write it explicitly | Flash sync for fast Bank 1 access; leave it off if Bank 1 exceeds 14 MB |

**Write `#USE_FLASH_BANK1` out rather than relying on a default.** The CHIRP
firmware starts it at **0** (`CHIRP_Audio.ino`, `useFlashForBank1 = false`,
"Default to SD unless enabled in INI") while the comment block at the top of the
same file says the default is 1. The two disagree, so a card that does not name
the key gets SD-backed Bank 1 whatever the upstream documentation says.

Alternative baud-rate method (no SD card edit): hold **Prev** and press
**Play/Stop** on the CHIRP board to cycle 115200 → 9600 → 2400 → 115200.
That is the firmware's order (`CHIRP_Audio.ino`); the upstream README lists the
three rates in a different one.

**protoArtoo driver mapping:**

| `AudioDriver` call | CHIRP command | Notes |
|---|---|---|
| `playTrack(n)` | `PLAY:n,1,A,N\n` | Flat compatibility path (Bank 1, Page A, index n) |
| `playTrackBanked(i,b,p)` | `PLAY:i,b,p,N\n` | Bank/page/index path used by CHIRP slot bindings, `$8nn` and `/api/audio/play-banked` |
| `stop()` | `STOP\n` | Stops every stream, a Background Track's included |
| `stopStream(s)` | `STOP:s\n` | Stops one stream |
| `stopVocals()` | `STOP:s\n` for each stream but the Background Track's | Track Stop; a bare `STOP\n` when no Background Track is held |
| `setVolume(v)` | `VOL:N\n` | Every stream, a Background Track's included. N = v*99/30 |
| `setStreamVolume(s,v)` | `VOL:s,V\n` | One stream |
| `playBackgroundTrack(i,b,p,v)` | `STOP:0\n`, then `PLAY:i,b,p,V\n` | Starts a Background Track on stream 0 |
| `stopBackgroundTrack()` | `STOP:s\n` | The Background Track's stream only |

`N` is the operator's volume and `V` a Background Track's own, both scaled from
0-30 to 0-99. A vocal's `PLAY` carries `N` because the module keeps a volume per
stream: without it, a vocal landing on a stream a Background Track used would
play at the Background Track's level. Before the boot volume has been sent the
field is left off.

#### Streams and the Background Track

A **Background Track** (ADR 0054) is a track that plays *under* what else the
droid is saying, at its own volume; vocals fire over it without stopping it.
Only CHIRP can do it: its registry row is the only one declaring
`AUDIO_CAP_MIXES` (0x40), and on a module without that bit AudioTask does not
play a Background Track and logs why. The DY-SV5W and MP3 Trigger have one
stream, and their Track Stop is a full stop.

The module, not the body, chooses the stream a `PLAY` lands on: its lowest
inactive stream, or stream 0 when all of them are busy. A stream that finishes
sends nothing, and on artoo-esp32 nothing the module says is heard while the
dome link holds the shared UART. So the driver keeps, per stream, only what it
can stand behind - *idle by proof* (it sent the `STOP`, or a status reply said
idle), *maybe a vocal*, or *the Background Track* - and works by these rules
(`src/drivers/audio_chirp.cpp`, "Stream model"):

- **The Background Track goes on stream 0.** `STOP:0` is sent every time and
  takes effect before the module reads the next command, so the Background
  Track's `PLAY` lands on stream 0 whether or not a reply can be read. A vocal
  still playing on stream 0 is cut by it. One Background Track at a time: a new
  Background Track replaces the old one. No reply moves the Background Track.
  Where replies can be heard, any `ERR:` line (`ERR:PARAM` for a bad target,
  `ERR:NOFILE` for a missing file) means the Background Track did not start -
  unless a vocal went out just before the Background Track, in which case the
  error may be the vocal's and the Background Track stays held. An `S:<n>,ply`
  naming another stream is logged and that stream is treated as busy. Only a
  `STAT` reply is proof that anything plays.
- **A vocal never lands on the Background Track's stream.** Before each vocal
  `PLAY` while a Background Track is held, some other stream must be idle by
  proof; if none is, the vocal started longest ago is stopped first and the new
  one plays. The Background Track is never the one stopped. "Longest ago" is the
  order protoArtoo sent them in, so the stream stopped may already have gone
  quiet on its own.
- **Track Stop** (Bounded Audio teardown, `sound.action.track-stop`) stops every
  stream but the Background Track's. **Quiet**, Sleep Mode entry and Sound
  switched off send the bare `STOP` and end the Background Track with everything
  else. The operator's volume is the bare `VOL:N`, which moves the Background
  Track to that level too.
- **Status** asks `STAT:0`, `STAT:1`, `STAT:2` and counts the replies for the
  play state, as before: a playing Background Track keeps the module reported as
  playing, and a stream that does not answer is not idle. Because the module
  answers each query at once, a reply inside a query's own window is that
  stream's; the driver uses that to confirm or drop the Background Track, and
  stops attributing for the rest of a snapshot once any query went unanswered.
  Because that missing reply may still arrive, the next snapshot first reads and
  discards one reply window (200 ms) before it asks.

**Limit:** the module frees a stream when its file ends and says nothing, so a
Background Track that ended on its own, or one held through an error that may
have been a vocal's, is still held until a STAT says idle or the Background
Track is stopped. Meanwhile the next vocal can land on stream 0, and a Track
Stop leaves that vocal playing.

`audioQueueBackgroundTrackStart(dollar, vol)` / `audioQueueBackgroundTrackStop()`
(`include/audio_task.h`) are the entry points. A Background Track is named by a
`$` command, as a vocal is, and AudioTask reads it the same way: a Named Track
is its CHIRP binding, or its numbered track where it has none; `$8nn` is bank 8
on the page the module reported, and is not played where there is no bank 8;
any other number is that track. A numbered track plays at Bank 1, Page A, where
CHIRP plays it as a vocal (`AUDIO_FLAT_BANK` / `AUDIO_FLAT_PAGE`,
`include/audio_driver.h`). A `$` that plays nothing (`$s`, `$R`, `$+`) is
logged and not played. The volume is 0-30. While a Background Track is held, a
Track Stop leaves the droid's sound reported as playing (`audioActive` in
`/api/status`); stopping the Background Track clears it unless a vocal may
still be playing over it. A Sequence's end sends the Background Track's stop
before the vocals' Track Stop, so after a show the sound reads as stopped.

A Sequence starts and stops a Background Track with its `backgroundTrack` and
`backgroundTrackStop` steps (`docs/sequence-authoring.md`, "A Background
Track"); the run's end stops it by the Bounded Audio rule, and any abnormal end
always does. On a module that cannot mix the run logs `Background Track <$>
not played - module-cannot-mix` beside AudioTask's own refusal; with Sound
switched off it logs `component-disabled` instead.

> ⚠ **Track numbers are module-specific.** CHIRP's `PLAY:n,1,A` command plays the
> *nth entry in the Bank 1 sound manifest* (sorted by basename after variant
> grouping), not a file sequence number. The default named track values
> (scream=126, leia=151, etc.) are calibrated for DY-SV5W community SD pack
> numbering and must be re-mapped via the Sound page when using CHIRP.

CHIRP catalog operations are integrated in the backend. AudioTask can queue a catalog
refresh (`GMAN` + `GNME`) and the driver caches up to `AUDIO_CATALOG_MAX_BANKS`
(**64**) bank/page rows and `AUDIO_CATALOG_MAX_ENTRIES` (**300**) entries for web
consumption (`include/audio_driver.h`). A card with more sounds than that is
listed as far as the cap and says so: `GET /api/audio/catalog` reports
`limits.entry_cap_reached`, alongside `limits.manifest_incomplete` for bank rows
the module's reply queue dropped and `limits.missing_names` for entries that came
back unnamed. A ready catalog is usable; `complete` is what says it is also whole.

Catalog source-of-truth is the connected module response. `tasks/CHIRP-SD` remains
reference-only developer data and is not used as runtime catalog input.

#### SD card layout

- Bank 1 folder format is `1A_<droidname>` (example: `1A_R2D2`). `1` is the bank
  number, `A` is the page letter, and the remainder is a human label.
- Bank 1 variant grouping is basename-driven: `beep_01.wav`, `beep_02.wav`,
  `beep_03.wav` become one logical sound (`beep`) with random variant selection.
- Keep Bank 1 at **14 MB or less** when `#USE_FLASH_BANK1 1` is enabled; larger
  Bank 1 collections should set `#USE_FLASH_BANK1 0`.
- Recommended format for Bank 1 is WAV 44.1 kHz mono (smallest files, fast flash
  sync). MP3 stereo is fine for Banks 2–6. Files at 48 kHz play about 8% slow,
  so resample before deployment.
- Banks 2–6 use `NA_Label/` naming where `N` is bank and `A` is page, for
  example: `2A_SW-Music/`, `2B_StarWarsClips/`.

See the upstream CHIRP examples and folder conventions in the CHIRP project docs:

- https://github.com/joymonkey/CHIRP

#### Status queries

CHIRP supports live status queries at any time, including active playback. The
protoArtoo CHIRP driver queries automatically every 10 seconds, so no operator
poll action is required. On artoo-esp32 the replies arrive on the dome link's
UART controller, so the query and the catalog refresh are skipped while the
dome link holds it and the Sound page says "Held by protoR2link"; on
firebeetle2 audio has its own controller (`PA_CAP_DEDICATED_AUDIO_UART`) and
both run whatever the dome link is doing.

Reported fields include module link state (ACK-based), play state (playing when
any of the module's default three streams reports playing), Bank 1 sound count
(from `GMAN` at boot), device type and current track. The Sound page status card
auto-refreshes for CHIRP, and **shows every one of those rows** — the CHIRP
registry row declares the device-type and current-track capability bits, so
`applyCapabilityUI()` displays both.

What those two rows mean here:

- **Device type** is the constant `Flash+SD`. CHIRP has no command that reports
  its storage, and Bank 1 lives on onboard flash while Banks 2–6 live on the
  card, so the driver states that arrangement rather than querying it.
- **Current track** is the catalog entry the module says it is *playing*, not the
  last index protoArtoo asked for. It is 0 after Stop, and 0 whenever the
  reported path does not identify exactly one catalog entry — unidentified while
  playing is honest; naming a sound that already stopped is not.

#### Catalog-assisted slot mapping (Sound page)

When CHIRP catalog capability is present, the Sound page adds a CHIRP workspace that:

- refreshes and lists live catalog entries (bank/page/index/name) from the module cache
- supports single-row map/play plus bulk mode with multi-select checkboxes and map-checked action
- includes map targets for every Named/System slot plus every sound category
- shows per-row pill badges for entries already mapped to Named/System slots and category ranges
- maps an entry directly to Named/System slots (CHIRP binding path via `chr_*`)
- maps entry/selection to category ranges (`snd_cat_*` `lo..hi`) and persists category bank/page binding (`chr_cat_*`)
  when rows are from the same bank/page
- filters by bank **and** page: a `B2B` tab lists B2B's sounds only, and `All banks` lists every page
- offers `Apply suggestions` to infer category mappings from CHIRP bank directory names (`*_chatty`, `*_sad`, etc.)
  and apply them in one action, withheld while part of the listing is missing — a suggested range spans
  `lo..hi` and would otherwise claim sounds nobody listed
- names what the listing is missing (banks that did not arrive, sounds listed by index, the entry cap)
- warns beside the Named sounds table when the module's `MSUM` checksum shows the card's sound list has
  changed since the assignments were saved. Saving stays available: the builder decides what the new
  numbers should point at
- locks catalog controls during refresh and shows long-running feedback (large catalogs can take about
  1 minute), waiting on the refresh **it** asked for rather than on any catalog being ready
- enables slot-aware playback resolution in firmware: CHIRP-capable named/system slots
  prefer `PLAY:index,bank,page` when a valid binding exists and fall back to numeric `snd_*`
  tracks otherwise

`GET /api/audio/tracks` includes both `chirp_bindings` (slot mappings) and
`chirp_category_bindings` (category bank/page mappings) when catalog support is active.
Entries are omitted when no valid binding is saved.

A Named Track bound to a bank, page and index records which file the card listed
there when it was bound: a 32-bit fingerprint of the name, under a `chf_*` NVS key
beside its `chr_*` binding (`include/chirp_binding_keys.h`). Each `chirp_bindings`
entry then says `file`: `same`, `changed` or `unchecked`, compared against the catalog
as it was last refreshed (`include/audio_named_track_file.h`). A changed file is the
builder's to resolve, by binding the same address again or another one. Nothing
re-points it.

**Source:** https://github.com/joymonkey/CHIRP

---

### 2.3 `AUDIO_MP3TRIGGER` — SparkFun MP3 Trigger

**File:** `src/drivers/audio_mp3trigger.cpp`

The SparkFun MP3 Trigger (WIG-13720) is the most widely used R2-D2 sound module
in the community. BetterDuino, SHADOW_MD, and Padawan360 all treat it as their
default. It uses a VS1063 audio codec with a simple 2-byte binary serial protocol.

**Baud rate:** 9600 (community standard). Factory default is 38400, so a board
out of the box will not answer protoArtoo. Configure it by putting a file named
`MP3TRIGR.INI` in the SD card root containing one line:

```
#BAUD 9600
```

Only 2400, 9600, 19200, 31250 and 38400 are accepted. The command must start
with `#` followed by a space; only the first 512 bytes of the file are parsed,
and the first `*` character ends the command section. No firmware changes are
required — the existing 9600-baud soft-UART path is compatible. Full detail in
[`spec-sheets/mp3-trigger-sound.md`](spec-sheets/mp3-trigger-sound.md).

#### SD card layout

Files in the SD root, named `NNNxxxx.MP3` where `NNN` is a zero-padded 3-digit
prefix. The `'t'` play command matches on the NNN prefix.

Community R2 track bank assignments (source-verified: BetterDuino, SHADOW_MD):

| Tracks | Category | Named-track defaults |
|---|---|---|
| 001–025 | General sounds | — |
| 026–050 | Chatty | — |
| 051–075 | Happy | — |
| 076–100 | Sad | — |
| 101–125 | Whistle | — |
| 126–150 | Scream | `cfg_snd_scream` = 126 ✓ |
| 151–175 | Leia | `cfg_snd_leia` = 151 ✓ |
| 176–200 | Sing / music | SW theme = 177, Imperial March = 178, Cantina = 180 ✓ |
| 201–225 | Music tracks | — |
| 254 | Silent / blank | Stop workaround track |
| 255 | Startup sound | `cfg_snd_startup` = 255 ✓ |

All protoArtoo named-track NVS defaults match this layout with no remapping needed.

#### Wire protocol

| Wire command | Action | Notes |
|---|---|---|
| `'t'` + `uint8_t(N)` | Play track N by filename prefix | N = 1–255 |
| `'v'` + `uint8_t(V)` | Set volume | V: 0=loudest, 255=silent (inverted VS1063 register) |
| `'S'+'0'` | Query firmware version | Response: `=MP3 Trigger v2.NN\r\n` |
| `'S'+'1'` | Query SD track count | Response: `=NNN\r\n` (strip `=` before parsing) |
| `'O'` | Toggle play/pause | Not used directly by driver |

**protoArtoo driver mapping:**

| `AudioDriver` call | Wire command | Notes |
|---|---|---|
| `playTrack(n)` | `'t'` + `uint8_t(n)` | n must be 1–255; values outside range are dropped |
| `stop()` | `'t'` + `0xFE` (254) | Play silent blank track MP3TRIGGER_STOP_TRACK |
| `setVolume(v)` | `'v'` + nativeVol | nativeVol = (30 − v) × 64 / 30 (vendor audible range; #396) |

> \u26a0 **Stop workaround:** The MP3 Trigger has no discrete stop command.
> `stop()` plays track 254, the community-standard silent blank track
> (used identically by BetterDuino and SHADOW_MD). Ensure `254XXXX.MP3`
> exists in the SD root — all R2 community packs include it.

#### Volume scaling (VS1063 register is inverted)

The register accepts 0–255 (0 = loudest). The vendor guide's useful range is
0–64; values much above that are inaudible. protoArtoo maps the operator's
0–30 slider onto that audible span (#396):

- vol=0 → nativeVol=64 (vendor floor)
- vol=15 → nativeVol=32 (mid)
- vol=20 → nativeVol=21 (shipped default)
- vol=30 → nativeVol=0 (maximum)

#### Status queries

The driver sends `'S'+'0'` (version) and `'S'+'1'` (track count) at init and
on each operator Poll to verify the serial link and refresh total tracks.
Auto-query is off (`QUERY_SAFE_PLAYING` is not set). Response lines are
`=`-prefixed; the `=` character is stripped before parsing. Leading `'X'` /
`'x'` / `'E'` are skipped so a finish byte cannot fail a live query.

Device type cannot be queried in this protocol; `device` is always `0xFF`.
Play-state is not a query either: unsolicited `'X'` (finished), `'x'`
(cancelled) and `'E'` (missing track) update cached `playState` (0 = stop,
1 = playing). Until the first of those bytes after boot, play-state is
`unknown`. The Sound page hides the Device row, shows a manual Poll button for
S0/S1, names a missing clip from `'E'`, and warns when a saved category range
includes 254 or 255.

> **Play-state follows the board's finish byte** (#396). It is not a query, and
> it stays `unknown` until the first `'X'` / `'x'` / `'E'`. `'E'` is a missing
> file on the card, not a wiring fault. Use Poll for link and track count; do
> not poll while a clip is playing.

---

## 3. MarcDuino `$` Command Mapping

All `$` commands received by AudioTask (from dome serial RX, RC trigger, or
web API) are parsed and dispatched through the active backend. The mapping is
backend-agnostic — AudioTask calls `AudioDriver` methods; the backend handles
the wire.

| Command | Description | Driver call |
|---|---|---|
| `$nnn` | Play track number `nnn` (1-based) | `playTrack(nnn)` |
| `$S` | Play scream sound | `playTrack(cfg_snd_scream)` — NVS `snd_scream` |
| `$F` | Play short circuit / faint | `playTrack(cfg_snd_faint)` — NVS `snd_faint` |
| `$L` | Play Leia message | `playTrack(cfg_snd_leia)` — NVS `snd_leia` |
| `$c` | Play short Cantina | `playTrack(cfg_snd_cantina_s)` |
| `$C` | Play long Cantina | `playTrack(cfg_snd_cantina_l)` |
| `$W` | Play Star Wars theme | `playTrack(cfg_snd_sw_theme)` |
| `$M` | Play Imperial March | `playTrack(cfg_snd_imp_march)` |
| `$B` | Play startup sound | `playTrack(cfg_snd_startup)` |
| `$D` | Disco | `playTrack(cfg_snd_disco)` when configured |
| `$H` | Happy / greeting clip | `playTrack(cfg_snd_happy)` |
| `$R` | Enable random playback mode | AudioTask state — no driver call |
| `$O` | Disable random playback mode | AudioTask state — no driver call |
| `$s` | Stop + disable random mode | `stop()` |
| `$+` | Volume up | `setVolume(currentVol + 1)` clamped to 30 |
| `$-` | Volume down | `setVolume(currentVol - 1)` clamped to 0 |
| `$m` | Mid volume (50%) | `setVolume(15)` |
| `$f` | Max volume | `setVolume(30)` |
| `$p` | Min volume | `setVolume(0)` |

Named sound defaults follow the installed backend's SD card layout.
All named track defaults are NVS-configurable without a firmware rebuild.

For CHIRP builds, named/system slot playback is backend-aware: if a valid `chr_*` binding
exists for the slot, firmware uses `playTrackBanked(index,bank,page)`; otherwise it falls
back to numeric `snd_*` via `playTrack(n)`.

Random/category playback also consumes category bindings: when a valid `chr_cat_*` mapping
exists for the selected category, firmware uses `playTrackBanked(track,bank,page)`;
otherwise it falls back to numeric playback from `snd_cat_*`/`snd_rand_*`.

`$D` behavior in current firmware:

- `$D` is parsed and supported.
- Playback uses the configurable `snd_disco` slot.
- If `snd_disco` is `0`, `$D` resolves to no playback by design.

---

## 4. Random Playback Mode

AudioTask manages the random sound timer internally — no driver involvement.

- `$R` → start timer; fire `playTrack(random in [snd_rand_min, snd_rand_max])`
  using the current mood interval (`snd_int_quiet|mid|full|awake`)
- `$O` or `$s` → stop random mode
- Active mood (`:SE10`/`:SE11`/`:SE13`/`:SE14`) governs whether random is on
  (see `docs/goal.md §6.8`)

**NVS keys:**

| Key | Default | Description |
|---|---|---|
| `snd_rand_min` | 1 | First track in random pool |
| `snd_rand_max` | 100 | Last track in random pool |
| `snd_int_quiet` | 0 | Quiet mode interval (`:SE10`) |
| `snd_int_mid` | 30 | Mid-awake interval (`:SE13`) |
| `snd_int_full` | 20 | Full-awake interval (`:SE11`) |
| `snd_int_awake` | 10 | Awake+ interval (`:SE14`) |

---

## 5. Operator Frontend Surfaces

The audio system is operated primarily through the Sound page, with Setup used
to enable/disable the hardware path. Which sound product is fitted is a
Component Member (`soundMember`), not a Setup toggle.

### Sound page (`/sound.html`)

Primary workflows:

- Sound module status and driver capabilities
- Global controls (volume, stop, random on/off)
- Named sounds table (play and track remap)
- System sounds table (boot/mode/drive/dome event sounds)
- Category ranges and mood mapping
- Mood interval timing controls
- Direct track playback
- CHIRP catalog tools (when backend supports catalog)
- MP3 Trigger: 3.3 V jumper and `MP3TRIGR.INI` `#BAUD 9600` wiring note,
  missing-clip banner from `'E'`, category-range warning for 254/255

Implementation references:

- `data/sound.html`
- `data/sound.js`

### Configuration and Maintenance (`/configuration.html`, `/maintenance.html`)

What was the Setup page is two surfaces since #404. Audio-related controls:

- `S2 - Sound` enable/disable toggle, on Configuration
- Live driver label for S2 (the fitted member's name, not a product picker), on
  Configuration
- Sound serial state in Maintenance's serial links

The board picture on Configuration is the body controller (`artoo_pcb` /
`firebeetle2`), not the sound module.

Implementation references:

- `data/configuration.html`, `data/configuration.js`
- `data/maintenance.html`, `data/maintenance.js`

---

## 6. Sources

1. [MarcDuino Command Reference](https://www.curiousmarc.com/r2-d2/marcduino-system/marcduino-software-reference/marcduino-command-reference)
2. [BetterDuinoFirmwareV4 GitHub](https://github.com/RealNobser/BetterDuinoFirmwareV4)
3. [CHIRP Audio Trigger GitHub](https://github.com/joymonkey/CHIRP)
4. [R2D2 Sounds — Printed Droid](https://www.printed-droid.com/kb/r2d2-sounds)
5. [DY-SV5W — Arduino Forum](https://forum.arduino.cc/t/how-to-use-dy-sv5w-mp3-player/1218247)
6. [DY-SV5W spec sheet](spec-sheets/dy-sv5w-sound.md) — this project's protocol and hardware research for the DY-SV5W
7. [MP3 Trigger spec sheet](spec-sheets/mp3-trigger-sound.md) — protocol, card layout, electricals, and what the driver actually sends
8. [DFPlayer Mini spec sheet](spec-sheets/dfplayer-mini-sound.md) — the planned fourth member
