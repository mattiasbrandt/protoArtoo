# HotRC DS-650 Spec Sheet (one-handed SBUS radio controller)

The **HotRC DS-650** is a palm-sized, one-handed 2.4 GHz radio controller with
six channels: a two-axis thumb joystick and four programmable buttons. Bundled
with a six-channel PWM receiver, it also binds HotRC's 16-channel **SBUS-A**
receiver, and droid builders carry it because it hides in a hand.

Research date 2026-09-12. Every number in this document comes from one of four
places: the vendor's own specification pages, the HotRC manual PDFs, the
astromech community projects named in Section 3, or a bench measurement of a
DS-650 with SBUS-A receivers (April 2026: one handset, two SBUS-A receivers,
SBUS output captured by an ESP32 RMT peripheral at 1 us per tick). Claims that
could not be sourced are marked `UNKNOWN` with the artefact or bench test that
would settle them.

This sheet covers the **product**. SBUS framing itself is
[`sbus-protocol.md`](sbus-protocol.md) and the ESP32 RMT peripheral is
[`rmt-esp32-idf5.md`](rmt-esp32-idf5.md).

> [!CAUTION]
> **This handset does not put standard SBUS on the wire.** Four deviations were
> measured, and each one defeats a decoder written to the SBUS documentation:
>
> 1. The SBUS-A receiver clocks its frames at roughly **115 kbaud, not 100
>    kbaud** -- 8.68 us per bit rather than 10.00 (Section 6.1).
> 2. Its footer byte is **`0x04`**, not `0x00`. A decoder that requires `0x00`
>    rejects **100 %** of frames (Section 6.2).
> 3. Out of the box the handset's endpoints are **nowhere near** Futaba's
>    172 / 992 / 1811. The throttle's *resting* value measured **1889-2028**,
>    above a 1811 maximum, which a 172 / 992 / 1811 calibration reads as
>    **full throttle on every frame** (Section 6.3).
> 4. Its CH1 travel is **inverted** against the microsecond value its own screen
>    shows (Section 6.4).
>
> All four are handset- or receiver-side facts. Three of them are correctable by
> configuring the handset; the baud is not.

> [!WARNING]
> **The throttle rests at an end of travel, not in the middle.** CH2 on a
> one-handed HotRC is a trigger, not a centring gimbal: released is one extreme
> of the SBUS range and full-forward is the other. The r2d2-astromech-simulator
> project names this hazard outright -- *"THE THROTTLE TRAP ... Wire that
> straight to the feet and the droid drives away at full reverse the moment you
> stop touching it"* (its `src/js/input/rc.js`). Section 6.3.

> [!NOTE]
> **The fix is permanent and lives in the handset.** Once the DS-650's
> EPA/stroke was reduced to put the sticks inside a Futaba-shaped range, the
> handset measured 192 -> 992 -> 1792 on both axes, and a generic
> `172 / 992 / 1811` calibration read it on every channel with no
> handset-specific table (Section 6.3).

## 1. Scope

Covers: the DS-650 handset as a physical product, its menu and the settings that
change what reaches the wire, its battery and power behaviour, the receiver
family it binds, the SBUS profile measured from the SBUS-A receiver, the
failsafe model, what six channels cannot do, and what the hobby does with these
handsets.

Does not cover: SBUS frame layout and bit packing (that is
[`sbus-protocol.md`](sbus-protocol.md)), the ESP32 RMT peripheral (that is
[`rmt-esp32-idf5.md`](rmt-esp32-idf5.md)), PWM/PPM receiver wiring (that is
[`rc-receiver-spec.md`](rc-receiver-spec.md)), the HotRC over-the-air modulation
below the point where it reaches the receiver's output pin, or the DS-800 beyond
the lineage table.

## 2. Products Covered

### 2.1 The handset, and its siblings

HotRC ships functional changes without incrementing the model number, so the
version column below is the community's, not the vendor's. The source is
bithead942's side-by-side of a personally owned collection of every version
(Section 9), which is the only place this lineage is written down.

| | DS-600 v1 | DS-600 v2 | DS-600 v3 | **DS-650** | DS-800 |
| --- | --- | --- | --- | --- | --- |
| Released | late 2021 | late 2021 | early 2023 | **mid-2025** | late 2025 |
| Channels | 4 | 6 | 6 | **6** | 8 |
| Antenna | external | external | internal | **internal** | internal |
| Screen | 3 LEDs | 3 LEDs | mono LCD 21.0x9.3 mm | **color LCD 22.5x12.0 mm** | color touch 40x30 mm |
| Size W x L x H (mm) | 38.5 x 125.6 x 52.8 | same | 38.5 x 134.0 x 51.4 | **38.8 x 137.6 x 51.4** | 39.5 x 141.3 x 42.5 |
| Weight | 72 g | 72 g | 81 g | **81 g** | 97 g |
| Battery | 1S Li 1200 mAh | 1200 mAh | 1200 mAh | **1S Li 1500 mAh** | 1500 mAh |
| RF power | 70 mW | 70 mW | 100 mW | **100 mW** | 180 mW |
| Bundled receiver | C-06A | C-06A | F-06A | **F-06A** | F-08A |
| Buttons | CH3 momentary, CH4-6 latching | same | CH3/4/6 momentary, CH5 latching | **all four programmable** | programmable |
| Price | USD 15-20 | 19-25 | 21-28 | **28-35** | 28-35 |

