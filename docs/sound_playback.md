# protoArtoo — Audio System Reference

The body controller is the **sole audio source** for the droid. All audio commands —
from RC input, web API, dome serial `$` RX, or mood presets — route through the
AudioTask queue and are dispatched through a pluggable driver backend. No other
task or subsystem writes to the audio GPIO directly.

---

## 1. Backend Architecture

Audio hardware is selected at compile time via the `PA_AUDIO_DRIVER` build flag
in `platformio.ini`. Each backend implements the `AudioDriver` interface and owns
all details of the wire protocol, command format, and volume scaling for its
module. The rest of the firmware is completely agnostic to which module is
physically installed.

```cpp
// include/audio_driver.h
class AudioDriver {
public:
    virtual void begin() = 0;
    virtual void playTrack(uint16_t track) = 0;  // 1-based track index
    virtual void stop() = 0;
    virtual void setVolume(uint8_t vol) = 0;     // 0–30 normalised
};
```

Volume is normalised to **0–30** at the interface boundary. Each backend maps
this to its module's native range.

### Available backends

| `PA_AUDIO_DRIVER` | Backend | Protocol | Status |
|---|---|---|---|
| `AUDIO_SOFT_UART` | Software UART binary frame | Binary frames, 9600 baud | ✅ Implemented (T01) |
| `AUDIO_CHIRP` | CHIRP Audio Trigger ASCII | ASCII commands, configurable baud | ✅ Implemented (T15) — TX+RX, live status queries |
| `AUDIO_DFPLAYER` | DFPlayer Mini | Binary frames, 9600 baud | 🔲 Not yet implemented |
| `AUDIO_MP3TRIGGER` | SparkFun MP3 Trigger | Binary, 9600 baud (community standard; factory default 38400) | ✅ Implemented (T67) — hardware validation pending |

To switch backends: change `PA_AUDIO_DRIVER` in `platformio.ini`, wire up the
new module, and flash. No other firmware changes required.

---

## 2. Backend Details

### 2.1 `AUDIO_SOFT_UART` — Software UART Binary Frame

**File:** `src/drivers/audio_soft_uart.cpp`

A TX-only software bit-bang UART driver that sends binary command frames in the
format `0xAA [CMD] [LEN] [DATA...] 0xAB` at 9600 baud on `PIN_AUDIO_TX`
(GPIO 26). All three hardware UARTs are reserved on this board, so software
bit-bang is used. At 9600 baud on a 240 MHz ESP32 this is reliable.

Tested with the **DY-SV5W** voice playback module. Likely compatible with other
modules using the same binary frame protocol.

> ⚠ Command bytes in the driver are flagged for hardware validation (T09).
> Different module firmware versions may differ. Verify with a logic analyser
> or serial monitor on first power-up.

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
(`001`..`N`) with no skipped numbers.

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
| `#BANK1_PAGE` | `A` (default) | Selects active page for Bank 1 vocals |
| `#USE_FLASH_BANK1` | `1` (default) | Enable flash sync for fast Bank 1 access; disable if Bank 1 exceeds 14 MB |

Alternative baud-rate method (no SD card edit): hold **Prev** and press
**Play/Stop** on the CHIRP board to cycle 115200 → 2400 → 9600.

**protoArtoo driver mapping:**

| `AudioDriver` call | CHIRP command | Notes |
|---|---|---|
| `playTrack(n)` | `PLAY:n,1,A\n` | Bank 1, Page A, index n |
| `stop()` | `STOP\n` | Stops all active streams |
| `setVolume(v)` | `VOL:${v*99/30}\n` | Scales 0–30 → 0–99 |

> ⚠ **Track numbers are module-specific.** CHIRP's `PLAY:n,1,A` command plays the
> *nth entry in the Bank 1 sound manifest* (sorted by basename after variant
> grouping), not a file sequence number. The default named track values
> (scream=126, leia=151, etc.) are calibrated for DY-SV5W community SD pack
> numbering and must be re-mapped via the Sound page when using CHIRP.

The base driver uses stream 0 for all `$`-command playback. Multi-stream layering
and CHIRP-exclusive features (`CHRP:` chirp tones, `GMAN` manifest queries) are
beyond the `AudioDriver` base interface and are candidates for a future extension.

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

**CHIRP SD card structure:**

```
SD:/1A_R2D2/           Bank 1, Page A (primary vocals — flash-backed)
    beep_01.wav        \  variant group — one picked at random
    beep_02.wav        /
    disagree.wav         standalone sound
SD:/2A_SW-Music/       Bank 2, Page A
    ImpMarch.mp3
    Cantina.wav
SD:/3A_Effects/        Bank 3, Page A
    periscope01.mp3
```

#### Status queries

CHIRP supports live status queries at any time, including active playback. The
protoArtoo CHIRP driver queries automatically every 2 seconds, so no operator
poll action is required.

Reported fields include module link state (ACK-based), play state (idle/playing
from `STAT` response), and Bank 1 sound count (from `GMAN` at boot). The Sound
page status card auto-refreshes for CHIRP; device type and current track are
not applicable for CHIRP and are hidden.

**Source:** https://github.com/joymonkey/CHIRP

---

