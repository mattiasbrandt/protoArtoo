# protoArtoo — ESP32 Body Controller Firmware

**Project:** protoArtoo
**Repository:** `protoArtoo` (GitHub, to be created)
**Target hardware:** Artoo Controller PCB v1.1 (ESP32 D1 Mini) — body controller
**Goal:** Open-source ESP32 firmware for hoverboard-driven MK4 astromech droid
**Protocol standard:** Marcduino command prefix routing
**Dome firmware:** `mattiasbrandt/AstroPixelsPlus` (fork of `reeltwo/AstroPixelsPlus`)
**Hoverboard firmware:** Compatible 8-byte UART protocol — `EFeru/hoverboard-firmware-hack-FOC` (STM32) or `RoboDurden/Hoverboard-Firmware-Hack-Gen2.x-GD32` (GD32)

---

## ⚠️ HOW THIS DIFFERS FROM THE CLASSIC MARCDUINO SETUP

> **Read this first if you have prior experience with the standard MarcDuino / SHADOW R2 build.**
> protoArtoo deliberately departs from the classic topology in several important ways.

### Classic vs protoArtoo — Side-by-Side

| Aspect | Classic MarcDuino Setup | protoArtoo |
|--------|------------------------|------------|
| **Body controller** | MarcDuino Body Master (ATmega328P) | Artoo Controller PCB v1.1 (ESP32 D1 Mini, 240 MHz) |
| **Dome controller** | MarcDuino Dome Master (ATmega328P) | AstroPixelsPlus ESP32 |
| **Dome slave** | Separate MarcDuino Slave (required) | Not used — AstroPixelsPlus handles everything internally |
| **Serial topology** | Body TX → Dome Master RX (one direction) | **Bidirectional** — body ↔ dome TX+RX over slip ring |
| **Sound authority** | Dome holds MP3 Trigger / DFPlayer | **Body holds audio module** — sole audio source for droid |
| **`$` sound routing** | Dome Master receives `$`, plays locally | Body receives `$` from dome RX, plays on body audio module |
| **Drive system** | Vex / JAW motors via Sabertooth | Hoverboard motors via Gen2.x UART protocol |
| **RC input** | PS2 via USB host (SHADOW) | Selectable receiver mode: Standard PWM, Single SBUS, or Dual SBUS |
| **Control interface** | PS2 controller or Android SHADOW app | RC transmitter + any phone browser via WiFi AP |
| **Dome command source** | SHADOW → Body Master → Dome Master | protoArtoo generates all Marcduino TX directly |
| **Slave board** | Required, daisy-chained | Not applicable — no slave board |
| **WiFi** | SHADOW uses WiFi, body/dome serial separate | WiFi native to both ESP32 boards |
| **OTA updates** | Requires physical FTDI cable | Built in to both body and dome firmware |
| **Library dependency** | Reeltwo for Dome Master dispatch | Reeltwo in dome only; body uses plain serial parsing |

### Why These Differences Matter for Code and Configuration

1. **No dome sound module** — the dome's `PREFERENCE_MARCSOUND` must be set to disabled/none. Any web UI or code assuming a DFPlayer or MP3 Trigger in the dome does not apply here.

2. **No MarcDuino Slave board** — commands like `*HP00` go to AstroPixelsPlus directly. Do not wire a slave board.

3. **Body ↔ dome serial is full-duplex** — the dome sends commands TO the body, not just receives them. This is the largest departure from upstream AstroPixelsPlus (see Section 3).

4. **The body is the primary controller** — in the classic setup, the Body Master was a relay for SHADOW. In protoArtoo, the body ESP32 generates all commands from RC + web API + its own behaviour logic.

---

## 1. Context & Motivation

The Artoo Controller board from artoo.uk is a custom PCB (v1.1) built around an **ESP32 D1 Mini** module (confirmed from board photo — see `tasks/artoo_board_v1.1.png`). The vendor's website describes the platform as "open-source" but the ESP32 firmware binary is distributed without source. Since all peripherals are standard (UART, PWM, SBUS), protoArtoo is a complete firmware replacement written from scratch — fully owned, auditable, and extensible.

### System Architecture

```
┌──────────────────────────────────────┐         ┌────────────────────────────────────────┐
│          protoArtoo (body)           │         │    mattiasbrandt/AstroPixelsPlus       │
│       Artoo Controller PCB           │         │         (dome)                         │
│      ESP32 D1 Mini                   │  TX ──→ │                                        │
│                                      │         │  • Dome lights (NeoPixel/FastLED)      │
│  • Drive (hoverboard Gen2.x UART)    │  RX ←── │  • Dome panel servos (PCA9685)         │
│  • RC input (PWM / single SBUS / dual SBUS) │ 9600 │  • Holoprojectors                      │
│  • Utility arm servos                │  baud   │  • Logic displays + PSI                │
│  • Audio module ← SOLE AUDIO        │ slip    │  • WiFi AP+STA                         │
│  • Dome motor ESC                    │  ring   │  • Async web UI + REST API             │
│  • WiFi AP+STA                       │         │                                        │
│  • Async web UI + REST API           │         └────────────────────────────────────────┘
└──────────────────────────────────────┘
         NOTE: NO MarcDuino Slave board.
         NOTE: NO dome-side sound module.
```

**Body → Dome TX:** Marcduino panel/holo/logic commands + periodic health heartbeat
**Dome → Body RX:** Dome-initiated sequence commands — sound `$`, arm `:SE3x`, servo `:OP/:CL/:MV`

---

## 2. Marcduino Command Routing

Every Marcduino command begins with a prefix that determines ownership. This is the ground truth for what protoArtoo processes vs discards.

All Marcduino serial commands are ASCII and carriage-return terminated (`\r`).
Parsers must accept complete CR-terminated lines and avoid partial-command
dispatch.

### 2.1 Prefix Routing Table

| Prefix | Domain | Body action |
|--------|--------|-------------|
| `:` | Panel sequences & positions | **PROCESS** — `:SE30`–`:SE36` → ServoTask; `:OP`/`:CL`/`:MV` → ServoTask |
| `*` | Holoprojectors | **DISCARD** — dome-only |
| `@` | Logic display | **DISCARD** — dome-only |
| `$` | Sound | **PROCESS** → AudioTask → body audio module |
| `#` | Setup / config | **PROCESS** — runtime config |
| `%` | Pass-through to 3rd board | **DISCARD** (no slave board in protoArtoo topology) |
| `!` | Alt sound / alt display | **DISCARD** |
| `&` | I2C commands | **DISCARD** (no I2C on body) |

> **Classic note:** In the standard topology `$` lived on an MP3 Trigger in the dome, and `%` forwarded to a slave. Neither applies here. `$` is the body's responsibility; `%` has no destination.

### 2.2 Body Sequences (:SE30–:SE36)

When AstroPixelsPlus runs a coordinated sequence, it sends these to the body over the slip ring alongside its own internal dome actions:

| Command | Sequence |
|---------|----------|
| `:SE30\r` | Utility arm open-and-close |
| `:SE31\r` | All body panels open and close |
| `:SE32\r` | All body doors open and wiggle-close |
| `:SE33\r` | Body — use gripper arm |
| `:SE34\r` | Body — use interface tool |
| `:SE35\r` | Body — ping-pong body doors |
| `:SE36\r` | BT-1 two-gripper sequence |

### 2.3 Coordinated Sequence Decomposition

Full-droid sequences (`:SE01`–`:SE16`) split execution across both controllers simultaneously:

```
Operator triggers :SE01 ("Scream")
    │
    ├── AstroPixelsPlus handles dome side internally:
    │       → Opens dome panels, fires holos, fires logic display effect
    │
    └── AstroPixelsPlus sends body side via slip ring TX:
            → $S\r       (sound — body plays via audio module)
            → :SE30\r    (arm sequence — body executes)
```

---

## 3. DOME FIRMWARE CHANGES — mattiasbrandt/AstroPixelsPlus

> **Dome-side implementation is complete.** The body-side counterpart (protoArtoo DomeLinkTask) handles bidirectional communication. Sections below describe the architectural rationale and implementation details.

### 3.1 Disable Local Sound Player (Configuration)

**Why:** Upstream AstroPixelsPlus assumes a dome-local sound module (DFPlayer, MP3 Trigger). protoArtoo has no dome sound hardware. If `PREFERENCE_MARCSOUND` is left at a non-disabled default the dome will attempt to play audio on a non-existent module.

**Runtime fix:** In the dome web interface → Sound Settings → set player type to **None/Disabled**.
This writes `PREFERENCE_MARCSOUND = MarcSound::kNone` to NVS.

**Compile-time default (recommended):**
```cpp
// protoArtoo build: no local sound module in dome.
// Body controller holds the audio module and is the sole audio source.
// Classic builds have DFPlayer or MP3 Trigger here — protoArtoo does not.
#define MARC_SOUND_PLAYER   MarcSound::kNone
```

### 3.2 ✅ Enable Serial2 TX — Body Commands from Dome Sequences

**Why:** This is the most critical change. In upstream `reeltwo/AstroPixelsPlus`, `COMMAND_SERIAL` (Serial2) is opened with both RX and TX pins, but the TX direction is **not used** — only incoming commands are received. The `MARC_SERIAL_PASS` pass-through is present but commented out in the source:

```cpp
// Upstream code as-is — Serial2 TX is open but never used to send body commands:
COMMAND_SERIAL.begin(..., SERIAL2_RX_PIN, SERIAL2_TX_PIN);
// if (preferences.getBool(PREFERENCE_MARCSERIAL_PASS, MARC_SERIAL_PASS))  ← COMMENTED OUT
marcduinoSerial.setStream(&COMMAND_SERIAL, &Serial);
```

In the protoArtoo build, the dome must send body-side commands through Serial2 TX when executing full-droid sequences. Without this, triggering "Scream" from the dome web UI will animate the dome but produce no sound and no arm movement.

**Add to dome fork:**
```cpp
// protoArtoo: dome sends body-side commands via Serial2 TX.
// In the classic MarcDuino topology, body commands came from SHADOW externally.
// Here the dome itself is a command source for the body — it sends :SE3x arm
// sequences and $ sound commands back over the same bidirectional slip-ring link.
void sendBodyCommand(const char* cmd) {
    COMMAND_SERIAL.print(cmd);
    COMMAND_SERIAL.print('\r');
}

// Add calls inside each sequence handler in AstroPixelsPlus.ino, e.g.:
case 1:  // SE01 — Scream
    sendBodyCommand("$S");    // → body audio module plays scream sound
    sendBodyCommand(":SE30"); // → body opens/closes utility arms
    break;
```

**Sequences with body TX calls:** `:SE01`–`:SE15` — all 13 full-droid sequences have `sendBodyCommand()` calls. `:SE16` had no applicable body-side action.

> **As-built implementation detail:** `sendBodyCommand()` guards on `COMMAND_SERIAL && cmd && *cmd` rather than checking `PREFERENCE_MARCSERIAL_ENABLED` inside the function. This is equivalent but safer — it validates the serial object is initialised and guards against null/empty strings. The preference is checked at call sites. No functional difference.

### 3.3 ✅ Body ↔ Dome Health Status — Serial Link Protocol

The body and dome exchange periodic heartbeat commands over the same UART link as Marcduino control traffic. Both sides track connection state from these and expose it in settings pages and `/api/status`.

**Grounded in real upstream code:** the upstream `AstroPixelsPlus.ino` opens `COMMAND_SERIAL` (Serial2) with both `SERIAL2_RX_PIN` and `SERIAL2_TX_PIN` and passes it to `marcduinoSerial.setStream()`. The TX pin is available but nothing writes to it. The existing `#AP` command namespace (`#APWIFI`, `#APREMOTE`) establishes the naming convention we follow.

**New NVS key — `"mbodylink"` (bool, default `true`)**
Follows the existing `m`-prefix NVS convention. Controls whether heartbeats are sent and body state is tracked. When disabled, the serial port falls back to standard Marcduino-only receive mode.

**Two new heartbeat commands:**

| Command | Direction | Rate | Naming rationale |
|---|---|---|---|
| `#PAHB\r` | Body → Dome | 1 Hz | `#PA` = protoArtoo namespace, `HB` = heartbeat |
| `#APHB\r` | Dome → Body | 1 Hz | `#AP` = existing AstroPixelsPlus namespace, `HB` = heartbeat |

**Connection state — three values, same rule on both sides:**
- **Connected** — heartbeat received AND `millis() - lastSeen < 5000`
- **Not seen** — no heartbeat received this boot cycle (wiring/config issue)
- **Lost** — was connected, now silent >5 s (slip ring dropout or crash)

"Not seen" vs "Lost" matters for diagnostics: "Not seen" means never appeared, "Lost" means it disappeared.