**Cross-version binding is constrained.** DS-600 v1 and v2 bind only the C-06A;
DS-600 v3 and DS-650 bind only the F-06A and are interchangeable with each
other; the DS-800 binds only the F-08A. A DS-650 cannot be paired to the
receiver from a v1 or v2 handset.

**The DS-650 and the DS-800 differ in shape and features.** The DS-800 adds two
channels, a timer, a trainer port and eight saved model profiles, and it is
longer and flatter than the palm-sized DS-650. The community source keeps the
DS-650 for its concealable shape. No bench measurement of the DS-800 exists for
this sheet.

### 2.2 The receivers

| Model | Channels | Output | Current | Size (mm) | Weight | Range | Gyro |
| --- | --- | --- | --- | --- | --- | --- | --- |
| **SBUS-A** | **16** | **SBUS serial** | 35 mA | 28 x 14 x 1 | **1.7 g** | 300-800 m | no |
| F-06A | 6 | PWM | 35 mA | 38 x 24 x 13 | 7.4 g | 300-800 m | no |
| F-06AT | 6 | PWM | 39 mA | 38 x 24 x 13 | 7.4 g | 300-800 m | yes |
| F-08A | 8 | PWM | 35 mA | 43 x 24 x 13 | 9 g | 300-800 m | no |

All four take **DC 4-9 V**. The SBUS-A additionally offers a return-voltage
telemetry input rated **0-27 V**, reported back to the handset's screen at up to
about 300 m.

> [!IMPORTANT]
> **The SBUS-A is not the receiver the DS-650 ships with.** A DS-650 bundle
> contains an F-06A, which emits six PWM channels; the SBUS-A is bought
> separately. The SBUS-A's own manual lists its compatible transmitters as
> *"HOTRC CT-4A, CT-6A, CT-8A, HT-8A, and DS600 transmitters"* -- the DS-650 is
> **not named**, because the receiver predates it. It binds anyway: a DS-650
> and SBUS-A pair was bound and run on a bench. Treat
> the vendor's list as incomplete rather than as a constraint, but buy the
> receiver knowing the vendor does not promise the combination.

Two size figures for the F-06A exist and they do not agree: the receiver manual
says 38 x 24 x 13 mm, the community measurement says 19.2 x 33.0 x 13.5 mm with
a 90 mm antenna. Both are recorded.

## 3. Sources Checked

| Source | URL or path | Extraction notes |
| --- | --- | --- |
| **Bench measurement, April 2026** | one DS-650, two SBUS-A receivers, SBUS output captured by an ESP32 RMT peripheral at 1 us per tick | **The source for everything on the wire** (Section 6): the 115 kbaud bit period, the `0x04` footer, the factory-stroke endpoint sweep, the display-to-raw formula, the button polarity, the held value with `lost_frame`, and two receivers linked to one handset at once |
| **DS-650 vendor specification** | https://manuals.plus/ae/1005009356275137 | The specification table, the full menu list, the bind procedure, the bundled F-06A |
| **DS-600 manual PDF** | https://www.printed-droid.com/wp-content/uploads/2023/04/HotRC-DS-600-Manual.pdf | Read in full. The button-combination era procedures -- mixing, reverse, stroke, failsafe -- and the panel layout. **This is the older sibling's manual, not the DS-650's** (Section 4.2) |
| **HotRC receiver manual** | https://manuals.plus/ae/1005006924396938 | The thirteen-model receiver table, the SBUS-A row, and the compatible-transmitter list that omits the DS-650 |
| SBUS-A product page | https://begreatrc.com/products/hotrc-sbus-a-24ghz-receiver | 16 channels, 4-9 V, 0-27 V return, 28 x 14 x 1 mm, 300-800 m. **No compatibility list at all** |
| **bithead942, "Comparing HotRC DS Controllers"** | https://bithead942.wordpress.com/2026/01/02/comparing-hotrc-ds-controllers/ | **The lineage table, and the only systematic comparison that exists.** Written by a droid builder from a personally owned set of every version. Also the 1:1 pairing claim and the "no ramp control" limitation |
| bithead942, "Modifying the HotRC DS-600" | https://bithead942.wordpress.com/2023/01/21/modifying-the-hotrc-ds-600/ | The battery-capacity modification, the wrist-strap and charge-LED complaints |
| Printed Droid knowledge base | https://www.printed-droid.com/kb/hotrc-ds-600/ | The droid-community framing: *"a very small remote control for mini droids"* that *"can be held in one hand and offers a reliable connection."* Reproduces the DS-600 manual; **no SBUS content** |
| r2d2-astromech-simulator, RC module | `src/js/input/rc.js` in that project | The throttle trap, and a split-about-centre RC normalisation |
| ShadowMD, Padawan360 | the projects' sources (`Shadow_MD_DualController_Template.ino`; Padawan360 with Maestro and DY-SV5W) | Read for the control grammar comparison in Section 9. **Neither reads an RC receiver at all** |

