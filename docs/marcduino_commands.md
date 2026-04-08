### MarcDuino Commands Overview

### protoArtoo Sync Contract (Dome <-> Body)

This repository uses a bounded Marcduino subset for synchronized dome/body
behavior. The dome (AstroPixelsPlus fork) runs full-droid `:SE01-:SE16`
animations and sends body-side subcommands over the slip-ring serial link.

Current body-side handling status in protoArtoo:

| Command family | Body status | Notes |
|---------|---------|---------|
| `:OP`, `:CL`, `:MV` | Implemented | Parsed in `src/drivers/dome_rx_parser.cpp`; routed to `ServoTask`. |
| `:SE30-:SE36` | Implemented | Direct body sequence IDs in `handleSequenceCommand()`. |
| `:SE01-:SE09` | Implemented | Decomposed via `marcduino_full_droid_body_actions()` into local audio + body sequence, no dome TX echo on this RX path. |
| `:SE10`, `:SE11`, `:SE13`, `:SE14` | Implemented (mood path) | Intercepted in `src/tasks/dome_link.cpp` via `applyMood()` before `parseMarcduinoCommand()`. |
| `:SE15`, `:SE16` | Implemented | Decomposed in `handleSequenceCommand()` using `marcduino_full_droid_body_actions()`. |
| `$...` | Implemented | Routed to `audioQueueDollar()` and parsed by `src/tasks/audio_dollar_parser.cpp`. |
| `$D` | Implemented | Uses NVS-backed `snd_disco` (`cfg_snd_disco`); track `0` disables playback. |
| `#APSL`, `#APWU` | Implemented | Parsed in `src/drivers/dome_rx_parser.cpp`; updates sleep state and broadcasts status. |
| `#PAHB`, `#APHB` | Implemented | `#PAHB` heartbeat TX and `#APHB` heartbeat RX/ack handled in `src/tasks/dome_link.cpp`. |
| `*`, `@`, `%`, `&`, `!` | Intentionally ignored on body | Dome-only or unsupported in this topology. |

Source of truth for coordinated behavior:

- Dome sequence implementation: `mattiasbrandt/AstroPixelsPlus` (`MarcduinoSequence.h`)
- Body routing contract: `docs/goal.md` Section 2
- Body parser: `src/drivers/dome_rx_parser.cpp`
- Dome link transport and heartbeat: `src/tasks/dome_link.cpp`

Practical rule: when adding/changing synchronized sequences in the dome fork,
keep the body payload decomposition explicit (`$...`, `:SE3x`, `:OP/:CL/:MV`)
and update this table as part of the same implementation change.

MarcDuino is a control system for astromech droids (e.g., R2-D2 replicas), handling panels, lights, sounds, and holoprojectors via Arduino-based boards (Master/Slave setup). Commands are ASCII strings prefixed by specific characters and terminated by carriage return (`\r`). Firmware versions (e.g., V1.6 Master, V1.5 Slave, V3) vary slightly, with V3 adding dynamic configuration like servo reversal and MP3 player selection.

#### Prefixes
- `:` - Panel commands (processed by Master, some forwarded to Slave).
- `*` - Holo Projector/Magic Panel commands (forwarded to Slave).
- `@` - Display commands (forwarded to Teeces/JawaLite on Slave).
- `$` - Sound commands (processed by Master).
- `!` - Custom extension 1 (to sound output).
- `%` - Custom extension 2 (to Slave Out port).
- `&` - I2C commands (V2+ firmware).
- `#` - Setup commands (V3 firmware for configuration).

#### Panel Commands (`:` Prefix)
Format: `:CCxx\r` (CC = command, xx = 00-99; 00 = all).