> **As-built implementation notes:**
> - `handleBodySerial()` uses a 65-byte line buffer, reads `COMMAND_SERIAL` manually in `loop()`, intercepts `#PAHB` before any other processing.
> - Reeltwo is explicitly disabled when body link is active: `marcduinoSerial.setStream(nullptr, nullptr)`. This prevents Reeltwo from consuming serial data that `handleBodySerial()` must read.
> - `handleBodyLinkHeartbeat()` sends `#APHB
` at 1 Hz and logs state transitions (connected / LOST) to serial debug — not in the original plan, useful for diagnostics.
> - **`last_rx_ms` deviation:** plan specified `-1` when no heartbeat ever received; implementation uses `0`. Both clearly indicate "never seen" — no functional impact.
> - Body link state is exposed in `/api/health` and the WebSocket `/api/state` broadcast (not `/api/status` as originally named in the plan — the endpoint naming diverged during implementation; note this for protoArtoo's body-side DomeLinkTask which must match the actual endpoint).

**Critical architectural note for the dome fork:** `marcduinoSerial.setStream()` pipes `COMMAND_SERIAL` directly into Reeltwo's `CommandEvent::process()`. `#PAHB` must be intercepted *before* that — so the dome reads `COMMAND_SERIAL` manually in `loop()`, intercepts `#PAHB`, then calls `CommandEvent::process()` on everything else. Existing Reeltwo gadget dispatch is unaffected.

> **Full implementation with real C++ code, NVS key names, web UI HTML/JS, and step-by-step test order is in the companion document:**
> `body_dome_serial_link_spec.md`

### 3.4 Web UI Context Notes in Dome

The dome settings page gains a "Body Controller Link" toggle (NVS key `"mbodylink"`) with an inline note:

> *Enable when paired with protoArtoo body controller. Marc Serial must also be enabled. Sound Player should be set to None — all audio is handled by the body controller.*

A live status badge (**Connected / Lost / Not seen / Disabled** — 4 states) appears below the toggle, populated from the `/api/health` JSON `body_link` object via WebSocket real-time updates. No page reload needed.

> **As-built:** 4-state badge (adds "Disabled" state when `mbodylink` is false) — an improvement over the 3-state plan. WebSocket `body_link` data feeds the dashboard indicator in `health-grid` as well as the settings page badge.

### 3.5 Dome Fork Change Summary

| Change | Symbol / key | Status |
|--------|---|---|
| Set `MARC_SOUND_PLAYER` to `kNone` | `PREFERENCE_MARCSOUND` → `msound` | ✅ Done |
| Add `sendBodyCommand()` helper | new static void | ✅ Done — guards on `COMMAND_SERIAL` validity |
| Add body TX calls in `:SE01`–`:SE15` handlers | per-sequence | ✅ Done — 13 sequences |
| Replace `marcduinoSerial.setStream()` with `handleBodySerial()` | loop change | ✅ Done — `setStream(nullptr, nullptr)` |
| Add `#PAHB` intercept in `handleBodySerial()` | `sBodyLastSeenMs`, `sBodyHeartbeatRx` | ✅ Done |
| Add `#APHB` sender in `handleBodyLinkHeartbeat()` | 1 Hz in loop() | ✅ Done — includes state transition logging |
| Add `"mbodylink"` NVS preference | `PREFERENCE_BODY_LINK_ENABLED` | ✅ Done — default `true` |
| Expose `body_link{}` in `/api/health` + WebSocket `/api/state` | JSON handler | ✅ Done — note: endpoint is `/api/health`, not `/api/status` |
| Add Body Controller toggle + 4-state badge to settings UI | `data/*.html` + JS | ✅ Done — Connected/Lost/Not seen/Disabled |

See `body_dome_serial_link_spec.md` for all code.

---

## 4. Hardware Inventory

### 4.1 Main Controller

- **MCU chip:** ESP32 (Xtensa LX6 dual-core, 240 MHz, 4 MB flash, 520 KB SRAM)
- **Module type:** ESP32 D1 Mini — confirmed from Artoo Controller PCB v1.1 board photo (`tasks/artoo_board_v1.1.png`) and the artoo.uk product listing. The D1 Mini uses the standard ESP32-WROOM-32 chip internally; GPIO pinout and firmware are identical to a bare WROOM module.
- **WiFi:** Simultaneous AP + STA
- **Board fix:** Overheating trace cut on tested unit — permanent, board stable. May not apply to all units.

### 4.2 PCB Connector Reference

> **Source:** Artoo Controller PCB schematic (EasyEDA rev 1.0) and verified continuity trace results in `docs/pin_map.md`. Use `docs/pin_map.md` and `include/config.h` as current hardware truth for implementation.

This board has **18 physical connectors** plus one 2-pin jumper. All servo/signal connectors are 3-pin (SIG / V+ / GND) matching standard RC servo wiring. Serial connectors are 3-pin (TX / RX / GND).

---

#### Serial headers (4×, left side of PCB)

| PCB label | Pins | Function | ESP32 UART | GPIO | Status |
|---|---|---|---|---|---|
| Serial 0 | TX, RX, GND | USB / debug | UART0 | 1 (TX) / 3 (RX) | Confirmed |
| Serial 1 | TX, RX, GND | Hoverboard drive | UART1 | **16** (TX) / **17** (RX) | Traced |
| Serial 2 | TX, RX, GND | Audio module — DY-SV5W | Dedicated serial pins | **26** (TX) / **35** (RX) | Traced |
| Serial 3 | TX, RX, GND | Dome serial — AstroPixels Plus | UART2 | **33** (TX) / **34** (RX) | Traced |

> **Why 4 serial labels but only 3 ESP32 hardware UARTs:** The PCB maps UART-capable resources to dedicated functions: Serial 1 = hoverboard (UART1), Serial 3 = dome link (UART2), Serial 2 = audio (9600 baud command path using traced S2 pins). The hoverboard firmware page's "UART3" naming refers to the hoverboard controller MCU peripheral, not an ESP32 UART3.

---

#### RC receiver input headers (6×, left/centre of PCB)

These are signal inputs from an RC receiver. Standard servo connector (SIG / V+ / GND), one per channel.

| PCB label | Pins | Function in protoArtoo | GPIO | Status |
|---|---|---|---|---|
| CH1 | SIG, V+, GND | RC input CH1 (or SBUS input in `single_sbus`/`dual_sbus`) | **15** | Confirmed (artoo.uk manual) |
| CH2 | SIG, V+, GND | RC input CH2 (or SBUS receiver #2 in `dual_sbus` mode) | **13** | Confirmed (manual + trace) |
| CH3 | SIG, V+, GND | RC input CH3 in `standard_pwm` mode | **2** | Confirmed (artoo.uk manual) |
| CH4 | SIG, V+, GND | RC input CH4 in `standard_pwm` mode | **4** | Confirmed (artoo.uk manual) |
| CH5 | SIG, V+, GND | RC input CH5 in `standard_pwm` mode | **12** | Confirmed (artoo.uk manual) |
| CH6 | SIG, V+, GND | RC input CH6 in `standard_pwm` mode | **27** | Confirmed |

> **Pin mapping from artoo.uk manual:** CH1=15, CH2=13, CH3=2, CH4=4, CH5=12, CH6=27. The manual confirms dual SBUS uses "Pins 15 & 13 for dual control" — so SBUS receiver #2 (dome spin) shares the **CH2** header (GPIO 13), not CH6 as was previously assumed from the schematic's red highlight.

> **Receiver mode selection:** protoArtoo supports three RC input modes selected by config key `rc_input_mode` and Setup page toggle:
> - `standard_pwm`: 6-channel PWM receiver on pins 15, 13, 2, 4, 12, 27
> - `single_sbus`: 1 SBUS receiver, 6 channels, pin 15 (hardware serial)
> - `dual_sbus`: 2 SBUS receivers, 12 channels, pins 15 and 13

---

#### Servo / ESC output headers (8×, left side and bottom of PCB)

Signal outputs driving servos or an ESC. Standard 3-pin servo connector (SIG / V+ / GND).

| PCB label | Pins | Function in protoArtoo | GPIO | Status |
|---|---|---|---|---|
| DOME | SIG, V+, GND | Dome rotation motor ESC signal | **25** | Confirmed (artoo.uk manual) |
| ARM1 | SIG, V+, GND | Utility arm servo #1 (top / left) | **23** | Confirmed (artoo.uk manual) |
| ARM2 | SIG, V+, GND | Utility arm servo #2 (bottom / right) | **5** | Confirmed (artoo.uk manual) |
| ARM3 | SIG, V+, GND | AUX output 1 — spare (future body panel, WS2812B, or input) | TBD trace | TBD |
| ARM4 | SIG, V+, GND | AUX output 2 — spare | TBD trace | TBD |
| ARM6 | SIG, V+, GND | AUX output 3 — spare | TBD trace | TBD |

> **ARM1/ARM2** are the two utility arm servo outputs.
> **ARM3/ARM4/ARM6** are the "3 AUX outputs" from the product spec — usable as servo outputs, WS2812B LED data lines, or signal inputs. Not wired in the default configuration.
> **ARM5 does not exist** on this board. The numbering skips from ARM4 to ARM6 intentionally.

---

#### SBUS receiver header

| PCB label | Pins | Function in protoArtoo | GPIO | Status |
|---|---|---|---|---|
| (dedicated SBUS header) | SIG, V+, GND | SBUS receiver #1 — drive (speed / steer / CH8 speed-limit) | **15** | Confirmed |

> SBUS receiver #1 connects to the dedicated SBUS header (GPIO 15). SBUS receiver #2 connects to GPIO 13 — which corresponds to the **CH2 header** position. The artoo.uk manual confirms dual SBUS uses "Pins 15 & 13".

---

#### I2C / expansion header

| PCB label | Pins | Function | GPIO | Status |
|---|---|---|---|---|
| KEYPAD / I2C P1 | SCL, SDA, GND, 3.3 V | I2C bus — available for keypad or future expansion | TBD trace | TBD |

---

#### Jumper

| PCB label | Pins | Purpose | Status |
|---|---|---|---|
| JP1 (M02) | 2-pin | Unknown — requires PCB trace investigation | TBD |

---

### 4.3 GPIO Assignment Summary

Collects all known and TBD assignments in one place. Fill in TBD column from PCB trace.

| GPIO | PCB connector | Function | Status |
|---|---|---|---|
| 1 | Serial 0 TX | USB debug UART0 TX | Confirmed |
| 3 | Serial 0 RX | USB debug UART0 RX | Confirmed |
| 15 | SBUS header / CH1 | SBUS receiver #1 — drive | Confirmed |
| 13 | CH2 | SBUS receiver #2 — dome spin (or RC CH2 in `standard_pwm` mode) | Confirmed |
| 16 | Serial 1 TX | Hoverboard TX | Traced |
| 17 | Serial 1 RX | Hoverboard RX | Traced |
| 26 | Serial 2 TX | Audio TX (DY-SV5W) | Traced |
| 35 | Serial 2 RX | Audio RX (optional status/ACK path) | Traced |
| 33 | Serial 3 TX | Dome serial TX | Traced |
| 34 | Serial 3 RX | Dome serial RX | Traced |
| 25 | DOME | Dome ESC signal (50 Hz PWM) | Confirmed |
| 23 | ARM1 | Utility arm servo #1 (50 Hz PWM) | Confirmed |
| 5 | ARM2 | Utility arm servo #2 (50 Hz PWM) | Confirmed |
| 19 | ARM3 | AUX1 type-configurable output (none/MG996R/MG90S/RGB) | Traced |
| 18 | ARM4 | AUX2 type-configurable output (none/MG996R/MG90S/RGB) | Traced |
| 32 | ARM5 | AUX3 type-configurable output (none/MG996R/MG90S/RGB) | Traced |
| 22 | I2C P1 SCL | I2C clock | Traced |
| 21 | I2C P1 SDA | I2C data | Traced |

### 4.4 External Subsystems

| Subsystem | Protocol | Hardware | PCB connector |
|---|---|---|---|
| Drive | 8-byte hoverboard UART 115200 (ESP32 UART1 → hoverboard controller) | Any hoverboard running compatible firmware (EFeru FOC / RoboDurden Gen2.x) | Serial 1 |
| Dome controller | Marcduino bidirectional 9600 | AstroPixelsPlus fork, slip ring | Serial 3 |
| Audio | UART 9600 (TX primary, RX optional) | DY-SV5W (default); other modules via AudioDriver interface | Serial 2 |
| RC input (`standard_pwm`) | Standard RC PWM (6 channels) | Regular RC receiver | CH1-CH6 headers |
| RC input (`single_sbus`) | SBUS (6 channels) | One SBUS receiver | CH1 header (GPIO 15) |
| RC input (`dual_sbus`) — drive | SBUS (12-channel dual-control mode) | SBUS receiver #1 | CH1 / SBUS header |
| RC input (`dual_sbus`) — dome spin | SBUS (12-channel dual-control mode) | SBUS receiver #2 | CH2 header |
| Utility arm #1 | RC PWM 50 Hz | Standard hobby servo (tested: MG996R) | ARM1 |
| Utility arm #2 | RC PWM 50 Hz | Standard hobby servo (tested: MG996R) | ARM2 |
| Dome motor | RC PWM 50 Hz | Standard RC ESC (tested: ISDT ESC70) | DOME |
| AUX outputs | RC PWM 50 Hz | Type-configurable per channel (none / MG996R / MG90S / RGB); persisted via `aux1_type`–`aux3_type` NVS keys; not wired in default configuration | ARM3, ARM4, ARM6 |

---

## 5. ESP32 Peripheral Resource Plan

### 5.1 UART Allocation

> **"UART3" naming clarification:** The Artoo Controller PCB documentation and the official hoverboard firmware download page both refer to "UART3" — this is the **hoverboard controller's** UART peripheral (the recommended, 5V-tolerant port on the sensor cable header). On GD32F130 boards this is UART3; on STM32 boards the peripheral name may differ. It is **not** ESP32 UART3. The ESP32 has only UART0/UART1/UART2. protoArtoo uses ESP32 UART1 for hoverboard (GPIO 16/17) and UART2 for dome serial (GPIO 33/34).

> **SBUS mode behavior:** In `single_sbus`, one SBUS receiver is used on CH1 GPIO 15 (hardware serial). In `dual_sbus`, two SBUS receivers are used on CH1 and CH2. In `standard_pwm`, CH1-CH6 are read as PWM input channels and SBUS decoders are not started.

| ESP32 Peripheral | PCB connector | Use | Baud | GPIO | Notes |
|---|---|---|---|---|---|
| UART0 | Serial 0 | USB / debug | — | 1 / 3 | Never reassign |
| UART1 | Serial 1 | Hoverboard drive | 115200 | 16 / 17 | Confirmed by trace and manual |
| Audio serial pins | Serial 2 | Audio module (DY-SV5W) | 9600 | 26 / 35 | TX primary, RX optional for status/ACK |
| UART2 | Serial 3 | Dome serial (AstroPixels Plus) | 9600 | 33 / 34 | Bidirectional slip-ring link |
| RMT channel 0 | — | SBUS receiver #1 — drive | 100 Kbaud 8E2 inv | GPIO 15 | `bolderflight/sbus` RMT mode |
| RMT channel 1 | — | SBUS receiver #2 — dome spin | 100 Kbaud 8E2 inv | GPIO 13 (CH2 header) | `bolderflight/sbus` RMT mode |

> **Serial allocation confirmed:** Hoverboard uses Serial 1 / UART1 (GPIO 16/17), audio uses Serial 2 pins (GPIO 26/35), and dome link uses Serial 3 / UART2 (GPIO 33/34), matching `docs/pin_map.md` and `include/config.h`.

All assignments confirmed by product description: "DRIVE via UART Serial, LIGHTS via UART Serial, SOUND via UART Serial."

### 5.2 RC Input Modes

`rc_input_mode` is a persisted runtime selection exposed in Setup page and
`/api/config`:

| Value | Wiring/decoder behavior |
|---|---|
| `standard_pwm` | Start PWM input capture on CH1-CH6 GPIO 15,13,2,4,12,27; do not start SBUS decoders |
| `single_sbus` | Start one SBUS receiver on CH1 GPIO 15 (hardware serial), 6 channels |
| `dual_sbus` | Start two SBUS receivers on CH1 GPIO 15 and CH2 GPIO 13, 12 channels |

The mode is applied during input-task initialization. Changing it in Setup page
updates NVS and requires restart/re-init of RC input tasks.

### 5.3 Single SBUS (`rc_input_mode=single_sbus`)

Single receiver SBUS mode uses one SBUS receiver on CH1 / GPIO 15.

| Receiver | GPIO | Role | Channel count |
|---|---|---|---|
| **#1 — Main RC** | 15 | Drive + mapped auxiliary functions | 6 channels |

Transport: hardware serial on pin 15.

### 5.4 SBUS — Dual Dedicated Receivers (`rc_input_mode=dual_sbus`)

Inverted 100 Kbaud 8E2. The ESP32 supports hardware line inversion natively, so no external inverter chip is needed.

| Receiver | GPIO | Role | Failsafe behaviour |
|---|---|---|---|
| **#1 — Drive** | 15 | Speed (CH1), steer (CH2), speed-limit dial (CH8) | Loss → drive failsafe (motors stop) |
| **#2 — Dome spin** | 13 | Dome motor speed (CH1) | Loss → dome motor stops, drive continues — physically connects to CH2 header on PCB |

These are not redundant backups — they are dedicated controllers for separate
subsystems. Losing receiver #1 is a drive safety event. Losing receiver #2 is a
dome inconvenience, not a safety event. The failsafe logic treats them independently.

**Physical connector locations:** SBUS receiver #1 (drive) connects to the dedicated SBUS header (GPIO 15). SBUS receiver #2 (dome spin) connects to the **CH2 header** (GPIO 13) — the artoo.uk manual confirms dual SBUS mode uses "Pins 15 & 13 for dual control".

Both receivers use the `bolderflight/sbus` library in **RMT mode** — this uses
the ESP32's RMT (Remote Control) peripheral rather than a hardware UART,
freeing UART1 and UART2 for dome serial and hoverboard drive. RMT handles the
precise 100 Kbaud 8E2 inverted timing natively with no software overhead.
No external inverter chip needed.

### 5.5 Standard RC PWM (`rc_input_mode=standard_pwm`)

In standard RC PWM mode, CH1-CH6 are read as six independent PWM input channels:

| RC channel | GPIO | Header |
|---|---|---|
| CH1 | 15 | CH1 |
| CH2 | 13 | CH2 |
| CH3 | 2 | CH3 |
| CH4 | 4 | CH4 |
| CH5 | 12 | CH5 |
| CH6 | 27 | CH6 |

Default functional mapping for `standard_pwm` mode is defined in the phase task plans
and stored as NVS-remappable channel assignments.

### 5.6 PWM (LEDC)

| Ch | Use | Frequency |
|---|---|---|
| 0 | Arm servo #1 | 50 Hz |
| 1 | Arm servo #2 | 50 Hz |
| 2 | Dome motor ESC | 50 Hz |


### 5.7 Slip Ring — Conductor Allocation

The slip ring has 12 conductors. Two are currently wired for serial communication. Three are spare (unused). The remaining seven carry power and ground for the dome subsystem.

| Conductor | Current use | Notes |
|---|---|---|
| 1 | Serial TX (body → dome) | UART2 TX from ESP32 |
| 2 | Serial RX (dome → body) | UART2 RX to ESP32 |
| 3 | Spare | Available for future use |
| 4 | Spare | Available for future use |
| 5 | Spare | Available for future use |
| 6–12 | Power / ground (dome) | Exact split TBD — verify against physical slip ring wiring |

**Recommended use for the 3 spare conductors:**

The three spare wires are most valuably used as additional power paths if needed, or left as true spares for now. Serial only needs 2 conductors (TX + RX, shared ground with power lines), so the current wiring is sufficient for the bidirectional Marcduino link.

> The serial link uses shared ground with the power conductors — there is no dedicated serial ground wire needed as long as body and dome share a common ground through the power conductors in the slip ring.

---

## 6. Protocol Reference

### 6.1 Hoverboard — Gen2.x UART Protocol

protoArtoo drives the hoverboard motors using the **8-byte UART frame protocol** originally introduced by `EFeru/hoverboard-firmware-hack-FOC` and adopted by `RoboDurden/Hoverboard-Firmware-Hack-Gen2.x-GD32`. This frame format is the de facto standard for community hoverboard firmware — any fork implementing it is compatible.

```
[0-1]  start:    0xABCD  (uint16, little-endian — 0xCD first on wire)
[2-3]  steer:    int16   (-1000..+1000)
[4-5]  speed:    int16   (-1000..+1000)
[6-7]  checksum: start XOR steer XOR speed
Baud: 115200, 8N1 — send at 50 Hz continuously
```

**Hoverboard firmware references:**
- EFeru FOC (STM32): https://github.com/EFeru/hoverboard-firmware-hack-FOC
- RoboDurden Gen2.x (GD32): https://github.com/RoboDurden/Hoverboard-Firmware-Hack-Gen2.x-GD32

(Note: the original Gen2.x URL `RoboDurden/Hoverboard-Firmware-Hack-Gen2.x` now redirects to the GD32 repo.)

**Zero-frame rule:** Always send `speed=0, steer=0` when stationary — never go silent. The hoverboard stops motors after ~500 ms of no frames; silence is only acceptable as a post-crash last resort.

**Compatibility note:** Any hoverboard firmware that accepts the same 8-byte XOR-checksum UART frame at 115200 baud (including EFeru-based forks) is compatible with protoArtoo's DriveTask without modification. The frame format is the interface contract, not the specific firmware project.

### 6.2 Dome Serial — Bidirectional Marcduino (9600 baud 8N1)

ASCII strings terminated with `\r`.

> **Classic note:** Standard MarcDuino body→dome serial was TX-only from the body. In protoArtoo the same physical wire carries commands in both directions over the slip ring.

#### Body → Dome TX

| Domain | Commands |
|---|---|
| Panels | `:OP00\r`, `:CL00\r`, `:OF00\r` |
| Holo | `*ON00\r`, `*OF00\r`, `*ST00\r`, `*HD07\r`–`*HD09\r` |
| Logic | `@0T12\r` (rainbow), `@0T15\r` (out), `@0T22\r` (fire), `@0T24\r` (pulse) |
| Sleep | `:SE10\r` + `*ST00\r` + `@0T15\r` |
| Wake | `:SE14\r` |
| Health | `#PAHB\r` (body heartbeat ~1 Hz — see Section 3.3 and `body_dome_serial_link_spec.md`) |

#### Dome → Body RX

| Prefix | Body action |
|---|---|
| `$` | → AudioTask → audio module |
| `:SE30`–`:SE36` | → ServoTask (body arm/door sequences) |
| `:SE01`–`:SE16` | → AudioTask (sound component of full sequence) |
| `:OP`/`:CL`/`:MV` | → ServoTask (direct arm position) |
| `#` | → ConfigTask |
| `*`, `@`, `%`, `&`, `!` | DISCARD — dome-side only, no slave board |

#### Sleep / Wake Coordination

- **Sleep:** body sends `:SE10\r` + `*ST00\r` + `@0T15\r`
- **Wake:** body sends `:SE14\r`

### 6.3 Audio Module

protoArtoo abstracts audio behind an `AudioDriver` interface. The **default driver** is the DY-SV5W, which is inexpensive and UART-controlled. Other audio modules (e.g. DFPlayer Mini) can be supported by implementing the same interface.

#### Default: DY-SV5W (UART 9600 baud)

```
Play track n:  AA 02 [n_hi] [n_lo] AB
Stop:          AA 04 AB
Volume (0-30): AA 07 [vol] AB
```

Sound files follow the **R2 community standard**: `$nnn` where nnn is a zero-padded decimal track number maps directly to the corresponding file on the SD card.

```
$ command → file mapping:
  $001     → 001.mp3 (or 001.wav) in root, or track 1 in folder hierarchy
  $023     → 023.mp3
  $S       → interpreted as play a random sound (body AudioTask picks a random track
              from the configured "general sounds" range — NVS keys snd_rand_min / snd_rand_max)
```

The DY-SV5W plays by track number using the `AA 02 [hi] [lo] AB` command. `AudioTask` parses the `$` string, extracts the track number, and sends the appropriate DY-SV5W frame. Non-numeric `$` strings (like `$S`) trigger random track selection within a configured range.

Canonical `$` command coverage for protoArtoo body audio path:

| Command | Meaning |
|---|---|
| `$nnn` | Play explicit track number (e.g. `$001`) |
| `$S` | Play random sound from configured random range |
| `$R` | Enable random playback mode |
| `$O` | Disable random playback mode |
| `$s` | Stop current playback |
| `$+` / `$-` | Increment volume up/down |
| `$m` / `$f` / `$p` | Mid / max / min volume presets |

SD card file layout recommendation (R2 community standard):
```
/001.mp3   General R2 sounds
/002.mp3
...
/099.mp3
/100.mp3   Scream sounds start
...
```

The exact folder structure and track ranges are configurable in NVS.

#### Alternative audio modules (community options)

| Module | Interface | Notes |
|---|---|---|
| **DY-SV5W** | UART 9600 | Default for protoArtoo — inexpensive, AliExpress sourced |
| Adafruit Audio FX / MP3 Trigger | UART / trigger | More expensive; well-supported in the R2 community; harder to source in Europe |
| DFPlayer Mini | UART 9600 | Very common in the community; similar UART command set to DY-SV5W; cheap and available |
| Cytron MP3-TF-16P | UART 9600 | DFPlayer-compatible variant |

**Driver abstraction in code:**
```cpp
// include/audio_driver.h
class AudioDriver {
public:
    virtual void begin() = 0;
    virtual void playTrack(uint16_t track) = 0;
    virtual void stop() = 0;
    virtual void setVolume(uint8_t vol) = 0;   // 0–30 normalised
};

// Default implementation:
// src/drivers/audio_dy_sv5w.cpp  → AudioDriverDySv5w : public AudioDriver

// To use DFPlayer instead:
// src/drivers/audio_dfplayer.cpp → AudioDriverDFPlayer : public AudioDriver
// Change one line in main.cpp — no other code changes needed
```

Build flag selects the driver:
```ini
; platformio.ini
-DPA_AUDIO_DRIVER=AUDIO_DY_SV5W     ; default
; -DPA_AUDIO_DRIVER=AUDIO_DFPLAYER  ; alternative
; -DPA_AUDIO_DRIVER=AUDIO_MP3TRIGGER
```

**All audio routes through AudioTask queue** regardless of source (RC, web API, dome serial `$` RX).

### 6.4 Audio — NVS-Configurable Settings

These config values are stored in NVS (Preferences) and exposed in the web UI config page. Ranges are aligned with the original Artoo firmware — this ensures consistent UX for builders migrating from the official firmware.

| Setting | NVS key | Range | Default | Notes |
|---|---|---|---|---|
| Volume limit | `vol_limit` | 0–30 | 20 | Matches DY-SV5W native range; 30 = max |
| Startup sound | `snd_startup` | 1–65 | 0 (none) | Track number to play on boot |
| Random sounds | `snd_random` | on/off | on | Automatic character sounds |
| Random interval | `snd_interval` | 1–2400 s | 10 | Time between random sound events |

> Volume range 0–30 matches both the DY-SV5W hardware range and the original Artoo firmware range, so existing sound card configurations carry over directly.

### 6.5 RC Input Modes and Channel Map (NVS-configurable)

Receiver mode toggle (Setup page + `/api/config`):

| Setting | NVS key | Values | Default | Notes |
|---|---|---|---|---|
| RC input mode | `rc_input_mode` | `standard_pwm`, `single_sbus`, `dual_sbus` | `dual_sbus` | Selects PWM, single-SBUS, or dual-SBUS startup path |

RC mapping policy is mode-agnostic:

- `single_sbus` and `standard_pwm` share the same default functional intent for
    CH1-CH6 (drive, steer, dome, arm triggers, AUX/sound trigger)
- `dual_sbus` keeps the split-controller default (receiver #1 drive, receiver #2
    dome), with remaining receiver #2 channels configurable
- all channel assignments are NVS-configurable and can be remapped without
    rebuilding firmware

Web configurability requirement:

- all RC channel bindings (action target + calibration values) must be editable
    from the webpage for every mode (`standard_pwm`, `single_sbus`, `dual_sbus`)
- web UI is the operator-facing configuration surface; no hidden compile-time-only
    mapping workflow is acceptable for normal tuning
- mapping UI should follow a modern, responsive control-editor pattern
    (clear channel cards, source badges, inline validation, and explicit save/apply
    feedback)

Protocol compliance requirements (from `tasks/SBUS_protocol.md`):

- 25-byte frame: `0x0F` header, 22-byte packed channel payload, flags byte,
    `0x00` end byte
- 100000 baud, 8E2, inverted signaling
- Flags byte bit positions:
    - bit 0: CH17 digital
    - bit 1: CH18 digital
    - bit 2: `lost_frame`
    - bit 3: `failsafe`

`lost_frame` should be counted/observable but must not be treated as an
immediate full failsafe event by itself.

**Receiver #1 — Single SBUS defaults (`single_sbus`, GPIO 15)**

| CH | Default assignment | Notes |
|---|---|---|
| 1 | Drive speed | Forward/reverse |
| 2 | Drive steer | Left/right |
| 3 | Dome rotation speed | Single-receiver default dome axis |
| 4 | ARM1 trigger | Default open/close trigger |
| 5 | ARM2 trigger | Default open/close trigger |
| 6 | AUX or sound trigger | Feature-gated by mapping config |
| 7 | Unassigned | |
| 8 | **Speed limit dial** | Continuous 0–100%; see Section 6.7 |
| 9–12 | Unassigned | |

**Receiver #1 — Drive defaults (`dual_sbus`, GPIO 15)**

| CH | Default assignment | Notes |
|---|---|---|
| 1 | Drive speed | Forward/reverse |
| 2 | Drive steer | Left/right |
| 8 | **Speed limit dial** | Continuous 0–100%; see Section 6.7 |
| 3–7, 9–12 | Configurable bindings | Optional mapped actions |

**Receiver #2 — Dome defaults (`dual_sbus` only, GPIO 13)**

| CH | Default assignment | Notes |
|---|---|---|
| 1 | Dome rotation speed | Centre = stop; ± = spin direction/speed |
| 2–12 | Configurable bindings | Map to predefined actions or Marcduino targets |

All channel assignments are NVS-configurable. Reassigning CH8 on receiver #1 to
a different channel is the main expected SBUS customization.

**`standard_pwm` mode — default channel map (same baseline intent as single SBUS)**

| RC PWM channel | Default assignment |
|---|---|
| CH1 | Drive speed |
| CH2 | Drive steer |
| CH3 | Dome rotation speed |
| CH4 | ARM1 trigger |
| CH5 | ARM2 trigger |
| CH6 | AUX or sound trigger |

This map is only the default profile. Channel assignments remain NVS-configurable
for builder-specific transmitter layouts.

`standard_pwm` is limited to six analog channels, so there is no native CH8 dial
in this mode. If a builder wants speed-limit control in PWM mode, the speed-limit
action can be mapped to any available channel via NVS channel bindings.

### 6.5.1 RC Action Target Model

Every configurable trigger/button channel binding carries an **action target** that
determines what the channel does at runtime. The target is NVS-persisted per
channel and editable from the Setup page without a firmware rebuild.

**Two-tier binding structure:**

- **Backbone bindings** — fixed-role slots (`drive_speed`, `drive_steer`, `dome_speed`,
  `speed_limit`): the action is implied by the field name; only source, channel, and
  calibration values are user-configurable. No action dropdown in the UI for these.
- **Trigger/button bindings** — user-assignable slots (`arm1`, `arm2`, `aux1`, `aux2`,
  `aux3`, `sound`, `op_mode`, plus N free slots): the operator configures both the
  RC source/channel binding AND the action target for each slot.

**`RcActionTarget` enum — all valid targets:**

| Token | Description | Input type |
|-------|-------------|------------|
| `none` | Slot disabled — no output | — |
| `drive_speed` | Forward / back movement axis | analog |
| `drive_steer` | Left / right steering axis | analog |
| `dome_speed` | Dome rotation speed axis | analog |
| `speed_limit` | Speed ceiling dial (0–100%) | analog |
| `op_mode` | Driving ↔ Stationary mode; LOW=Drive, HIGH=Stationary | switch |
| `arm1_toggle` | ARM1 open↔close toggle on rising edge | switch/button |
| `arm2_toggle` | ARM2 open↔close toggle on rising edge | switch/button |
| `aux1_toggle` | AUX1 toggle on rising edge | switch/button |
| `aux2_toggle` | AUX2 toggle on rising edge | switch/button |
| `aux3_toggle` | AUX3 toggle on rising edge | switch/button |
| `seq` | Fire a body Marcduino sequence on rising edge; payload = `SE30`..`SE36` | switch/button |
| `marcduino` | Send a body-accepted Marcduino command on rising edge; payload = command string | switch/button |
| `estop` | Latch E-Stop when channel goes high (UI requires explicit opt-in to activate) | switch/button |

**Trigger/button binding NVS format** (version 2):

```
source:channel:target:payload:min:center:max:deadband:reverse
example: "sbus1:4:arm1_toggle::172:992:1811:50:0"
example: "sbus1:6:seq:SE30:172:992:1811:50:0"
example: "pwm:6:marcduino::OP01:1000:1500:2000:100:0"
unbound: "none:0:none::1000:1500:2000:0:0"
```

**Tier-2 slot NVS keys and boot defaults (11 slots, all ≤15 chars):**

| NVS key | Slot | Boot default binding | Boot default target |
|---------|------|---------------------|--------------------|
| `rc_arm1` | ARM1 trigger | `sbus1:4:arm1_toggle::172:992:1811:50:0` | `arm1_toggle` |
| `rc_arm2` | ARM2 trigger | `sbus1:5:arm2_toggle::172:992:1811:50:0` | `arm2_toggle` |
| `rc_aux1` | AUX1 trigger | unbound | `none` |
| `rc_aux2` | AUX2 trigger | unbound | `none` |
| `rc_aux3` | AUX3 trigger | unbound | `none` |
| `rc_sound` | Sound trigger | unbound — CH6 is natural candidate | `none` |
| `rc_opmode` | Op-mode switch | unbound — CH8 dial is primary mode mechanism | `none` |
| `rc_free0` | Free slot 0 | unbound | `none` |
| `rc_free1` | Free slot 1 | unbound | `none` |
| `rc_free2` | Free slot 2 | unbound | `none` |
| `rc_free3` | Free slot 3 | unbound | `none` |

Source is embedded in each stored binding string; no separate `rc_pwm_*`/`rc_sbus_*` NVS
variants are needed for tier-2 slots. Free slots (4) cover the natural dual-SBUS
expansion channels (receiver #2 CH2–CH5) without over-engineering.

- `rc_bind_ver` NVS key must be incremented when this format changes; stale data
  that fails to parse silently resets the affected slot to default (`none`)
- Payload is empty string for non-payload targets; max 15 chars
- `marcduino` payload must start with a body-owned prefix (`:`, `$`, `#`);
  unsafe prefixes (`*`, `@`, `%`, `&`, `!`) are rejected with a validation error

**Session-state routing rule:** all RC input paths (PWM, SBUS1, SBUS2) must
route through a target-dispatch function that switches on `binding.target` — not
hardcoded channel-index assumptions. Analog targets pipe the normalized value
(-1.0..1.0) to the relevant command function. Switch/button targets fire once on
rising edge (previous LOW, current HIGH); no repeat-fire while held.

### 6.6 Utility Arms — Servo PWM

LEDC 50 Hz output with per-channel pulse clamps (NVS configurable).

- Use conservative defaults (1000-2000 us) unless hardware calibration confirms
    wider safe travel.
- Calibrate each servo's open/close endpoints to avoid mechanical hard-stop
    stall conditions.
- Servo power must come from an external 5-6V rail with common ground to ESP32;
    do not power arm servos from ESP32 regulator pins.

For the dome ESC (ISDT ESC70), runtime control is also standard RC PWM
(1000-2000 us, neutral 1500 us). BLE/app controls are configuration-time only,
not runtime control path.

### 6.7 Operation Modes and CH8 Speed Limit Dial

The **Driving ↔ Stationary** mode state is controlled either by the CH8 dial
(default) or by mapping any available channel to the `op_mode` action target
(see Section 6.5.1). The `op_mode` target makes mode-switching fully remappable
— any button or 2-pos/3-pos switch on the transmitter can toggle modes without
requiring CH8 or a firmware rebuild.

#### CH8 — Speed Limit Dial (primary behaviour)

CH8 on receiver #1 is a **continuous speed-limit dial** by default:

- CH8 at minimum (0): drive output = 0 (droid stationary regardless of CH1/CH2)
- CH8 increasing: scales the maximum allowed drive output linearly from 0 to `SPEED_LIMIT_MAX`
- This gives the operator a physical "confidence dial" — turn it up slowly when moving in tight spaces, full up for open floor

The effective speed output is: `speed_out = speed_in × (ch8_value / CH8_MAX)`

#### Stationary Mode lock (NVS-configurable, default OFF)

An optional NVS setting `ch8_mode_lock` (bool, default `false`) changes CH8 behaviour:

| `ch8_mode_lock` | CH8 at zero | CH8 above zero |
|---|---|---|
| `false` (default) | Drive locked at zero, no mode change | Speed limited proportionally |
| `true` | Enters **Stationary Mode** (random sounds, dome auto-spin, drive locked) | **Drive Mode** (sounds manual, dome manual) |

When `ch8_mode_lock` is `true`:

| Mode | Trigger | Drive | Dome spin | Sound |
|---|---|---|---|---|
| **Drive Mode** | CH8 above zero threshold | Tank-style, speed-limited by CH8 | Manual via receiver #2 CH1 | Manual via CH4/CH5 |
| **Stationary Mode** | CH8 at zero | Locked — zero frames to hoverboard | Auto-spin at configured speed | Random sounds enabled |

In **Stationary Mode**, random audio events fire at the configured interval, dome rotates automatically, and drive is electronically locked. This is the "performing" state.

The mode-lock option is configurable in the web UI config page, NVS key `ch8_mode_lock`. The operator can switch between simple dial behaviour and mode-lock behaviour without reflashing.

### 6.8 Mood Selector — Droid Idle Behavior Profiles

The **Mood Selector** sets R2-D2's idle behavior baseline — the state of sound chatter,
holo projector motion, and visual outputs when no specific choreography is running.

#### Dual-Path Architecture

Mood execution spans **both controllers independently**. These two paths run in parallel
and each can operate without the other:

| Path | Runs on | Requires dome link? | Effect |
|---|---|---|---|
| **Audio (body)** | protoArtoo → AudioTask | **No** | Stop/resume idle sound chatter (`$s` / `$R`) |
| **Visual (dome)** | AstroPixelsPlus | **Yes** | Holo servos, holo LEDs, dome panels, logic displays |

**The body is the sole audio source for the droid.** When a mood is activated, the
body applies the audio portion directly to its AudioTask — it does not wait for the
dome to relay `$` commands back over the slip ring. This means idle sound chatter
follows the active mood correctly even when the dome serial link is absent.

**Dome visual effects require the dome link.** Holo projector motion, dome panel
positions, and logic display states are exclusively AstroPixelsPlus's responsibility
and cannot be influenced without an active UART2 link.

#### How Each Mood Trigger Works

When any mood is activated (from UI, RC, or a `:SE1x` command received via dome RX):

1. **Body dispatches audio command directly** to its own AudioTask queue (`$s` or `$R`)
   — this happens regardless of dome link state.
2. **If dome link is active:** body also sends `:SE1x` to `domeTxQueue` → `DomeLinkTask`
   → UART2 TX → AstroPixelsPlus, which then handles all dome-side visual effects.
3. **If dome link is not active:** step 2 is skipped silently; audio still works.
4. Body updates `robotState.activeMood` (under `portMUX`) to reflect the new mood.

When a `:SE1x` mood command arrives FROM the dome (dome-initiated trigger), the body:
1. Applies the audio component locally (same as above).
2. Does **not** echo the command back to the dome (no loop).

#### The Four Moods

| Button label | Command | Body audio action | Dome visual effects (requires link) |
|---|---|---|---|
| **Quiet** | `:SE10` | → AudioTask: `$s` (stop chatter) | Holo LEDs off; holos parked; all dome panels close; logics NORMAL |
| **Mid-Awake** | `:SE13` | → AudioTask: `$R` (resume chatter) | Holo servos **stopped/parked**; LEDs off; panels close; logics NORMAL |
| **Full-Awake** | `:SE11` | → AudioTask: `$R` (resume chatter) | Holo servos moving randomly; LEDs off; panels close; logics NORMAL |
| **Awake+** | `:SE14` | → AudioTask: `$R` (resume chatter) | Holo servos moving randomly; **holo LEDs on**; panels close; logics NORMAL |

Key difference between the awake states: holo projector behavior (dome side only).
- Mid-Awake → holos parked, LEDs off
- Full-Awake → holos moving randomly, LEDs off
- Awake+ → holos moving randomly, LEDs on (brightest/most active baseline)

These visual sequences are handled in AstroPixelsPlus by `QuietModeReset`,
`MidAwakeModeReset`, `FullAwakeModeReset`, and `AwakePlusModeReset` in
`MarcduinoSequence.h`.

#### RobotState Tracking

`robotState.activeMood` tracks the current mood (`uint8_t`, values 10/11/13/14 matching
`:SE1x` command index, 0 = unset). It is written under `portMUX` whenever a mood
command is applied, and read by the status API and dashboard poll. It is NVS-backed
(`NVS key: last_mood`) so the droid restores the previous mood on boot.

#### Sleep / Wake Integration

- **Sleep entry:** body sends `:SE10` (Quiet) audio + dome TX; dome continues with
  LIGHTSOUT logic display.
- **Wake exit:** body sends `:SE14` (Awake+) audio + dome TX to restore full active baseline.
- `:SE11`, `:SE13`, `:SE14` bypass the dome sleep gate in AstroPixelsPlus — any awake
  mood command from the RC remote wakes the droid.

#### Homepage Dashboard Card

The `index.html` dashboard carries a **Mood Selector** card with four clearly labelled
buttons: Quiet / Mid-Awake / Full-Awake / Awake+. Each fires the corresponding
`:SE1x` command. The active mood is highlighted visually.

Card hierarchy on the dashboard (secondary priority — appears after safety and
drive/arm controls):
1. Safety / Estop
2. Movement status + drive controls
3. Servo / dome controls
4. Mood Selector
5. Diagnostics and logs

Mood sound takes effect immediately on button press regardless of dome link state.
When the dome link is not connected, a non-intrusive note is shown on the card
indicating dome visual effects are inactive; this does not suppress the audio action.

#### RC Action Target

The `dome_seq` action target (§6.5.1) covers mood changes via RC. A builder can
map any switch or button channel to fire a mood command on rising edge. Payload
`SE10`..`SE14` selects the mood; full range `SE10`..`SE16` covers all
full-droid sequences defined in AstroPixelsPlus.

---

## 7. Firmware Architecture

Implementation truth for GPIO/UART assignments is always `include/config.h` and
`docs/pin_map.md`. Architecture snippets below may show historical placeholder
examples and should not override those canonical files.

### 7.1 Framework & Dependencies

**Arduino framework on ESP32** via PlatformIO. Versions pinned to match dome firmware.

| Library | Version | Notes |
|---|---|---|
| `me-no-dev/ESPAsyncWebServer` | `3.5.1` | Match dome |
| `me-no-dev/AsyncTCP` | `3.3.2` | Match dome |
| `bblanchon/ArduinoJson` | latest stable | |
| `bolderflight/sbus` | latest stable | |
| `madhephaestus/ESP32Servo` | latest stable | |
| Arduino `Preferences` | — | NVS |
| Arduino `LittleFS` | — | Web assets |
| Arduino `ArduinoOTA` | — | OTA |

**No Reeltwo on the body.** Reeltwo belongs in the dome where Marcduino dispatch is the main job. protoArtoo's body only needs to parse the small subset of commands the dome sends it — a lightweight line-buffer parser with a prefix switch is all that's needed.

```ini
[env:protoArtoo]
platform = espressif32@5.2.0
board = esp32dev
framework = arduino
build_flags =
    -Os
    -DESP32_ARDUINO_NO_RGB_BUILTIN
    -DPA_ENABLE_AUDIO=1
    -DPA_ENABLE_ARMS=1
    -DPA_ENABLE_DOME_MOTOR=1
    -DPA_ENABLE_DOME_SBUS=1          ; SBUS receiver #2 = dome spin control (GPIO 13)
    -DPA_AUDIO_DRIVER=AUDIO_DY_SV5W    ; change to swap audio module
    -DPA_DROID_NAME="R2-D2"
    ; No -DUSE_REELTWO, no -DMARC_SLAVE, no -DMARC_MASTER
    ; protoArtoo is not a MarcDuino node.
```

**`include/config.h` — confirmed and provisional GPIO assignments:**

```cpp
// =============================================================================
// protoArtoo — config.h
// GPIO pin assignments for Artoo Controller PCB (ESP32 — WROOM-32 or D1 Mini, TBD)
//
// CONFIRMED from official Artoo firmware documentation:
//   Motor controller, servos, dome ESC — verified against official hardware ref.
// CONFIRMED from PCB schematic (EasyEDA rev 1.0) and logic analysis:
//   SBUS receivers, USB serial, serial connector layout.
// TBD — must be confirmed by physical trace:
//   Dome serial (slip ring), audio module UART
// =============================================================================

// --- Hoverboard drive (ESP32 UART2 → hoverboard controller UART) -------------
// GPIO pins TBD — confirm by PCB trace
// Note: "UART3" in hoverboard firmware docs refers to the hoverboard controller's
// UART3 peripheral (sensor cable header, 5V tolerant). On GD32F130 boards this is
// UART3; on STM32 boards the port may differ. The ESP32 side uses UART2.
#define PIN_HOVERBOARD_TX   TBD  // ESP32 UART2 TX → hoverboard RX (sensor cable)
#define PIN_HOVERBOARD_RX   TBD  // ESP32 UART2 RX ← hoverboard TX (sensor cable)

// --- RC input (SBUS — confirmed) ---------------------------------------------
#define PIN_SBUS_DRIVE      15   // SBUS receiver #1 — drive (speed/steer/speed-limit)
#define PIN_SBUS_DOME       13   // SBUS receiver #2 — dome spin motor

// --- Servos (LEDC PWM — confirmed from official firmware) --------------------
#define PIN_ARM_SERVO_1     23   // Utility arm top/left   — Arm 1
#define PIN_ARM_SERVO_2      5   // Utility arm bottom/right — Arm 2
#define PIN_DOME_ESC        25   // Dome rotation motor ESC signal

// --- Dome serial bidirectional (slip ring — TBD) -----------------------------
#define PIN_DOME_SERIAL_TX  TBD  // UART1 TX → dome RX via slip ring
#define PIN_DOME_SERIAL_RX  TBD  // UART1 RX ← dome TX via slip ring

// --- Audio module UART (TBD) -------------------------------------------------
#define PIN_AUDIO_TX        TBD  // Software serial TX → audio module RX
// PIN_AUDIO_RX not required for DY-SV5W (TX-only control)

// --- ADC (battery monitoring — ADC1 only, ADC2 unusable with WiFi active) ---
#define PIN_BATTERY_ADC     34   // ADC1 CH6 — voltage divider input (TBD confirm)
```

### 7.2 FreeRTOS Task Architecture

```
Core 0 — Network & Config
├── WiFiManagerTask
├── WebServerTask       ESPAsyncWebServer — UI + REST API
└── OTATask

Core 1 — Real-time Control
├── SBUSInputTask       100 Hz — decode → RobotState
├── DriveTask           50 Hz — Gen2.x hoverboard frames (ESP32 UART2 → GD32 UART3)
├── DomeLinkTask        Marcduino TX (body→dome) + RX (dome→body) + heartbeat
├── AudioTask           event-driven — multi-source queue → audio module
├── ServoTask           50 Hz — LEDC PWM arm control
└── SafetyMonitorTask   Periodic state audit — see Section 7.8 note
```

> **SafetyMonitorTask note:** The active failsafe logic (SBUS watchdog, TWDT feed, web timeout) lives inside `SBUSInputTask` and `DriveTask` where it runs inline with the data it monitors. `SafetyMonitorTask` is a lower-priority audit task that runs at 10 Hz and handles secondary checks: logs `failsafeTriggerCount` increases to serial debug, verifies `domeConnected()` state transitions and posts UI SSE events, and monitors free heap — posting a warning if it drops below a safe threshold. It does not directly control motors. `src/tasks/safety.cpp`.

### 7.3 DomeLinkTask — Bidirectional Design

Two independent execution paths within one task loop:

**TX path** (body-initiated, proactive):
- Drains `domeTxQueue` → writes to dome UART TX
- Sends `#PAHB\r` health heartbeat on timer (~1 Hz)
- Increments `bodyHeartbeatCount` counter

**RX path** (dome-initiated, reactive):
- Line-buffer reader: accumulates chars until `\r`
- On complete line: intercept `#APHB` (dome heartbeat) **before** calling `parse_dome_rx(buf)`
- `#APHB` → update `robotState.domeHbRx` and `robotState.domeLastSeenMs`, do NOT forward to queue
- Everything else → `parse_dome_rx(buf)`

> **This mirrors the dome's own design:** the dome intercepts `#PAHB` before Reeltwo's `CommandEvent::process()`. The body must do the same for `#APHB` — if it falls through to `parse_dome_rx()`, the `#` prefix routes it to `configQueue` incorrectly.

```cpp
// ────────────────────────────────────────────────────────────────────────────
// src/drivers/marcduino_rx.cpp
//
// ARCHITECTURAL NOTE for anyone familiar with classic MarcDuino builds:
//
// In the classic topology, the body controller (MarcDuino Body Master) only
// RECEIVED commands from SHADOW. The Dome Master never sent commands TO the body.
//
// In protoArtoo, AstroPixelsPlus actively sends body-side commands over the
// same slip-ring link that carries body→dome Marcduino. This function handles
// those inbound dome→body commands.
//
// Prefixes processed:
//   '$'  → AudioTask (body audio module — sole sound source)
//   ':'  → (:SE30-:SE36) ServoTask body sequences
//          (:SE01-:SE16) sound component → AudioTask
//          (:OP/:CL/:MV) direct arm servo → ServoTask
//   '#'  → ConfigTask
//
// Prefixes discarded (not body concerns, no slave board):
//   '*'  holoprojectors
//   '@'  logic display
//   '%'  slave pass-through — no slave in protoArtoo topology
//   '&'  I2C
//   '!'  alt commands
// ────────────────────────────────────────────────────────────────────────────
void parse_dome_rx(const char* line) {
    if (!line || line[0] == '\0') return;

    switch (line[0]) {
        case '$':
            xQueueSend(audioQueue, &(AudioCmd{SRC_DOME_SERIAL, line}), 0);
            break;

        case ':':
            if (strncmp(line + 1, "SE", 2) == 0) {
                int id = atoi(line + 3);
                if (id >= 30 && id <= 36)
                    xQueueSend(servoQueue, &(ServoCmd{BODY_SEQUENCE, id}), 0);
                else if (id >= 1 && id <= 16)
                    xQueueSend(audioQueue, &(AudioCmd{SRC_DOME_SERIAL, line}), 0);
            } else if (strncmp(line+1,"OP",2)==0 ||
                       strncmp(line+1,"CL",2)==0 ||
                       strncmp(line+1,"MV",2)==0) {
                xQueueSend(servoQueue, &(ServoCmd{DIRECT_SERVO, line}), 0);
            }
            break;

        case '#':
            xQueueSend(configQueue, &(ConfigCmd{line}), 0);
            break;

        default:
            break; // *, @, %, &, ! — discard silently
    }
}
```

### 7.4 RobotState

```cpp
struct RobotState {
    // --- Drive output (written by DriveTask) ---------------------------------
    int16_t  driveSpeed;         // Current commanded speed  (-1000..+1000)
    int16_t  driveSteer;         // Current commanded steer  (-1000..+1000)
    float    domeTargetSpeed;

    // --- Subsystem state -----------------------------------------------------
    bool     audioActive;
    bool     armOpen[2];         // [0]=left/top  [1]=right/bottom
    bool     sleepMode;

    // --- Failsafe state (see Section 7.8) ------------------------------------
    bool     estop;              // Hard estop active — motors zeroed, not clearable by RC
    bool     sbusSignalLost;     // Receiver #1 (drive) watchdog fired — no valid frame >SBUS_TIMEOUT_MS
    bool     sbusHwFailsafe;     // Receiver #1 hardware failsafe flag
    bool     sbus2SignalLost;    // Receiver #2 (dome spin) watchdog — dome motor stops, drive continues
    bool     webDriveExpired;    // Web API drive command timed out
    FailsafeSource failsafeSource; // Which layer triggered current failsafe

    // --- Command source tracking (for failsafe timeout logic) ----------------
    CommandSource lastDriveSource;    // Who last commanded a non-zero drive
    uint32_t      lastDriveCommandMs; // millis() of last drive command (any source)

    // --- Timestamps ----------------------------------------------------------
    uint32_t lastSbus1Ms;        // millis() of last valid frame from receiver #1 (drive)
    uint32_t lastSbus2Ms;        // millis() of last valid frame from receiver #2 (dome spin)
    uint32_t lastDomeRxMs;       // millis() of last command received FROM dome

    // --- CH8 speed limit (receiver #1) --------------------------------------
    float    speedLimitScale;    // 0.0–1.0 derived from CH8 value
    bool     stationary;         // true when ch8_mode_lock=true AND CH8 at zero

    // --- Health counters (visible in /api/status) ----------------------------
    uint32_t bodyHbTx;           // #PAHB heartbeats sent to dome
    uint32_t domeHbRx;           // #APHB heartbeats received from dome
    uint32_t domeLastSeenMs;
    uint32_t failsafeTriggerCount; // Lifetime count of failsafe events this boot

    bool domeConnected() const {
        return domeHbRx > 0 && (millis() - domeLastSeenMs) < 5000UL;
    }

    // --- NVS-backed config (loaded at boot, tasks read these, never call NVS directly) ---
    int16_t  cfg_speedLimitMax;      // Hard speed ceiling (default SPEED_LIMIT_MAX)
    uint32_t cfg_sbusTimeoutMs;      // SBUS loss timeout (default 200)
    uint32_t cfg_webDriveTimeoutMs;  // Web drive command expiry (default 500)
    bool     cfg_ch8ModeLock;        // CH8 binary mode-lock (default false)
    uint16_t cfg_sndRandMin;         // Random sound track range min
    uint16_t cfg_sndRandMax;         // Random sound track range max
    uint8_t  cfg_volume;             // Audio volume 0-30
    uint16_t cfg_startupSound;       // Startup track (0 = none)
    bool     cfg_randomSounds;       // Random sounds enabled
    uint16_t cfg_randomIntervalS;    // Interval between random sounds (seconds)
};

enum FailsafeSource : uint8_t {
    FS_NONE            = 0,
    FS_SBUS_TIMEOUT    = 1,   // No SBUS frame received within timeout window
    FS_SBUS_HW         = 2,   // Receiver #1 hardware failsafe flag
    FS_SBUS2_TIMEOUT   = 3,   // Receiver #2 (dome spin) lost — dome stops, NOT a drive failsafe
    FS_WEB_TIMEOUT     = 4,   // Web API drive command expired
    FS_ESTOP_CMD       = 5,   // Explicit estop from web UI or serial debug
    FS_WATCHDOG_RESET  = 6,   // Recovering from hardware watchdog reset
};
```

### 7.5 Command Source Tagging

```cpp
enum CommandSource {
    SRC_SBUS,           // RC transmitter
    SRC_WEB_API,        // REST API
    SRC_DOME_SERIAL,    // Dome-initiated via slip ring RX
    SRC_SERIAL_USB,     // Debug serial
};
// Makes log lines like "[AUDIO] SRC_DOME_SERIAL $S → track 12" unambiguous
```


### 7.6 NVS Namespace and Boot Sequence

#### NVS Namespace

protoArtoo uses the NVS namespace `"proto"` for all stored preferences, following the same convention as the dome firmware (`"astro"`). This prevents any key collisions if both firmwares ever run on the same development chip.

```cpp
// All NVS reads/writes use this namespace
Preferences nvs;
nvs.begin("proto", false);   // false = read/write
```

Key naming uses lowercase with underscores: `vol_limit`, `ch8_mode_lock`, `sbus_timeout_ms`, etc. Full key list is in `src/web/nvs_config.cpp`.

**NVS access pattern — tasks never call NVS directly:**

```cpp
// src/web/nvs_config.cpp — called once in setup() after nvs.begin()
void loadConfigToState(Preferences& nvs, RobotState& state) {
    state.cfg_speedLimitMax    = nvs.getInt("spd_max",    SPEED_LIMIT_MAX);
    state.cfg_sbusTimeoutMs    = nvs.getInt("sbus_tmout", SBUS_TIMEOUT_MS);
    state.cfg_webDriveTimeoutMs= nvs.getInt("web_tmout",  WEB_DRIVE_TIMEOUT_MS);
    state.cfg_ch8ModeLock      = nvs.getBool("ch8_mode_lock", false);
    state.cfg_sndRandMin       = nvs.getInt("snd_rand_min", 1);
    state.cfg_sndRandMax       = nvs.getInt("snd_rand_max", 99);
    state.cfg_volume           = nvs.getInt("vol_limit",  20);
    state.cfg_startupSound     = nvs.getInt("snd_startup", 0);
    state.cfg_randomSounds     = nvs.getBool("snd_random", true);
    state.cfg_randomIntervalS  = nvs.getInt("snd_interval", 10);
    // ... all other NVS-backed settings
}
// FreeRTOS tasks read state.cfg_* fields only — never call nvs.getBool() directly.
// When web UI changes a setting: API handler writes to NVS, then updates state.cfg_*
// field under portMUX protection. No task restart needed.
```

The `Preferences nvs` object lives in `nvs_config.cpp` as a file-scoped global, opened once in `setup()` and kept open for the lifetime of the firmware. Web API config handlers access it through `nvs_config.cpp` functions only.

#### Boot Sequence — `setup()` Order

The startup order matters. Hoverboard UART must open before WiFi starts (ADC2 conflict), and the async web server must be event-gated behind the WiFi stack.

```
1. Serial0 (USB debug) — begin 115200
2. Check esp_reset_reason() — if ESP_RST_TASK_WDT, set estop=true, failsafeSource=FS_WATCHDOG_RESET
3. NVS open ("proto") — load all saved config
4. LEDC PWM init — arm servos and dome ESC to safe positions (arms closed, ESC at zero)
5. UART2 begin (115200, GPIO TBD from trace) — hoverboard (connects to hoverboard controller sensor-cable header). Send 5 zero frames immediately.
6. SBUS receivers begin — RMT mode (GPIO 15 drive receiver, GPIO 13 dome receiver, 100K 8E2 inverted)
7. TWDT init — esp_task_wdt_init(WATCHDOG_TIMEOUT_S, true)
8. Audio module UART begin — play startup sound if configured
9. WiFi.onEvent() registered — initAsyncWeb() called ONLY from event callback
10. WiFi.mode(WIFI_AP_STA) + softAP("protoArtoo", ...) + WiFi.begin(sta_ssid, sta_pass)
11. FreeRTOS tasks launched:
      Core 1: SBUSInputTask, DriveTask, DomeLinkTask, AudioTask, ServoTask
      Core 0: WiFiManagerTask, OTATask
      (WebServerTask starts inside initAsyncWeb() when WiFi event fires)
12. ArduinoOTA begin
```

> **Critical:** Step 5 (hoverboard UART2) before step 10 (WiFi). The ESP32 ADC2 is unusable while WiFi is active. UART2 does not conflict with WiFi. Establishing the hoverboard connection early means zero frames are flowing before any other subsystem starts — the droid is in a known-safe state from the first millisecond.

> **Critical:** Step 2 (watchdog reset detection). If the firmware crashed and the TWDT fired, the droid must not silently resume driving. `estop = true` on watchdog reset is a hard requirement — the operator must explicitly clear it via web UI before the droid will accept drive commands again.
### 7.7 WiFi — AP + STA

```cpp
// Gate initAsyncWeb() behind WiFi event — avoids tcpip_api_call crashloop
WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    if (event == ARDUINO_EVENT_WIFI_AP_START ||
        event == ARDUINO_EVENT_WIFI_STA_CONNECTED) {
        initAsyncWeb();   // Only here — never directly in setup()
    }
});
WiFi.mode(WIFI_AP_STA);
WiFi.softAP("protoArtoo", AP_PASSWORD);  // Always on, 192.168.4.1
// Note: original Artoo firmware uses "Artoo Inventions" / 10.10.10.10
// protoArtoo uses standard ESP32 AP defaults with project name as SSID
WiFi.begin(saved_ssid, saved_password);  // STA optional, fails silently
```

### 7.8 Multi-Layer Drive Failsafe

> **This is a physical safety requirement, not optional.**
> A droid at 20 kg moving at walking speed carries enough momentum to injure a person or damage property. Any single point of failure — radio dropout, code bug, loose antenna — must result in immediate motor stop, not continued motion.

#### The failure modes to protect against

| Failure | What happens without protection |
|---|---|
| RC transmitter switched off / out of range | Hoverboard holds last received speed command indefinitely |
| SBUS cable pulls loose | No frames — same result as above |
| Web API client disconnects without sending stop | Drive continues at whatever speed was last commanded |
| ESP32 task hang / stack overflow | DriveTask stops sending frames — hoverboard self-stops after ~500 ms, but this is slow |
| Firmware bug sends max speed | No mitigation if we trust commands blindly |
| RC transmitter battery dies mid-session | Receiver enters hardware failsafe mode |

#### Five defence layers

```
Layer 1:  SBUS receiver hardware failsafe  (receiver firmware)
Layer 2:  SBUS software watchdog            (SBUSInputTask)
Layer 3:  Web API drive command timeout     (DriveTask)
Layer 4:  ESP32 task watchdog timer (TWDT)  (hardware)
Layer 5:  Hoverboard own UART timeout       (hoverboard firmware)
```

Each layer is independent. Layers 1–3 are active protective measures that act in milliseconds. Layers 4–5 are backstops for scenarios where layers 1–3 cannot run.

---

#### Layer 1 — SBUS receiver hardware failsafe

Your RC receiver has a built-in failsafe that activates when the transmitter signal is lost (typically ~100 ms of no signal). When activated, the receiver either:
- Outputs all channels at a pre-programmed safe value (usually centre/zero), **or**
- Sets the SBUS hardware failsafe flag in the status byte (byte 23, bit 3)

**Required setup (one-time, done at the transmitter/receiver, not in firmware):**
Program your receiver's failsafe with throttle/speed channel at zero and all other channels at centre. Consult your specific receiver documentation. This is a hardware-level guarantee that runs independent of the ESP32.

The `bolderflight/sbus` library exposes both flags:
```cpp
sbus_rx.failsafe()    // true = receiver hardware failsafe activated
sbus_rx.lost_frame()  // true = individual frame was lost (less severe)
```

`SBUSInputTask` checks `failsafe()` on every decoded frame and sets
`robotState.sbusHwFailsafe` accordingly.

---

#### Layer 2 — SBUS software watchdog (primary real-time protection)

This is the most important layer for normal operation.

```cpp
// -------------------------------------------------------------------------
// include/config.h — failsafe timing constants
// -------------------------------------------------------------------------
#define SBUS_TIMEOUT_MS       200   // No valid SBUS frame for this long = failsafe
                                    // At 100 Hz, 200 ms = 20 missed frames.
                                    // Conservative: detects loss quickly but ignores
                                    // a few dropped frames from interference.

#define WEB_DRIVE_TIMEOUT_MS  500   // Web API drive command expires after this long
                                    // Client must re-send /api/drive to keep moving.
                                    // Prevents runaway if browser tab closes.

#define SPEED_LIMIT_MAX      600    // Hard cap on |driveSpeed| regardless of source
                                    // 1000 = full hoverboard speed; 600 = ~60% cap
                                    // Prevents firmware bugs from commanding full speed
```

```cpp
// ─────────────────────────────────────────────────────────────────────────────
// src/tasks/sbus_input.cpp
//
// `rc_input_mode=dual_sbus` path (single_sbus uses a separate one-receiver path):
// TWO RECEIVERS — DEDICATED ROLES, NOT REDUNDANCY:
//   sbus_drive (GPIO 15, UART): speed (CH1), steer (CH2), speed-limit dial (CH8)
//   sbus_dome  (GPIO 13, UART): dome spin speed (CH1)
//
// Losing sbus_drive  → drive failsafe (motors stop)         — safety event
// Losing sbus_dome   → dome motor stops, drive continues     — non-safety event
// ─────────────────────────────────────────────────────────────────────────────

// RMT mode — uses ESP32 RMT peripheral, NOT hardware UART.
// This frees UART1 (dome serial) and UART2 (hoverboard) from SBUS conflicts.
// bolderflight/sbus supports RMT natively on ESP32.
static Sbus sbus_drive(PIN_SBUS_DRIVE, true);  // RMT, GPIO 15, inverted
static Sbus sbus_dome (PIN_SBUS_DOME,  true);  // RMT, GPIO 13, inverted

// setDriveCommand(): thread-safe write to RobotState drive fields.
// Must be called from SBUSInputTask or WebServerTask only — never from DriveTask.
static portMUX_TYPE driveStateMux = portMUX_INITIALIZER_UNLOCKED;

void setDriveCommand(int16_t speed, int16_t steer, CommandSource src)
{
    portENTER_CRITICAL(&driveStateMux);
    robotState.driveSpeed         = speed;
    robotState.driveSteer         = steer;
    robotState.lastDriveSource    = src;
    robotState.lastDriveCommandMs = millis();
    portEXIT_CRITICAL(&driveStateMux);
}

void SBUSInputTask(void* params)
{
    for (;;)
    {
        // ── Receiver #1 — Drive ──────────────────────────────────────────────
        if (sbus_drive.Read())
        {
            auto ch = sbus_drive.ch();
            robotState.lastSbus1Ms    = millis();
            robotState.sbusHwFailsafe = sbus_drive.failsafe();
            robotState.sbusSignalLost = false;

            if (sbus_drive.failsafe())
            {
                setDriveCommand(0, 0, SRC_SBUS);
                robotState.failsafeSource = FS_SBUS_HW;
                robotState.failsafeTriggerCount++;
            }
            else
            {
                // CH8 speed-limit dial: scale 0.0–1.0 from raw SBUS value
                float scale = (ch[7] - SBUS_MIN) / float(SBUS_MAX - SBUS_MIN);  // CH8 (0-indexed)
                scale = constrain(scale, 0.0f, 1.0f);
                robotState.speedLimitScale = scale;

                // Mode-lock (optional NVS setting): CH8 at zero = Stationary Mode
                if (robotState.cfg_ch8ModeLock && scale < 0.02f)
                {
                    robotState.stationary = true;
                    setDriveCommand(0, 0, SRC_SBUS);
                }
                else
                {
                    robotState.stationary = false;
                    int16_t maxOut = (int16_t)(SPEED_LIMIT_MAX * scale);
                    int16_t speed  = constrain(mapSbusToSpeed(ch[0]), -maxOut, maxOut);
                    int16_t steer  = constrain(mapSbusToSteer(ch[1]), -maxOut, maxOut);
                    setDriveCommand(speed, steer, SRC_SBUS);
                }
            }
        }

        // ── Receiver #1 watchdog ─────────────────────────────────────────────
        if ((millis() - robotState.lastSbus1Ms) > SBUS_TIMEOUT_MS && !robotState.sbusSignalLost)
        {
            robotState.sbusSignalLost = true;
            robotState.failsafeSource = FS_SBUS_TIMEOUT;
            robotState.failsafeTriggerCount++;
            setDriveCommand(0, 0, SRC_SBUS);
        }

        // ── Receiver #2 — Dome spin (non-safety) ─────────────────────────────
#if PA_ENABLE_DOME_SBUS
        if (sbus_dome.Read())
        {
            robotState.lastSbus2Ms    = millis();
            robotState.sbus2SignalLost = false;
            if (!sbus_dome.failsafe())
            {
                // CH1 of dome receiver → dome motor speed
                float domeSpeed = mapSbusToDomeSpeed(sbus_dome.ch()[0]);
                portENTER_CRITICAL(&driveStateMux);
                robotState.domeTargetSpeed = domeSpeed;
                portEXIT_CRITICAL(&driveStateMux);
            }
            else
            {
                portENTER_CRITICAL(&driveStateMux);
                robotState.domeTargetSpeed = 0.0f;
                portEXIT_CRITICAL(&driveStateMux);
            }
        }

        // Receiver #2 watchdog: dome motor stops, drive is NOT affected
        if ((millis() - robotState.lastSbus2Ms) > SBUS_TIMEOUT_MS && !robotState.sbus2SignalLost)
        {
            robotState.sbus2SignalLost = true;
            // Note: FS_SBUS2_TIMEOUT does NOT set sbusSignalLost — drive continues
            portENTER_CRITICAL(&driveStateMux);
            robotState.domeTargetSpeed = 0.0f;
            portEXIT_CRITICAL(&driveStateMux);
        }
#endif

        vTaskDelay(pdMS_TO_TICKS(5));  // 200 Hz poll
    }
}
```

---

#### Layer 3 — Web API drive command timeout

When drive comes from the web UI (phone browser), the connection can silently vanish. The client must continuously re-send `/api/drive` to keep moving. If the last drive command was from the web API and nothing has arrived for `WEB_DRIVE_TIMEOUT_MS`, DriveTask zeroes the output.

```cpp
// -------------------------------------------------------------------------
// src/tasks/drive.cpp — timeout check inside DriveTask
// -------------------------------------------------------------------------
void DriveTask(void* params)
{
    uint32_t lastFrameMs = 0;

    for (;;)
    {
        // --- Failsafe check (runs before every frame) ----------------------
        int16_t speed = 0;
        int16_t steer = 0;

        bool failsafeActive =
            robotState.sbusSignalLost     ||
            robotState.sbusHwFailsafe     ||
            robotState.estop;

        // Web API timeout check (only applies when web was the last drive source)
        if (!failsafeActive &&
            robotState.lastDriveSource == SRC_WEB_API &&
            (millis() - robotState.lastDriveCommandMs) > WEB_DRIVE_TIMEOUT_MS)
        {
            robotState.webDriveExpired = true;
            robotState.failsafeSource  = FS_WEB_TIMEOUT;
            robotState.failsafeTriggerCount++;
            failsafeActive = true;
        }

        if (!failsafeActive)
        {
            // Apply hard speed cap regardless of source (bug protection)
            speed = constrain(robotState.driveSpeed, -SPEED_LIMIT_MAX, SPEED_LIMIT_MAX);
            steer = constrain(robotState.driveSteer, -SPEED_LIMIT_MAX, SPEED_LIMIT_MAX);
        }
        // else: speed and steer remain 0 — zero frame sent to hoverboard

        // --- Send frame to hoverboard at 50 Hz -----------------------------
        if (millis() - lastFrameMs >= 20)
        {
            lastFrameMs = millis();
            eferu_send(speed, steer);
            // eferu_send() ALWAYS sends a frame — even zero — never goes silent
        }

        esp_task_wdt_reset();  // Feed Layer 4 hardware watchdog
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
```

The hard `SPEED_LIMIT_MAX` cap in DriveTask is a last-resort software guard. Even if `setDriveCommand()` is somehow called with an out-of-range value (bug in SBUSInputTask mapping, integer overflow, corrupted memory), DriveTask clamps it before it reaches the hoverboard.

---

#### Layer 4 — ESP32 hardware task watchdog (TWDT)

The ESP32 has a Task Watchdog Timer that resets the chip if any monitored task stops calling `esp_task_wdt_reset()` within the configured timeout. DriveTask feeds it every loop (as shown above).

```cpp
// In setup() — configure TWDT
esp_task_wdt_init(3, true);          // 3-second timeout, panic (reset) on expiry
esp_task_wdt_add(driveTaskHandle);   // Only DriveTask is monitored
// DriveTask is the critical task — if it hangs, the whole system resets.
// On reboot: hoverboard receives no UART frames and stops within ~500 ms (Layer 5).
```

A full system reset is preferable to a hung firmware sitting at unknown motor commands. After reset, protoArtoo boots into the safe state: speed=0, steer=0, failsafe active until first valid SBUS frame.

---

#### Layer 5 — Hoverboard firmware own UART timeout

The Gen2.x hoverboard firmware (RoboDurden) stops the motors if no valid UART frame is received for approximately 500 ms. This is the last line of defence — it is independent of protoArtoo firmware entirely and runs on the hoverboard controller's own GD32 MCU.

This is why the **zero-frame rule** (`eferu_send(0, 0)`) is important: as long as protoArtoo is alive and sending zero frames, the hoverboard never hits its own timeout. The timeout only saves us if the ESP32 itself crashes or loses power.

---

#### Failsafe state machine summary

```
Normal operation:
  SBUS frames arriving, no failsafe flags
    → DriveTask passes speed/steer with SPEED_LIMIT_MAX cap applied

SBUS signal lost (Layer 2):
  sbusSignalLost = true
    → DriveTask sends zero frames immediately
    → /api/status shows failsafe_source = "SBUS_TIMEOUT"
    → Web UI shows red "Signal Lost" banner
    → Clears automatically when valid SBUS frames resume

SBUS hardware failsafe (Layer 1+2):
  sbusHwFailsafe = true
    → Same as above + sbusHwFailsafe flag set
    → Clears when receiver reconnects and flag drops

Web drive timeout (Layer 3):
  webDriveExpired = true
    → DriveTask sends zero frames
    → Clears when new /api/drive command arrives

Hard estop (any layer or explicit):
  estop = true
    → DriveTask sends zero frames
    → Does NOT clear automatically — requires explicit POST /api/estop/clear
    → Use for: deliberate emergency stop from web UI, or post-watchdog-reset state
```

Failsafe clears:
- `sbusSignalLost` and `sbusHwFailsafe` clear automatically when valid SBUS resumes — this is intentional so normal RC operation requires no user action after a brief dropout.
- `webDriveExpired` clears on the next `/api/drive` command.
- `estop` requires an explicit clear (`POST /api/estop/clear`) — this is a deliberate human gate.

---

#### `/api/status` — failsafe fields

```json
{
  "failsafe": {
    "active":   true,
    "source":   "SBUS_TIMEOUT",
    "trigger_count": 3,
    "sbus_signal_lost": true,
    "sbus_hw_failsafe": false,
    "web_drive_expired": false,
    "estop": false
  },
  "sbus": {
    "ok": false,
    "last_frame_ms": 4821,
    "hw_failsafe": false,
    "lost_frame": false
  }
}
```

`trigger_count` counts how many times failsafe has fired since last reboot. Useful for detecting an intermittent loose antenna — the droid behaves normally but `trigger_count` climbs over a session.

---

#### Web UI — failsafe indicators

The dashboard must make failsafe state unmissable:

```
Normal:   [READY] green header bar, no banner
Failsafe: [FAILSAFE — SIGNAL LOST] red full-width banner, drive controls disabled
Estop:    [ESTOP ACTIVE] red full-width banner + "Clear Estop" button
```

Drive controls (speed slider, direction buttons) in the web UI must be visually disabled and unresponsive when any failsafe is active. This prevents the web UI from appearing to accept commands while the droid is not responding to them.

### 7.9 NVS Safety Configuration

```cpp
// These are configurable in the web UI config page, stored in NVS
// They have sensible defaults and should not need changing in normal use

#define SBUS_TIMEOUT_MS      200    // ms before SBUS loss declared
#define WEB_DRIVE_TIMEOUT_MS 500    // ms before web drive command expires
#define SPEED_LIMIT_MAX      600    // hard cap on drive output (0-1000)
#define WATCHDOG_TIMEOUT_S     3    // ESP32 TWDT reset timeout
```

---

## 8. Coding Standards and Best Practices

> **These are non-negotiable requirements for all protoArtoo code, not suggestions.**
> Every file committed to the repository must follow these standards. The project is safety-critical (a 20 kg robot), community-facing (open source), and multi-year (you will forget context). Good comments and testable structure are part of the deliverable.

---

### 8.1 Code Commenting — Multi-Level Standard

**The rule:** every non-trivial block of code must have a comment that explains the *why*, not just the *what*. Use indented depth to match comment specificity to code specificity.

#### File header (every `.cpp` and `.h` file)

```cpp
// =============================================================================
// src/tasks/drive.cpp
//
// DriveTask — sends 8-byte Gen2.x frames to the hoverboard motor controller.
//
// ARCHITECTURE NOTE:
//   This task owns all writes to the hoverboard UART (ESP32 UART2). No other
//   task writes to it. Speed/steer values are read from RobotState under
//   portMUX critical section — see setDriveCommand() in robot_state.cpp.
//
// PROTOCOL:
//   8-byte XOR-checksum frame at 115200 baud 8N1, sent at 50 Hz continuously.
//   Silence causes hoverboard self-stop after ~500 ms — we never go silent.
//   Ref: https://github.com/RoboDurden/Hoverboard-Firmware-Hack-Gen2.x-GD32
//
// FAILSAFE:
//   Speed is hard-capped at SPEED_LIMIT_MAX before frame construction.
//   See Section 7.8 of the firmware plan for the full 5-layer failsafe design.
//
// LAST MODIFIED: 2026-03
// =============================================================================
```

#### Function header (every non-trivial function)

```cpp
// -----------------------------------------------------------------------------
// sendHoverboardFrame()
//
// Builds and sends one 8-byte Gen2.x hoverboard command frame over UART2.
//
// Params:
//   speed  — forward/reverse, range -1000..+1000 (0 = stop)
//   steer  — left/right differential,  range -1000..+1000 (0 = straight)
//
// Called by: DriveTask at 50 Hz.
// Thread safety: called only from DriveTask — no locking needed here.
// Hardware: UART2 at 115200 baud 8N1, GPIO pins from config.h TBD.
//
// Ref: Frame format documented at
//   https://github.com/RoboDurden/Hoverboard-Firmware-Hack-Gen2.x-GD32
//   src/config.h → INPUT_TYPE_UART / steer+speed structure
// -----------------------------------------------------------------------------
static void sendHoverboardFrame(int16_t speed, int16_t steer) {
```

#### Inline block comments (inside functions — 3-level depth example)

```cpp
void DriveTask(void* pvParams) {

    // ── Level 1: What this block does ─────────────────────────────────────────
    // Main 50 Hz control loop. Reads RobotState, applies failsafe logic,
    // builds UART frame, and sends it. Never exits — TWDT registered here.

    while (true) {

        // ── Level 2: Why this sub-block exists ────────────────────────────────
        // Feed the ESP32 Task Watchdog Timer. If this line is not reached
        // within WATCHDOG_TIMEOUT_S seconds, the TWDT fires a chip reset.
        // This is intentional — a stuck DriveTask is a safety event.
        // Ref: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/
        //      api-reference/system/wdts.html
        esp_task_wdt_reset();

        // ── Level 3: Specific implementation detail ───────────────────────────
        // portMUX critical section — must complete atomically with
        // setDriveCommand() writes from SBUSInputTask (Core 1) and
        // WebServerTask (Core 0). See robot_state.h for mutex declaration.
        portENTER_CRITICAL(&driveStateMux);
        int16_t spd = robotState.driveSpeed;
        int16_t str = robotState.driveSteer;
        bool    fs  = robotState.estop || robotState.sbusSignalLost;
        portEXIT_CRITICAL(&driveStateMux);

        if (fs) {
            // Failsafe active — send zero frame but DO NOT go silent.
            // Hoverboard requires continuous frames to stay in controlled state.
            // Ref: zero-frame rule, Section 7.8 / 6.1 of firmware plan.
            spd = 0; str = 0;
        }

        sendHoverboardFrame(spd, str);
        vTaskDelay(pdMS_TO_TICKS(20));  // 20 ms = 50 Hz
    }
}
```

**Rule summary:**
- Level 1 comments explain the *purpose* of a block (full sentence, standalone readable)
- Level 2 comments explain *why* a design decision was made (not obvious from code)
- Level 3 comments explain *tricky implementation details* and link to references
- External reference links go on the comment line or the line below the comment — not buried in a README

---

### 8.2 PlatformIO Project Configuration

#### `platformio.ini` — required structure

```ini
; =============================================================================
; platformio.ini — protoArtoo build configuration
;
; Environments:
;   protoArtoo   — firmware upload to Artoo Controller PCB
;   native       — desktop unit tests (no hardware required)
;
; Ref: https://docs.platformio.org/en/latest/projectconf/index.html
; Ref: https://docs.platformio.org/en/latest/platforms/espressif32.html
; =============================================================================

[env:protoArtoo]
platform    = espressif32@5.2.0          ; pin platform version — never use @latest
board       = esp32dev
framework   = arduino
monitor_speed = 115200
upload_protocol = esptool

; OTA upload — use the dedicated env (do not uncomment here):
; [env:protoArtoo_ota]
; extends         = env:protoArtoo
; upload_protocol = espota
; upload_port     = 10.0.0.22            ; STA client IP (not AP 192.168.4.1)

; Partition table with OTA support (2 OTA slots + NVS + SPIFFS/LittleFS)
board_build.partitions = partitions_ota.csv
; Ref: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/
;      partition-tables.html

build_flags =
    -Os                                  ; optimise for size — 4 MB flash
    -Wall                                ; all warnings
    -Wextra                              ; extra warnings
    -Werror                              ; treat warnings as errors (enforce quality)
    -DESP32_ARDUINO_NO_RGB_BUILTIN
    -DPA_ENABLE_AUDIO=1
    -DPA_ENABLE_ARMS=1
    -DPA_ENABLE_DOME_MOTOR=1
    -DPA_ENABLE_DOME_SBUS=1
    -DPA_AUDIO_DRIVER=AUDIO_DY_SV5W
    -DPA_DROID_NAME='"R2-D2"'

; Test config — on-device tests only in protoArtoo env
test_build_src = true
test_ignore    = test_native

lib_deps =
    me-no-dev/ESPAsyncWebServer @ 3.5.1  ; pin exact versions — no ^ or ~
    me-no-dev/AsyncTCP          @ 3.3.2
    bblanchon/ArduinoJson
    bolderflight/sbus
    madhephaestus/ESP32Servo

; =============================================================================
[env:native]
; Desktop unit tests — pure logic, no ESP32 hardware. Runs on dev machine.
; Tests in test/test_native/ only. No GPIO, no UART, no FreeRTOS.
; Ref: https://docs.platformio.org/en/latest/advanced/unit-testing/
;      frameworks/unity.html
; =============================================================================
platform   = native
test_ignore = test_embedded
build_flags = -std=c++17 -Wall -Wextra
```

#### `.clang-format` — required at repo root

```yaml
# .clang-format — protoArtoo code style
# Style: Google base with embedded-friendly adjustments
# Run: clang-format -i src/**/*.cpp src/**/*.h
# Ref: https://clang.llvm.org/docs/ClangFormatStyleOptions.html
BasedOnStyle:  Google
IndentWidth:   4
ColumnLimit:   100
AlignTrailingComments: true
SortIncludes:  false    # do not reorder — hardware header order matters
```

#### `.gitignore` additions

```
.pio/           # PlatformIO build cache — never commit
.vscode/        # VS Code local settings
*.elf
*.bin
*.map
```

---

### 8.3 Testing — Two-Environment Strategy

protoArtoo uses the **Unity test framework** via PlatformIO, split into two environments:

| Environment | Where it runs | What it tests | Speed |
|---|---|---|---|
| `native` | Dev machine (no hardware) | Pure logic: parsers, protocol frames, state machines | Fast — seconds |
| `protoArtoo` | ESP32 on-device | Hardware-dependent: UART timing, GPIO, FreeRTOS task interaction | Slow — requires flash + boot |

**Test folder layout:**
```
test/
├── test_native/                  ← runs in [env:native] only
│   ├── test_marcduino_rx.cpp     # prefix routing, parse_dome_rx() edge cases
│   ├── test_hoverboard_frame.cpp # frame checksum, boundary values, overflow
│   ├── test_sbus_channel_map.cpp # CH8 scale math, speed_limit_scale formula
│   └── test_audio_track_map.cpp  # $001 → track 1, $S → random, bounds
│
└── test_embedded/                ← runs in [env:protoArtoo] only
    ├── test_uart_loopback.cpp    # UART2 TX→RX loopback with known frame
    ├── test_nvs_roundtrip.cpp    # write NVS key, read back, verify
    └── test_failsafe_timing.cpp  # SBUS watchdog fires within SBUS_TIMEOUT_MS
```

**Example native test — Marcduino prefix router:**

```cpp
// test/test_native/test_marcduino_rx.cpp
//
// Unit tests for parse_dome_rx() — the body-side Marcduino command dispatcher.
//
// These tests run on the dev machine (no ESP32 hardware needed).
// They exercise every prefix branch and verify queue routing is correct.
//
// Ref: Section 7.3 of firmware plan (DomeLinkTask / marcduino_rx.cpp)
// Ref: https://docs.platformio.org/en/latest/advanced/unit-testing/

#include <unity.h>
#include "marcduino_rx.h"   // the module under test
#include "test_mocks.h"     // mock queues — capture what would go to audioQueue etc.

void setUp()    { clearMockQueues(); }
void tearDown() {}

void test_dollar_routes_to_audio() {
    // $ prefix must always route to AudioTask regardless of track number
    parse_dome_rx("$001");
    TEST_ASSERT_EQUAL(1,      mockAudioQueue.size());
    TEST_ASSERT_EQUAL_STRING("$001", mockAudioQueue.front().cmd);
}

void test_se30_routes_to_servo() {
    // :SE30 is a body arm sequence — must go to ServoTask, NOT AudioTask
    parse_dome_rx(":SE30");
    TEST_ASSERT_EQUAL(1, mockServoQueue.size());
    TEST_ASSERT_EQUAL(0, mockAudioQueue.size());
}

void test_star_prefix_discarded() {
    // * commands are holoprojectors (dome-side only) — body must silently discard
    parse_dome_rx("*ON00");
    TEST_ASSERT_EQUAL(0, mockAudioQueue.size());
    TEST_ASSERT_EQUAL(0, mockServoQueue.size());
    TEST_ASSERT_EQUAL(0, mockConfigQueue.size());
}

void test_empty_string_safe() {
    // Robustness: parser must not crash on empty or null input
    parse_dome_rx("");
    parse_dome_rx(nullptr);
    // No assertion needed — test passes if no crash/hang
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_dollar_routes_to_audio);
    RUN_TEST(test_se30_routes_to_servo);
    RUN_TEST(test_star_prefix_discarded);
    RUN_TEST(test_empty_string_safe);
    return UNITY_END();
}
```

**Run commands:**
```bash
pio test -e native              # fast — run all logic tests on dev machine
pio test -e protoArtoo          # slow — flash device and run hardware tests
pio test -e native -v           # verbose output
pio test -e native -f test_marcduino_rx  # run single test file
```

References:
- PlatformIO Unit Testing: https://docs.platformio.org/en/latest/advanced/unit-testing/
- Unity framework: https://github.com/ThrowTheSwitch/Unity
- ESP32 unit testing guide (Espressif): https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/unit-tests.html

---

### 8.4 Static Analysis

PlatformIO includes `cppcheck` and `clang-tidy` via `pio check`. Run before every commit.

```ini
; Add to platformio.ini under [env:protoArtoo]
check_tool     = cppcheck
check_flags    =
    cppcheck: --enable=all --suppress=missingIncludeSystem
check_severity = high, medium   ; fail build on high/medium findings
```

```bash
pio check                       # run static analysis
pio check --severity=high       # only fail on serious issues
```

Key issues `cppcheck` catches that matter for this codebase:
- Uninitialized variables in `RobotState` fields
- Buffer overflows in `parse_dome_rx()` line buffer
- Missing null checks before pointer dereference
- Integer overflow in checksum / speed scaling math

References:
- PlatformIO Check: https://docs.platformio.org/en/latest/advanced/static-code-analysis/
- cppcheck manual: https://cppcheck.sourceforge.io/manual.html

---

### 8.5 FreeRTOS-Specific Rules

These rules apply to every task and inter-task communication point. Violating them causes intermittent crashes that are extremely hard to reproduce.

#### Stack sizing

```cpp
// ── RULE: Always measure actual stack high-water mark in development ──────────
// Never guess stack size. Run the task, exercise all code paths,
// then read the high-water mark. Add 25% headroom.
//
// Ref: https://www.freertos.org/uxTaskGetStackHighWaterMark.html
UBaseType_t hwm = uxTaskGetStackHighWaterMark(driveTaskHandle);
Serial.printf("[STACK] DriveTask HWM: %u words free
", hwm);
// If hwm < 64 words, increase stack in xTaskCreatePinnedToCore().
```

#### Shared state — portMUX required

```cpp
// ── RULE: ALL reads AND writes to shared RobotState fields use portMUX ────────
// RobotState is written by SBUSInputTask (Core 1) and WebServerTask (Core 0).
// Without a critical section, reads in DriveTask see torn values.
//
// DO THIS:
portENTER_CRITICAL(&driveStateMux);
robotState.driveSpeed = newSpeed;
portEXIT_CRITICAL(&driveStateMux);

// NOT THIS (data race — undefined behaviour with dual-core ESP32):
robotState.driveSpeed = newSpeed;   // ← WRONG — no protection
```

#### Queue patterns

```cpp
// ── RULE: Always use timeout 0 for non-blocking sends from ISR/realtime tasks ─
// xQueueSend with portMAX_DELAY in a realtime task blocks the task indefinitely
// if the queue is full — this stalls the control loop and trips the TWDT.
//
// Use timeout 0 (non-blocking). If queue full, the command is dropped —
// this is correct behaviour for real-time control (stale commands are useless).
//
// Ref: https://www.freertos.org/a00117.html
xQueueSend(audioQueue, &cmd, 0);    // ← correct: non-blocking
xQueueSend(audioQueue, &cmd, portMAX_DELAY);  // ← WRONG in realtime task
```

#### Core assignment

```cpp
// ── Core assignment rule — document why each task is on its assigned core ─────
// Core 1: all real-time tasks (SBUS, Drive, DomeLink, Servo, Audio)
//   → keeps latency deterministic, no WiFi stack interruption
// Core 0: WiFi, web server, OTA
//   → WiFi stack naturally uses Core 0; isolating it avoids interference
//
// Ref: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/
//      api-guides/freertos-smp.html
xTaskCreatePinnedToCore(DriveTask, "DriveTask",
    4096,   // stack — confirm with uxTaskGetStackHighWaterMark()
    nullptr, 5, &driveTaskHandle,
    1);     // Core 1 — real-time control
```

References:
- FreeRTOS ESP32 docs: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/freertos.html
- ESP32 dual-core SMP guide: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/freertos-smp.html
- Task stack sizing: https://www.freertos.org/uxTaskGetStackHighWaterMark.html

---

### 8.6 Memory and Resource Rules

ESP32 has 520 KB SRAM. With WiFi active, usable heap is roughly 200–250 KB. FreeRTOS task stacks consume a significant portion. These rules prevent the heap fragmentation and stack overflow crashes that characterise poorly written ESP32 firmware.

```cpp
// ── RULE: No dynamic allocation after setup() ─────────────────────────────────
// malloc()/new inside tasks causes heap fragmentation over hours of uptime.
// All buffers must be statically sized and allocated at compile time or in
// task init — never inside a loop.
//
// WRONG:
void DomeLinkTask(void*) {
    while (true) {
        char* buf = new char[65];  // ← heap allocation in loop — causes fragmentation
        ...
        delete[] buf;
    }
}
// CORRECT:
void DomeLinkTask(void*) {
    char buf[65];  // ← static stack allocation — size known, freed automatically
    while (true) { ... }
}

// ── RULE: Monitor free heap in SafetyMonitorTask ──────────────────────────────
// Log a warning if free heap drops below 20 KB — this indicates a leak.
// Ref: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/
//      api-reference/system/heap_debug.html
uint32_t freeHeap = esp_get_free_heap_size();
if (freeHeap < 20000) {
    Serial.printf("[WARN] Low heap: %u bytes
", freeHeap);
}
```

---

### 8.7 Defensive Programming — Embedded Rules

```cpp
// ── RULE: Bounds-check every buffer write ────────────────────────────────────
// The line buffer in DomeLinkTask accumulates bytes until '
'.
// If the sender never sends '
' (corrupt slip ring data), the buffer overflows.
// Always check position before write, discard and reset on overflow.
void handleRxByte(uint8_t byte) {
    if (byte == '
') {
        rxBuf[rxPos] = '';
        parse_dome_rx(rxBuf);
        rxPos = 0;           // reset for next line
    } else {
        if (rxPos < sizeof(rxBuf) - 1) {
            rxBuf[rxPos++] = byte;
        } else {
            // Buffer full without '
' — corrupt/runaway data. Discard and reset.
            // This can happen on slip ring glitch; log and continue.
            Serial.printf("[WARN] DomeLink RX buffer overflow — discarding
");
            rxPos = 0;
        }
    }
}

// ── RULE: Assert critical invariants in DEBUG builds only ─────────────────────
// configASSERT() fires a panic in DEBUG builds — never ship assertions that
// would reboot a running droid. Use CONFIG_FREERTOS_ASSERT_FAIL_ABORT in
// debug, and static_assert for compile-time checks everywhere.
static_assert(SPEED_LIMIT_MAX <= 1000, "SPEED_LIMIT_MAX exceeds hoverboard range");
static_assert(SBUS_TIMEOUT_MS  >= 100, "SBUS timeout unreasonably short");

// ── RULE: Validate all external input before use ─────────────────────────────
// Web API, dome serial, and SBUS channels are all external inputs.
// Clamp before use — never trust that values are in expected range.
int16_t speed = constrain(rawSpeed, -SPEED_LIMIT_MAX, SPEED_LIMIT_MAX);
```

---

### 8.8 Serial Debug Logging

```cpp
// ── Pattern: TAG-prefixed log lines ──────────────────────────────────────────
// Every module defines its own TAG. Log lines are grep-able.
// Format: [TAG] event — value
// This matches ESP-IDF's ESP_LOG* convention and is readable in serial monitor.
//
// Ref: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/
//      api-reference/system/log.html

static const char* TAG = "DriveTask";

// Use Serial.printf() in Arduino framework — format matches ESP_LOGI style
Serial.printf("[%s] Failsafe active — source: %d, speed zeroed
", TAG, (int)src);
Serial.printf("[%s] Frame sent — spd:%d str:%d
", TAG, speed, steer);

// ── Build-flag-gated verbose logging ─────────────────────────────────────────
// Heavy per-frame logging must be gated by a build flag — at 50 Hz it
// saturates the serial port and starves other debug output.
#ifdef PA_VERBOSE_DRIVE
    Serial.printf("[%s] 50Hz frame — spd:%d str:%d csum:0x%04X
",
                  TAG, speed, steer, checksum);
#endif
```

Add to `platformio.ini` when debugging drive:
```ini
build_flags = ... -DPA_VERBOSE_DRIVE
```

---

### 8.9 Conventional Commits, Semantic Versioning, and Changelog

> protoArtoo is an open-source safety-critical firmware project. Its git history must be machine-readable, human-understandable, and unambiguous about what changed in every release. "vibecoded yolo commits" are explicitly forbidden.
>
> The three tools that enforce this together: **Conventional Commits** for message format, **Semantic Versioning** for release numbers, and **Keep a Changelog** for the human-readable release record.

---

#### Conventional Commits — commit message format

Every commit message follows this structure:

```
<type>(<scope>): <short description — imperative, lowercase, no period>

[optional body — what AND why, not how. Wrap at 72 chars.]

[optional footer(s) — BREAKING CHANGE:, Closes #n, Refs #n]
```

**Types and their effect on version numbers:**

| Type | When to use | Version bump |
|---|---|---|
| `feat` | New user-facing feature (new endpoint, new behaviour, new driver) | MINOR |
| `fix` | Bug fix or safety correction | PATCH |
| `feat!` or `BREAKING CHANGE:` footer | Change that breaks hardware wiring, API, or NVS key names | MAJOR |
| `refactor` | Code restructure, no behaviour change | none |
| `test` | Add or fix tests | none |
| `docs` | Documentation, comments, README, plan | none |
| `chore` | Build config, deps, `.gitignore`, CI | none |
| `style` | Formatting only (clang-format pass) | none |
| `perf` | Performance improvement, no behaviour change | none |

**Scopes for protoArtoo** — always use one:

| Scope | What it covers |
|---|---|
| `drive` | DriveTask, hoverboard UART frames, failsafe |
| `sbus` | SBUSInputTask, both receivers, CH8 dial |
| `failsafe` | Any of the 5 safety layers, estop, TWDT |
| `dome` | DomeLinkTask, bidirectional serial, heartbeat |
| `audio` | AudioTask, AudioDriver, DY-SV5W, track mapping |
| `servo` | ServoTask, arm servos, LEDC PWM |
| `web` | Web server, REST API, web UI, SSE |
| `nvs` | NVS config, Preferences, boot sequence |
| `wifi` | AP/STA, OTA, network manager |
| `hw` | Hardware config, pin map, PCB trace results |
| `plan` | Firmware plan document, architecture decisions |
| `test` | Test files, mocks, test infrastructure |
| `ci` | platformio.ini, static analysis, build flags |

**Real examples from protoArtoo:**

```
feat(drive): add CH8 speed-limit dial with linear scaling

CH8 on receiver #1 now scales drive output linearly from 0 to
SPEED_LIMIT_MAX. CH8 at minimum completely locks drive output.
This gives the operator a physical confidence dial for tight spaces.

NVS key ch8_mode_lock (default false) enables optional binary
mode-lock behaviour — see Section 6.7 of firmware plan.
```

```
fix(failsafe): set sbusSignalLost=true as boot default

Previously sbusSignalLost defaulted to false, allowing web API
drive commands before any SBUS frame was confirmed received.
This is a safety regression — the droid could be driven before
the RC system is confirmed present.

Boot default is now true; clears on first valid SBUS frame only.
```

```
feat(hw)!: confirm UART2 GPIO pins from PCB trace — update config.h

BREAKING CHANGE: PIN_HOVERBOARD_TX and PIN_HOVERBOARD_RX now have
real values from PCB trace. Any build using the TBD placeholder
values will fail to compile (intentional — TBD must not be flashed).

PIN_HOVERBOARD_TX = 17
PIN_HOVERBOARD_RX = 16
```

```
feat(dome): implement DomeLinkTask TX+RX with heartbeat protocol

Implements full bidirectional dome link:
- TX path: drains domeTxQueue, sends #PAHB heartbeat at 1 Hz
- RX path: line-buffer reader, intercepts #APHB before parse_dome_rx()
- Heartbeat state: connected / lost / not-seen, exposed in /api/status

Refs: body_dome_serial_link_spec.md, firmware plan Section 7.3
```

```
docs: mark dome fork implementation complete (Section 3)

Dome-side body link protocol implemented in mattiasbrandt/AstroPixelsPlus.
```

---

#### Semantic Versioning — release strategy

protoArtoo uses **SemVer 2.0.0** (`MAJOR.MINOR.PATCH`).

**Version number rules:**

| Increment | Triggered by | Example |
|---|---|---|
| **PATCH** x.x.+1 | `fix` commit — bug fix, safety correction, timing improvement | `v0.1.1` |
| **MINOR** x.+1.0 | `feat` commit — new working feature, new endpoint, new driver | `v0.2.0` |
| **MAJOR** +1.0.0 | `BREAKING CHANGE` — hardware pin change, API break, NVS key rename | `v1.0.0` |

**What constitutes a BREAKING CHANGE in this project:**
- GPIO pin number changes in `config.h` (breaks existing wired hardware)
- REST API endpoint rename or field removal (breaks web UI / external integrations)
- NVS key rename (existing saved config silently becomes invalid on flash)
- Hoverboard frame format change (breaks motor controller compatibility)
- Heartbeat command string change (`#PAHB`/`#APHB`) before dome fork is updated

**Pre-v1.0.0 development releases:**

| Version | Milestone condition |
|---|---|
| `v0.1.0` | Drive via RC, stops within 200 ms of TX off, CH8 locks drive, TWDT functional |
| `v0.2.0` | Control from phone browser, OTA working, AP name `protoArtoo` |
| `v0.3.0` | Arms move via web + RC, dome ESC responds |
| `v0.4.0` | Dome sequences trigger body audio + arms, heartbeat shows connected |
| `v1.0.0` | Community release — all tests passing, docs complete, no known safety issues |

> **Note on v0.x.x:** Pre-1.0, MINOR bumps may include breaking changes at the hardware/wiring level — the project is not yet declared stable. All breaking changes still require `BREAKING CHANGE:` footer and must be explicitly documented in CHANGELOG.

**Tagging convention:**

```bash
# After merging a milestone to main:
git tag -a v0.1.0 -m "Drive via RC with full failsafe"
git push origin v0.1.0

# Patch release after a bug fix:
git tag -a v0.1.1 -m "fix(failsafe): boot default sbusSignalLost=true"
git push origin v0.1.1
```

GitHub Releases are created from every tag with the relevant CHANGELOG section as the release body.

---

#### Branch strategy

```
main            — always releasable; tagged at every version
│
├── dev         — integration branch; phase work merges here first
│   │
│   ├── feature/phase1-drive-task
│   ├── feature/phase1-sbus-input
│   ├── fix/sbus-timeout-edge-case
│   └── docs/pin-map-phase0
```

Rules:
- `main` only receives merges from `dev` when the milestone condition is met and tests pass
- `dev` receives feature/fix branches via pull request — even as a solo project, PRs create a review checkpoint and a clean git history
- Direct commits to `main` forbidden except `docs:` and `chore:` with no code changes
- Feature branches named `feature/<phase>-<what>`, fix branches `fix/<what>`, doc branches `docs/<what>`

---

#### Keep a Changelog — CHANGELOG.md format

The project `CHANGELOG.md` at repo root follows [keepachangelog.com](https://keepachangelog.com) format, aligned with Semantic Versioning.

```markdown
# Changelog

All notable changes to protoArtoo are documented here.
Format: [keepachangelog.com](https://keepachangelog.com/en/1.1.0/)
Versioning: [semver.org](https://semver.org/)

## [Unreleased]
### Added
- (items not yet released go here)

## [0.1.0] — YYYY-MM-DD
### Summary
Drive via RC with full 5-layer failsafe system.

### Added
- DriveTask: Gen2.x hoverboard 8-byte frame at 50 Hz via ESP32 UART2
- SBUSInputTask: dual SBUS receivers in RMT mode (GPIO 15 drive, GPIO 13 dome spin)
- CH8 speed-limit dial with linear scaling and optional ch8_mode_lock mode
- 5-layer failsafe: SBUS HW failsafe, SBUS watchdog (200 ms), web drive timeout,
  ESP32 TWDT (3 s), hoverboard own timeout
- Watchdog reset detection on boot — sets estop=true if TWDT fired
- POST /api/estop and POST /api/estop/clear endpoints
- NVS namespace "proto" with full config load at boot via loadConfigToState()

### Changed
- Boot default: sbusSignalLost=true (was false — safety regression fix)

### Fixed
- (none — initial release)

### Breaking Changes
- (none — initial release)

[Unreleased]: https://github.com/mattiasbrandt/protoArtoo/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/mattiasbrandt/protoArtoo/releases/tag/v0.1.0
```

Each released version section contains: **Summary** (one sentence), **Added**, **Changed**, **Fixed**, **Removed**, **Breaking Changes**. Omit empty sections.

---

#### What never goes in a commit

- WiFi credentials — use `src/secrets.h` excluded via `.gitignore`
- `config.h` with `TBD` placeholders replaced by guesses (must compile-error on TBD)
- Code that does not compile
- A `//TODO` that disables or bypasses safety logic
- Commit messages like `"fix stuff"`, `"wip"`, `"update"`, `"asdf"`, `"."` — these are rejected

---

### 8.10 Key Reference Links

These are the authoritative sources referenced throughout the codebase. Keep them bookmarked.

| Topic | URL |
|---|---|
| ESP32 Arduino core | https://github.com/espressif/arduino-esp32 |
| ESP-IDF Programming Guide | https://docs.espressif.com/projects/esp-idf/en/latest/esp32/ |
| FreeRTOS API reference | https://www.freertos.org/a00106.html |
| ESP32 FreeRTOS SMP guide | https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/freertos-smp.html |
| ESP32 Watchdog timers | https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/wdts.html |
| ESP32 NVS (Preferences) | https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/storage/nvs_flash.html |
| ESP32 Partition tables | https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/partition-tables.html |
| PlatformIO project config | https://docs.platformio.org/en/latest/projectconf/index.html |
| PlatformIO unit testing | https://docs.platformio.org/en/latest/advanced/unit-testing/ |
| PlatformIO static analysis | https://docs.platformio.org/en/latest/advanced/static-code-analysis/ |
| Unity test framework | https://github.com/ThrowTheSwitch/Unity |
| bolderflight/sbus | https://github.com/bolderflight/sbus |
| Gen2.x hoverboard firmware | https://github.com/RoboDurden/Hoverboard-Firmware-Hack-Gen2.x-GD32 |
| ESPAsyncWebServer | https://github.com/me-no-dev/ESPAsyncWebServer |
| ArduinoJson | https://arduinojson.org/v7/doc/ |
| clang-format options | https://clang.llvm.org/docs/ClangFormatStyleOptions.html |

---
## 9. REST API

**Drive:** `POST /api/drive` — `{ "speed": int, "steer": int }`

**Audio:**
```
POST /api/audio/play    { "event": int }        Marcduino :SExx ID
POST /api/audio/raw     { "cmd": string }        Raw $-prefixed string
POST /api/audio/stop
GET  /api/audio/events                           Event name list
```

**Arms:** `POST /api/arms` — `{ "arm": 0|1|2, "open": bool }`

**Dome:**
```
POST /api/dome/command  { "cmd": string }        Raw Marcduino TX string
POST /api/dome/panels   { "action": "open"|"close"|"auto" }
POST /api/sleep                                  body idle + dome :SE10
POST /api/wake                                   body active + dome :SE14
POST /api/estop                                  hard estop (latching, requires explicit clear)
POST /api/estop/clear                            clear hard estop (manual human gate)
```

**Config / Status / Network / OTA:**
```
GET  /api/status        → health JSON
GET  /api/events        → SSE live stream
GET  /api/config
POST /api/config
POST /api/config/reset
POST /api/wifi          { "ssid": string, "password": string }
GET  /api/wifi/scan
POST /upload/firmware      → OTA firmware flash (U_FLASH, multipart .bin)
POST /upload/filesystem    → OTA filesystem flash (U_SPIFFS/LittleFS, multipart .bin)
```

**`/api/status` — canonical response (single authoritative definition):**
```json
{
  "project": "protoArtoo",
  "uptime_ms": 0,
  "sleep_mode": false,
  "drive": { "speed": 0, "steer": 0, "speed_limit_pct": 100 },
  "arms": [false, false],
  "audio_driver": "DY-SV5W",
  "dome_link": {
    "connected": false,
    "hb_tx": 0,
    "hb_rx": 0,
    "last_rx_ms": 0,
    "commands_received": 0
  },
  "failsafe": {
    "active": false,
    "source": "NONE",
    "trigger_count": 0,
    "estop": false,
    "sbus_signal_lost": false,
    "sbus_hw_failsafe": false,
    "sbus2_signal_lost": false,
    "web_drive_expired": false
  },
  "sbus": {
    "rx1_ok": true,
    "rx1_last_frame_ms": 0,
    "rx1_hw_failsafe": false,
    "rx2_ok": true,
    "rx2_last_frame_ms": 0,
    "rx2_hw_failsafe": false
  },
  "battery_mv": 0,
  "wifi_ap_clients": 0,
  "wifi_sta_connected": false,
  "free_heap": 0,
  "min_free_heap": 0
}
```

`failsafe.source` is one of: `"NONE"`, `"SBUS_TIMEOUT"`, `"SBUS_HW"`, `"SBUS2_TIMEOUT"`, `"WEB_TIMEOUT"`, `"ESTOP_CMD"`, `"WATCHDOG_RESET"`.
`failsafe.trigger_count` is the lifetime count since boot — useful for diagnosing intermittent antenna dropouts.
`drive.speed_limit_pct` reflects the current CH8 dial position (0–100%).

---

## 10. Web UI — Context and Labelling

### Dashboard (`/`)

Status banner: **protoArtoo — Hoverboard Drive Build**

The dashboard real-time status elements are aligned with the original Artoo firmware's home page display, adapted for protoArtoo's hardware:

| Element | Source | Display |
|---|---|---|
| Battery | ADC1 voltage reading | Colour-coded badge: Green (>70%) / Orange (30–70%) / Red (<30%) |
| Drive input | SBUS CH1/CH2 live values | Stick position visualisation |
| Speed limit | CH8 dial (receiver #1) | 0–100% bar; "Stationary" badge when locked |
| Dome spin | SBUS receiver #2 CH1 | Rotation speed indicator |
| Arm status | ServoTask state | Open/Closed per arm |
| Dome link | `domeConnected()` | Connected / Lost / Not seen badge |

> **WiFi AP difference from original firmware:** The original Artoo firmware uses SSID "Artoo Inventions" and IP 10.10.10.10. protoArtoo uses SSID "protoArtoo" and standard ESP32 AP IP 192.168.4.1. This is intentional — the two firmwares should not be confused on the same network.

Dome link health showing both directions:
- Sent: `bodyHbTx` (’#PAHB’ heartbeats to dome)
- Received: `domeHbRx` (’#APHB’ heartbeats from dome)
- Time since last dome RX

### Audio Settings page

> **protoArtoo uses a body-mounted audio module as the sole audio source.**
> There is no sound module in the dome. The dome's "Sound Player" must be set to None.
> All `$` sound commands — from RC, web API, or dome sequences — play through the body audio module.
> **Default module:** DY-SV5W. Build flag `PA_AUDIO_DRIVER` selects the driver.

### Dome Link page

> **Serial is a full-duplex body↔dome link, not a one-way master connection.**
> The dome (AstroPixelsPlus) sends arm and sound commands to protoArtoo during sequences.
> There is no MarcDuino Slave board. This is not a classic MarcDuino topology.

---

## 11. Project Structure

```
protoArtoo/
├── platformio.ini
├── include/
│   ├── config.h              // All GPIO #defines (populate from PCB trace)
│   ├── robot_state.h         // RobotState, FreeRTOS queues, mutexes
│   ├── marcduino.h           // Marcduino command string constants + SE IDs
│   └── audio_driver.h        // AudioDriver abstract interface
├── data/                     // LittleFS web assets
│   ├── index.html            // Dashboard — SSE live + dome link health
│   ├── control.html          // Manual control
│   ├── config.html           // Config + audio/arm settings
│   ├── network.html          // WiFi settings
│   └── app.js                // Shared JS
├── src/
│   ├── main.cpp
│   ├── wifi_manager.cpp
│   ├── tasks/
│   │   ├── sbus_input.cpp
│   │   ├── drive.cpp         // 8-byte hoverboard UART, ESP32 UART2, 50 Hz
│   │   ├── dome_link.cpp     // TX + RX + health heartbeat (bidirectional)
│   │   ├── audio.cpp         // Multi-source queue → audio driver
│   │   ├── servo_arms.cpp
│   │   └── safety.cpp
│   ├── drivers/
│   │   ├── hoverboard_uart.cpp   // Gen2.x 8-byte frame builder + sender
│   │   ├── audio_dy_sv5w.cpp     // DY-SV5W driver (default)
│   │   ├── audio_dfplayer.cpp    // DFPlayer driver (alternative)
│   │   ├── marcduino_tx.cpp      // Body→dome command sender
│   │   ├── marcduino_rx.cpp      // Dome→body parser + dispatcher
│   │   └── sbus_decoder.cpp      // bolderflight/sbus RMT wrappers — drive + dome instances
│   └── web/
│       ├── web_server.cpp
│       ├── api_drive.cpp
│       ├── api_audio.cpp
│       ├── api_arms.cpp
│       ├── api_dome.cpp
│       ├── api_config.cpp
│       ├── api_status.cpp
│       └── nvs_config.cpp
├── test/
│   ├── test_native/                    // [env:native] — no hardware, fast iteration
│   │   ├── test_marcduino_rx.cpp       // parse_dome_rx() prefix routing
│   │   ├── test_hoverboard_frame.cpp   // frame checksum, boundary values
│   │   ├── test_sbus_channel_map.cpp   // CH8 scale math, speed_limit formula
│   │   └── test_audio_track_map.cpp    // $001→track1, $S→random, bounds
│   └── test_embedded/                  // [env:protoArtoo] — on-device
│       ├── test_uart_loopback.cpp      // UART2 TX→RX loopback
│       ├── test_nvs_roundtrip.cpp      // NVS write/read/verify
│       └── test_failsafe_timing.cpp    // SBUS watchdog timing
├── tools/
│   └── command_compat.py
├── CHANGELOG.md               // Conventional commits changelog — all releases
├── CONTRIBUTING.md            // How to contribute, commit format, PR checklist
└── docs/
    ├── pin_map.md             // GPIO assignments
    ├── api.md
    └── topology.md            // Classic MarcDuino vs protoArtoo comparison
```

---

## 12. docs/topology.md

```markdown
# Topology: protoArtoo vs Classic MarcDuino

## Classic R2 Setup

SHADOW (Android/PS2) → Body Master (MarcDuino) → Dome Master (MarcDuino)
                                                          ↓
                                                  Dome Slave (MarcDuino)

Sound: Dome Master → MP3 Trigger / DFPlayer (in dome)
Drive: Body Master → Sabertooth motor controller
Input: PS2 controller via USB host (SHADOW)

## protoArtoo

RC Transmitter (SBUS) ──→ protoArtoo ESP32 (body) ←──→ AstroPixelsPlus ESP32 (dome)
Phone browser (WiFi)  ──→         ↑
                             UART2↔GD32-UART3 8-byte frames
                                   ↓
                          Hoverboard controllers
                           (RoboDurden Gen2.x-GD32)

Sound: protoArtoo → body audio module (default: DY-SV5W) — no dome sound module
Input: SBUS RC transmitter + phone browser on "protoArtoo" WiFi AP

## Key Differences

| | Classic | protoArtoo |
|---|---|---|
| Body→dome serial | TX only | Full-duplex bidirectional |
| Sound location | Dome | Body |
| Motor controller | Sabertooth | Gen2.x hoverboard UART |
| RC input | PS2 / SHADOW | SBUS receivers |
| Serial board count | 2–3 MarcDuino PCBs | 2 ESP32 boards only |

## Compatibility

- Standard Marcduino command strings work normally via /api/dome/command
- protoArtoo is NOT a MarcDuino Master or Slave node
- The dome has NO local sound player
- There is NO MarcDuino Slave board
- SHADOW / Droid Remote apps can bridge via /api/dome/command REST endpoint
```

---

## 13. Critical Considerations

**SBUS failsafe before any drive:** Never trust a drive command until at least one valid SBUS frame has been received. Set `sbusSignalLost = true` as the boot default — it clears only on first valid SBUS frame. This prevents web API driving before the RC system is confirmed present.

**Speed limit cap is unconditional:** `SPEED_LIMIT_MAX` is the absolute hardware ceiling applied in `DriveTask`. CH8 dial scales *within* this ceiling — even at CH8 max, output is capped at `SPEED_LIMIT_MAX`. Two limits, layered.

**Dual SBUS roles are not interchangeable:** Receiver #1 is drive; receiver #2 is dome spin. They are not redundant. Swapping wiring will result in dome control input driving the motors. Label cables physically.

**Estop does not auto-clear:** `estop = true` requires `POST /api/estop/clear`. SBUS signal loss and web timeout DO auto-clear (they are transient conditions). This distinction matters for the web UI logic.

**Async web startup:** `initAsyncWeb()` only from WiFi event callback — dome analysis found crashloop if called early.

**Bench stage vs full hardware:** Keep controller-only web/API/OTA validation explicitly separate from full Artoo PCB/peripheral validation. Bench-tested success does not prove hoverboard, SBUS receiver, servo, audio, or dome-link behavior.

**ADC2 + WiFi:** GPIO 13/15 are used as digital UART (fine). Use ADC1 (GPIO 32–39) for battery monitoring.

**GPIO 15 strapping pin:** Test boot with and without SBUS receiver connected.

**Hoverboard zero-frame rule:** Never go silent — always send `speed=0, steer=0` frames.

**ESPAsyncWebServer + FreeRTOS:** Handlers on Core 0 — always post to queues, never touch hardware directly.

**Dome RX parser robustness:** Non-blocking reads; line buffer handles partial frames; overflow discards safely; never blocks TX path.

**Audio authority:** Confirm dome `PREFERENCE_MARCSOUND = kNone` before audio testing. If the dome attempts to play audio on a non-existent module simultaneously with the body module, you will get driver errors or unexpected behaviour.

**`%` prefix:** Never expected on body serial RX in protoArtoo topology. Discard silently — do not attempt to forward.

**Hoverboard firmware variant:** The 8-byte XOR-checksum UART frame is common across Gen2.x hoverboard firmware forks. If using a different fork, verify the frame format matches before assuming compatibility.

**Audio module voltage:** ESP32 is 3.3V. Check audio module UART TX output level — add voltage divider if 5V.

**Overheating trace:** Permanent trace cut — stays cut forever.

---

## 15. Long-Term Vision

Once stable, protoArtoo becomes a better product than what Artoo.uk promised:

- **Truly open** — full source, docs, ownership; no vendor lock-in
- **Named and versioned** — `protoArtoo` is the project identity; `mattiasbrandt/AstroPixelsPlus` is the dome partner
- **Bidirectional dome link** — body and dome act as a unified droid
- **Body = audio authority** — single clean source, no conflicts
- **Web-native** — any phone on the AP, no app install
- **Audio-agnostic** — `AudioDriver` interface allows any community module
- **Hoverboard-agnostic** — 8-byte UART frame works with any compatible Gen2.x firmware
- **Field-updatable** — OTA on both body and dome
- **Documented topology** — `docs/topology.md` makes the architectural differences explicit for the community

---

*Last updated: March 2026*