**What did not survive checking.** A web research pass reported that the DS-650
*"only works with the F-06A receiver"* and that the SBUS-A is *"NOT standard with
DS-650 bundles"*. The second half is right and useful. The first half is
**contradicted by bench measurement**: one DS-650 ran two SBUS-A receivers at
once. The same pass reported an EPA granularity of *"20 gear positions for
steering, 5 for throttle"* attributing it to a DS-650 page; that figure is real
but it comes from the **DS-600** manual, which is a different generation with a
different adjustment interface. It is reported in Section 4.2 with the right
attribution.

A second claim is recorded but **not relied on**: that a HotRC handset *"can bind
to more than one receiver ... but it will only work with one at a time ... the
most recently bound."* That is a first-hand community observation about the
PWM receivers, and it is inconsistent with a dual-SBUS bench run, where both
SBUS-A receivers were linked simultaneously and reported identical channel
values. Open Item 1.

## 4. The handset

### 4.1 What is actually in your hand

A palm-sized, thumb-operated block, held in one hand, with no second gimbal and
no switch bank. Everything an operator can do is one of six things:

| Control | Channel | Behaviour |
| --- | --- | --- |
| Joystick, left/right | **CH1** | proportional, self-centring |
| Joystick, up/down | **CH2** | proportional, **does not self-centre to mid-range** (Section 6.3) |
| Button 3 | **CH3** | programmable: momentary, latching, three-position or jog |
| Button 4 | **CH4** | same |
| Button 5 | **CH5** | same |
| Button 6 | **CH6** | same |
| CH1 trim rocker | -- | shifts CH1's transmitted centre |
| CH2 trim rocker | -- | shifts CH2's transmitted centre |
| Power switch | -- | see 4.3 |
| Color LCD, 0.96 in | -- | battery, signal, receiver voltage, channel states, menu |

That is the whole instrument. **Six channels, two of them proportional.**
Section 8 is what follows from that number.

The vendor's specification table:

| Field | Value |
| --- | --- |
| Model type | Car / Ship / Tank |
| Channels | 6 |
| RF | 2.4 GHz ISM, FHSS, GFSK |
| RF distance | 300 m (see the contradiction below) |
| Reaction speed (PWM) | <= 20 ms |
| Transmitter voltage | DC 3.7-9 V |
| Receiver voltage | DC 3.7-9 V |
| Color screen | 0.96 inch |
| Net weight | 80 g |
| Battery | built-in 1500 mAh rechargeable lithium |
| Charging | USB Type-C |

Three figures disagree across sources and all three are recorded rather than
resolved:

- **Range.** The DS-650 page says 300 m. The receiver manual says 300-800 m
  ("about 300 m from the ground and 800 m in the air"). The DS-600 manual says
  water surface 300-500 m, ground 400-600 m, and adds its own disclaimer that
  *"the marked distance is only for reference."* For a droid working a hall, all
  three are far beyond what matters and none of them describe a room full of
  bodies at 2.4 GHz. `UNKNOWN` what the usable range is indoors among people;
  no source measures it.
- **Voltage.** The DS-650 page says DC 3.7-9 V both sides. The receiver manual
  and the community comparison both say DC 4-9 V. The DS-600 manual says 5 V
  transmitter, DC 4-14 V receiver. Use **4-9 V** as the safe intersection for
  any receiver you power yourself.
- **RF power.** The DS-650 page prints *"Transmit Power ~100mA"*, which is a
  unit error; the community table says **100 mW** and the DS-600 manual says
  *"< 100 mW"*. 100 mW is the figure to use.

The community comparison additionally records what no vendor page states:
receiver sensitivity -96 to -97 dBm, *"4096 channels of resolution"* on the
sticks, and a 15-minute idle auto-shutdown.

### 4.2 The menu, and the settings that change the wire

The DS-650's color screen carries a real menu. Its entries are:

`REV`, `EPA`, `SUB TR`, `Button Type`, `MIXES`, `CCS`, `RX Info`, `Fail Safe`,
`Servos`, `Gyro`, `Joystick`, `Reset`, and `System` (Language, Sound, Auto-off,
Lock).

Six of those change what an SBUS decoder sees, and anyone debugging a "decoder
bug" should rule them out first:

| Menu entry | What it does to the wire |
| --- | --- |
| **`REV`** | inverts a channel's direction. The value the decoder reads runs the other way; nothing about framing changes |
| **`EPA`** | end-point adjustment, the stroke limits. **This is the setting that moved the measured endpoints from 255/1919 to 192/1792** (Section 6.3) |
| **`SUB TR`** | sub-trim, shifts the transmitted centre independently of the trim rockers |
| **`MIXES`** | tank/differential mixing. **With mixing on, CH1 and CH2 stop being steer and throttle** and become left and right motor commands. A model whose steering suddenly does nothing sensible should check this before anyone touches code |
| **`Button Type`** | the DS-650's own advance: each of CH3-CH6 can be *2 gears* (on/off), *3 gears* (three positions) or *jog* (momentary). This decides whether a button reads as a level or as a pair of edges |
| **`CCS`** | constant/cruise speed. A latched throttle. Section 4.3 |

`Fail Safe` is Section 7.3. `Gyro` applies only to the `-AT` receiver variants
and does nothing with an SBUS-A. `Joystick` is a centre-calibration routine, and
`Reset` is a factory reset -- the DS-650 is the first model in the line to have
one.

