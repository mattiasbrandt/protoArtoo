# GPIO Pin Map — Artoo Controller PCB

GPIO assignments traced from a physical PCB v1.2 board (artoo.uk). Applies to PCB v1.1
as well — the only difference between revisions is a single corrected trace; all GPIO
assignments are identical.

## Table of Contents

- [Source of Truth Contract](#source-of-truth-contract)
- [Supported Hardware](#supported-hardware)
- [Serial Ports](#serial-ports)
- [GPIO Assignment Summary](#gpio-assignment-summary)
    - [RC Receiver Wiring Modes](#rc-receiver-wiring-modes)
    - [RC Binding Defaults](#rc-binding-defaults)
    - [Battery Monitoring](#battery-monitoring)
- [PCB Revisions](#pcb-revisions)
- [artoo.uk Hardware Reference](#artoouk-hardware-reference)

---

## Source of Truth Contract

This document is the canonical hardware mapping reference for Artoo Controller PCB pinouts.

When pin information is needed during implementation or review, use this precedence:
1. `docs/pin_map.md`
2. `include/config.h`

If these sources ever diverge, reconcile them in the same change.

---

## Supported Hardware

The Artoo Controller PCB is purpose-built for the **dual-header ESP32 D1 Mini clone**
(`wemos_d1_mini32` in PlatformIO). This is a third-party clone — not an official
Wemos/LOLIN product — sold under names such as "ESP32 D1 Mini", "D1 Mini32", or
"Wemos D1 Mini ESP32". It has dual-row headers on both long sides (~40 pins total)
with inner rows matching the original ESP8266 D1 Mini shield footprint.

No other ESP32 board is supported.

---

## Serial Ports

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

## GPIO Assignment Summary

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
