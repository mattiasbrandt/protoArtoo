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
  - [Known Issue: GPIO 48-52 LDO Rails (Unmeasured)](#known-issue-gpio-48-52-ldo-rails-unmeasured)

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
  so SBUS costs no UART controller on this board.
- **S2 and S3 are not simultaneous.** This chip has three HP UART controllers
  (`SOC_UART_HP_NUM = 3`): UART0 is S0, UART1 is S1, and the single remaining controller
  serves both S3 (dome link, TX and RX) and S2's RX. Ownership alternates via
  `domeUartAcquire()` / `domeUartRelease()`, so audio status queries only run while the dome
  link is on its WiFi fallback; S2's TX is a software bit-bang for the same reason. Both are
  consequences of the controller count, not of the PCB wiring — see
  `PA_CAP_DEDICATED_AUDIO_UART` in `include/config.h`, which is 0 here and 1 on FireBeetle 2.

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

| Silkscreen | Function | TX GPIO | RX GPIO | Baud | Protocol | Notes |
|--------|----------|---------|---------|------|----------|-------|
| main field rows `20` + `21` | Drive (UART1) | 20 | 21 | 115200 | Gen2.x 8-byte hoverboard frames | Default for this board; ADR 0029 amendment (2026-08-26) |
| main field rows `22` + `23` | Dome link (UART2) | 22 | 23 | 9600 | Marcduino ASCII | Owned by DomeLinkTask for the whole boot; not shared |
| main field rows `34` + `36` | Audio module (UART3) | 34 | 36 | 9600 | DY-SV5W binary | Hardware UART both directions |

**No audio/dome UART sharing on this board (#254).** The ESP32-P4 has five HP UARTs, so each
consumer gets its own controller: `UART0` console, `UART1` drive, `UART2` dome, `UART3` audio,
`UART4` unclaimed by the firmware (borrowed by `bringup/p4_rt_bench.cpp`). The allocation is
declared in `include/config.h` as `UART_PORT_DRIVE` / `UART_PORT_DOME` / `UART_PORT_AUDIO`, and
`PA_CAP_DEDICATED_AUDIO_UART` is 1 here.

Consequences specific to this board, all of which are the artoo-esp32 behaviour NOT being inherited:

- Audio TX on GPIO34 is a real hardware UART, not the `softUartTxByte()` bit-bang.
- The `domeUartAcquire()` / `domeUartRelease()` ownership handoff does not run. `DomeUartOwner`
  still exists and `/api/status` still reports `dome_link.uart_owner`, but on this board the dome
  link simply holds it from boot.
- Audio status queries work while the dome link is on serial -- the posture epic #182 calls primary
  for this board. On artoo-esp32 that combination starves audio RX by design.

Binding a UART to GPIO34/36 costs no pin: `UART0`-`UART4` route TX/RX to any GPIO through the GPIO
matrix (spec sheet "UART Lane Plan"), so audio uses the two pins it already had.

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
| 35   | S2 RX         | Audio module RX (input-only)          | UART2     |

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

### How the DFR1237 is physically laid out (read this before wiring anything)

> [!IMPORTANT]
> **There is no `J3` printed on the board.** `J3`, `J5`, `J6`, `J1`, `J2`, `J4`, `J7`, `J9` are
> KiCad **schematic reference designators** from the DFR1237 schematic. They are not on the
> silkscreen, and you cannot use them to find anything with the board in your hand. Operator-confirmed
> against the physical board, 2026-09-01. Earlier revisions put `J3` in a "Header" column on 15 of
> the 20 production rows and on all 3 serial-port rows -- one repeated value carrying no locating
> information at all. The **Silkscreen** column below is what is actually printed.

The main GPIO field is **17 rows x 3 columns**, with the column header silkscreened:

```
        IO   3V3  GND        <- printed above the field
   4  [ o ]  [ o ]  [ o ]    <- one row per GPIO
   5  [ o ]  [ o ]  [ o ]
  20  [ o ]  [ o ]  [ o ]
  ...
  52  [ o ]  [ o ]  [ o ]
```

- The **GPIO number is printed once per row, down the left edge**, in this order top to bottom:
  `4`, `5`, `20`, `21`, `22`, `23`, `31`, `32`, `33`, `34`, `35`, `36`, `48`, `49`, `50`, `51`, `52`.
  (Matches the schematic's signal order exactly.)
- **A 3-pin servo / ESC lead plugs onto ONE ROW**, spanning the three columns: signal into `IO`,
  power into `3V3`, ground into `GND`. You do **not** plug it across three GPIOs -- each row is one
  GPIO with its own power and ground beside it. This is the whole point of the three-column field.
- **Every row in this field is a plain GPIO number. No row carries an alias.** Operator-confirmed
  against the board, 2026-09-01. The `A0`-`A4` analog aliases and the `I3C` markings that appear in
  the DFR1237 schematic (`20/A0`, `32/I3C/SCL`, ...) are **KiCad net names**, and the Arduino variant
  adds `A5`-`A7` on GPIO49/50/52 on top of that. **None of them is printed on this field.** Use the
  bare number when working with the board in hand; the alias columns in the tables below are for
  reading code, not for finding a pin.
- **GPIO32/GPIO33 carry no special marking on this board.** They are the P4's I3C SCL/SDA pair in
  silicon, and the schematic net names read `32/I3C/SCL` / `33/I3C/SDA` -- but on the physical
  DFR1237 they are ordinary numbered rows like any other (operator-confirmed, 2026-09-01). The spec
  sheet previously called them "silkscreened I3C"; that was a net name mistaken for a board marking
  and has been corrected.

> [!CAUTION]
> **The middle column is `3V3`, not 5 V.** Standard hobby servos and ESCs expect 5 V-6 V, and pulling
> servo stall current through the board's 3.3 V regulator can brown out the controller mid-operation
> -- which on this project means the drive lane too. **Power servos and ESCs from a separate BEC**,
> and bring only the signal wire and a common ground to this field. Whether the board's `VIN` pin can
> source 5 V for this instead is `UNKNOWN` and is not to be assumed: no source in this repo answers
> it, and it needs the DFR1237 schematic PDF read or a meter on the pin.

> [!NOTE]
> **This layout is one of the reasons the FireBeetle 2 was chosen** (operator, 2026-09-01). Most of
> this droid's components -- servos, ESCs, RC channels -- terminate in a 3-pin lead, and a
> row-per-GPIO `IO`/`3V3`/`GND` field means each one plugs onto its own row without a breakout, an
> adapter harness, or splicing a power rail. Keep that in mind before proposing a pin reallocation
> that moves a 3-pin component off this field: the wiring ergonomics are a feature of the board, not
> an accident of it. It is also why the `3V3`-not-5 V caution above matters so much -- the layout
> invites exactly the plug-and-go wiring that the rail cannot actually power.

### What can and cannot be reassigned in code

The research pass that selected this board noted that pin assignments can be re-aligned in firmware
to suit the build. That is true, and it is one of the board's real strengths -- but it applies to
**rows, not columns**, and the difference is the difference between a config change and a damaged
board.

| | Reassignable in code? | Why |
|---|---|---|
| **Which GPIO row carries which function** (drive TX, a servo, an RC channel) | ✅ **Yes, freely, within the taxonomy below** | ESP32-P4 routes most peripherals through the GPIO matrix, so UART, LEDC PWM and RMT can be pointed at almost any exposed pin |
| **The `IO` / `3V3` / `GND` columns** | ⛔ **No. Not at all.** | `IO` is the GPIO net; `3V3` and `GND` are **power and ground planes** -- copper, not pins. The spec sheet lists them as rails (`J14`/`J15`). No firmware change can make the `3V3` column deliver 5 V, or turn the `GND` column into a signal |

So the `3V3`-not-5 V caution above is **not** a firmware problem and has no firmware fix. It is
what the copper does. A servo still needs its power from a BEC.

Row reassignment is bounded by the spec sheet's suitability taxonomy, not free everywhere:

- **P2** -- any GPIO via the matrix, usable without restriction. Move these freely.
- **P1** -- fixed IO MUX pins, or pins with peripheral-specific hardware (the GPIO32/33 I3C pair).
  Reassignable here only because protoArtoo does not use I3C.
- **P3** -- usable, but conflicts with an important function: strapping (GPIO34-GPIO38),
  JTAG (GPIO2-GPIO5), UART0 (GPIO37/GPIO38), USB Serial/JTAG (GPIO24/GPIO25). Several production
  pins already sit here deliberately; moving something *onto* a P3 pin needs the conflict understood
  first.

> [!WARNING]
> **Reassignment permutes, it does not create.** Measured 2026-08-23: this board routes **15 usable
> GPIOs against a demand of 14** once the "pairs to avoid" rules are honoured -- **one spare, and it
> is itself an avoid-list pin**. You can rearrange which row does what; you cannot free up a pin that
> is not there. Treat any proposal that needs an extra GPIO as a design change, not a remap.

The other connectors, with their silkscreen labels:

| Silkscreen block | Pins as printed | Schematic refdes |
|---|---|---|
| `SPI` | `30/MI`, `29/MO`, `28/SCK`, `GND`, `3V3` | `J2` |
| `UART` | `37/T`, `38/R`, `GND`, `3V3` | `J9` |
| `I2C` | `8/C` (SCL), `7/D` (SDA), `3V3`, `GND` | `J1` / `J7` |
| `RST GND` | reset and ground | `J4` |
| `VIN:5V GND` | 5 V input and ground | `J4` |

**Production Pin Inventory (20 pins total):**

14 firmware-design outputs (safety-critical, #190):

| GPIO | Silkscreen (what is printed on the board) | Function | Peripheral | Arduino alias | Notes |
|------|-------------------------------------------|----------|------------|---------------|-------|
| 28 | `SPI` block, pin `28/SCK` | SBUS receiver #1 (drive) | RMT | SCK | P2 unimpeachable; SPI header |
| 29 | `SPI` block, pin `29/MO` | SBUS receiver #2 (dome) | RMT | MOSI | P2 unimpeachable; SPI header |
| 30 | `SPI` block, pin `30/MI` | RC channel #3 | RMT | MISO | P2 unimpeachable; SPI header |
| 31 | main field, row `31` | RC channel #4 | RMT | SS | P2 unimpeachable; spec sheet "best clean pin in <=36 range" |
| 32 | main field, row `32` | RC channel #5 | GPIO | — | P1 (reassignable, protoArtoo does not use I3C) |
| 33 | main field, row `33` | RC channel #6 | GPIO | — | P1 (reassignable, protoArtoo does not use I3C) |
| 34 | main field, row `34` | Audio module TX (UART3) | UART3 | — | P3 strapping (JTAG source); hardware UART via GPIO matrix (#254) |
| 36 | main field, row `36` | Audio module RX (UART3) | UART3 | — | P3 strapping (ROM print); audio's own controller, not shared (#254) |
| 49 | main field, row `49` | Arm servo #1 (left/top) | LEDC PWM | A5 (code alias; nothing is printed on the field) | LDO caution (VDD_IO_6); ADC2_CHANNEL0 |
| 50 | main field, row `50` | Arm servo #2 (right/bottom) | LEDC PWM | A6 (code alias; nothing is printed on the field) | LDO caution (VDD_IO_6); ADC2_CHANNEL1 |
| 4 | main field, row `4` | Arm servo #3 (aux strip) | LEDC PWM | T0 | P3 JTAG MTMS (post-debug); WS2812B capable |
| 5 | main field, row `5` | Arm servo #4 (aux strip) | LEDC PWM | T1 | P3 JTAG MTDO (post-debug); WS2812B capable |
| 51 | main field, row `51` | Arm servo #5 (aux strip) | LEDC PWM | A4 | LDO caution (VDD_IO_6); WS2812B capable; ADC2_CHANNEL2 |
| 48 | main field, row `48` | Dome rotation ESC | LEDC PWM | — | LDO caution (VDD_IO_5); **unmeasured under load** (see Known Issue below); P2 with LDO caution |

6 board bring-up interface lanes:

| GPIO | Silkscreen (what is printed on the board) | Function | Peripheral | Arduino alias | Notes |
|------|-------------------------------------------|----------|------------|---------------|-------|
| 20 | main field, row `20` | Drive TX (UART1) | UART1 | A0 | Spec sheet "Recommended allocation"; ADC1_CHANNEL4 |
| 21 | main field, row `21` | Drive RX (UART1) | UART1 | A1 | Spec sheet "Recommended allocation"; ADC1_CHANNEL5 |
| 22 | main field, row `22` | Dome TX (UART2) | UART2 | A2 | Spec sheet "Recommended allocation"; ADC1_CHANNEL6 |
| 23 | main field, row `23` | Dome RX (UART2) | UART2 | A3 | Spec sheet "Recommended allocation"; ADC1_CHANNEL7; dome link owns it from boot |
| 7 | `I2C` block, pin `7/D` | I2C SDA | I2C | T2 | Board default SDA; P2 unimpeachable |
| 8 | `I2C` block, pin `8/C` | I2C SCL | I2C | T3 | Board default SCL; P2 unimpeachable |

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
| **GPIO 52** | **Genuinely free** | **1 pin only** | ADC2_CHANNEL3, Arduino A7, P2 with LDO caution; **the sole free GPIO on this board** |

**Production implications:**
- GPIO 48–52 remain **unmeasured under load** — see [Known Issue: GPIO 48-52 LDO Rails](#known-issue-gpio-48-52-ldo-rails-unmeasured). Four production outputs sit in this LDO-caution range; design decisions and operation validation must treat them as a group.
- GPIO 52 is dual-purpose: the sole free GPIO and a reserved ADC fallback for battery sense (should I2C battery charger decisions reverse).
- No headroom for additional peripherals without removing an existing lane.

See spec sheet "Exposed GPIO table" (lines 906–929) and the chip errata for P3 strapping pins (GPIO 4, 5, 34, 35, 36) if GPIO matrix bindings are needed.

---

## Known Issue: GPIO 48-52 LDO Rails (Unmeasured)

**Status:** open, unquantified, no instrumented measurement planned. Carried forward from #191
(closed 2026-08-31, not planned) and #193 (droid-hardware gate, dropped 2026-09-01). The risk
outlived both tickets, so it lives here.

**The claim.** Espressif's own P4 variant header says *"Use GPIOs 36 or lower on the P4 DevKit to
avoid LDO power issues with high numbered GPIOs"* (`docs/spec-sheets/firebeetle2-esp32-p4-spec-sheet.md`,
"Exposed GPIO table" region). GPIO 48 sits on `VDD_IO_5` and GPIO 49-52 on `VDD_IO_6`, both fed by
internal LDO regulators. The spec sheet's open question #2 calls the warning **unquantified**, and
it remains so: confirming or dismissing it needs an oscilloscope or logic analyser, and this bench
has neither (operator decision, 2026-08-31).

**What sits on those pins** (`include/config.h`, firebeetle2 servo block):

| GPIO | Rail | Signal |
|------|------|--------|
| 48 | `VDD_IO_5` | `PIN_DOME_ESC` — dome rotation ESC |
| 49 | `VDD_IO_6` | `PIN_ARM1_SERVO` |
| 50 | `VDD_IO_6` | `PIN_ARM2_SERVO` |
| 51 | `VDD_IO_6` | `PIN_ARM5_SERVO` (AUX3) |
| 52 | `VDD_IO_6` | deliberately unassigned — the board's sole free GPIO |

**How it would present.** A servo or ESC decodes *pulse width*. A sagging logic level or a slow
edge shifts where the receiver perceives the edge, so the decoded width drifts. It shows up as
**arm servo twitch or jitter, and erratic dome ESC throttle** — never as a firmware error, and
never visible to any software check in this project. Do not look for it in logs.

**If it appears on real droid hardware:**

1. Run the same firmware on the `artoo-esp32` target as a control. If the P4 is jittery where the
   artoo-esp32 is clean, `VDD_IO_5`/`VDD_IO_6` LDO behaviour is the first hypothesis — not the
   servo, not the PWM code, not the mechanics.
2. Fallback: drop peripheral demand to fit. GPIO 52 is held unassigned precisely to leave that
   room.
3. A dirty result means instrumented measurement has become necessary and must be sourced. It does
   **not** mean the pins are fine.

This is a watch item, not a blocker. Behavioural evidence is weaker than the scope trace #191
wanted; it is the evidence that exists.

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
- **Building and flashing:** the project `README`, "Building and flashing" — `BUILD_ENV=firebeetle2` selects the ESP32-P4 toolchain for every make target