> [!CAUTION]
> **The DS-600 manual's procedures are not the DS-650's, and they collide.** The
> widely circulated HotRC PDF (Printed Droid hosts a copy) documents the
> *button-combination* generation, where settings were reached by holding a
> channel button while powering on:
>
> | Hold at power-on | DS-600 effect |
> | --- | --- |
> | Channel 3 | toggles **mixing** on/off (green indicator) |
> | Channel 4 | enters **CH1/CH2 reverse** setting |
> | Constant-speed key | enters **stroke (EPA)** setting: `ST.TRIM` +/- adjusts CH1 across **20 steps**, `TH.TRIM` +/- adjusts CH2 across **5 steps**, and CH3-CH6 cycle through **10 steps** each |
>
> The DS-650 does all of this from its menu instead. The collision is this:
> **the HotRC receiver manual's bind procedure is "hold Channel 3 while turning
> the transmitter on"** -- which on a DS-600 is the mixing toggle, and on a
> DS-650 is nothing at all, because the DS-650 binds from `Bind Set` -> `Start`
> while the receiver's BIND button is held. Do not follow a receiver manual's
> bind steps on a DS-650. Open Item 3.
>
> The *"CH1 adjusts in finer steps than CH2"* claim in
> [`hotrc-sbus-spec.md`](hotrc-sbus-spec.md) is real and this is its source:
> 20 steps against 5. It is a **DS-600** figure. `UNKNOWN` what the DS-650's
> menu-driven EPA granularity is; no source read states it.

### 4.3 Power, cruise, and two operational traps

**Battery.** 1S lithium, 1500 mAh, charged over USB-C. The community source
reports the 1200 mAh predecessor running out inside a field day and documents
replacing it; the DS-650's extra 300 mAh and its longer handle make that
modification easier rather than unnecessary. Plan for an event, not a bench
session.

> [!WARNING]
> **`CCS` -- constant speed -- is a latched throttle on a machine that drives
> among people.** It is a first-class feature of this handset with its own
> dedicated key, and on a boat it is exactly right. On a droid it means the
> operator's thumb leaving the stick no longer stops the droid. Nothing on the
> receiving side can detect it: a cruising DS-650 transmits a steady non-neutral
> CH2 that is indistinguishable from a held stick. **Decide deliberately whether
> the droid's handset has this enabled**; whatever bounds a cruising droid has
> to live on the droid.

> [!NOTE]
> **The DS-650 refuses to power off while its receiver is still powered.** The
> community source records this as *"both nice and annoying"*. For a droid it is
> mostly nice -- it makes "operator switched the radio off while the droid was
> live" harder to do by accident -- but an operator who expects the handset to
> die on command will find it does not, and the droid must be powered down first.

## 5. Receivers and binding

### 5.1 SBUS-A

Sixteen SBUS channels out of a 28 x 14 x 1 mm, **1.7 g** sliver -- it is a
flexible strip rather than a cased receiver. DC 4-9 V, 35 mA. A separate
return-voltage input reports 0-27 V back to the handset's screen.

Only **six** of its sixteen channels carry anything from a DS-650. CH7-CH16 sit
at whatever the handset transmits for an unused channel and carry no control.

Wiring is the usual three-wire servo convention -- signal, VCC, ground -- with
signal white or orange, VCC red, ground black or brown. The signal is standard
inverted SBUS logic.

**A hardware UART tolerates this receiver better than a bitstream decoder.** A
UART re-synchronises on every byte's start bit, so the baud deviation in Section
6.1 costs it little. A decoder that reconstructs bits from captured edge timings
(an ESP32 RMT capture, for example) has to estimate the bit period and
re-synchronise per byte itself, and Section 6.1 is what happens when it does not.

### 5.2 F-06A and F-06AT, the bundled PWM receivers

Six PWM channels, 1000-2000 us, frame period documented only as *"reaction speed
(PWM) <= 20 ms"*. DC 4-9 V, 35 mA (39 mA for the gyro variant). These are what a
DS-650 bundle contains: six signal wires, one per channel, no SBUS at all.

The `-AT` variants carry a gyro whose sensitivity is set from the handset's CH1
trim and whose on/off is CH3. On a droid this is a heading-hold intended for
surface vehicles; enabling it means the receiver is modifying the steering
channel before the controller ever sees it. Leave it off.

[`rc-receiver-spec.md`](rc-receiver-spec.md) is the sheet for this path,
including the HotRC "analog mode" 20 ms / 50 % duty neutral versus "digital
mode" 3 ms / 333 Hz distinction and the 3.9k/6.8k divider for 5 V receiver
outputs into an ESP32.

### 5.3 Binding

On a DS-650: power the receiver, hold its **BIND** button, turn the handset on,
then `Bind Set` -> `Start`. The receiver's LED goes from rapid flashing --
powered, no signal, unpaired -- to steady, which is the pairing confirmation.

On the older button-combination handsets, and on the CT/HT series the SBUS-A's
manual was written for, binding used a **bind plug** ("the Coder") inserted into
the receiver's `B` port, or holding Channel 3 at power-on. Section 4.2's caution
applies: do not carry those steps across.