| Command | Description |
|---------|-------------|
| `:SExx` | Launch sequence xx (see sequences below). |
| `:OPxx` | Open panel(s) xx (01-13; 00=all, 14=top, 15=bottom). |
| `:CLxx` | Close panel(s) xx (slow for 00). |
| `:LKxx` | Lock panel(s) xx at current position. |
| `:ULxx` | Unlock panel(s) xx. |
| `:RCxx` | RC control for panel(s) xx. |
| `:STxx` | Soft-hold (remove RC, shut servo off). |
| `:HDxx` | Hold (remove RC, keep position). |
| `:MVxxdddd` | Move panel xx to dddd degrees (0000-0180) or microseconds (>0544). |

##### Sequences (`:SExx`)
- 00: Close all (init).
- 01: Scream (all open).
- 02: Wave (one-by-one).
- 03: Fast wave (smirk).
- 04: Wave 2 (progressive open/close).
- 05: Beep cantina (marching ants).
- 06: Faint/short circuit.
- 07: Cantina dance.
- 08: Leia.
- 09: Disco.
- 10: Quiet reset.
- 11: Full awake reset.
- 12: Top panels to RC.
- 13: Mid awake reset.
- 14: Awake+ reset.
- 15: Screams (no panels).
- 16: Panel wiggle.
- 30-36: Body sequences (e.g., utility arms, doors).
- 51-57: Panel-only variants of 01-07.

#### Holo Projector Commands (`*` Prefix)
Format: `*CCxx\r` (xx=00-99; 99=permanent).

| Command | Description |
|---------|-------------|
| `*RDxx` | Random movement (xx=01-03; 00=all). |
| `*ONxx` | Light on (xx=01-03; 00=all). |
| `*OFxx` | Light off. |
| `*ONxxaaabbbcccddd` | RGB light on (aaa=R, bbb=G, ccc=B, ddd=brightness). |
| `*COxxaaabbbcccddd` | Set RGB color. |
| `*RCxx` | RC vertical control. |
| `*TExx` | Movement test loop. |
| `*STxx` | Reset (stop movement, lights off). |
| `*HDxx` | Hold (stop movement, lights stay). |
| `*MOxx` | Magic panel on (xx seconds). |
| `*MFxx` | Magic panel flicker. |
| `*Hzxx` / `*Fzxx` | Timed on/flicker for holo z (0=all). |
| `*Hzxxaaabbbcccddd` | RGB timed on. |
| `*Fzxxaaabbbcccddd` | RGB timed flicker. |
| `*CHxx` | Center holo xx. |

#### Display Commands (`@` Prefix)
Format: `@xxYzzz\r` (xx=address: 0=all, 1-3=logics, 4-5=PSI).

| Command | Description |
|---------|-------------|
| `xT0` | Test (all on). |
| `xT1` | Normal random. |
| `xT2-5` | Flash/alarm/red alert. |
| `xT6` | Leia. |
| `xT11` | March. |
| `xT92` | Spectrum bar-graph. |
| `xMmessage` | Set text (upper-case). |
| `xP60/61` | Latin/Aurabesh font. |
| `xT20` | Turn off. |
| `xRx` | Random style (0-6). |
| `xSx` | PSI state (0-4). |

#### Sound Commands (`$` Prefix)

| Command | Description |
|---------|-------------|
| `$xy(y)` | Play bank x, sound y(y) (or next). |
| `$R` | Random from banks 1-5. |
| `$O` / `$s` | Off/stop. |
| `$+/-` / `$m` / `$f` / `$p` | Volume up/down/mid/max/min. |
| `$S/F/L/c/W/M/C/D` | Special sounds (scream, faint, Leia, cantina, etc.). |
| `$B` | Startup sound. |

#### Setup Commands (`#` Prefix, V3 only)
- `#SD00/01`: Normal/reverse all servos.
- `#SRxxy`: Individual servo xx direction y (0/1).
- `#SQ00/01`: Chatty/silent mode.
- `#SS00-03`: Startup sound options.
- `#SM00/01`: MP3 Trigger/DF Player Mini.

