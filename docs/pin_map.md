# GPIO Pin Map — Artoo Controller PCB

> Populated from Phase 0 PCB continuity trace on 2026-03-12.
> Traced on: PCB v1.2 (shipped by Steve / artoo.uk with a corrected trace).
> Target board: PCB v1.1 (with the same trace fix applied manually).
> Module: ESP32 D1 Mini.
>
> GPIO assignments are believed identical between v1.1 and v1.2 — the only
> difference is a single corrected trace that was patched on v1.1 by hand
> and fixed in the v1.2 fabrication.

---

## Task 0.1 — Serial Port Trace Results

Physical continuity trace of the PCB "SERIAL COMMS" header block.

The PCB silkscreen includes a legend defining each serial port's intended purpose:

```
0 - ESP
1 - Hoverboard
2 - Sound
3 - Dome Control
```

This legend is the **source of truth** for which peripheral each serial port connects to.

| PCB Label | Function (per PCB legend) | D1 Mini Pin | ESP32 GPIO | Status |
|-----------|----------|-------------|------------|--------|
| S0 TX | ESP — USB debug TX | TXD | 1 | ✅ Confirmed (standard) |
| S0 RX | ESP — USB debug RX | RXD | 3 | ✅ Confirmed (standard) |
| S1 TX | Hoverboard TX | IO16 | 16 | ✅ Traced |
| S1 RX | Hoverboard RX | IO17 | 17 | ✅ Traced |
| S2 TX | Sound — audio module TX | IO26 | 26 | ✅ Traced |
| S2 RX | Sound — audio module RX | IO35 | 35 | ✅ Traced |
| S3 TX | Dome Control TX | IO33 | 33 | ✅ Traced |
| S3 RX | Dome Control RX | IO34 | 34 | ✅ Traced |

### Deviations from Prior Documentation

The artoo.uk product manual listed the hoverboard serial as **GPIO 16 (TX) / GPIO 17 (RX)**.
Physical PCB trace confirms the **GPIOs are correct** — the manual only had the wrong
serial port label (S3 instead of S1).

| Pin | artoo.uk Manual | PCB Trace | Notes |
|-----|-----------------|-----------|-------|
| Hoverboard TX | GPIO 16 (on S3) | **GPIO 16** (on S1) | Correct GPIO, wrong header label |
| Hoverboard RX | GPIO 17 (on S3) | **GPIO 17** (on S1) | Correct GPIO, wrong header label |

**Impact:** The artoo.uk manual had the correct GPIOs (16/17) but attributed them to S3.
The PCB silkscreen legend is definitive: S1 = Hoverboard. The serial port header assignment
was wrong in the manual, but the GPIO numbers were right all along.

### Additional Findings

1. **GPIO 16/17 confirmed for hoverboard** — The artoo.uk manual was right about
   GPIO 16 (TX) and GPIO 17 (RX) for the hoverboard, but placed them on the wrong
   serial port header (S3 instead of S1). Per the PCB silkscreen legend, S1 = Hoverboard.

2. **S1 and S3 swapped vs prior assumption** — Earlier documentation assumed S3 was hoverboard
   and S1 was dome. The PCB silkscreen legend is definitive: S1 = Hoverboard, S3 = Dome Control.

3. **S2 has both TX and RX** — Prior assumption was TX-only for the audio module. The PCB
   provides a full bidirectional serial connection:
   - S2 TX → GPIO 26 (output capable)
   - S2 RX → GPIO 35 (input-only pin — valid for RX)

   The DY-SV5W audio module does send status/ACK responses, so this RX line may be useful
   for read-back if a future audio driver supports it. Not required for basic operation.

4. **GPIO 34 and 35 are input-only** — These ESP32 pins cannot drive outputs. This is correct
   for their use as RX lines (S3 RX and S2 RX), but they cannot be repurposed as TX or GPIO
   outputs.

5. **GPIO 33 for dome serial TX** — This is an ADC1 channel (ADC1_CH5). Using it as UART TX
   is fine, but it cannot simultaneously serve as an ADC input.

---

## Serial Port Summary

| Serial | PCB Header | PCB Legend | Function | TX GPIO | RX GPIO | Baud | Protocol |
|--------|------------|-----------|----------|---------|---------|------|----------|
| 0 | S0 | ESP | USB debug | 1 | 3 | 115200 | Standard UART0 |
| 1 | S1 | Hoverboard | Hoverboard drive | 16 | 17 | 115200 | Gen2.x 8-byte frames |
| 2 | S2 | Sound | Audio module | 26 | 35 | 9600 | DY-SV5W binary (TX primary, RX optional) |
| 3 | S3 | Dome Control | Dome link (slip ring) | 33 | 34 | 9600 | Marcduino ASCII, bidirectional |

## Runtime UART Ownership