Pairing is **one handset to one receiver at a time** according to the community
source, with the most recently bound receiver winning and an earlier one
resuming if the newer is powered down. That is a first-hand observation of the
PWM receivers. A bench run with two SBUS-A receivers bound to one DS-650
contradicts it: both were linked at the same time and reported identical
channel values. Until Open Item 1 is settled, **a builder planning two
receivers on one handset should verify both link simultaneously before
designing around it.**

## 6. What actually arrives on the wire

Everything in this section was measured on a bench in April 2026: one DS-650,
SBUS-A receivers, the SBUS output captured by an ESP32 RMT peripheral at 1 MHz
(1 us per tick). Where a vendor claim exists it is named.

### 6.1 The baud is not 100 kbaud

SBUS is specified as 100000 baud, 8E2, inverted. The HotRC SBUS-A transmits at
**approximately 115 kbaud** -- a standard UART rate, and 15 % fast.

At a 1 MHz capture resolution that is **8.68 ticks per bit** rather than
10.00. For a decoder that assumes 10, the consequence is not a small timing
error; it is a decode failure mode:

- With period 10 and an actual 8.68 ticks per bit, any run of 4 or more
  physical bits is under-counted by one bit (`floor(8.68N/10) < N` for
  `N >= 4`). The result is bit slip throughout every frame and **98.4 % of
  frames failing to extract**, measured.
- Estimating the period from the frame instead works: a 25-byte 8E2 frame is
  300 bits, so a true 100 kbaud frame sums to about 3000 ticks (period **10**)
  and a HotRC frame to about **2604** ticks (period **9**).
- **The inter-frame gap must stay out of that sum.** The ESP32 RMT driver puts
  the roughly 300 us idle gap into the last captured symbol's duration;
  including it rounds 9 up to 10 and brings back the 98 % failure.
- Period 9 against a true 8.68 still drifts, so the decoder also has to
  **re-synchronise on each byte's start bit**, as a hardware UART does. The
  measured profile is **a single-bit slip per frame** as the dominant mode, so
  a +/-1 bit search per byte is enough.

Without per-byte re-synchronisation, **no frame with CH1 above 1472 decoded**,
across a full physical sweep of the stick: that byte pattern produced a single
uncompensated slip that pushed the footer off position. A blind spot covering
half the steering travel, produced entirely by a 15 % baud deviation.

Parity is a trap at this baud for a bitstream decoder: gating on the parity bit
was tried and every frame failed, because the bit-run expansion shifts the
parity positions.

### 6.2 The footer is not 0x00

Standard SBUS ends `0x00`. The HotRC SBUS-A sends **`0x04`**, the SBUS2-style
footer, or a variant whose low nibble is `0x04`. A decoder that checks for
`0x00` alone rejects **100 % of frames**, measured.

The footer rule in `bolderflight/sbus` v8.x accepts both:

```c
footer == 0x00 || (footer & 0x0F) == 0x04
```

Under that rule `0x00`, `0x04` and `0x14` are accepted and `0x03` is rejected.

### 6.3 The endpoints are not Futaba's until you make them so

**As shipped**, with factory stroke settings, the sweep measured:

| Channel | Role | Observed min | Observed max | At rest |
| --- | --- | --- | --- | --- |
| CH1 | steer (joystick horizontal) | ~255 | ~1919 | **~1472** |
| CH2 | throttle (joystick vertical) | ~97 | ~2028 | **~1889-2028** |

Read against a Futaba-shaped `172 / 992 / 1811` calibration, the second of
these is dangerous:

1. CH1's rest value of 1472 is not 992. Normalised against a 992 centre it reads
   **+59 % deflection, permanently**, with the stick untouched.
2. **CH2's rest value is above the 1811 maximum.** 1889 clamps to 1811, which
   normalises to **+1.0**: full throttle, on every frame, with the trigger
   released. This is the throttle trap.

**The fix is in the handset.** Reducing the EPA/stroke brought the transmitted
range inside a Futaba-shaped window, and the bench measured:

- neutral sticks: CH1 = 992, CH2 = 992
- full sweep: **192 -> 992 -> 1792** on both axes
- against a 172 / 992 / 1811 calibration that maps to **+/- 0.977**, exactly
  `800/819`: the handset's output range sits just inside the calibration's

After that recalibration the factory-stroke values above (neutral 1472 / 1889)
no longer apply to that handset.

> [!IMPORTANT]
> **The measured endpoints are a handset configuration, not a product
> characteristic.** REV, EPA, sub-trim and the trim rockers all move them, and a
> second DS-650 set up by a different builder will read differently. Calibrate
> per handset configuration: make the handset fit a generic
> `min:center:max` calibration, or calibrate to the handset. A table of one
> builder's numbers fits only that builder's handset.

**The handset has no expo, rate curve or ramp.** The community source lists
*"No Ramp control"* among the DS series' limitations, and separately describes
the throttle as going *"zero to 100 very suddenly"*. A softer throttle has to
come from whatever reads the receiver.

### 6.4 CH1's screen value is inverted against SBUS raw

Measured on an SBUS-A, with the handset's own display in microseconds:

```
SBUS_raw = -2.176 x (display_us - 1500) + 1472
```

| Stick | Handset display | SBUS raw |
| --- | --- | --- |
| neutral | 1500 us | 1472 |
| max right | ~2000 us | 384 |
| max left | ~1000 us | ~2047 |

