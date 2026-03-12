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

---

## GPIO Assignment Summary

All confirmed assignments to date. Items still pending Task 0.2 are marked TBD.

| GPIO | PCB Connector | Function | Peripheral | Status |
|------|---------------|----------|------------|--------|
| 1 | S0 TX | USB debug TX | UART0 | ✅ Confirmed |
| 3 | S0 RX | USB debug RX | UART0 | ✅ Confirmed |
| 5 | ARM2 | Utility arm servo #2 | LEDC PWM | ✅ Confirmed |
| 13 | CH2 | SBUS receiver #2 (dome) | RMT | ✅ Confirmed |
| 15 | CH1 | SBUS receiver #1 (drive) | RMT | ✅ Confirmed |
| 16 | S1 TX | Hoverboard TX | UART1 | ✅ Traced |
| 17 | S1 RX | Hoverboard RX | UART1 | ✅ Traced |
| 18 | ARM4 | Spare servo output | LEDC PWM | ✅ Traced |
| 23 | ARM1 | Utility arm servo #1 | LEDC PWM | ✅ Confirmed |
| 25 | DOME | Dome motor ESC | LEDC PWM | ✅ Confirmed |
| 26 | S2 TX | Audio module TX | SoftSerial | ✅ Traced |
| 33 | S3 TX | Dome serial TX | UART2 | ✅ Traced |
| 34 | S3 RX | Dome serial RX | UART2 | ✅ Traced |
| 35 | S2 RX | Audio module RX | SoftSerial | ✅ Traced |
| 19 | ARM3 | Spare servo output | LEDC PWM | ✅ Traced |
| 32 | ARM5 | Spare servo output | LEDC PWM | ✅ Traced |
| 22 | I2C (C) | I2C SCL | I2C | ✅ Traced |
| 21 | I2C (D) | I2C SDA | I2C | ✅ Traced |

### Spare GPIOs (CH headers, available for future use)

| GPIO | PCB Header | Original Function | Notes |
|------|------------|-------------------|-------|
| 2 | CH3 | RC PWM input | Spare — also ESP32 strapping pin |
| 4 | CH4 | RC PWM input | Spare |
| 12 | CH5 | RC PWM input | Spare — also ESP32 strapping pin |
| 27 | CH6 | RC PWM input | Spare |

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

### Deviations from artoo.uk Manual

| Item | artoo.uk Manual | PCB v1.1/v1.2 Actual | Resolution |
|------|-----------------|-----------------|------------|
| Hoverboard serial | S3 / GPIO 16+17 | **S1** / GPIO 16+17 | PCB legend says S1 = Hoverboard (GPIOs match manual) |
| Dome serial | (not specified) | **S3** / GPIO 33+34 | PCB legend says S3 = Dome Control |
| Audio serial | TX-only assumed | TX (26) + RX (35) | RX available but optional |

If you have a different PCB revision and find different assignments, please open an issue.