| UART | Arduino name | Assigned owner | Condition |
|------|--------------|----------------|-----------|
| UART0 | Serial | USB debug | Always occupied |
| UART1 | Serial1 | DriveTask (hoverboard) | S1 enabled |
| UART1 | Serial1 | RcInputTask (SBUS1/SBUS2 RX) | SBUS mode active — conflicts with hoverboard |
| UART2 | Serial2 | DomeLinkTask (dome link) | S3 enabled |
| UART2 | Serial2 | RcInputTask (SBUS2 RX) | Dual SBUS mode — conflicts with dome link |
| — | GPIO 26 soft-UART | AudioTask (DY-SV5W TX) | S2 enabled |

Safe simultaneous combinations: Standard PWM + dome link + audio; Single SBUS + audio (dome link optional with UART2 query degradation to cached state).

---

## GPIO Assignment Summary

All confirmed assignments to date. Items still pending Task 0.2 are marked TBD.

| GPIO | PCB Connector | Function | Peripheral | Status |
|------|---------------|----------|------------|--------|
| 1 | S0 TX | USB debug TX | UART0 | ✅ Confirmed |
| 3 | S0 RX | USB debug RX | UART0 | ✅ Confirmed |
| 5 | ARM2 | Utility arm servo #2 — Bottom / Right arm | LEDC PWM | ✅ Confirmed |
| 13 | CH2 | SBUS receiver #2 (dome spin) | RMT | ✅ Confirmed |
| 15 | CH1 | SBUS receiver #1 (drive) | RMT | ✅ Confirmed |
| 16 | S1 TX | Hoverboard TX | UART1 | ✅ Traced |
| 17 | S1 RX | Hoverboard RX | UART1 | ✅ Traced |
| 18 | ARM4 | Spare servo output (AUX2) | LEDC PWM | ✅ Traced |
| 23 | ARM1 | Utility arm servo #1 — Top / Left arm | LEDC PWM | ✅ Confirmed |
| 25 | DOME | Dome rotation ESC (artoo.uk: "Dome Servo") | LEDC PWM | ✅ Confirmed |
| 26 | S2 TX | Audio module TX | SoftSerial | ✅ Traced |
| 33 | S3 TX | Dome serial TX | UART2 | ✅ Traced |
| 34 | S3 RX | Dome serial RX | UART2 | ✅ Traced |
| 35 | S2 RX | Audio module RX | SoftSerial | ✅ Traced |
| 19 | ARM3 | Spare servo output (AUX1) | LEDC PWM | ✅ Traced |
| 32 | ARM5 | Spare servo output (AUX3) | LEDC PWM | ✅ Traced |
| 22 | I2C (C) | I2C SCL | I2C | ✅ Traced |
| 21 | I2C (D) | I2C SDA | I2C | ✅ Traced |

### RC Receiver Wiring Modes

The six CH headers (CH1–CH6) support three mutually exclusive receiver types
(per artoo.uk manual):

| Mode           | Wiring                              | Channels available |
|----------------|-------------------------------------|--------------------|
| Standard PWM   | CH1–CH6 → GPIO 15,13,2,4,12,27     | 6 (one per pin)    |
| Single SBUS    | SBUS → CH1 (GPIO 15)               | 6                  |
| Dual SBUS      | SBUS1 → CH1 (GPIO 15), SBUS2 → CH2 (GPIO 13) | 12       |

protoArtoo supports all three receiver modes shown above:

- `standard_pwm` reads CH1-CH6 directly as six PWM inputs
- `single_sbus` uses CH1 / GPIO 15 as the drive SBUS receiver
- `dual_sbus` uses CH1 / GPIO 15 for drive SBUS and CH2 / GPIO 13 for dome SBUS

> ⚠️ **GPIO 15 strapping pin — USB upload caveat:**
> GPIO 15 is an ESP32 strapping pin (`MTDO`). When the ESP32 is seated in the
> Artoo PCB with a SBUS receiver connected to CH1/GPIO 15, the receiver can drive
> this pin during the esptool reset-into-bootloader sequence and prevent the
> bootloader from entering download mode. USB upload will fail or time out.
>
> **Unseated ESP32 — auto-reset works:** when the ESP32 is removed from the PCB
> socket there is no receiver load on GPIO 15. DTR/RTS auto-reset enters bootloader
> mode reliably — no BOOT button press required.
> `pio run -e protoArtoo --target upload --upload-port /dev/ttyUSB0`
> (`protoArtoo` env uses `board_upload.before_reset = default_reset`.)
>
> **Seated ESP32 — OTA only:** use `pio run -e protoArtoo_ota --target upload`
> (defaults to STA IP `10.0.0.22`). OTA bypasses bootloader entry entirely.
> For AP-only builds (`protoArtoo_prod`), connect to the `protoArtoo` open AP
> and use `--upload-port 192.168.4.1`.
>
> **esptool flag placement:** always use `board_upload.before_reset` in platformio.ini,
> never `upload_flags = --before ...`. See `tasks/lessons.md` for the full write-up.

In SBUS modes, CH3-CH6 (GPIO 2, 4, 12, 27) are idle PWM-capable inputs that become active again
when `standard_pwm` mode is selected:

| GPIO | PCB Header | RC PWM function   | Notes                                  |
|------|------------|-------------------|----------------------------------------|
| 2    | CH3        | Standard PWM CH3  | Available in `standard_pwm`; unused in SBUS modes; strapping pin |
| 4    | CH4        | Standard PWM CH4  | Available in `standard_pwm`; unused in SBUS modes |
| 12   | CH5        | Standard PWM CH5  | Available in `standard_pwm`; unused in SBUS modes; strapping pin |
| 27   | CH6        | Standard PWM CH6  | Available in `standard_pwm`; unused in SBUS modes |

### Current persisted RC binding defaults (Phase 3 backend)

The RC mapping profile is persisted in NVS and exposed through `/api/config`,
`/api/rc`, and the Setup-page RC Mapping surface. The current firmware defaults are:

| Runtime profile | Action | Default binding | Notes |
|-----------------|--------|-----------------|-------|
| `standard_pwm` | Drive speed | `pwm:1:1000:1500:2000:0:0` | CH1 |
| `standard_pwm` | Drive steer | `pwm:2:1000:1500:2000:0:0` | CH2 |
| `standard_pwm` | Drive limit | `none:0:1000:1500:2000:0:0` | No CH8-equivalent default in PWM mode |
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
| `single_sbus` / `dual_sbus` | Sound trigger | `none:0:1000:1500:2000:0:0` | Disabled by default |

SBUS channels `17` and `18` are also valid persisted binding channels for digital
trigger actions. When a binding targets `17` or `18`, `/api/rc` reports it in the
`digital` object instead of the analog `channels` array.

### Battery Monitoring

The Artoo Controller PCB has **no battery monitoring circuitry** — no voltage divider,
no dedicated ADC trace. The board is a bare PCB with traces, pin headers, and DC
terminals only. If battery voltage sensing is needed in the future, an external voltage
divider must be wired to a spare ADC1 pin (GPIO 36 or 39 are available).

---

## Revision Notes

Different Artoo Controller PCB revisions may have different GPIO assignments. Always verify
by continuity trace against the physical board before using these values.

### PCB Revision History

| Revision | Notes |
|----------|-------|
| v1.1 | Original production run. Has one incorrect trace (fixed manually with a cut/jumper). |
| v1.2 | Corrected trace fix from Steve (artoo.uk board designer). GPIO assignments otherwise identical to v1.1. |

The continuity trace in this document was performed on a v1.2 board. The results apply
to v1.1 as well — the only change between revisions was the corrected trace.

### Confirmed against artoo.uk Hardware Reference

The artoo.uk manual hardware reference table lists the following assignments, all of
which match our PCB trace:

| artoo.uk label     | GPIO(s) | artoo.uk description    | protoArtoo label |
|--------------------|---------|-------------------------|------------------|
| Arm 1 (Top)        | 23      | Left utility arm        | ARM1             |
| Arm 2 (Bottom)     | 5       | Right utility arm       | ARM2             |
| Dome Servo         | 25      | Dome rotation control   | DOME (ESC)       |
| Motor Controller   | 16, 17  | Hoverboard serial       | S1 Hoverboard    |

Note: the artoo.uk manual calls GPIO 25 "Dome Servo" but the signal drives an ESC
(ISDT ESC70) for a **brushed** gearmotor, not a servo. The label is a naming convention
from the original artoo.uk firmware; protoArtoo uses "Dome rotation ESC" for clarity.

### Deviations from artoo.uk Manual

| Item | artoo.uk Manual | PCB v1.1/v1.2 Actual | Resolution |
|------|-----------------|-----------------|------------|
| Hoverboard serial | S3 / GPIO 16+17 | **S1** / GPIO 16+17 | PCB legend says S1 = Hoverboard (GPIOs match manual) |
| Dome serial | (not specified) | **S3** / GPIO 33+34 | PCB legend says S3 = Dome Control |
| Audio serial | TX-only assumed | TX (26) + RX (35) | RX available but optional |
| GPIO 25 label | "Dome Servo" | Dome rotation ESC | Signal drives ISDT ESC70 brushed ESC + brushed gearmotor path, not a servo |

If you have a different PCB revision and find different assignments, please open an issue.


---

## Supported Hardware

The Artoo Controller PCB is purpose-built for the **dual-header ESP32 D1 Mini clone**
(`wemos_d1_mini32` in PlatformIO). This is a third-party clone — not an official
Wemos/LOLIN product — sold under names such as "ESP32 D1 Mini", "D1 Mini32", or
"Wemos D1 Mini ESP32". It has dual-row headers on both long sides (~40 pins total)
with inner rows matching the original ESP8266 D1 Mini shield footprint.

No other ESP32 board is supported. The WEMOS LOLIN S3 Mini is physically incompatible
with the Artoo PCB socket despite sharing the "D1 Mini" name in marketing materials —
it has a different physical size and header count and does not fit the PCB socket.