#### I2C Commands (`&` Prefix, V2+)
Format: `&addr,data,...\r` (data: decimal, xhex, 'char, "string).

#### Custom Extensions
- `!text\r`: To sound output.
- `%text\r`: To Slave Out.

---

### BetterDuino Commands Overview

BetterDuino is an enhanced MarcDuino variant with additional features like RGB holo support and body sequences. It supports up to 13 panels, maintains MarcDuino compatibility where possible, but omits some features (e.g., RC control on slave, I2C chaining). Commands are routed via Master to Slave.

#### Unsupported MarcDuino Features
- RC/hold for holos/panels on Slave.
- Some sequences (e.g., 12,14,15).
- I2C commands not forwarded.
- Limited sound banks.

#### Panel Commands (`:` Prefix)
Similar to MarcDuino, with additions like `:LKxx` (lock), `:ULxx` (unlock), `:MVxxdddd` (move to position).

#### Sequence Commands (`:SExx`)
Extends MarcDuino with body-focused (30-36) and panel-only (51-58) variants.

#### Holo Projector Commands (`*` Prefix)
Adds RGB support (`*ONxxaaabbbcccddd`, `*COxxaaabbbcccddd`) and centering (`*CHxx`).

#### Sound Commands (`$` Prefix)
Similar to MarcDuino, with additions like `$p` (min volume), `$B` (startup), and beep variants (`$w/i`).

#### Display Commands (`@` Prefix)
Forwarded to Teeces; same as MarcDuino.

#### Setup Commands (`#` Prefix)
Processed by both Master/Slave (EEPROM storage).

---

### ReelTwo Commands Overview

ReelTwo is an Arduino library (C++/ESP32) for modular astromech control, focusing on components like servos, holos, logics. It uses classes/methods rather than prefixed strings, but supports MarcDuino-compatible serial commands in extensions (e.g., AstroPixelsPlus). Includes SMQ (serial messaging) for networked control.

#### Key Classes/API
- **ServoDispatch**: `moveServosTo(mask, start, end, duration)` - Group servo moves.
- **HoloLights**: Assign servos, process commands like "HPF0026|20".
- **LogicEngine**: `selectScrollTextLeft(text, color, speed, duration)` - Text animations.
- **AnimationPlayer**: `ANIMATION_PLAY_ONCE(player, sequence)` - Play effects.
- **CommandEvent**: `process("CMD")` - Send strings (e.g., "$08" for sound).
- **StealthCommand**: Send to external (e.g., "tmpvol=100,15").

#### Supported Serial Commands (via AstroPixelsPlus)
- Configuration: `#APWIFI[0|1]`, `#APREMOTE[0|1]`, `#APZERO`, `#APRESTART`.
- Logics: `@xTy` (e.g., `@0T1` normal, `@0T3` alarm).
- Text: `@xMtext` (e.g., `@1MHello`).
- Fonts: `@xP60/61` (Latin/Aurabesh).
- Panels: `:CL00`, `:OP00`, `:OF00`, etc.
- Holos: `*ON01`, `*RD01`, `*HPS301` (pulse), `*HP001` (position), `*HN01` (nod).
- AstroPixels-Specific: `@APLELEECSNN` (e.g., `@APLE51000` solid red).

#### Usage
- Include `<ReelTwo.h>`.
- Define components in setup.
- Process animations in loop: `AnimatedEvent::process()`.

### Sources
1. [MarcDuino Command Reference](https://www.curiousmarc.com/r2-d2/marcduino-system/marcduino-software-reference/marcduino-command-reference)
2. [MarcDuino V3 Firmware Commands](https://www.printed-droid.com/kb/marcduino-v3-firmware-commands)
3. [BetterDuino Firmware V4](https://www.printed-droid.com/kb/betterduino-firmware-v4)
4. [ReelTwo Documentation](https://reeltwo.github.io/Reeltwo/html/index.html)
5. [ReelTwo GitHub](https://github.com/reeltwo/Reeltwo)
6. [AstroPixelsPlus GitHub](https://github.com/reeltwo/AstroPixelsPlus)
