# HOTRC SBUS-A Receiver & DS-650 Transmitter
**Specification Document for Coding Agents**

**Version**: 1.0 (April 2026)
**Purpose**: Complete reference for parsing SBUS output from the HOTRC SBUS-A receiver when paired with the DS-650 6-channel one-handed transmitter. Includes exact serial protocol, channel mapping, transmitter-side adjustments, and implementation notes.

## 1. Overview
The HOTRC SBUS-A is a compact 2.4 GHz FHSS multichannel receiver that outputs standard Futaba S.BUS (16 channels, single-wire digital serial). It is fully compatible with the HOTRC DS-650 (and DS-600, CT-6A/B, CT-8A/B, etc.).

The DS-650 is a 6-channel color-screen one-handed transmitter (released ~2025) that sends stick/switch data over the 2.4 GHz link. The SBUS-A packs the first 6 active channels (plus neutral on 7–16) into a standard 25-byte SBUS frame. All transmitter adjustments (reverse, EPA, sub-trim, mixes, failsafe) are applied before the RF link, so the SBUS values already reflect the configured behavior.

No HOTRC-specific proprietary transport was identified in project bench work; treat it as SBUS-compatible framing/packing.
Important implementation note: do not assume only one footer byte value in production decoders. Bench captures in this project required accepting standard SBUS footer handling used by common libraries (0x00 and SBUS2-style low-nibble 0x04 variants).
[Official HOTRC site – product lineup and compatibility](https://en.hotrc.cn/)
[SBUS-A receiver product page – full specs](https://www.aliexpress.com/item/1005010033707063.html)
[DS-650 transmitter product page – color screen model](https://www.aliexpress.com/item/1005009386250204.html)

## 2. HOTRC SBUS-A Receiver Specifications
- Channels: 16 (digital SBUS output only)
- Frequency: 2.4 GHz FHSS/GFSK
- Input voltage: DC 4–9 V
- Current draw: ~35 mA
- Range: ~300 m (ground) / ~800 m (air) — environment-dependent
- Telemetry: Signal strength + RX voltage (0–27 V) returned to compatible transmitters (visible on DS-650 screen)
- Size / weight: 28 × 14 × 1 mm / 1.7 g
- Antenna: Single internal/external (keep straight, away from metal/carbon)
- Connector: 1.25 mm pitch 3-pin (signal / VCC / GND)
- No built-in gyro (unlike some F-xxA “AT” variants)
- Binding: Transmitter off → power receiver + hold BIND button → transmitter on → menu “Bind Set” → Start. LED: fast flash → solid when bound.

**Wiring (3-pin connector)**:
- Pin 1 (signal, usually white/orange) → SBUS to FC RX pin
- Pin 2 (red) → VCC (4–9 V)
- Pin 3 (black/brown) → GND

[SBUS-A detailed product listing with specs and wiring notes](https://www.aliexpress.com/item/1005010033707063.html)
[Compatible SBUS-A for DS-650 bundle](https://www.aliexpress.com/item/1005010067953217.html)

## 3. HOTRC DS-650 Transmitter – Joystick & Channel Mapping (Highlighted)
The DS-650 is a palm-held, one-handed transmitter with a single 2-axis joystick + 4 programmable buttons/switches and color LCD for real-time feedback (channel positions, RX voltage, signal strength, battery, mixes).

**Joystick mapping (core one-handed control)**:
- Horizontal axis (left/right) → Channel 1 (CH1): Steering / direction / rudder.
- Vertical axis (up/down) → Channel 2 (CH2): Throttle / acceleration / speed (forward + reverse).

This is the standard “throttle per channel” layout for one-handed RC transmitters used in cars, boats, tanks, and crawlers: thumb-operated joystick gives simultaneous steering + throttle without a separate wheel or trigger.

**Additional channels (via buttons/switches)**:
- CH3, CH4, CH5, CH6: Programmable momentary or latching switches (lights, winch, gear, cruise control, etc.).

**Transmitter adjustments that directly modify SBUS values** (applied before RF transmission):
- REV (reverse): Per-channel (visible on DS-650 color screen).
- EPA / Stroke / Travel limit: Unilateral % adjustment per channel (CH1: 20 steps, CH2: 5 steps, CH3–6: 10 steps).
- SUB-TR / Trim: Neutral offset (ST.TRIM for CH1, TH.TRIM for CH2).
- MIX: CH1+CH2 differential / tank / boat mixing (activated by holding CH3 switch on power-up).
- Failsafe: Per-channel values set on transmitter (default: CH1 neutral, CH2 brake/neutral).
- Cruise control / throttle delay / constant speed: Affects CH2 output.
- Channel definition: 2/3-gear, jog modes (DS-650 only).

All settings are visible and adjustable on the color LCD. Factory defaults: CH1 reduced stroke, CH2 maximum stroke.
[DS-600 / DS-650 Manual PDF – full channel mapping, joystick, EPA, REV, and failsafe instructions](https://www.printed-droid.com/wp-content/uploads/2023/04/HotRC-DS-600-Manual.pdf)
[DS-650 function explanation video – joystick calibration, stroke %, throttle mapping](https://www.youtube.com/watch?v=cCakbP9sRMQ)
[Comparing HotRC DS Controllers (2026) – DS-650 specifics vs DS-600](https://bithead942.wordpress.com/2026/01/02/comparing-hotrc-ds-controllers/)

## 4. SBUS Serial Communication Details (HOTRC-Specific Notes)
The SBUS-A outputs exactly the standard Futaba S.BUS protocol — no proprietary framing, baud-rate changes, extra bytes, or value offsets. Community reverse-engineering, Arduino libraries, and FC firmwares all treat it identically to Futaba/FrSky SBUS.

- Physical layer: Inverted UART, nominal 100 000 baud, 8 data bits, even parity, 2 stop bits (8E2).
- Frame: 25-byte packet every ~7–14 ms.
  - Byte 0: 0x0F (header)
  - Bytes 1–22: 16 × 11-bit channels (packed LSB-first) + CH17/CH18 flags
  - Byte 23: Usually 0x00 (or telemetry flags)
  - Byte 24: commonly 0x00; robust decoders should also accept SBUS2-style footer variants where `(byte & 0x0F) == 0x04`.
- Raw channel values (11-bit): 0–2047 (standard scaling used by all parsers).
  - Common reference points often documented in SBUS examples: 172 / 992 / 1811.
  - Project calibration note: do not hardcode these as active endpoint/neutral values for HOTRC operation. Use live `/api/rc` captures and transmitter settings (REV/EPA/SUB-TR/MIX/failsafe) to determine actual min/center/max per channel.

No HOTRC remapping at transport level — the DS-650’s REV/EPA/SUB-TR/MIX are already baked into the raw 11-bit values sent over SBUS. Only CH1–CH6 are expected active in the default profile. CH7–CH16 are typically neutral unless explicitly configured.

Failsafe: Transmitter-configured values are sent in the frame when signal is lost (failsafe flag set).
Inversion: Signal is inverted — use hardware inverter or UART with inversion support (STM32, ESP32, RP2040). Standard Arduino Serial requires inversion.

Example parser scaling (plain formula):
pwm_us = ((sbus_raw - 992) * 0.625) + 1500.0

## 5. Implementation Notes for Coding Agents
- Use any standard SBUS library (no custom HOTRC code needed).
- Only CH1–CH6 are active from DS-650 joystick/buttons.
- Telemetry (signal/RX voltage) is received by the transmitter, not output on SBUS.
- Test failsafe by powering off the DS-650 while monitoring SBUS.
- Binding and antenna orientation are critical for reliable range.
- DS-650 LCD shows exact channel % values — use this for calibration verification.

## 6. Bench-Proven Project Profile Snapshot (Regression Troubleshooting, April 2026)

Scope: protoArtoo bench troubleshooting data for HOTRC DS-650 + HOTRC SBUS-A in dual_sbus mode. This section is operational project evidence, not a generic vendor datasheet replacement.

Confirmed observations from project bench work:
- Receiver/transmitter stack under test: DS-650 controller with SBUS-A on CH1 (drive) and CH2 (dome).
- False-positive decode pattern `{1663, 2019}` was removed after first-LOW frame-start alignment; stable neutral samples then centered around `{ch1=1472, ch2=1889}` for sbus1.
- sbus2 showed intermittent acceptance in that phase (ageMs occasionally climbing into multi-second gaps), indicating timing/slip sensitivity in decoder implementation rather than HOTRC-specific framing.
- Hardcoding FrSky-style calibration references (`172/992/1811`) caused incorrect mapped outputs in this project (for example full throttle at neutral when `ch2` neutral was above configured max).

Observed channel behavior used for calibration work (project-specific):
- CH1 steering axis: neutral near 1472, observed range approximately 255-1919 during sweep.
- CH2 throttle axis: neutral/released near 1889, full-forward near 97, with effective inverted/unidirectional behavior in the tested profile.

Provisional binding targets from regression troubleshooting (apply only after decode stability is confirmed):
- driveSpeed: `sbus1:2:97:993:1889:0:1`
- driveSteer: `sbus1:1:255:1472:1919:0:0`
- domeSpeed (interim): `sbus2:1:255:1472:1919:0:0`
- domeSpeed (target after full steering-side decode validation): `sbus2:1:384:1472:2047:0:0`

Validation status note:
- These values come from active regression issue troubleshooting and must be treated as bench-proven/project-scoped until full bench validation gates are passed.
- If new bench evidence contradicts this snapshot, update this section and the internal troubleshooting notes together in the same change.

## 7. Full Reference Links (All Sources Used)
- [Official HOTRC site](https://en.hotrc.cn/)
- [SBUS-A receiver full specs](https://www.aliexpress.com/item/1005010033707063.html)
- [DS-650 transmitter listing](https://www.aliexpress.com/item/1005009386250204.html)
- [DS-600/DS-650 Manual PDF – channel mapping & settings](https://www.printed-droid.com/wp-content/uploads/2023/04/HotRC-DS-600-Manual.pdf)
- [DS-650 function video – joystick and throttle details](https://www.youtube.com/watch?v=cCakbP9sRMQ)
- [Comparing HotRC DS Controllers blog – DS-650 specifics](https://bithead942.wordpress.com/2026/01/02/comparing-hotrc-ds-controllers/)
- [Manuals.plus HotRC section – quick-start guides](https://manuals.plus/category/hotrc)
- [SBUS-A compatible receiver listing for DS-650](https://www.aliexpress.com/item/1005010067953217.html)