Right-stick produces a *lower* raw value. Someone reading the handset's screen
and someone reading the decoded SBUS value are watching the same axis run in
opposite directions, and both are correct. This was measured **before** the
recalibration in 6.3; the inversion is a property of the axis, the specific
constants are not.

### 6.5 The buttons idle high

CH3-CH6 on a DS-650 **idle high and press low** -- the opposite of the
convention a `172 -> 1811` calibration assumes. Read without reversing those
channels, every button reads as permanently pressed and every press as a
release. Measured on the SBUS-A.

## 7. Failsafe

### 7.1 Three distinct things can go wrong and they signal differently

| What happened | What the receiver does | What a decoder sees |
| --- | --- | --- |
| A frame was corrupted or missed | keeps sending; sets **`lost_frame`** (flags bit 2) | a frame with `lost_frame` set and a **held** channel value (7.2) |
| The link is gone and the receiver has declared it | keeps sending; sets **`failsafe`** (flags bit 3), channels set to the stored failsafe positions | a frame with `failsafe` set |
| The receiver stopped, unplugged, or lost power | nothing arrives | silence; only a timeout catches it |

SBUS's signalling advantage over CRSF is exactly this: **the frame keeps coming
and carries a flag**, so the first two cases are positive information rather than
silence. [`elrs-crsf-radio.md`](elrs-crsf-radio.md), "Failsafe", documents the
other side of that coin, where the frame simply stops.

### 7.2 `lost_frame` is not failsafe, and the held value is convincing

The single most dangerous measured behaviour: on any brief signal interruption
the SBUS-A outputs a held position (`ch1=389`, about -89 % of travel) with
**`lost_frame=true`, `failsafe=false`**.

389 is inside the legal range, decodes cleanly, and reads as a large deflection
-- with only `lost_frame` distinguishing it from a real command. A consumer that
acts on every frame without `failsafe` set acts on the held value. Treating
`lost_frame` as a diagnostic counter is the bug; and a consumer that refreshes
its link-freshness timeout on a `lost_frame` frame will hold that value for as
long as the interruption lasts.

### 7.3 What the handset's own failsafe setting does

The DS-650 carries a `Fail Safe` menu entry; the DS-600 generation set the same
thing by holding the throttle where you wanted it and inserting the bind plug
for three seconds. Either way the model is identical: **the receiver stores a
per-channel output value and sets the channels to it when the link is lost.**
The DS-600 manual states the default is the neutral point, and carries the
warning that matters:

> The safe return position of the throttle servo is recommended to be set to the
> brake or neutral point. **Do not set it to the throttle open position to avoid
> danger.**

The stored positions arrive alongside `failsafe=true`. A controller that
discards channel values whenever that flag is set never uses them; setting the
handset's failsafe to neutral is still correct, because it protects anything
wired straight to the receiver and costs nothing.

Verification is one action: **switch the handset off and confirm the model
stops.**

## 8. What the handset cannot do for you

- **Six channels, two of them proportional.** One DS-650 gives a controller two
  axes and four buttons; a second handset or a second receiver bound to the
  same handset (Open Item 1) gives twelve channels.
- **No modifier chords.** ShadowMD and Padawan360 multiply a gamepad's buttons
  by holding L1/L2/R1 -- four actions per face button (Section 9). A DS-650
  sends CH3-CH6 as independent channel values with no notion of simultaneity,
  so anything chord-like has to be built by whatever reads them.
- **No throttle curve, no ramp.** Section 6.3.
- **No return path.** The handset displays receiver voltage and signal strength
  on its own screen from the receiver's telemetry, but **none of that is an SBUS
  field** and none of it reaches the controller wired to the receiver. Unlike
  CRSF, a model on a HotRC link cannot report anything back to its operator.

## 9. How the hobby actually uses these (non-normative)

Everything in this section is evidence of practice.

### 9.1 Why droid builders buy this handset

Printed Droid's knowledge base lists it as *"a very small remote control for
mini droids"* that *"can be held in one hand and offers a reliable connection."*
bithead942, who has owned every version since 2022, puts the reason more
directly: *"the form-factor alone is the best selling point for me because of my
need for concealment."*

That is the whole argument. A droid at an event is performing, and an operator
visibly working a tray-mounted transmitter breaks it. A DS-650 fits in a palm or
a pocket, costs USD 28-35, and leaves the operator's other hand free. Builders
commonly carry **two** -- one per hand -- which is a twelve-channel setup for
under USD 70.

The same source is candid about the trade: the DS series is *"mid-level"*, with
no ramp control, no stabilisation, no macro record, and -- explicitly -- *"No
Open protocols ... no support for OpenTX or ELRS or CRSF or anything similar."*
A builder choosing a DS-650 is choosing concealment and price over
programmability, deliberately.

### 9.2 What the other astromech projects do, and why it does not transfer

