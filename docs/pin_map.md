# GPIO Pin Map

Canonical hardware mapping reference for the **artoo-esp32** and **firebeetle2** controller boards.

Artoo Controller PCB (v1.1–v1.2) assignments traced from physical boards (artoo.uk).
FireBeetle 2 assignments transcribed from `include/config.h`'s firebeetle2 block, cross-referenced to the spec sheet's allocation tables.

## Table of Contents

- [Source of Truth Contract](#source-of-truth-contract)
- [Supported Hardware](#supported-hardware)
- **Artoo-esp32 section:**
  - [Serial Ports (Artoo-esp32)](#serial-ports-artoo-esp32)
  - [GPIO Assignment Summary (Artoo-esp32)](#gpio-assignment-summary-artoo-esp32)
    - [RC Receiver Wiring Modes](#rc-receiver-wiring-modes)
    - [RC Binding Defaults](#rc-binding-defaults)
    - [Battery Monitoring](#battery-monitoring)
  - [PCB Revisions](#pcb-revisions)
  - [artoo.uk Hardware Reference](#artoouk-hardware-reference)
- **FireBeetle 2 section:**
  - [Serial Ports (FireBeetle 2)](#serial-ports-firebeetle-2)
  - [FireBeetle 2: GPIO Assignment Summary](#firebeetle-2-gpio-assignment-summary)
  - [GPIO Budget and Exposed Pin Constraints](#gpio-budget-and-exposed-pin-constraints)

---

## Source of Truth Contract

This document is the canonical hardware mapping reference for both boards.

**For artoo-esp32 (`PA_BOARD_ARTOO_ESP32`):**
1. `docs/pin_map.md` (authoritative: traced from physical PCB v1.2 board)
2. `include/config.h` (compile-time implementation must match this document)

**For FireBeetle 2 (`PA_BOARD_FIREBEETLE2`):**
1. `docs/spec-sheets/firebeetle2-esp32-p4-spec-sheet.md` (authoritative: hardware constraints, recommended allocation tables from spec and chip datasheet)
2. `include/config.h` (firebeetle2 block: compile-time implementation of the allocations)
3. `include/firebeetle_required_pins.inc` (production pin inventory and compile-time guards)
4. `docs/pin_map.md` (this document: human-readable cross-reference derived from the above sources)

**Why the difference:** Artoo-esp32 was designed before this document existed, so the PCB itself is ground truth. FireBeetle 2 pins were allocated from the spec sheet to minimize conflicts, and this document is derived from that design. In both cases, if sources diverge, reconcile them in the same change. A discrepancy is a defect.

---

## Supported Hardware

Two primary targets are supported:

**1. Artoo-esp32** (`PA_BOARD_ARTOO_ESP32`)
- Artoo Controller PCB (v1.1–v1.2, purpose-built for the dual-header ESP32 D1 Mini clone)
- Board identifier in PlatformIO: `wemos_d1_mini32`
- This is a third-party clone — not an official Wemos/LOLIN product — sold under names such as "ESP32 D1 Mini", "D1 Mini32", or "Wemos D1 Mini ESP32".
- Dual-row headers on both long sides (~40 pins total), with inner rows matching the original ESP8266 D1 Mini shield footprint.

**2. FireBeetle 2** (`PA_BOARD_FIREBEETLE2`)
- DFRobot FireBeetle 2 ESP32-P4 board (`DFR1172`) + IO Expansion shield (`DFR1237`)
- Board identifier in PlatformIO: `dfrobot_firebeetle2_esp32p4`
- Chip revision v1.x (360 MHz, 32 MB PSRAM, 16 MB flash)
- All GPIO assignments via the DFR1237 expansion shield headers (no direct soldering required for standard configuration)

---

## Serial Ports (Artoo-esp32)

The PCB silkscreen legend defines each serial header's purpose:

```
S0 — ESP (USB debug)
S1 — Hoverboard
S2 — Sound
S3 — Dome Control
```

| PCB Header | Function | TX GPIO | RX GPIO | Baud | Protocol |
|------------|----------|---------|---------|------|----------|
| S0 | USB debug | 1 | 3 | 115200 | Standard UART0 |
| S1 | Hoverboard drive | 16 | 17 | 115200 | Gen2.x 8-byte frames |
| S2 | Audio module | 26 | 35 | 9600 | DY-SV5W binary (TX primary, RX for status queries) |
| S3 | Dome link (slip ring) | 33 | 34 | 9600 | Marcduino ASCII, bidirectional |

**Notes:**
- GPIO 34 (S3 RX) and GPIO 35 (S2 RX) are ESP32 input-only pins — they cannot be used as outputs.
- SBUS1 (GPIO 15) and SBUS2 (GPIO 13) use the ESP32 RMT peripheral, not hardware UARTs,
  so all four serial functions (hoverboard, audio, dome link, SBUS) can operate simultaneously.

**Dome Control slip ring wiring (cross-connection required):**

UART is a cross-connected protocol — body TX must land on dome RX and vice versa.
With a straight 1:1 servo cable through the slip ring, one connector end must have its
two signal wires swapped (or the cable itself must be a crossover).

```
Body "Dome Control" header   Slip ring (straight-through)    Dome board header
GPIO 33  (TX) ───────────────────────────────────────────────────── RX
GPIO 34  (RX) ───────────────────────────────────────────────────── TX
GND           ───────────────────────────────────────────────────── GND
```

A straight cable (pin 1→1, 2→2, 3→3) will connect TX→TX and RX→RX, which is incorrect.
Easiest fix: swap the two signal crimp pins in one connector housing before routing through the slip ring.

---

## Serial Ports (FireBeetle 2)

The DFR1237 IO expansion shield routes all UART lanes to dedicated headers. Standard transports follow the spec sheet's "Recommended allocation" to minimize conflicts with RC receiver channels and other production peripherals.

| Header | Function | TX GPIO | RX GPIO | Baud | Protocol | Notes |
|--------|----------|---------|---------|------|----------|-------|
| J3 | Drive (UART1) | 20 | 21 | 115200 | Gen2.x 8-byte hoverboard frames | Default for this board; ADR 0029 amendment (2026-08-26) |
| J3 | Dome link (UART2) | 22 | 23 | 9600 | Marcduino ASCII | Shared with audio RX via arbiter (see below) |
| J3 | Audio module | 34 (bit-bang) | 36 | 9600 | DY-SV5W binary | **Discrepancy note:** `include/config.h` comment claims "Dedicated hardware UART TX/RX paths", but audio TX is software bit-bang via `softUartTxByte()` (`src/drivers/audio_dy_sv5w.cpp:32-33`); only RX is HardwareSerial(2). This is a known mismatch between the config comment and actual driver behavior. |

**Audio and Dome Arbiter:**
Both audio RX and dome link use HardwareSerial(2) (GPIO22/23 for dome, GPIO36 for audio RX). They share the UART2 peripheral and are arbitrated by `domeUartAcquire()` / `domeUartRelease()` with the `DomeUartOwner` enum (`include/robot_state.h:66-70`, live field at `:194`). When dome control holds the lane, audio module state queries return cached values instead of querying over UART2 (`audio_dy_sv5w.cpp:32-33`).

---

## GPIO Assignment Summary (Artoo-esp32)

| GPIO | PCB Connector | Function | Peripheral |
|------|---------------|----------|------------|
| 1    | S0 TX         | USB debug TX                          | UART0     |
| 3    | S0 RX         | USB debug RX                          | UART0     |
| 5    | ARM2          | Utility arm servo #2 — Bottom / Right | LEDC PWM  |
| 13   | CH2           | SBUS receiver #2 (dome spin)          | RMT       |
| 15   | CH1           | SBUS receiver #1 (drive)              | RMT       |
| 16   | S1 TX         | Hoverboard TX                         | UART1     |
| 17   | S1 RX         | Hoverboard RX                         | UART1     |
| 18   | ARM4          | Spare servo output (AUX2)             | LEDC PWM  |
| 19   | ARM3          | Spare servo output (AUX1)             | LEDC PWM  |
| 21   | I2C (D)       | I2C SDA                               | I2C       |
| 22   | I2C (C)       | I2C SCL                               | I2C       |
| 23   | ARM1          | Utility arm servo #1 — Top / Left     | LEDC PWM  |
| 25   | DOME          | Dome rotation ESC                     | LEDC PWM  |
| 26   | S2 TX         | Audio module TX                       | Soft UART |
| 32   | ARM5          | Spare servo output (AUX3)             | LEDC PWM  |
| 33   | S3 TX         | Dome serial TX                        | UART2     |
| 34   | S3 RX         | Dome serial RX (input-only)           | UART2     |
| 35   | S2 RX         | Audio module RX (input-only)          | Soft UART |

### RC Receiver Wiring Modes

The six CH headers (CH1–CH6) support three mutually exclusive wiring modes:

| Mode           | Wiring                              | Channels available |
|----------------|-------------------------------------|--------------------|
| Standard PWM   | CH1–CH6 → GPIO 15,13,2,4,12,27     | 6 (one per pin)    |
| Single SBUS    | SBUS → CH1 (GPIO 15)               | Up to 16           |
| Dual SBUS      | SBUS1 → CH1 (GPIO 15), SBUS2 → CH2 (GPIO 13) | Up to 32  |

Configure the active mode from the RC Setup page (`rc_mode` setting).

In SBUS modes, CH3–CH6 (GPIO 2, 4, 12, 27) are unused:

| GPIO | PCB Header | Standard PWM function   | Notes                    |
|------|------------|-------------------------|--------------------------|
| 2    | CH3        | Standard PWM CH3        | Unused in SBUS modes     |
| 4    | CH4        | Standard PWM CH4        | Unused in SBUS modes     |
| 12   | CH5        | Standard PWM CH5        | Unused in SBUS modes     |
| 27   | CH6        | Standard PWM CH6        | Unused in SBUS modes     |

### RC Binding Defaults

The RC mapping profile is persisted in NVS and configurable from the RC Setup page.
Factory defaults:

| Runtime profile | Action | Default binding | Notes |
|-----------------|--------|-----------------|-------|
| `standard_pwm` | Drive speed | `pwm:1:1000:1500:2000:0:0` | CH1 |
| `standard_pwm` | Drive steer | `pwm:2:1000:1500:2000:0:0` | CH2 |
| `standard_pwm` | Drive limit | `none:0:1000:1500:2000:0:0` | Unbound by default |
| `standard_pwm` | Dome speed | `pwm:3:1000:1500:2000:0:0` | CH3 |
| `standard_pwm` | ARM1 trigger | `pwm:4:1000:1500:2000:0:0` | CH4 |
| `standard_pwm` | ARM2 trigger | `pwm:5:1000:1500:2000:0:0` | CH5 |
| `standard_pwm` | Sound trigger | `pwm:6:1000:1500:2000:0:0` | CH6 |
| `single_sbus` / `dual_sbus` | Drive speed | `sbus1:1:172:992:1811:0:0` | SBUS #1 CH1 |
| `single_sbus` / `dual_sbus` | Drive steer | `sbus1:2:172:992:1811:0:0` | SBUS #1 CH2 |
| `single_sbus` / `dual_sbus` | Drive limit | `sbus1:8:172:992:1811:0:0` | SBUS #1 CH8 |
| `single_sbus` / `dual_sbus` | Dome speed | `sbus2:1:172:992:1811:0:0` | Active in `dual_sbus`; inactive in `single_sbus` until remapped |
| `single_sbus` / `dual_sbus` | ARM1 trigger | `sbus2:2:172:992:1811:0:0` | Active in `dual_sbus`; can be remapped |
| `single_sbus` / `dual_sbus` | ARM2 trigger | `sbus2:3:172:992:1811:0:0` | Active in `dual_sbus`; can be remapped |
| `single_sbus` / `dual_sbus` | Sound trigger | `none:0:1000:1500:2000:0:0` | Unbound by default |

SBUS channels `17` and `18` are also valid persisted binding channels for digital
trigger actions. When a binding targets `17` or `18`, `/api/rc` reports it in the
`digital` object instead of the analog `channels` array.

### Battery Monitoring

The Artoo Controller PCB has **no battery monitoring circuitry** — no voltage divider,
no dedicated ADC trace. The board is a bare PCB with traces, pin headers, and DC
terminals only. If battery voltage sensing is needed in the future, an external voltage
divider must be wired to a spare ADC1 pin (GPIO 36 or 39 are available).

---

## FireBeetle 2: GPIO Assignment Summary

All GPIO assignments are transcribed from `include/config.h`'s firebeetle2 block and verified against the DFR1237 shield routing and the spec sheet's "Exposed GPIO" allocation table.

**Production Pin Inventory (20 pins total):**

14 firmware-design outputs (safety-critical, #190):

| GPIO | Header (J*) | Function | Peripheral | Arduino alias | Notes |
|------|-------------|----------|------------|---------------|-------|
| 28 | J2 (SPI) | SBUS receiver #1 (drive) | RMT | SCK | P2 unimpeachable; SPI header |
| 29 | J2 (SPI) | SBUS receiver #2 (dome) | RMT | MOSI | P2 unimpeachable; SPI header |
| 30 | J2 (SPI) | RC channel #3 | RMT | MISO | P2 unimpeachable; SPI header |
| 31 | J3 | RC channel #4 | RMT | SS | P2 unimpeachable; spec sheet "best clean pin in <=36 range" |
| 32 | J3 | RC channel #5 | GPIO | I3C/SCL | P1 (reassignable, protoArtoo does not use I3C) |
| 33 | J3 | RC channel #6 | GPIO | I3C/SDA | P1 (reassignable, protoArtoo does not use I3C) |
| 34 | J3 | Audio module TX | GPIO matrix | — | P3 strapping (JTAG source); software bit-bang via GPIO matrix |
| 36 | J3 | Audio module RX | HardwareSerial(2) | — | P3 strapping (ROM print); shared UART2 with dome link via arbiter |
| 49 | J3 | Arm servo #1 (left/top) | LEDC PWM | A5 (not labeled on silkscreen) | LDO caution (VDD_IO_6); ADC2_CHANNEL0 |
| 50 | J3 | Arm servo #2 (right/bottom) | LEDC PWM | A6 (not labeled on silkscreen) | LDO caution (VDD_IO_6); ADC2_CHANNEL1 |
| 4 | J3 | Arm servo #3 (aux strip) | LEDC PWM | T0 | P3 JTAG MTMS (post-debug); WS2812B capable |
| 5 | J3 | Arm servo #4 (aux strip) | LEDC PWM | T1 | P3 JTAG MTDO (post-debug); WS2812B capable |
| 51 | J3 | Arm servo #5 (aux strip) | LEDC PWM | A7 (not labeled on silkscreen) | LDO caution (VDD_IO_6); WS2812B capable; ADC2_CHANNEL2 |
| 48 | J3 | Dome rotation ESC | LEDC PWM | — | LDO caution (VDD_IO_5); **unmeasured under load (#191)**; P2 with LDO caution |

6 board bring-up interface lanes:

| GPIO | Header | Function | Peripheral | Arduino alias | Notes |
|------|--------|----------|------------|---------------|-------|
| 20 | J3 | Drive TX (UART1) | UART1 | A0 | Spec sheet "Recommended allocation"; ADC1_CHANNEL4 |
| 21 | J3 | Drive RX (UART1) | UART1 | A1 | Spec sheet "Recommended allocation"; ADC1_CHANNEL5 |
| 22 | J3 | Dome TX (UART2) | UART2 | A2 | Spec sheet "Recommended allocation"; ADC1_CHANNEL6 |
| 23 | J3 | Dome RX (UART2) | UART2 | A3 | Spec sheet "Recommended allocation"; ADC1_CHANNEL7; shared with audio RX via arbiter |
| 7 | J1 (I2C) | I2C SDA | I2C | T2 | Board default SDA; P2 unimpeachable |
| 8 | J7 (I2C) | I2C SCL | I2C | T3 | Board default SCL; P2 unimpeachable |

---

## GPIO Budget and Exposed Pin Constraints

The FireBeetle 2's DFR1237 shield exposes exactly **24 GPIO pins** from the ESP32-P4 to the IO headers.

| Category | GPIOs | Count | Notes |
|----------|-------|-------|-------|
| **Exposed on DFR1237 headers** | 4, 5, 7, 8, 20–23, 28–38, 48–52 | 24 total | See spec sheet "Exposed GPIO table" (lines 906–929) |
| **Claimed by production inventory** | 20 pins (see table above) | 20 | All firmware design outputs + board bring-up lanes |
| **Remaining after production** | 35, 37, 38, 52 | 4 GPIOs | Breakdown below |
| **GPIO 37** | Console/download UART TX | Reserved | Always keep as UART0 TX for flashing and serial debug |
| **GPIO 38** | Console/download UART RX | Reserved | Always keep as UART0 RX for flashing and serial debug |
| **GPIO 35** | Avoid (boot-mode strapping) | Reserved | Held low at reset forces joint download boot mode |
| **GPIO 52** | **Genuinely free** | **1 pin only** | ADC2_CHANNEL3, Arduino A7, P2 clean; **the sole free GPIO on this board** |

**Production implications:**
- GPIO 48–52 remain **unmeasured under load** (#191 — characterization pending). Four production outputs sit in this LDO-caution range; design decisions and operation validation must treat them as a group.
- GPIO 52 is dual-purpose: the sole free GPIO and a reserved ADC fallback for battery sense (should I2C battery charger decisions reverse, #191 notes).
- No headroom for additional peripherals without removing an existing lane.

See spec sheet "Exposed GPIO table" (lines 906–929) and the chip errata for P3 strapping pins (GPIO 4, 5, 34, 35, 36) if GPIO matrix bindings are needed.

---

## PCB Revisions

| Revision | Notes |
|----------|-------|
| v1.1 | Original production run. Has one incorrect trace (fixed manually with a cut/jumper). |
| v1.2 | Corrected trace fix from Steve (artoo.uk). GPIO assignments identical to v1.1. |

---

## artoo.uk Hardware Reference

The artoo.uk manual GPIO assignments all match the PCB trace. Two labels differ from
protoArtoo convention:

| artoo.uk label     | GPIO(s) | artoo.uk description    | protoArtoo label | Note |
|--------------------|---------|-------------------------|------------------|------|
| Arm 1 (Top)        | 23      | Left utility arm        | ARM1             | — |
| Arm 2 (Bottom)     | 5       | Right utility arm       | ARM2             | — |
| Dome Servo         | 25      | Dome rotation control   | DOME (ESC)       | Drives an ESC (ISDT ESC70), not a servo |
| Motor Controller   | 16, 17  | Hoverboard serial       | S1 Hoverboard    | artoo.uk manual places these on S3; PCB silkscreen is correct: S1 = Hoverboard |

If you have a different PCB revision and find different assignments, please open an issue.

---

## Cross-References

- **Spec sheet:** [`docs/spec-sheets/firebeetle2-esp32-p4-spec-sheet.md`](spec-sheets/firebeetle2-esp32-p4-spec-sheet.md) — hardware truth, chip revision details, allocation tables, P3 strapping notes, and board errata for both FireBeetle 2 boards
- **Build configuration:** `include/config.h` — compile-time pin assignments (`PA_BOARD_ARTOO_ESP32` and `PA_BOARD_FIREBEETLE2` blocks)
- **Inventory verification:** `include/firebeetle_required_pins.inc` — production pin roster and compile-time guards (FireBeetle 2 only)
- **Developer setup:** `tasks/firebeetle2-developer-setup.md` (WIP, untracked) — flashing, build environment, serial monitor, and troubleshooting for FireBeetle 2
