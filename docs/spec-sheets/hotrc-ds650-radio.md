# HotRC DS-650 Spec Sheet (one-handed SBUS radio controller)

Working spec for the **HotRC DS-650** as the `supported` Radio Controller member
([#389](https://github.com/mattiasbrandt/protoArtoo/issues/389)), reached over
the **SBUS** Component Protocol, registered at `include/component_registry.inc:115`
as part id **3**.

Research date 2026-09-12. Every number in this document was read this session
from one of four places: the vendor's own specification pages, the HotRC manual
PDFs, this repository's source and bench records, or the astromech projects on
disk. Claims that could not be sourced are marked `UNKNOWN` with the artefact or
bench test that would settle them.

This sheet owns the **product**. SBUS framing itself is
[`sbus-protocol.md`](sbus-protocol.md) and the RMT peripheral is
[`rmt-esp32-idf5.md`](rmt-esp32-idf5.md); the resolution order
`CONTRIBUTING.md:305` sets -- RMT driver-level, then SBUS protocol-level, then
HotRC profile-level -- is unchanged, and this sheet is the third of those.

> [!CAUTION]
> **This handset does not put standard SBUS on the wire, and every one of the
> four deviations broke our decoder in production.** They are all documented
> here because each one cost a bench session to find:
>
> 1. The SBUS-A receiver clocks its frames at roughly **115 kbaud, not 100
>    kbaud** -- 8.68 us per bit rather than 10.00 (Section 7.1).
> 2. Its footer byte is **`0x04`**, not `0x00`. A decoder that requires `0x00`
>    rejects **100 %** of frames (Section 7.2).
> 3. Out of the box the handset's endpoints are **nowhere near** Futaba's
>    172 / 992 / 1811. The throttle's *resting* value measured **1889-2028**,
>    above our configured maximum, which normalised to +1.0 and commanded
>    **full throttle on every frame** (Section 7.3).
> 4. Its CH1 travel is **inverted** against the microsecond value its own screen
>    shows (Section 7.4).
>
> All four are handset- or receiver-side facts. Three of them are correctable by
> configuring the handset; the baud is not.

> [!WARNING]
> **The throttle rests at an end of travel, not in the middle.** CH2 on a
> one-handed HotRC is a trigger, not a centring gimbal: released is one extreme
> of the SBUS range and full-forward is the other. The astromech simulator on
> this machine names this hazard outright -- *"THE THROTTLE TRAP ... Wire that
> straight to the feet and the droid drives away at full reverse the moment you
> stop touching it"*
> (`~/Documents/GitHub/r2d2-astromech-simulator/src/js/input/rc.js:292-299`).
> We hit exactly that, for exactly that reason, on 2026-04-15. Section 7.3.

> [!NOTE]
> **The good news is that the fix is permanent and lives in the handset.** Once
> the DS-650's EPA/stroke was reduced to put the sticks inside a Futaba-shaped
> range, protoArtoo's generic `172 / 992 / 1811` binding worked unchanged, and
> `tasks/phase5-tasks.md:2790` records the result: *"**Final HOTRC binding
> profile:** `min=172, center=992, max=1811` -- all channels."* There is no
> HotRC-specific calibration table in firmware, and there should not be one.

## Where this sits in the lineup

| Category | Product | Role | Status |
| --- | --- | --- | --- |
| **Radio Controller** | HotRC DS-650 | the radio the droid is driven with | `supported` |
| **Radio Controller** | RC Transmitter - SBUS | any other SBUS handset, same driver | `supported` |
| **Radio Controller** | RC Transmitter - PWM | six PWM channels, one per pin | `supported` |
| **Radio Controller** | RC Transmitter - ELRS | CRSF, a different driver | `roadmap` |
| **Radio Controller** | Xbox Controller | Bluetooth HID, a different driver | `roadmap` |

The DS-650 is the **named product** in a category that also carries two generic
rows. That is deliberate and it is not redundancy: `component_registry.inc:110-113`
records that Standard PWM, single SBUS and dual SBUS all ship today and that
*"single-or-dual is configuration rather than a second product"*. The DS-650 row
exists because it is the handset every bench result in this repository was taken
against -- `README.md:75` says *"tested: HOTRC 650"* -- and because a builder
picking parts wants to know which radio is the one that is actually proven, not
merely which protocol is supported.

Under `CONTEXT.md`'s test -- *"whether it changes the driver"* -- the DS-650 is
**not** a protocol question. It speaks SBUS, the driver we already have. It earns
a product row and this sheet because its *profile* within SBUS is unusual enough
to have cost four bench sessions, and because that profile is worth writing down
once rather than rediscovering.

## 0. Authority Contract

This document is an implementation authority for the HotRC DS-650 handset, for
the HotRC receivers that bind to it, and for the SBUS profile those receivers
actually produce.

Authority order for agent decisions:

1. **This repository's bench records** for anything about what arrives on the
   wire -- `tasks/phase5-tasks.md` T19, `tasks/lessons.md:862-867`, and the
   tests in `test/test_native/test_sbus_decode/`. These were measured against
   the hardware in hand and they outrank every vendor claim about SBUS output,
   because the vendor publishes none.
2. **Vendor specification pages and HotRC manuals** for the product itself --
   dimensions, battery, menu structure, RF, receiver electricals.
3. This document.
4. **Community documentation** (Section 10), which is evidence of practice and
   the source of the version-lineage table in Section 2.

If references conflict:

- Prefer the bench record over any vendor or community claim about **SBUS
  timing, framing, endpoints or failsafe signalling**.
- Prefer the vendor page over community writing for **product specifications**,
  and where two vendor sources disagree, record both rather than choosing
  (Section 5 does this three times).
- If still unresolved, mark `UNKNOWN` and stop dependent work.

This document **supersedes** [`hotrc-sbus-spec.md`](hotrc-sbus-spec.md) for
every claim the two both make. That sheet predates the T19 bench work and states
100 kbaud, a `0x00` footer and a centring CH2, all three of which the hardware
contradicted.

Agent requirements when using this document:

- MUST NOT assume 100 kbaud. The bit period is **estimated per frame** and the
  measured value is 9 ticks at 1 MHz, not 10 (Section 7.1).
- MUST NOT require footer `0x00`. Accept `0x00` and any byte whose low nibble is
  `0x04` (Section 7.2).
- MUST NOT treat CH2 as a centring axis. Its rest position is an **endpoint**
  (Section 7.3).
- MUST NOT treat `lost_frame` as a harmless counter. The receiver emits a
  **plausible, held channel value** alongside it (Section 8.2).
- MUST NOT hardcode a HotRC endpoint tuple into the decoder or the mapper. The
  handset's REV, EPA, sub-trim and mix settings all change what is transmitted,
  so calibration is per handset configuration, not per product (Section 7.3).
- MUST NOT claim the sixteen SBUS channels are available. This handset has
  **six**, and CH7-CH16 carry nothing (Section 9).

## 1. Scope

Covers: the DS-650 handset as a physical product, its menu and the settings that
change what reaches the wire, its battery and power behaviour, the receiver
family it binds, the SBUS profile measured from the SBUS-A receiver in this
project, the failsafe model end to end, the control grammar six channels can
address in protoArtoo, and what the hobby does with these handsets.

Does not cover: SBUS frame layout and bit packing (that is
[`sbus-protocol.md`](sbus-protocol.md)), the RMT peripheral and its per-chip
budget (that is [`rmt-esp32-idf5.md`](rmt-esp32-idf5.md) and
`include/sbus_rmt_budget.h`), PWM/PPM receiver wiring (that is
[`rc-receiver-spec.md`](rc-receiver-spec.md)), the HotRC over-the-air modulation
below the point where it reaches the receiver's output pin, or the DS-800 beyond
the lineage table.

## 2. Products Covered

### 2.1 The handset, and its siblings

HotRC ships functional changes without incrementing the model number, so the
version column below is the community's, not the vendor's. The source is
bithead942's side-by-side of a personally owned collection of every version
(Section 10), which is the only place this lineage is written down.

| | DS-600 v1 | DS-600 v2 | DS-600 v3 | **DS-650** | DS-800 |
| --- | --- | --- | --- | --- | --- |
| Released | late 2021 | late 2021 | early 2023 | **mid-2025** | late 2025 |
| Channels | 4 | 6 | 6 | **6** | 8 |
| Antenna | external | external | internal | **internal** | internal |
| Screen | 3 LEDs | 3 LEDs | mono LCD 21.0x9.3 mm | **colour LCD 22.5x12.0 mm** | colour touch 40x30 mm |
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

**Why the DS-650 and not the DS-800.** The DS-800 adds two channels, a timer, a
trainer port and eight saved model profiles, and loses the palm-sized shape that
is the whole point for a droid operator who wants the controller invisible. The
community source recommends the DS-650 for exactly that reason, and this project
has no bench evidence for the DS-800 at all.

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
> **The SBUS-A is not the receiver the DS-650 ships with, and it is the one we
> use.** A DS-650 bundle contains an F-06A, which emits six PWM channels.
> protoArtoo's proven path is **DS-650 plus separately-purchased SBUS-A**, which
> is what every bench record in `tasks/phase5-tasks.md` was taken against. The
> SBUS-A's own manual lists its compatible transmitters as *"HOTRC CT-4A, CT-6A,
> CT-8A, HT-8A, and DS600 transmitters"* -- the DS-650 is **not named**, because
> the receiver predates it. It binds anyway; `tasks/phase5-verification-closeout.md:583`
> records `full-hardware-verified` for the pair. Treat the vendor's list as
> incomplete rather than as a constraint, but buy the receiver knowing the vendor
> does not promise the combination.

Two size figures for the F-06A exist and they do not agree: the receiver manual
says 38 x 24 x 13 mm, the community measurement says 19.2 x 33.0 x 13.5 mm with
a 90 mm antenna. Both are recorded; neither matters to protoArtoo, which does not
use this receiver.

## 3. Project Integration

- **[`src/drivers/sbus_decoder.cpp`](../../src/drivers/sbus_decoder.cpp)** -- the
  RMT decoder. Lines 365-371 name the DS-650 explicitly as the reason only one
  decode attempt per frame is made. `_parseSymbols()` at 353 is where the
  adaptive bit period is applied.
- **[`include/sbus_decode_helpers.h`](../../include/sbus_decode_helpers.h)** --
  the pure helpers, shared with native tests. `sbusEstimateBitPeriod()` at
  175-188 is the 115 kbaud fix; `isValidSbusFooter()` at 41-44 is the `0x04`
  fix; `kSbusDecodeMaxStartBitSlip = 1` at 22 carries the comment *"HOTRC
  dominant mode is single-bit slip per frame."*
- **[`include/sbus_decoder.h`](../../include/sbus_decoder.h)** -- names the
  SBUS-A as the hardware at line 11, and publishes `SbusData{ch[16], ch17, ch18,
  lost_frame, failsafe}`.
- **[`src/tasks/rc_input.cpp`](../../src/tasks/rc_input.cpp)** -- the RC task,
  Core 1, priority 5, 5 ms poll. Lines 16-18 carry the two safety layers this
  sheet's Section 8 is mostly about.
- **[`include/rc_binding_types.h`](../../include/rc_binding_types.h)** --
  `RC_SBUS_DEFAULT_MIN/CENTER/MAX` = 172 / 992 / 1811 at 16-18, and
  `rcTriggerDefaultReverse()` at 61-68, **the only DS-650-specific behaviour in
  firmware** (Section 7.5).
- **[`include/config.h`](../../include/config.h)** -- `SBUS_MIN` / `SBUS_MAX` /
  `SBUS_TIMEOUT_MS` at 503-505, and the SBUS receiver pins per Board Variant
  (187-188 and 311-312).
- **[`include/failsafe_gate.h`](../../include/failsafe_gate.h)** -- two of the
  five failsafe layers, `SBUS_HW` and `SBUS_WATCHDOG`, belong to this product.
- **[`src/config_store.cpp`](../../src/config_store.cpp)** -- the shipped
  defaults at 281-308, which assume a dual-SBUS DS-650 (Section 9.2).
- **[`tasks/rc_diagnostics_contract.md`](../../tasks/rc_diagnostics_contract.md)**
  -- the `raw` / `normalized` / `mapped` contract the RC page renders.
- **[`docs/pin_map.md`](../pin_map.md)** lines 166-172 -- the three wiring modes.
- **ADR 0029** (Board Capability Gates), **ADR 0042** (Component Families),
  **ADR 0065** (line drawings; this part is one of the sixteen).

## 4. Sources Checked

| Source | URL or path | Extraction notes |
| --- | --- | --- |
| **protoArtoo T19 bench record** | `tasks/phase5-tasks.md:2240-2820` | **The primary source for everything on the wire.** The 115 kbaud measurement, the `0x04` footer, the pre-calibration endpoint sweep, the display-to-raw formula, the held-value-with-`lost_frame` behaviour, and the final accepted profile |
| protoArtoo lessons | `tasks/lessons.md:862-867` | The gap-tick inflation that pushed the period estimate from 9 to 10 and cost ~98 % of frames |
| protoArtoo decode tests | `test/test_native/test_sbus_decode/test_sbus_decode.cpp:409-433` | The three adaptive-period cases, including `// 115 kbaud (HOTRC DS-650 measured)` |
| protoArtoo verification closeout | `tasks/phase5-verification-closeout.md:356,571,583` | The `full-hardware-verified` classifications for the DS-650 + SBUS-A pair |
| **DS-650 vendor specification** | https://manuals.plus/ae/1005009356275137 | The specification table, the full menu list, the bind procedure, the bundled F-06A |
| **DS-600 manual PDF** | https://www.printed-droid.com/wp-content/uploads/2023/04/HotRC-DS-600-Manual.pdf | Read in full this session. The button-combination era procedures -- mixing, reverse, stroke, failsafe -- and the panel layout. **This is the older sibling's manual, not the DS-650's** (Section 5.2) |
| **HotRC receiver manual** | https://manuals.plus/ae/1005006924396938 | The thirteen-model receiver table, the SBUS-A row, and the compatible-transmitter list that omits the DS-650 |
| SBUS-A product page | https://begreatrc.com/products/hotrc-sbus-a-24ghz-receiver | 16 channels, 4-9 V, 0-27 V return, 28 x 14 x 1 mm, 300-800 m. **No compatibility list at all** |
| **bithead942, "Comparing HotRC DS Controllers"** | https://bithead942.wordpress.com/2026/01/02/comparing-hotrc-ds-controllers/ | **The lineage table, and the only systematic comparison that exists.** Written by a droid builder from a personally owned set of every version. Also the 1:1 pairing claim and the "no ramp control" limitation |
| bithead942, "Modifying the HotRC DS-600" | https://bithead942.wordpress.com/2023/01/21/modifying-the-hotrc-ds-600/ | The battery-capacity modification, the wrist-strap and charge-LED complaints |
| Printed Droid knowledge base | https://www.printed-droid.com/kb/hotrc-ds-600/ | The droid-community framing: *"a very small remote control for mini droids"* that *"can be held in one hand and offers a reliable connection."* Reproduces the DS-600 manual; **no SBUS content** |
| astromech simulator RC module | `~/Documents/GitHub/r2d2-astromech-simulator/src/js/input/rc.js` | The throttle trap, and a split-about-centre normalisation identical in shape to ours |
| ShadowMD, Padawan360 | `~/Documents/GitHub/ShadowMD`, `~/Documents/GitHub/Padawan360_mega_maestro_DYSV5W` | Read for the control grammar comparison in Section 10. **Neither reads an RC receiver at all** |

**What did not survive checking.** A web research pass reported that the DS-650
*"only works with the F-06A receiver"* and that the SBUS-A is *"NOT standard with
DS-650 bundles"*. The second half is right and useful. The first half is
**contradicted by this project's own hardware**: a DS-650 drives two SBUS-A
receivers on this bench and the result is recorded as `full-hardware-verified`.
The same pass reported an EPA granularity of *"20 gear positions for steering, 5
for throttle"* attributing it to a DS-650 page; that figure is real but it comes
from the **DS-600** manual, which is a different generation with a different
adjustment interface. It is reported in Section 5.2 with the right attribution.

A second claim is recorded but **not relied on**: that a HotRC handset *"can bind
to more than one receiver ... but it will only work with one at a time ... the
most recently bound."* That is a first-hand community observation about the
PWM receivers, and it is flatly inconsistent with this project's dual-SBUS bench
run, where both receivers were linked simultaneously and reported identical
channel values. Open Item 1.

## 5. The handset

### 5.1 What is actually in your hand

A palm-sized, thumb-operated block, held in one hand, with no second gimbal and
no switch bank. Everything an operator can do is one of six things:

| Control | Channel | Behaviour |
| --- | --- | --- |
| Joystick, left/right | **CH1** | proportional, self-centring |
| Joystick, up/down | **CH2** | proportional, **does not self-centre to mid-range** (Section 7.3) |
| Button 3 | **CH3** | programmable: momentary, latching, three-position or jog |
| Button 4 | **CH4** | same |
| Button 5 | **CH5** | same |
| Button 6 | **CH6** | same |
| CH1 trim rocker | -- | shifts CH1's transmitted centre |
| CH2 trim rocker | -- | shifts CH2's transmitted centre |
| Power switch | -- | see 5.3 |
| Colour LCD, 0.96 in | -- | battery, signal, receiver voltage, channel states, menu |

That is the whole instrument. **Six channels, two of them proportional.** Every
design decision in Section 9 follows from that number.

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
| Colour screen | 0.96 inch |
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

### 5.2 The menu, and the settings that change the wire

The DS-650's colour screen carries a real menu. Its entries are:

`REV`, `EPA`, `SUB TR`, `Button Type`, `MIXES`, `CCS`, `RX Info`, `Fail Safe`,
`Servos`, `Gyro`, `Joystick`, `Reset`, and `System` (Language, Sound, Auto-off,
Lock).

Six of those change what an SBUS decoder sees, and an agent debugging a
"decoder bug" should rule them out first:

| Menu entry | What it does to the wire |
| --- | --- |
| **`REV`** | inverts a channel's direction. The value the droid reads runs the other way; nothing about framing changes |
| **`EPA`** | end-point adjustment, the stroke limits. **This is the setting that moved our endpoints from 255/1919 to 192/1792** (Section 7.3) |
| **`SUB TR`** | sub-trim, shifts the transmitted centre independently of the trim rockers |
| **`MIXES`** | tank/differential mixing. **With mixing on, CH1 and CH2 stop being steer and throttle** and become left and right motor commands. A droid whose steering suddenly does nothing sensible should check this before touching code |
| **`Button Type`** | the DS-650's own advance: each of CH3-CH6 can be *2 gears* (on/off), *3 gears* (three positions) or *jog* (momentary). This decides whether a button reads as a level or an edge (Section 9.3) |
| **`CCS`** | constant/cruise speed. A latched throttle. Section 5.3 |

`Fail Safe` is Section 8.3. `Gyro` applies only to the `-AT` receiver variants
and does nothing with an SBUS-A. `Joystick` is a centre-calibration routine, and
`Reset` is a factory reset -- the DS-650 is the first model in the line to have
one.

> [!CAUTION]
> **The DS-600 manual's procedures are not the DS-650's, and they collide.** The
> widely circulated HotRC PDF (Printed Droid hosts the copy this project has
> always cited) documents the *button-combination* generation, where settings
> were reached by holding a channel button while powering on:
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
> menu-driven EPA granularity is; nothing read this session states it.

### 5.3 Power, cruise, and two operational traps

**Battery.** 1S lithium, 1500 mAh, charged over USB-C. The community source
reports the 1200 mAh predecessor running out inside a field day and documents
replacing it; the DS-650's extra 300 mAh and its longer handle make that
modification easier rather than unnecessary. Plan for an event, not a bench
session.

> [!WARNING]
> **`CCS` -- constant speed -- is a latched throttle on a machine that drives
> among people.** It is a first-class feature of this handset with its own
> dedicated key, and on a boat it is exactly right. On a droid it means the
> operator's thumb leaving the stick no longer stops the droid. Nothing in
> protoArtoo can detect it: a cruising DS-650 transmits a steady non-neutral CH2
> that is indistinguishable from a held stick. **Decide deliberately whether the
> droid's handset has this enabled**, and note that the estop and the drive
> speed cap remain the only things that bound it.

> [!NOTE]
> **The DS-650 refuses to power off while its receiver is still powered.** The
> community source records this as *"both nice and annoying"*. For a droid it is
> mostly nice -- it makes "operator switched the radio off while the droid was
> live" harder to do by accident -- but an operator who expects the handset to
> die on command will find it does not, and the droid must be powered down first.

## 6. Receivers and binding

### 6.1 SBUS-A, the one protoArtoo runs

Sixteen SBUS channels out of a 28 x 14 x 1 mm, **1.7 g** sliver -- it is a
flexible strip rather than a cased receiver, and it is the smallest part in the
whole drive path. DC 4-9 V, 35 mA. A separate return-voltage input reports
0-27 V back to the handset's screen.

Only **six** of its sixteen channels carry anything from a DS-650. CH7-CH16 sit
at whatever the handset transmits for an unused channel and must not be bound.

Wiring is the usual three-wire servo convention -- signal, VCC, ground -- with
signal white or orange, VCC red, ground black or brown. Both protoArtoo boards
take the signal directly on a GPIO:

| Board Variant | SBUS1 (drive) | SBUS2 (dome) |
| --- | --- | --- |
| artoo-esp32 | GPIO 15 | GPIO 13 |
| firebeetle2 | GPIO 28 | GPIO 29 |

> [!IMPORTANT]
> **On artoo-esp32, SBUS1 shares GPIO 15 with a strapping pin, and that blocks
> USB serial while the ESP32 is seated** (`AGENTS.md:335`, `docs/console.md:101`).
> This is not an SBUS problem and not a receiver problem, but it is the first
> thing a builder meets and it belongs on this sheet: with the module in its
> socket, use the network Console Adapter.

**No hardware UART is consumed.** protoArtoo decodes SBUS on the RMT peripheral
(`include/sbus_decoder.h:5-9`), which is why the dome link and the drive
transport keep their UARTs. That decision is what makes dual SBUS affordable at
all, and it is also why the baud deviation in Section 7.1 mattered so much: an
ESP32 UART would have re-synchronised on every byte's start bit for free.

### 6.2 F-06A and F-06AT, the bundled PWM receivers

Six PWM channels, 1000-2000 us, frame period documented only as *"reaction speed
(PWM) <= 20 ms"*. DC 4-9 V, 35 mA (39 mA for the gyro variant). These are what a
DS-650 bundle contains, and protoArtoo supports them through its
`standard_pwm` mode on CH1-CH6 -- six GPIOs, one per channel, no SBUS at all.

The `-AT` variants carry a gyro whose sensitivity is set from the handset's CH1
trim and whose on/off is CH3. On a droid this is a heading-hold intended for
surface vehicles; nothing in protoArtoo interacts with it, and enabling it means
the receiver is modifying the steering channel before the droid ever sees it.
Leave it off.

`rc-receiver-spec.md` is the sheet for this path, including the HotRC "analog
mode" 20 ms / 50 % duty neutral versus "digital mode" 3 ms / 333 Hz distinction
and the 3.9k/6.8k divider for 5 V receiver outputs into an ESP32.

### 6.3 Binding

On a DS-650: power the receiver, hold its **BIND** button, turn the handset on,
then `Bind Set` -> `Start`. The receiver's LED goes from rapid flashing --
powered, no signal, unpaired -- to steady, which is the pairing confirmation.

On the older button-combination handsets, and on the CT/HT series the SBUS-A's
manual was written for, binding used a **bind plug** ("the Coder") inserted into
the receiver's `B` port, or holding Channel 3 at power-on. Section 5.2's caution
applies: do not carry those steps across.

Pairing is **one handset to one receiver at a time** according to the community
source, with the most recently bound receiver winning and an earlier one
resuming if the newer is powered down. That is a first-hand observation of the
PWM receivers. protoArtoo's dual-SBUS bench contradicts it for the SBUS-A --
Open Item 1 -- and until that is settled, **a builder planning dual SBUS should
verify both receivers link simultaneously before designing around it.**

## 7. What actually arrives on the wire

Everything in this section was measured against the hardware in hand during T19
(2026-04-15 to 2026-04-18) and is recorded in `tasks/phase5-tasks.md`. Where a
vendor claim exists it is named; in every case where the two differ, the
measurement is what the firmware implements.

### 7.1 The baud is not 100 kbaud

SBUS is specified as 100000 baud, 8E2, inverted. The HotRC SBUS-A transmits at
**approximately 115 kbaud** -- a standard UART rate, and 15 % fast.

At the decoder's 1 MHz RMT resolution that is **8.68 ticks per bit** rather than
10.00. The consequence is not a small timing error; it is a decode failure mode
that took two bench sessions to characterise:

> With period=10 and actual 8.68 ticks/bit, any run of >= 4 physical bits is
> under-counted by 1 bit (`floor(8.68N/10) < N` for `N >= 4`). This caused
> massive bit slip throughout every frame, producing `bc < 300` and **near-total
> extract failures (98.4 % on bench)**.
> -- commit `a26aaae2`

The fix is an adaptive estimate rather than a constant. `sbusEstimateBitPeriod()`
sums the durations of every captured symbol **except the last**, divides by the
300 bits a 25-byte 8E2 frame contains, and clamps the result to `[8, 10]`:

```c
uint32_t period = (totalTicks + 150U) / 300U;   // round-nearest
if (period < kAdaptiveMin)       period = kAdaptiveMin;   // 8
if (period > kBitPeriodTicksStd) period = kBitPeriodTicksStd;   // 10
```

- a true 100 kbaud frame gives `totalTicks ~ 3000` -> period **10**
- a HotRC frame gives `totalTicks ~ 2604` -> period **9**

**The last-symbol exclusion is not an optimisation, it is the whole fix.** RMT's
final captured symbol carries the roughly 300-tick inter-frame gap in its
`duration0`. Including it inflated the sum by exactly enough to round 9 up to
10, which is the 98 % failure above. `tasks/lessons.md:862-867` records it as a
standing lesson, and three native tests pin it
(`test_sbus_decode.cpp:409-433`).

Period 9 against a true 8.68 still drifts, and the second half of the fix is
**per-byte start-bit re-synchronisation** -- what a hardware UART does for free
and an RMT bitstream decoder must be told to do. `kSbusDecodeMaxStartBitSlip = 1`
bounds the search, and the comment beside it states the profile plainly:
*"HOTRC dominant mode is single-bit slip per frame."*

Before that re-sync existed, the decoder **never once decoded a frame where
CH1 > 1472**, across a full physical sweep of the stick, because that byte
pattern produced a single uncompensated slip that pushed the footer off position
(`tasks/phase5-tasks.md:2401-2410`). A blind spot covering half the steering
travel, produced entirely by a 15 % baud deviation.

### 7.2 The footer is not 0x00

Standard SBUS ends `0x00`. The HotRC SBUS-A sends **`0x04`**, the SBUS2-style
footer, or a variant whose low nibble is `0x04`. protoArtoo's original
`kSbusFooter == 0x00` check therefore rejected **100 % of frames**
(`tasks/phase5-tasks.md:1836`).

The accepted rule, matching `bolderflight/sbus` v8.x:

```c
inline bool isValidSbusFooter(uint8_t footer) {
    return footer == 0x00 || (footer & 0x0FU) == 0x04U;
}
```

Tests pin `0x00`, `0x04` and `0x14` as accepted and `0x03` as rejected.

### 7.3 The endpoints are not Futaba's until you make them so

**As shipped**, with factory stroke settings, the sweep measured:

| Channel | Role | Observed min | Observed max | At rest |
| --- | --- | --- | --- | --- |
| CH1 | steer (joystick horizontal) | ~255 | ~1919 | **~1472** |
| CH2 | throttle (joystick vertical) | ~97 | ~2028 | **~1889-2028** |

Two things are wrong with that against a `172 / 992 / 1811` binding, and the
second is dangerous:

1. CH1's rest value of 1472 is not 992. Normalised against a 992 centre it reads
   **+59 % steering, permanently**. On the dome channel that is a dome spinning
   at 59 % with the stick untouched.
2. **CH2's rest value is above the configured maximum.** 1889 clamps to 1811,
   which normalises to **+1.0**: full throttle, on every frame, with the trigger
   released. This is the throttle trap, and it is not a theoretical hazard --
   `tasks/phase5-tasks.md:2257` records it as *"a secondary bug that must be
   corrected."*

**The fix is in the handset, not the firmware.** Reducing the EPA/stroke brought
the transmitted range inside a Futaba-shaped window, and the 2026-04-18 bench
run confirms the result:

- neutral sticks: CH1 = 992, CH2 = 992
- full sweep: **192 -> 992 -> 1792** on both axes
- mapped endpoints **+/- 0.977** against the 172/1811 binding, which is exactly
  `800/819` -- the RC output range sitting just inside the binding's

`tasks/phase5-tasks.md:2743` states it for the record: *"Transmitter was
recalibrated to standard Futaba SBUS range prior to this session. Previous
HOTRC-specific values (neutral=1472/1889) are now outdated."*

> [!IMPORTANT]
> **Do not put a HotRC endpoint table in firmware.** The measured values above
> are a *handset configuration*, not a product characteristic: REV, EPA, sub-trim
> and the trim rockers all move them, and a second DS-650 set up by a different
> builder will read differently. The firmware's job is the generic
> `min:center:max:deadband:reverse` binding it already has, and the builder's job
> is to make the handset fit it -- or to calibrate the binding to the handset.
> Either is correct; hardcoding one builder's numbers is not.

The calibration arithmetic, for reference
(`include/rc_binding_types.h:209-252`): raw is clamped to `[min, max]`, offset
against `center`, rejected to 0.0 inside `deadband`, then scaled **asymmetrically**
-- above centre by `max - center`, below by `center - min` -- clamped to
`[-1, +1]`, and finally negated if `reverse`. A non-central `center` therefore
gives different gain on each side, which is the correct behaviour for a stick
whose travel is not symmetric. **There is no expo and no rate curve anywhere in
firmware**, and the handset has none either -- the community source lists *"No
Ramp control"* among the DS series' limitations, and separately describes the
throttle as going *"zero to 100 very suddenly"*. If a droid needs a softer
throttle, nothing in this chain will provide it.

### 7.4 CH1's screen value is inverted against SBUS raw

Measured on the dome receiver, with the handset's own display in microseconds:

```
SBUS_raw = -2.176 x (display_us - 1500) + 1472
```

| Stick | Handset display | SBUS raw |
| --- | --- | --- |
| neutral | 1500 us | 1472 |
| max right | ~2000 us | 384 |
| max left | ~1000 us | ~2047 |

Right-stick produces a *lower* raw value. An operator reading the handset's
screen and an agent reading `/api/rc` are watching the same axis run in opposite
directions, and both are correct. Note this was measured **before** the
recalibration in 7.3; the inversion is a property of the axis, the specific
constants are not.

There is **no SBUS-raw-to-microsecond conversion anywhere in protoArtoo's
firmware**. `rawUs` exists in the diagnostics contract as a reporting field only.
Any microsecond figure in a log or a document about this handset came from the
handset's screen, not from us.

### 7.5 The buttons idle high, and that is the only HotRC special case in code

CH3-CH6 on a DS-650 **idle high and press low** -- the opposite of the
convention a `172 -> 1811` binding assumes. Left alone, every button would read
as permanently pressed and every press as a release.

This is the one place the product name appears in shipped firmware
(`include/rc_binding_types.h:61-68`):

```c
// DS-650 button channels (SBUS CH3-CH6) idle high and press low.
// Default trigger polarity therefore needs reverse=true on those channels
// so idle decodes as "not pressed" and press decodes as "pressed".
inline bool rcTriggerDefaultReverse(RcBindingSource source, uint8_t channel) {
    return (source == RC_BINDING_SBUS1 || source == RC_BINDING_SBUS2) &&
           channel >= 3 && channel <= 6;
}
```

It applies to **SBUS channels 3-6 on either receiver**, and not to PWM. It is
pinned both ways by `test_rc_mapping.cpp:67-78`.

Two things follow. First, the default is a *default*: an operator who re-binds a
channel can override `reverse`, and should if their handset differs. Second, the
rule is keyed on channel number rather than on any product identity, so **a
non-HotRC SBUS handset whose CH3-CH6 idle low will read inverted out of the
box.** That is a deliberate trade -- the named product wins the default -- but it
is worth knowing before diagnosing it as a bug.

## 8. Failsafe

This is the section a droid among people is written for.

### 8.1 Three distinct things can go wrong and they signal differently

| What happened | What the receiver does | What protoArtoo sees |
| --- | --- | --- |
| A frame was corrupted or missed | keeps sending; sets **`lost_frame`** (flags bit 2) | counter increments, dome dispatch suppressed |
| The link is gone and the receiver has declared it | keeps sending; sets **`failsafe`** (flags bit 3), channels driven to the stored failsafe positions | `FailsafeLayer::SBUS_HW` triggered |
| The receiver stopped, unplugged, or lost power | nothing arrives | `FailsafeLayer::SBUS_WATCHDOG` after `SBUS_TIMEOUT_MS` |

SBUS's signalling advantage over CRSF is exactly this: **the frame keeps coming
and carries a flag**, so the first two cases are positive information rather than
silence. [`elrs-crsf-radio.md`](elrs-crsf-radio.md) Section 8 documents the
other side of that coin, where the frame simply stops and Layer 1 does not exist.

### 8.2 `lost_frame` is not failsafe, and the held value is convincing

The single most dangerous observed behaviour, recorded in commit `d9f4a50e`:

> Receiver outputs programmed hold position (`ch1=389`, `normalized=-89%`) with
> **`lost_frame=true`, `failsafe=false`** on any brief signal interruption.
> Without this guard that value reaches the dome task directly.

A brief RF interruption produces a **fully plausible channel value** -- 389 is
inside the legal range, decodes cleanly, and means 89 % dome rotation -- with
only `lost_frame` distinguishing it from a real command. The fix was to suppress
dispatch on `lost_frame` as well as on `failsafe`, and to **not** update the
freshness timestamp, so that a persistent condition also fires the watchdog
rather than being silently absorbed.

An agent adding any new SBUS consumer must do the same. Treating `lost_frame` as
a diagnostic counter is the bug.

### 8.3 What the handset's own failsafe setting does

The DS-650 carries a `Fail Safe` menu entry; the DS-600 generation set the same
thing by holding the throttle where you wanted it and inserting the bind plug
for three seconds. Either way the model is identical: **the receiver stores a
per-channel output value and drives the channels to it when the link is lost.**
The DS-600 manual states the default is the neutral point, and carries the
warning that matters:

> The safe return position of the throttle servo is recommended to be set to the
> brake or neutral point. **Do not set it to the throttle open position to avoid
> danger.**

For protoArtoo this setting is **secondary**, and it is worth being explicit
about why. The stored positions arrive alongside `failsafe=true`, and the
firmware discards the channel values entirely when that flag is set -- the drive
path is cut by `FailsafeLayer::SBUS_HW`, not by the receiver's chosen numbers.
Setting the handset's failsafe to neutral anyway is still correct: it is what
protects a droid whose SBUS-A is driving anything the ESP32 does not mediate,
and it costs nothing.

Verification is one action: **switch the handset off and confirm the droid
stops.** `tasks/phase5-tasks.md:2775` records that gate as passed on 2026-04-18
-- *"watchdog fires on unplug, drive stops, control restores cleanly on
re-plug."*

### 8.4 protoArtoo's two layers, and the boot latch

`src/tasks/rc_input.cpp:16-18` names them:

```
//   Layer 1: SBUS receiver hardware failsafe flag (data.failsafe)
//   Layer 2: SBUS software watchdog (SBUS_TIMEOUT_MS = 200 ms)
```

They are two of the five in `include/failsafe_gate.h` -- `SBUS_HW`,
`SBUS_WATCHDOG`, `WEB_TIMEOUT`, `WATCHDOG_RESET`, `ESTOP` -- and the two the
DS-650 owns.

- **200 ms** is the default (`include/config.h:505`), configurable 50-5000 ms
  through the API. Against the *documented* 7-14 ms SBUS cadence that is ten to
  thirty missed frames -- but this handset's real frame period was never
  measured here (Open Item 2), so treat 200 ms as a wall-clock safety number
  rather than as a frame count.
- The timeout check treats **"never received" as timed out**
  (`include/sbus_math.h:56-62`), so a droid that has never seen a frame is in
  failsafe, not in limbo.
- **Boot latches drive off.** `main.cpp:504-509` triggers `SBUS_WATCHDOG` at
  startup whenever a drive watchdog source exists, so drive is dead until the
  first valid frame clears it. `bootSbusSafeGuardDecision()` is the pure
  statement of that rule and `test_failsafe_boot_sbus.cpp` is its whole contract:
  *"From zeroed state, boot arming path leaves failsafe active before any RC
  frame is processed."*
- **`ESTOP` is latching** and only an explicit operator action clears it. A
  reconnecting radio never clears an estop.

One measured number closes this section. During T19 the timeout was temporarily
raised to 5000 ms as a diagnostic while the decoder was unreliable, and the
saved controller backup in `tasks/artoo-backup-20260903-203848-config.json` still
carries `sbusTimeoutMs: 5000`. **That is a diagnostic value, not a setting to
ship.** Any controller found with it should be returned to 200 ms.

For precision, the SBUS1 policy is a three-way branch
(`src/tasks/rc_input_step.cpp:167-184`) and **neither of the two bad branches
dispatches anything**:

```c
if (in.failsafe) {                    // Layer 1
    out.triggerSbusHw = true;
    out.submitDriveZeroFrame = true;  // an explicit zero, not just an absence
} else if (in.lostFrame) {
    out.incrementLostFrameCount = true;   // counted, NOT dispatched
} else {
    out.clearSbusHw = true;
    out.clearSbusWatchdog = true;
    out.dispatchBindings = true;      // only a clean frame commands anything
}
```

Only a clean frame clears the watchdog, so a run of `lost_frame` frames ends in
`SBUS_WATCHDOG` rather than in a droid quietly holding a stale command.

## 9. The control grammar: six channels against thirty-eight actions

### 9.1 The arithmetic

| | Count | Source |
| --- | --- | --- |
| Channels a DS-650 transmits | **6** | the product |
| ... of which proportional | **2** | CH1, CH2 |
| ... of which buttons | **4** | CH3-CH6 |
| Channels with two receivers (dual SBUS) | **12** | `docs/pin_map.md:166-172` |
| protoArtoo analog backbone bindings | **3** | drive speed, drive steer, dome speed |
| protoArtoo trigger slots | **11** | `RC_TRIGGER_MAX`, `include/rc_input_processor.h:20` |
| **Total binding slots** | **14** | 3 + 11 |
| RC-bindable action targets | **38** | `rc_token` entries in `docs/action-registry.yaml` |

**A single DS-650 can address six of thirty-eight actions, and two of those six
are consumed by driving.** That is the defining constraint of this handset, and
it is why dual SBUS exists in this project at all: a second receiver bound to
the same handset -- or a second handset -- doubles the channel count without
changing a line of the decoder.

The RC page reflects the real number rather than the protocol's: `data/rc.js:698`
renders **six** channels per SBUS source, not sixteen, even though the binding
parser accepts channels 1-18.

### 9.2 What the shipped defaults assume

`src/config_store.cpp:281-308` boots `RC_INPUT_DUAL_SBUS` with a map that is a
DS-650 map:

| Source | Channel | Bound to |
| --- | --- | --- |
| SBUS1 | CH1 | `drive_speed` |
| SBUS1 | CH2 | `drive_steer` |
| SBUS1 | CH4 | `arm1_toggle` (trigger slot, `reverse=1`) |
| SBUS1 | CH5 | `arm2_toggle` (trigger slot, `reverse=1`) |
| SBUS2 | CH1 | `dome_speed` |
| SBUS2 | CH2 | backbone arm1 |
| SBUS2 | CH3 | backbone arm2 |

All six per-channel enables default **false**, so a freshly flashed controller
boots inert and the operator opts each channel in. Every binding uses the
generic `172 / 992 / 1811` with **deadband 0**; the only product-specific value
anywhere is the `reverse` flag from Section 7.5.

> [!NOTE]
> `tasks/rc_diagnostics_contract.md` documents the ARM1/ARM2 boot defaults as
> deadband **50** and `reverse` **0**. The code uses deadband **0** and
> `reverse` **1**. The contract is stale on both fields, and it still names
> `src/tasks/sbus_input.cpp`, a file since renamed to `rc_input.cpp`. Worth a
> correction pass on that document; it does not change behaviour.

### 9.3 Buttons, edges, and what `Button Type` decides

protoArtoo classifies a bound action three ways, and the DS-650's per-button
`Button Type` setting decides which classification suits it:

- **Analog targets** (`drive_speed`, `drive_steer`, `dome_speed`) need a
  proportional channel. Only CH1 and CH2 qualify.
- **Level targets** -- `arm1_toggle`, `arm2_toggle`, `op_mode` -- follow the
  channel's state. A **latching** button suits these: pressed is open, released
  is closed.
- **One-shot targets** -- every sound, every sequence, `estop`, `sleep_toggle`,
  `speed_preset_cycle` -- fire once per transition. The code comment names the
  handset: *"One-shot actions fire once per button click on latched channels.
  For DS-650 CH3-CH6 this means triggering on either edge transition"*
  (`src/rc_action_types.cpp:371-372`).

That last sentence is the important one. On a **latching** DS-650 button, a
"click" is one edge, and the next click is the opposite edge; firing on both is
what makes a latching switch behave like a button. On a **jog** (momentary)
button both edges also arrive, so a sound would fire on press and again on
release -- which is why `kOneShotEdgeDebounceMs = 120` and
`kSwitchEdgeConfirmFrames = 2` exist (`src/rc_input_processor.cpp:9-10`).

Three-position (`3 gears`) buttons map onto
`rcAnalogToSwitchState()`'s `LOW` / `MID` / `HIGH` split at
`RC_SWITCH_TRIGGER = 0.4f`, but **the trigger dispatch path is boolean** -- it
asks "pressed or not", not "which of three". A three-position button therefore
buys nothing today beyond a different pressed/not threshold. `UNKNOWN` whether a
three-state binding target is worth adding; it is a design question, not a
research one.

### 9.4 What the handset cannot do for you

- **No modifier chords.** ShadowMD and Padawan360 multiply a gamepad's buttons
  by holding L1/L2/R1 -- four actions per face button (Section 10). Nothing on
  a DS-650 reads a chord, because CH3-CH6 arrive as independent channel values
  with no notion of simultaneity in the dispatcher. Six is six.
- **No throttle curve, no ramp.** Section 7.3.
- **No return path.** The handset displays receiver voltage and signal strength
  on its own screen from the receiver's telemetry, but **none of that is an SBUS
  field** and none of it reaches the ESP32. Unlike CRSF, a droid on a HotRC link
  cannot report anything back to its operator.

## 10. How the hobby actually uses these (non-normative)

Everything in this section is evidence of practice. None of it is normative and
none of it is a protoArtoo commitment.

### 10.1 Why droid builders buy this handset

Printed Droid's knowledge base lists it as *"a very small remote control for
mini droids"* that *"can be held in one hand and offers a reliable connection."*
bithead942, who has owned every version since 2022, puts the reason more
directly: *"the form-factor alone is the best selling point for me because of my
need for concealment."*

That is the whole argument. A droid at an event is performing, and an operator
visibly working a tray-mounted transmitter breaks it. A DS-650 fits in a palm or
a pocket, costs USD 28-35, and leaves the operator's other hand free. Builders
commonly carry **two** -- one per hand -- which is a twelve-channel setup for
under USD 70, and which is also why protoArtoo has a dual-SBUS mode.

The same source is candid about the trade: the DS series is *"mid-level"*, with
no ramp control, no stabilisation, no macro record, and -- explicitly -- *"No
Open protocols ... no support for OpenTX or ELRS or CRSF or anything similar."*
A builder choosing a DS-650 is choosing concealment and price over
programmability, deliberately.

### 10.2 What the other astromech projects do instead, and why it does not transfer

Read this session, on disk:

| Project | Input | Grammar | Actions addressable |
| --- | --- | --- | --- |
| **ShadowMD** | PS3 Navigation over Bluetooth | per-button config blocks, each naming a MarcDuino function, an MP3, a logic display type and a panel sequence | the MarcDuino function table, codes **1-89** (`Shadow_MD_DualController_Template.ino:102-193`), plus custom combinations |
| **Padawan360** | Xbox 360 over USB Host Shield | face button x modifier (L1 / L2 / R1 / none) | ~16 direct |
| **ShadyRC / CHIRP** | ExpressLRS, CRSF | channel map plus arming | see [`elrs-crsf-radio.md`](elrs-crsf-radio.md) |
| **astromech simulator** | USB transmitter via the Gamepad API | four Mode 2 axes onto a pad stub | 4 axes |
| **protoArtoo** | **SBUS** | 14 binding slots onto 38 registry targets | 38, six at a time |

**Neither ShadowMD nor Padawan360 reads an RC receiver at all.** Both are
gamepad projects, and their rich grammars come from having a dozen buttons and
two modifiers to chord with. Their approach does not port to a six-channel
handset, and this is worth stating plainly because it is the reason protoArtoo's
grammar is *configuration* -- a binding table the operator edits from a web page
-- rather than a hardcoded button map. With six inputs, which six is the only
interesting question, and it must be answerable without a rebuild.

Their drive constants, for calibration comparison:

| | ShadowMD | Padawan360 | protoArtoo |
| --- | --- | --- | --- |
| Drive speeds | 70 / 110 (of 127) | 60 / 100 / 127 | `speedLimitMax`, preset cycle |
| Turn speed | 50 | 50 | mapper output |
| Dome speed | 100 | 80 | `dome_speed` binding |
| Ramping | 1 per loop | 5 per loop | **none** |
| Stick deadzone | 15 foot / 10 dome | 20 | `deadband`, default **0** |

Two things stand out. Both mature projects **ramp**, and protoArtoo does not --
combined with a DS-650's own *"zero to 100 very suddenly"* throttle, that is a
real difference in feel worth knowing about. And both default to a **non-zero
deadzone** where our bindings default to zero, which on a stick whose centre
wanders is the difference between a still droid and a creeping one.

### 10.3 The throttle trap, named by someone else first

The simulator on this machine implements an RC channel model closely parallel to
ours -- split-about-centre normalisation, per-channel reverse, deadzone rescaled
rather than subtracted so a calibrated stick still reaches 1.000 at the stop --
and it carries this comment at the point where it auto-assigns channels to the
feet:

> *THE THROTTLE TRAP. Channel 3 on a Mode 2 set rests at the BOTTOM of its
> travel, and the calibration correctly calls that a full-span axis -- top to
> bottom is -1 to +1. Wire that straight to the feet and the droid drives away at
> full reverse the moment you stop touching it, which is precisely the bench
> accident this whole panel exists to avoid.*

Its answer is to force anything auto-assigned to a stick back to rest-is-zero.
protoArtoo has no such guard: our binding model can express it (set `center` to
the rest value) but nothing prevents a builder from leaving `center` at 992 with
a throttle that rests at 1889. We met the accident in April 2026 for exactly
this reason. **This is the single most valuable non-normative finding in this
sheet** and Open Item 5 is whether the mapper should refuse such a binding.

## 11. What protoArtoo already does, and what is left

### 11.1 Already done, and hardware-verified

| Piece | State |
| --- | --- |
| SBUS decode on RMT, both receivers, no UART cost | shipping |
| 115 kbaud adaptive bit period, gap-tick exclusion | shipping, three native tests |
| Per-byte start-bit re-sync, +/-1 slip | shipping |
| SBUS2-style footer acceptance | shipping, four native tests |
| Two failsafe layers plus boot latch | shipping, `full-hardware-verified` |
| `lost_frame` dispatch suppression | shipping |
| Per-channel binding, calibration and persistence | shipping, `full-hardware-verified` for mapping and persistence across reboot |
| Live `/api/rc` diagnostics | shipping |
| DS-650 CH3-CH6 reverse default | shipping, pinned both ways by test |

`tasks/phase5-verification-closeout.md:356` classifies the DS-650 + SBUS-A
dome-control path `full-hardware-verified` with a real dome motor under stick
control; `:583` does the same for RC mapping persistence across reboot.

### 11.2 Not done, and honest about it

- **RC-to-drive on real motors remains deferred.** The T19 closeout is explicit:
  *"Drive motor output remains deferred (drive not connected in this session)."*
  Everything above the drive arbiter is proven; the last metre to the wheels was
  verified with the dome, not the feet.
- **`rcMapChannels()` hardcodes `useCh2 = false`** (`src/rc_channel_mapper.cpp:257-259`)
  with the comment *"For initial implementation ... This can be extended in
  future if dual-receiver mapping becomes needed."* The live task path routes
  per source and does not go through that constant, but the pure mapper's own
  dual-receiver story is unfinished.
- **Parity is not validated.** `parityFailCount`, `sbusDecodePopcount8()` and the
  whole diagnostic plumbing through to the log line exist; nothing increments
  the counter. Parity gating was tried during T19 and **rejected** -- at 115
  kbaud the bit-run expansion shifts parity positions and every frame fails. It
  is on the no-backtracking list and must not be re-attempted without new
  telemetry.
- **Two file-header comments are stale.** `include/sbus_decoder.h:22-23` and
  `src/drivers/sbus_decoder.cpp:18-21` still describe a polarity fallback and a
  200 kbaud attempt; commit `d9f4a50e` reduced both arrays to a single element
  because they were wrong for this handset and produced 75 % of the header-
  mismatch noise.

### 11.3 Costs to state plainly

- Two RMT RX channels for dual SBUS, and on the ESP32-P4 that is **4 of 4**
  RX-capable channels (`include/sbus_decoder.h:36-43`). There is no third
  receiver on that board.
- 1536 bytes of `.bss` per decoder for the symbol double-buffer.
- On artoo-esp32, SBUS1 costs USB serial while the module is seated (Section 6.1).
- Six channels. Every feature that wants an operator input competes for four
  buttons.

## 12. Agent Lookup Quick Reference

- Field: Component Protocol. Required value: **`sbus`**.
- Field: Registry row. Required value: `include/component_registry.inc:115`, part id **3**, id `hotrc_ds650`, operator-visible name **"HotRC DS-650"**.
- Field: Lineup status. Required value: **`supported`**. Capabilities bitmask **0** -- nothing to ask, not unknown.
- Field: Channels the handset transmits. Required value: **6** (CH1-CH2 proportional, CH3-CH6 buttons). CH7-CH16 carry nothing.
- Field: Receiver protoArtoo runs. Required value: **HotRC SBUS-A**, 16-channel SBUS, 1.7 g, DC 4-9 V, 35 mA.
- Field: Receiver the handset ships with. Required value: **F-06A**, six PWM channels -- *not* the SBUS-A.
- Field: Measured baud. Required value: **~115 kbaud**, 8.68 us/bit. **Not 100 kbaud.**
- Field: Bit period at 1 MHz RMT. Required value: **9 ticks**, from `sbusEstimateBitPeriod()`, clamped `[8, 10]`.
- Field: Bit-period estimator input. Required value: all captured symbols **except the last**. The last carries the ~300-tick inter-frame gap.
- Field: Footer. Required value: **`0x00` or any byte with low nibble `0x04`**. A `0x00`-only check rejects 100 % of frames.
- Field: Header. Required value: **`0x0F`**, unchanged.
- Field: Frame length. Required value: **25 bytes**, 300 bits at 12 bits/byte.
- Field: Byte re-sync tolerance. Required value: **+/-1 bit** (`kSbusDecodeMaxStartBitSlip`). Dominant HotRC mode is one slip per frame.
- Field: Parity validation. Required value: **not performed, and deliberately so.** Rejected during T19; on the no-backtracking list.
- Field: Decode attempts per frame. Required value: **one** -- adaptive period, `invertBits=false`. 200 kbaud and inverted-bit attempts are wrong for this handset.
- Field: Channel raw domain. Required value: **0-2047**; this handset's usable window depends on its EPA setting.
- Field: Binding defaults. Required value: **172 / 992 / 1811**, deadband **0**, from `RC_SBUS_DEFAULT_MIN/CENTER/MAX`.
- Field: Measured endpoints, factory stroke. Required value: CH1 ~255 / ~1472 / ~1919; CH2 ~97 / rest ~1889-2028. **CH2's rest is above 1811 and normalises to full throttle.**
- Field: Measured endpoints, after EPA reduction. Required value: **192 -> 992 -> 1792**, mapped **+/-0.977**.
- Field: CH2 rest position. Required value: **an endpoint, not the centre.** It is a trigger.
- Field: CH1 display-to-raw. Required value: `SBUS_raw = -2.176 x (display_us - 1500) + 1472` -- **inverted** against the handset's own screen.
- Field: CH3-CH6 idle polarity. Required value: **idle high, press low** -> `reverse=true` by default on SBUS channels 3-6.
- Field: Failsafe flag. Required value: flags **bit 3**. Triggers `FailsafeLayer::SBUS_HW` and an explicit drive zero-frame.
- Field: Lost-frame flag. Required value: flags **bit 2**. **Suppresses dispatch and does not clear the watchdog.** The receiver emits a plausible held value alongside it.
- Field: Software watchdog. Required value: **`SBUS_TIMEOUT_MS` = 200 ms**, configurable 50-5000 ms. Never-received counts as timed out.
- Field: Boot state. Required value: **drive latched off** until the first clean frame clears `SBUS_WATCHDOG`.
- Field: Binding slots. Required value: **3 analog + 11 triggers = 14**, against **38** registry targets.
- Field: Switch thresholds. Required value: **+/-0.4** (`RC_SWITCH_TRIGGER`) for LOW / MID / HIGH; trigger dispatch is boolean.
- Field: One-shot debounce. Required value: **120 ms**, with **2**-frame edge confirmation.
- Field: SBUS pins. Required value: artoo-esp32 **GPIO 15 / 13**; firebeetle2 **GPIO 28 / 29**.
- Field: Decode peripheral. Required value: **RMT**, 1 us/tick, 300 us gap threshold. No UART is consumed.
- Field: Telemetry to the droid. Required value: **none.** Receiver voltage and signal strength reach the handset's screen only.
- Field: Handset settings that change the wire. Required value: **REV, EPA, SUB TR, MIXES, Button Type, CCS**, plus the two trim rockers.
- Field: Frame period. Status: **UNKNOWN** -- documented as 7-14 ms, never measured on this bench. Observed `ageMs` stayed under 20-30 ms with the link healthy. Settled by capturing the receiver's output and timing frame starts.
- Field: Simultaneous receivers per handset. Status: **UNKNOWN** -- the community says one at a time, this project's dual-SBUS run says two. Open Item 1.
- Field: Indoor range among people. Status: **UNKNOWN** -- three vendor figures exist and none describe a crowded hall.

If a required value cannot be proven for the handset in hand, status is
`UNKNOWN` and dependent work stops. For anything in Section 8, that means the
droid does not drive.

## 13. Open Items

1. **Can one DS-650 hold two SBUS-A receivers linked at once?** Section 6.3. The
   community's first-hand answer for the PWM receivers is *"one at a time, the
   most recently bound"*; this project's own bench shows both `sbus1` and `sbus2`
   linked simultaneously reporting identical channel values, which is only
   possible if both are receiving. **This decides whether dual SBUS is a
   supported topology or a lucky one.** Settled on a bench: bind two SBUS-A
   receivers to one handset, power both, and watch `/api/rc` `sources` for an
   hour -- specifically whether the earlier-bound one ever drops.
2. **What is the actual frame period?** Section 12. Never measured here, and it
   sets whether 200 ms is ten missed frames or thirty. Settled by a logic-analyser
   capture of the SBUS-A output, or by timestamping accepted frames in the
   decoder over a minute.
3. **Which bind procedure does a DS-650 + SBUS-A pair actually use?** Section
   5.2. The receiver's manual documents a Channel-3-at-power-on sequence written
   for the CT/HT handsets; the DS-650 has a `Bind Set` menu; and on a DS-600,
   Channel 3 at power-on toggles mixing instead. Settled by binding the pair on
   a bench and writing down what worked -- fifteen minutes, and it removes a
   documented collision from a builder's first hour.
4. **Should `sbusTimeoutMs` scale with the frame period?** 200 ms is a wall-clock
   number chosen before the cadence was known. It is also the value a bench
   session raised to 5000 ms and at least one saved controller backup still
   carries. `UNKNOWN` whether the default is right; it is a safety decision, not
   a research result.
5. **Should the mapper refuse a binding whose `center` is at an endpoint?**
   Section 10.3. A `center` within a deadband of `min` or `max` means "rest is
   full deflection", which is the throttle trap expressed as configuration. The
   simulator guards against it; we do not. This is a design decision for the
   operator, not a fact to look up.
6. **Does the DS-650 emit the same ~115 kbaud on every unit, or did we measure
   one handset?** Section 7.1. The adaptive estimator makes this mostly moot --
   it would track 100 kbaud equally well -- but the `[8, 10]` clamp encodes an
   assumption about how far the deviation can go. `UNKNOWN` whether a second
   DS-650 measures the same, and there is no second one on this bench.
7. **What is the DS-650's EPA granularity?** Section 5.2. The 20-step / 5-step
   figures are the DS-600's button-era numbers and the DS-650 adjusts from a
   menu. Settled by reading the handset's own screen while adjusting.
8. **Is the throttle's lack of a curve worth compensating in firmware?** Neither
   the handset nor protoArtoo ramps, while both ShadowMD and Padawan360 do
   (Section 10.2). This is scope, not research, and it belongs with the drive
   work rather than with this sheet.
9. **Does the `Gyro` menu entry do anything with an SBUS-A?** It is documented for
   the `-AT` receivers. `UNKNOWN` whether enabling it on a handset paired to a
   non-gyro receiver changes the transmitted channels. Settled by toggling it and
   watching `/api/rc`.

## 14. Sources

**Normative (primary): this project's bench record**

- `tasks/phase5-tasks.md` T19, lines 2240-2820 -- the endpoint sweep, the
  115 kbaud finding, the `0x04` footer, the display-to-raw formula, the
  per-byte re-sync work, the final accepted profile, and the six DoD gates
- `tasks/lessons.md:862-867` -- the gap-tick inflation, recorded as a standing
  lesson
- `tasks/phase5-verification-closeout.md:356, 571, 583` -- the
  `full-hardware-verified` classifications for the DS-650 + SBUS-A pair
- Commits `6f5a9d42`, `d9f4a50e`, `c4ccfdbd`, `b86a1bf5`, `a26aaae2` -- the
  five-step decode fix, each message carrying its own measurement

**Normative (primary): this project's source**

- `src/drivers/sbus_decoder.cpp`, `include/sbus_decoder.h`,
  `include/sbus_decode_helpers.h` -- the decoder and its pure helpers
- `src/tasks/rc_input.cpp`, `src/tasks/rc_input_step.cpp` -- the RC task and its
  frame policy
- `include/rc_binding_types.h`, `src/rc_channel_mapper.cpp`,
  `src/rc_action_types.cpp` -- calibration, mapping and action classification
- `include/config.h`, `src/config_store.cpp` -- constants and shipped defaults
- `test/test_native/test_sbus_decode/`, `test_rc_mapping/`,
  `test_failsafe_boot_sbus/` -- what the behaviour is pinned to

**Normative (primary): vendor documentation**

- HotRC DS-650 specification and manual: https://manuals.plus/ae/1005009356275137
- HotRC DS-650 alternate manual: https://manuals.plus/ae/1005009793615853
- HotRC receiver manual, thirteen models including the SBUS-A:
  https://manuals.plus/ae/1005006924396938
- HotRC F-06A / F-06AT receiver manual: https://manuals.plus/ae/1005007076664946
- **HotRC DS-600 manual (PDF, read in full this session)**:
  https://www.printed-droid.com/wp-content/uploads/2023/04/HotRC-DS-600-Manual.pdf
  -- the previous generation's procedures and panel layout
- HotRC official site: https://en.hotrc.cn/
- SBUS-A product page: https://begreatrc.com/products/hotrc-sbus-a-24ghz-receiver
- DS-650 product page:
  https://www.begreatrc.com/products/hotrc-ds650-24ghz-6ch-fhss-built-in-lipo-battery-lcd-screen-radio-controller-with-f-06a-receiver

**Builder documentation (primary source, non-normative)**

- **bithead942, "Comparing HotRC DS Controllers", January 2026**:
  https://bithead942.wordpress.com/2026/01/02/comparing-hotrc-ds-controllers/
  -- the version lineage, the receiver compatibility matrix, the DS-650's
  advanced functions, the stated limitations, and the pairing FAQ
- bithead942, "Modifying the HotRC DS-600":
  https://bithead942.wordpress.com/2023/01/21/modifying-the-hotrc-ds-600/
- Printed Droid knowledge base:
  https://www.printed-droid.com/kb/hotrc-ds-600/

**Ecosystem survey (primary source, non-normative)**

- astromech simulator RC module, read locally:
  `~/Documents/GitHub/r2d2-astromech-simulator/src/js/input/rc.js`
- ShadowMD, read locally: `~/Documents/GitHub/ShadowMD`
- Padawan360, read locally:
  `~/Documents/GitHub/Padawan360_mega_maestro_DYSV5W`
- CHIRP, read locally: `~/Documents/GitHub/CHIRP`

**Protocol references, used only where this sheet touches framing**

- [`sbus-protocol.md`](sbus-protocol.md) -- the frame layout and bit packing
- [`rmt-esp32-idf5.md`](rmt-esp32-idf5.md) -- the RMT peripheral
- [`rc-receiver-spec.md`](rc-receiver-spec.md) -- the PWM/PPM path for the
  bundled F-06A
- [`elrs-crsf-radio.md`](elrs-crsf-radio.md) -- the roadmap alternative, and the
  contrasting failsafe model
- `bolderflight/sbus`: https://github.com/bolderflight/sbus -- the footer
  validation our `isValidSbusFooter()` matches