| Project | Input | Grammar | Actions addressable |
| --- | --- | --- | --- |
| **ShadowMD** | PS3 Navigation over Bluetooth | per-button config blocks, each naming a MarcDuino function, an MP3, a logic display type and a panel sequence | the MarcDuino function table, codes **1-89** (in `Shadow_MD_DualController_Template.ino`), plus custom combinations |
| **Padawan360** | Xbox 360 over USB Host Shield | face button x modifier (L1 / L2 / R1 / none) | ~16 direct |
| **ShadyRC / CHIRP** | ExpressLRS, CRSF | channel map plus arming | see [`elrs-crsf-radio.md`](elrs-crsf-radio.md) |
| **astromech simulator** | USB transmitter via the Gamepad API | four Mode 2 axes onto a pad stub | 4 axes |

**Neither ShadowMD nor Padawan360 reads an RC receiver at all.** Both are
gamepad projects, and their rich grammars come from having a dozen buttons and
two modifiers to chord with. Their approach does not port to a six-channel
handset: with six inputs, which six is the only interesting question.

Their drive constants, for calibration comparison:

| | ShadowMD | Padawan360 |
| --- | --- | --- |
| Drive speeds | 70 / 110 (of 127) | 60 / 100 / 127 |
| Turn speed | 50 | 50 |
| Dome speed | 100 | 80 |
| Ramping | 1 per loop | 5 per loop |
| Stick deadzone | 15 foot / 10 dome | 20 |

Both mature projects **ramp**, which matters against a DS-650's own *"zero to
100 very suddenly"* throttle. And both use a **non-zero deadzone**, which on a
stick whose centre wanders is the difference between a still droid and a
creeping one.

### 9.3 The throttle trap, named by someone else first

The r2d2-astromech-simulator project implements an RC channel model --
split-about-centre normalisation, per-channel reverse, deadzone rescaled rather
than subtracted so a calibrated stick still reaches 1.000 at the stop -- and it
carries this comment at the point where it auto-assigns channels to the feet:

> *THE THROTTLE TRAP. Channel 3 on a Mode 2 set rests at the BOTTOM of its
> travel, and the calibration correctly calls that a full-span axis -- top to
> bottom is -1 to +1. Wire that straight to the feet and the droid drives away at
> full reverse the moment you stop touching it, which is precisely the bench
> accident this whole panel exists to avoid.*

Its answer is to force anything auto-assigned to a stick back to rest-is-zero.
On a DS-650 the same trap is CH2 (Section 6.3): a calibration whose `center`
is left at 992 with a throttle that rests at 1889 reads full deflection at rest.
Setting `center` to the rest value is the configuration that avoids it.

## 10. Quick Reference

- Field: Channels the handset transmits. Value: **6** (CH1-CH2 proportional, CH3-CH6 buttons). CH7-CH16 carry nothing.
- Field: SBUS receiver. Value: **HotRC SBUS-A**, 16-channel SBUS, 1.7 g, DC 4-9 V, 35 mA. Bought separately.
- Field: Receiver the handset ships with. Value: **F-06A**, six PWM channels -- *not* the SBUS-A.
- Field: Measured baud. Value: **~115 kbaud**, 8.68 us/bit. **Not 100 kbaud.**
- Field: Bit period at a 1 MHz capture. Value: **9 ticks** (frame sum ~2604 ticks over 300 bits), against 10 for true 100 kbaud.
- Field: Bit-period estimate on an ESP32 RMT capture. Value: sum every captured symbol **except the last**; the last carries the ~300 us inter-frame gap.
- Field: Footer. Value: **`0x04`** (low nibble `0x04`). A `0x00`-only check rejects 100 % of frames.
- Field: Header. Value: **`0x0F`**, unchanged.
- Field: Frame length. Value: **25 bytes**, 300 bits at 12 bits/byte.
- Field: Byte re-sync tolerance. Value: **+/-1 bit**. Dominant mode is one slip per frame.
- Field: Parity. Value: **unusable as a frame gate on a bitstream decoder at this baud**; every frame failed when tried.
- Field: Polarity. Value: standard inverted SBUS. A non-inverted decode never produced a frame.
- Field: Channel raw domain. Value: **0-2047**; this handset's usable window depends on its EPA setting.
- Field: Measured endpoints, factory stroke. Value: CH1 ~255 / ~1472 / ~1919; CH2 ~97 / rest ~1889-2028. **CH2's rest is above 1811 and normalises to full throttle against 172 / 992 / 1811.**
- Field: Measured endpoints, after EPA reduction. Value: **192 -> 992 -> 1792**, which a 172 / 992 / 1811 calibration maps to **+/-0.977**.
- Field: CH2 rest position. Value: **an endpoint, not the centre.** It is a trigger.
- Field: CH1 display-to-raw. Value: `SBUS_raw = -2.176 x (display_us - 1500) + 1472` -- **inverted** against the handset's own screen.
- Field: CH3-CH6 idle polarity. Value: **idle high, press low.**
- Field: Failsafe flag. Value: flags **bit 3**; channels carry the stored failsafe positions.
- Field: Lost-frame flag. Value: flags **bit 2**. The receiver emits a plausible held value alongside it.
- Field: Telemetry to the controller. Value: **none.** Receiver voltage and signal strength reach the handset's screen only.
- Field: Handset settings that change the wire. Value: **REV, EPA, SUB TR, MIXES, Button Type, CCS**, plus the two trim rockers.
- Field: Frame period. Status: **UNKNOWN** -- documented as 7-14 ms, never measured. With the link healthy, the age of the newest decoded frame stayed under 20-30 ms. Settled by capturing the receiver's output and timing frame starts.
- Field: Simultaneous receivers per handset. Status: **UNKNOWN** -- the community says one at a time, a dual-SBUS bench run says two. Open Item 1.
- Field: Indoor range among people. Status: **UNKNOWN** -- three vendor figures exist and none describe a crowded hall.

