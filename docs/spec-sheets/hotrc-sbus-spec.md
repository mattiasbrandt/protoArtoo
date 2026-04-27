# HOTRC SBUS-A Receiver and DS-650 Transmitter

## Purpose

This document captures factual protocol and product behavior for the HOTRC SBUS-A receiver when used with HOTRC DS-series transmitters (with DS-650 focus).

## 1. Receiver Summary (SBUS-A)

- RF band: 2.4 GHz FHSS/GFSK
- Output interface: SBUS digital serial
- Channel capacity over SBUS: 16 proportional + 2 digital flags (SBUS CH17/CH18)
- Input voltage: 4-9 V DC
- Typical current draw: about 35 mA
- Typical range claims: about 300 m ground, about 800 m line-of-sight air (environment dependent)
- Telemetry shown on compatible transmitters: link quality and receiver voltage
- Connector: 3-pin (signal, VCC, GND)
- Approximate size/weight: 28 x 14 x 1 mm, 1.7 g
- No integrated gyro on the SBUS-A variant

Wiring (typical color convention):
- Signal: white/orange
- VCC: red
- GND: black/brown

Typical bind flow:
1. Power receiver while holding bind button.
2. Enter bind mode on transmitter.
3. Confirm receiver link LED transitions from flashing to solid.

## 2. DS-650 Control Mapping (Typical)

Typical DS-650 default behavior:
- CH1: joystick horizontal axis (steering)
- CH2: joystick vertical axis (throttle)
- CH3-CH6: assignable switches/buttons
- CH7-CH16: usually neutral unless explicitly configured

Transmitter-side settings that affect SBUS values before transmission:
- channel reverse (REV)
- travel/end-point adjustment (EPA)
- sub-trim
- mix modes
- failsafe output values
- throttle-delay and cruise/constant-speed functions (when enabled)

Observed DS-650 adjustment granularity in public docs:
- CH1 EPA/stroke supports finer adjustment steps than CH2 and auxiliary channels.
- CH2 EPA/stroke is coarser than CH1 and is often tuned for one-handed throttle behavior.

## 3. SBUS Wire Protocol Behavior

The receiver output follows standard SBUS framing and packing rules:

- UART mode: 100000 baud, 8E2
- Electrical convention: inverted signal logic
- Frame length: 25 bytes
- Typical frame period: about 7-14 ms depending on transmitter/receiver mode
- Header: 0x0F
- Payload: 16 channels packed as 11-bit values (LSB-first packing)
- Flag byte: CH17, CH18, frame-lost, failsafe
- Footer: commonly 0x00; robust decoders in mixed ecosystems may also accept SBUS2-style footer variants with low nibble 0x04

Flag-byte convention reminder:
- Byte 23 carries status flags.
- CH17/CH18 and failsafe/lost-frame bits are decoded from this byte.

Channel value domain:
- Raw decode range is 0-2047 per channel
- Application mapping to us/percent is outside SBUS decode itself

Typical HOTRC profile behavior:
- CH1-CH6 are the actively used channels for DS-650 default one-handed operation.
- CH7-CH16 are usually neutral unless explicitly configured.

## 4. Integration Notes (Protocol-Safe)

- Do not assume only one hardcoded neutral/min/max tuple across all transmitter profiles.
- Calibrate per transmitter configuration because REV/EPA/sub-trim/mix directly change transmitted SBUS values.
- Validate failsafe behavior by switching transmitter power off and observing SBUS failsafe/lost-frame flags.
- Telemetry shown on the transmitter display is not emitted as separate SBUS telemetry fields.
- Because the signal is inverted, use either:
  - a UART/peripheral that supports inversion, or
  - an external inverter stage.

## 5. References

- HOTRC official site: https://en.hotrc.cn/
- HOTRC SBUS-A listing/spec details: https://www.aliexpress.com/item/1005010033707063.html
- HOTRC DS-650 listing/details: https://www.aliexpress.com/item/1005009386250204.html
- DS-600/DS-650 manual PDF: https://www.printed-droid.com/wp-content/uploads/2023/04/HotRC-DS-600-Manual.pdf
- SBUS-A compatible receiver listing (DS-series context): https://www.aliexpress.com/item/1005010067953217.html