### 2.3 `AUDIO_MP3TRIGGER` — SparkFun MP3 Trigger

**File:** `src/drivers/audio_mp3trigger.cpp`

The SparkFun MP3 Trigger (DEV-13720) is the most widely used R2-D2 sound module
in the community. BetterDuino, SHADOW_MD, and Padawan360 all treat it as their
default. It uses a VS1053 audio codec with a simple 2-byte binary serial protocol.

**Baud rate:** 9600 (community standard). Factory default is 38400. Configure
to 9600 by placing a baud rate init file in the SD root; refer to the SparkFun
MP3 Trigger v2.4 Hookup Guide for the exact filename and format. No firmware
changes are required — the existing 9600-baud soft-UART path is compatible.

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
| `'v'` + `uint8_t(V)` | Set volume | V: 0=loudest, 255=silent (inverted VS1053 register) |
| `'S'+'0'` | Query firmware version | Response: `=MP3 Trigger v2.NN\r\n` |
| `'S'+'1'` | Query SD track count | Response: `=NNN\r\n` (strip `=` before parsing) |
| `'O'` | Toggle play/pause | Not used directly by driver |

**protoArtoo driver mapping:**

| `AudioDriver` call | Wire command | Notes |
|---|---|---|
| `playTrack(n)` | `'t'` + `uint8_t(n)` | n must be 1–255; values outside range are dropped |
| `stop()` | `'t'` + `0xFE` (254) | Play silent blank track MP3TRIGGER_STOP_TRACK |
| `setVolume(v)` | `'v'` + nativeVol | nativeVol = (30 − v) × 255 / 30 |

> \u26a0 **Stop workaround:** The MP3 Trigger has no discrete stop command.
> `stop()` plays track 254, the community-standard silent blank track
> (used identically by BetterDuino and SHADOW_MD). Ensure `254XXXX.MP3`
> exists in the SD root — all R2 community packs include it.

#### Volume scaling (VS1053 register is inverted)

- vol=0 \u2192 nativeVol=255 (silent)
- vol=15 \u2192 nativeVol=127 (mid)
- vol=30 \u2192 nativeVol=0 (maximum)

Following BetterDuino: practical audible range is approximately 0–100 on the
native scale; values above ~100 are near-inaudible but technically valid.

#### Status queries

The driver sends `'S'+'0'` (version) and `'S'+'1'` (track count) at init and
on each periodic query to verify the serial link and refresh total tracks.
Response lines are `=`-prefixed; the `=` character is stripped before parsing.

Play state and device type cannot be queried in this protocol. `AudioModuleState`
returns `playState=0xFF` and `device=0xFF` always. The Sound page hides the
Device row and shows a manual Poll button for this backend.

> \u26a0 **Play-state indicator always shows `unknown`** for the MP3 Trigger.
> Use the Track counter (cached from last `playTrack()` call) to confirm
> commands are reaching the module.

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
| `$W` | Play Star Wars theme | `playTrack(cfg_snd_sw)` |
| `$M` | Play Imperial March | `playTrack(cfg_snd_march)` |
| `$B` | Play startup sound | `playTrack(cfg_snd_startup)` |
| `$D` | Disco (not implemented — see note below) | — |
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

> **`$D` — Disco (unimplemented by design):**
> `$D` is the standard MarcDuino disco command (default track 206 in community SD packs),
> triggered by dome sequence SE09. It is intentionally omitted from protoArtoo's parser.
> The Disco sequence plays non-Star Wars music (unrelated to the R2-D2 character) and
> is not considered a fit for this build. If needed in the future, adding `$D` requires:
> a new `AUDIO_TRACK_DISCO` constant, an NVS key `snd_disco`, a `parseAudioDollar()`
> case, and a Sound page row — all small additions. The command currently arrives from
> the dome as a no-op (parser returns `AUDIO_ACTION_NONE`, silent drop).

---

## 4. Random Playback Mode

AudioTask manages the random sound timer internally — no driver involvement.

- `$R` → start timer; fire `playTrack(random in [snd_rand_min, snd_rand_max])`
  at a random interval within `[snd_interval_min_s, snd_interval_max_s]`
- `$O` or `$s` → stop random mode
- Active mood (`:SE10`/`:SE11`/`:SE13`/`:SE14`) governs whether random is on
  (see `docs/goal.md §6.8`)

**NVS keys:**

| Key | Default | Description |
|---|---|---|
| `snd_rand_min` | 1 | First track in random pool |
| `snd_rand_max` | 100 | Last track in random pool |
| `snd_interval` | 10 | Seconds between random sounds |

---

## 5. Sources

1. [MarcDuino Command Reference](https://www.curiousmarc.com/r2-d2/marcduino-system/marcduino-software-reference/marcduino-command-reference)
2. [BetterDuinoFirmwareV4 GitHub](https://github.com/RealNobser/BetterDuinoFirmwareV4)
3. [CHIRP Audio Trigger GitHub](https://github.com/joymonkey/CHIRP)
4. [R2D2 Sounds — Printed Droid](https://www.printed-droid.com/kb/r2d2-sounds)
5. [DY-SV5W — Arduino Forum](https://forum.arduino.cc/t/how-to-use-dy-sv5w-mp3-player/1218247)