## 11. Open Items

1. **Can one DS-650 hold two SBUS-A receivers linked at once?** Section 5.3. The
   community's first-hand answer for the PWM receivers is *"one at a time, the
   most recently bound"*; a bench run showed two SBUS-A receivers linked
   simultaneously reporting identical channel values, which is only possible if
   both are receiving. Settled on a bench: bind two SBUS-A receivers to one
   handset, power both, and watch both decoded streams for an hour --
   specifically whether the earlier-bound one ever drops.
2. **What is the actual frame period?** Section 10. Never measured. Settled by
   a logic-analyser capture of the SBUS-A output, or by timestamping accepted
   frames in a decoder over a minute.
3. **Which bind procedure does a DS-650 + SBUS-A pair actually use?** Section
   4.2. The receiver's manual documents a Channel-3-at-power-on sequence written
   for the CT/HT handsets; the DS-650 has a `Bind Set` menu; and on a DS-600,
   Channel 3 at power-on toggles mixing instead. Settled by binding the pair on
   a bench and writing down what worked -- fifteen minutes, and it removes a
   documented collision from a builder's first hour.
4. **Does the DS-650 emit the same ~115 kbaud on every unit, or was one handset
   measured?** Section 6.1. A per-frame period estimate tracks 100 kbaud equally
   well, but any clamp on that estimate encodes an assumption about how far the
   deviation can go. `UNKNOWN` whether a second DS-650 measures the same; only
   one has been measured.
5. **What is the DS-650's EPA granularity?** Section 4.2. The 20-step / 5-step
   figures are the DS-600's button-era numbers and the DS-650 adjusts from a
   menu. Settled by reading the handset's own screen while adjusting.
6. **Does the `Gyro` menu entry do anything with an SBUS-A?** It is documented for
   the `-AT` receivers. `UNKNOWN` whether enabling it on a handset paired to a
   non-gyro receiver changes the transmitted channels. Settled by toggling it and
   watching the decoded channels.

## 12. Sources

**Bench measurement**

- April 2026: one DS-650, two SBUS-A receivers, SBUS output captured by an
  ESP32 RMT peripheral at 1 us per tick -- the endpoint sweep, the 115 kbaud
  bit period, the `0x04` footer, the display-to-raw formula, the per-byte
  re-sync, the button polarity, the held value with `lost_frame`, the
  recalibrated 192 / 992 / 1792 profile, and two receivers linked to one
  handset at once

**Vendor documentation**

- HotRC DS-650 specification and manual: https://manuals.plus/ae/1005009356275137
- HotRC DS-650 alternate manual: https://manuals.plus/ae/1005009793615853
- HotRC receiver manual, thirteen models including the SBUS-A:
  https://manuals.plus/ae/1005006924396938
- HotRC F-06A / F-06AT receiver manual: https://manuals.plus/ae/1005007076664946
- **HotRC DS-600 manual (PDF, read in full)**:
  https://www.printed-droid.com/wp-content/uploads/2023/04/HotRC-DS-600-Manual.pdf
  -- the previous generation's procedures and panel layout
- HotRC official site: https://en.hotrc.cn/
- SBUS-A product page: https://begreatrc.com/products/hotrc-sbus-a-24ghz-receiver
- DS-650 product page:
  https://www.begreatrc.com/products/hotrc-ds650-24ghz-6ch-fhss-built-in-lipo-battery-lcd-screen-radio-controller-with-f-06a-receiver

**Builder documentation (non-normative)**

- **bithead942, "Comparing HotRC DS Controllers", January 2026**:
  https://bithead942.wordpress.com/2026/01/02/comparing-hotrc-ds-controllers/
  -- the version lineage, the receiver compatibility matrix, the DS-650's
  advanced functions, the stated limitations, and the pairing FAQ
- bithead942, "Modifying the HotRC DS-600":
  https://bithead942.wordpress.com/2023/01/21/modifying-the-hotrc-ds-600/
- Printed Droid knowledge base:
  https://www.printed-droid.com/kb/hotrc-ds-600/

**Ecosystem survey (non-normative)**

- r2d2-astromech-simulator, RC module `src/js/input/rc.js`
- ShadowMD
- Padawan360 (Maestro and DY-SV5W variant)
- CHIRP

**Protocol references, used only where this sheet touches framing**

- [`sbus-protocol.md`](sbus-protocol.md) -- the frame layout and bit packing
- [`rmt-esp32-idf5.md`](rmt-esp32-idf5.md) -- the ESP32 RMT peripheral
- [`rc-receiver-spec.md`](rc-receiver-spec.md) -- the PWM/PPM path for the
  bundled F-06A
- [`elrs-crsf-radio.md`](elrs-crsf-radio.md) -- CRSF, and its contrasting
  failsafe model
- `bolderflight/sbus`: https://github.com/bolderflight/sbus -- the footer rule
  in Section 6.2
