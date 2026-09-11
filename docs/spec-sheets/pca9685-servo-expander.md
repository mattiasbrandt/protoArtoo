# PCA9685 Servo Expander Spec Sheet

Working spec for the NXP PCA9685 16-channel I2C PWM controller as a **Body servo
controller** lineup member (issue
[#306](https://github.com/mattiasbrandt/protoArtoo/issues/306), minted from
[#303](https://github.com/mattiasbrandt/protoArtoo/issues/303)).

Research date 2026-09-10. Every register, formula and electrical value below was
read from the NXP datasheet or from library source this session; nothing is
recalled. Claims that could not be sourced are marked `UNKNOWN` with the artefact
that would settle them.

> [!NOTE]
> **We already run two of these.** The dome carries a PCA9685 at `0x40` (panels)
> and another at `0x41` (holos), driven by our AstroPixelsPlus fork. Section 10 is
> what that deployment has already taught us, and it is the most valuable part of
> this sheet -- those are measured field findings, not literature.

## Where this sits in the lineup

The **Body servo controller** category holds three products, and a builder picks
one:

| Product | How it drives a servo | Status |
| --- | --- | --- |
| ESP32 GPIO (LEDC) | MCU peripheral, one timer channel per output | `supported` |
| **PCA9685** | I2C expander, 16 outputs per board | `roadmap` |
| Pololu Maestro | Serial controller with an onboard motion engine | `roadmap` |

These are peer choices with different shapes, not a ladder. LEDC is what
protoArtoo drives today because it needs no extra hardware; that is a fact about
where the project started, not a verdict.

## 0. Authority Contract

This document is an implementation authority for PCA9685 register programming and
for the electrical behaviour of the common breakout boards.

Authority order for agent decisions:

1. The NXP PCA9685 data sheet (Rev. 4, 16 April 2015).
2. This document.
3. Library source (Adafruit, Reeltwo) -- which is evidence of practice, **not** of
   correctness. Section 8 lists four places where the most popular library
   contradicts the datasheet.
4. Vendor learn guides and community build notes.

If references conflict:

- Prefer the datasheet. Where a library diverges, this sheet says so explicitly
  rather than treating the library as the contract.
- If still unresolved, mark the value `UNKNOWN` and stop implementation changes
  that depend on it.

Agent requirements when using this document:

- MUST treat Sections 6 and 7 as normative.
- MUST NOT invent a register address, bit position, or prescale value.
- MUST NOT assume the internal oscillator runs at 25 MHz in a given unit. It is a
  typical value with **no min/max specified** in the datasheet's electrical
  characteristics (Section 7.5).
- MUST distinguish "0 % duty" from "full OFF". They are different hardware states
  and confusing them is the cause of our first field bug (Section 10.1).
- MUST treat channel numbering as project-specific. Three conventions are in use
  and porting a table between them shifts every servo (Section 9.3).

## 1. Scope

Covers the PCA9685 chip, the common breakout boards, the register-level
programming contract, the two libraries that matter to this project, what our own
dome deployment has proven, and what protoArtoo would have to build.

Does not cover: the Pololu Maestro beyond the comparison in Section 11 -- it is a
separate lineup member and earns its own sheet if it is built. Does not cover LED
dimming, the chip's original purpose.

## 2. Boards Covered

| Item | Notes |
| --- | --- |
| Adafruit 16-Channel 12-bit PWM/Servo Driver (product 815) | The reference board. Ships as a kit; headers are loose and need soldering |
| Generic 16-channel clones | Direct layout copies, usually with headers pre-soldered |
| Waveshare Servo Driver HAT | Same chip, different board: onboard 5 V regulator, 6-12 V input, 3 A |

> [!IMPORTANT]
> **No PCA9685 breakout has a level shifter** -- not Adafruit's, not the clones. I
> read the Adafruit rev C schematic: SDA and SCL go straight to the chip with two
> pull-ups to `VCC`. The board is 3.3 V-safe because **the chip is**, not because
> anything translates. What varies between builds is only what `VCC` is tied to.
> Anyone claiming otherwise is wrong about both.

Two Adafruit silkscreen variants exist in the wild. Adafruit's product page:
*"As of December 20, 2022 - we've updated this PCB with Adafruit Pinguin to make a
lovely and legible silkscreen - you may get the new PCB or the older version with
vector fonts - both are identical other than the fancy silkscreen."* Channel
numbers `0`-`15` are printed above the PWM row on both.

## 3. Project Integration

- **[`include/config.h`](../../include/config.h)** -- `PIN_I2C_SDA` / `PIN_I2C_SCL`
  on both targets (`:232-233` artoo-esp32, `:325-326` firebeetle2).
- **[`docs/pin_map.md`](../pin_map.md)** -- I2C rows (`:155-156`), broken out as
  labelled headers on the artoo.uk PCB.
- **[`include/ledc_pwm.h`](../../include/ledc_pwm.h)** -- the servo backend that
  ships today, and the resolution this one is compared against.
- **[`docs/adr/0043-estop-releases-servo-outputs-rather-than-closing-them.md`](../adr/0043-estop-releases-servo-outputs-rather-than-closing-them.md)**
  -- Output Release, which this part inherits.

## 4. Official Sources Checked

| Source | URL | Extraction notes |
| --- | --- | --- |
| NXP PCA9685 data sheet, Rev. 4, 2015-04-16 | https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf | **Authoritative.** Register map, prescale formula, MODE1/MODE2 bits, restart sequence, electrical characteristics, address arithmetic |
| Adafruit product page 815 | https://www.adafruit.com/product/815 | Board contents, dimensions, silkscreen revisions, 5 V compliance statement |
| Adafruit learn guide -- Pinouts | https://learn.adafruit.com/16-channel-pwm-servo-driver/pinouts | VCC range, 10K pull-ups, V+ role, 220 ohm series resistors, OE, shared-frequency constraint |
| Adafruit learn guide -- Hooking it Up | https://learn.adafruit.com/16-channel-pwm-servo-driver/hooking-it-up | Capacitor sizing, brownout warning, servo plug orientation |
| Adafruit learn guide -- Chaining | https://learn.adafruit.com/16-channel-pwm-servo-driver/chaining-drivers | Address jumper arithmetic, 62-board chain |
| Adafruit learn guide -- FAQ | https://learn.adafruit.com/16-channel-pwm-servo-driver/faq | The 0x70 all-call collision; the 4096-vs-4095 full-off idiom |
| `Adafruit-PWM-Servo-Driver-Library` v3.0.3 | https://github.com/adafruit/Adafruit-PWM-Servo-Driver-Library | The reference implementation, and four divergences from the datasheet |
| Adafruit library issue #40 | https://github.com/adafruit/Adafruit-PWM-Servo-Driver-Library/issues/40 | The prescale rounding defect, with a worked 200 Hz -> 210 Hz example |
| `reeltwo/Reeltwo` `src/ServoDispatchPCA9685.h` | https://github.com/reeltwo/Reeltwo | What our dome actually runs. Channel math, staggering, clock calibration, temperature correction |
| `ryan-sondgeroth/FlthyHPs` | https://github.com/ryan-sondgeroth/FlthyHPs | Ships **both** a PCA9685 and a Maestro edition -- the best like-for-like comparison in the hobby |
| NXP product page (lifecycle) | https://www.nxp.com/products/PCA9685 | Status Active; Product Longevity enrolment |

## 5. Electrical (board level)

| Item | Value | Source |
| --- | --- | --- |
| VCC (logic) | *"should be 3 - 5V max!"* | Adafruit Pinouts |
| I2C pull-ups | **10K to VCC, on the board** | Adafruit Pinouts |
| V+ (servo rail) | *"5-6VDC if you are using servos"* | Adafruit Pinouts |
| V+ role | *"It is not used at all by the chip."* | Adafruit Pinouts |
| Per-output series resistance | **220 ohm on every PWM line** | Adafruit product page |
| Output logic level | *"the output logic is the same as VCC"* | Adafruit Pinouts |
| Capacitor | *"n * 100uF where n is the number of servos ... 470uF or more for 5 servos"* | Adafruit Hooking it Up |
| Dimensions | 62.5 x 25.4 x 3 mm, 5.5 g bare | Adafruit product page |
| Mounting holes | 2.5 mm diameter | Adafruit Downloads |

> [!WARNING]
> **Adafruit's own guide contradicts itself on the V+ ceiling.** The Pinouts page
> says V+ can go *"higher to 12VDC"*; the Hooking it Up page says *"The V+ pin can
> be as high as 6V even if VCC is 3.3V"*. Same guide, two numbers. Treat **6 V** as
> the safe figure. V+ is only copper to the servo headers, so the real limit is
> the connectors and traces rather than the chip -- and Adafruit publishes no trace
> or terminal-block current rating at all (`UNKNOWN`; the EagleCAD files plus the
> terminal block's own datasheet would settle it).

**Servo signal level at 3.3 V.** Tying VCC to 3.3 V makes the PWM outputs swing to
3.3 V. Most hobby servos accept that; it is not universally guaranteed, and it is
worth stating in builder docs rather than discovering per servo.

**Brownout is the documented failure mode.** Adafruit: *"It is not a good idea to
use the Arduino 5v pin to power your servos. Electrical noise and 'brownouts' from
excess current draw can cause your Arduino to act erratically, reset and/or
overheat."* And on budgeting: *"Even micro servos will draw several hundred mA when
moving. Some High-torque servos will draw more than 1A each under load."*

**OE pin.** *"When this pin is low all pins are enabled. When the pin is high the
outputs are disabled. Pulled low by default so it's an optional pin!"* This is a
hardware kill for every channel on the board at once, and Section 11 explains why
it matters to ADR 0043.

## 6. Electrical (chip level)

From the NXP data sheet Rev. 4. Included because these are the limits any driver
must respect regardless of which breakout carries the chip.

| Parameter | Value |
| --- | --- |
| Supply voltage `VDD` | 2.3 V to 5.5 V |
| Input/output tolerance | 5.5 V |
| Supply current `IDD` | 6 mA typ, 10 mA max (operating, no load, `fSCL` = 1 MHz) |
| Standby current `Istb` | 2.2 uA typ, 15.5 uA max |
| Power-on reset voltage `VPOR` | 1.70 V typ, 2.0 V max |
| Channels | 16, each 12-bit (4096 steps) |
| Output frequency range | **24 Hz to 1526 Hz**, all channels share one frequency |
| Output drive, open-drain | 25 mA sink at 5 V |
| Output drive, totem pole | 25 mA sink, 10 mA source at 5 V |
| `IOL` per output | 12 mA min, 25 mA typ (`VOL` = 0.5 V) |
| `IOL(tot)` all outputs | **400 mA max** |
| I2C bus speed | up to **1 MHz** (Fast-mode Plus) |
| Internal oscillator | 25 MHz typical |
| External clock `EXTCLK` | DC to 50 MHz |
| Address pins | 6, giving 64 addresses minus 2 reserved = **62 usable** |
| Operating temperature | -40 C to +85 C |
| Packages | TSSOP28, HVQFN28 |

The 62-device figure is the datasheet's own arithmetic: *"There are a maximum of
64 possible programmable addresses using the 6 hardware address pins. Two of these
addresses, Software Reset and LED All Call, cannot be used because their default
power-up state is ON, leaving a maximum of 62 addresses."*

## 7. The programming contract (normative)

### 7.1 Register map

| Register | Hex | Register | Hex |
| --- | --- | --- | --- |
| MODE1 | `00h` | LED0_ON_L | `06h` |
| MODE2 | `01h` | ... | ... |
| SUBADR1 | `02h` | LED15_OFF_H | `45h` |
| SUBADR2 | `03h` | ALL_LED_ON_L | `FAh` |
| SUBADR3 | `04h` | ALL_LED_ON_H | `FBh` |
| ALLCALLADR | `05h` | ALL_LED_OFF_L | `FCh` |
| | | ALL_LED_OFF_H | `FDh` |
| | | PRE_SCALE | `FEh` |

Each channel occupies four consecutive registers from `06h`, so
`LEDn_ON_L = 06h + 4n`.

### 7.2 MODE1 and MODE2 bits

**MODE1 (`00h`):**

| Bit | Symbol | Meaning |
| --- | --- | --- |
| 7 | RESTART | Read: restart logic state. Write 1 to clear |
| 6 | EXTCLK | Use EXTCLK pin. **Sticky -- clearable only by power cycle or software reset** |
| 5 | AI | Register auto-increment |
| 4 | SLEEP | Low power mode, oscillator off. **Default 1** |
| 3-1 | SUB1-3 | Respond to I2C subaddress 1-3 |
| 0 | ALLCALL | Respond to LED All Call address. **Default 1** |

**MODE2 (`01h`):**

| Bit | Symbol | Meaning |
| --- | --- | --- |
| 4 | INVRT | Output logic inverted |
| 3 | OCH | 0 = outputs change on STOP, 1 = on ACK |
| 2 | OUTDRV | 0 = open-drain, 1 = totem pole. **Default 1** |
| 1-0 | OUTNE[1:0] | Output state when `OE` = 1 |

`OCH` matters for multi-board rigs: *"Change of the outputs at the STOP command
allows synchronizing outputs of more than one PCA9685."*

### 7.3 Full ON and full OFF are hardware states

> [!IMPORTANT]
> **`LEDn_ON_H[4]` is full-ON and `LEDn_OFF_H[4]` is full-OFF, and the power-on
> default of the full-OFF bit is `1`.** These are not the same thing as a 0 %
> duty cycle. A channel at 0 % duty is still being driven; a channel with full-OFF
> set is not driven at all.

This distinction is the direct cause of our first field bug (Section 10.1) and the
reason the fix had to write the register rather than change firmware state.

The Adafruit library never names the bit. It reaches it arithmetically by passing
`4096` as a 16-bit tick value and letting `buffer[2] = on >> 8` produce `0x10` --
which *is* bit 4. Adafruit's guide documents the idiom as the user contract:
*"You can set the pin to be fully on with `pwm.setPWM(pin, 4096, 0);` You can set
the pin to be fully off with `pwm.setPWM(pin, 0, 4096);`"*

### 7.4 Prescale and frequency

```
prescale value = round( osc_clock / (4096 * update_rate) ) - 1
```

Datasheet worked example: 200 Hz at 25 MHz gives prescale 30 (`0x1E`).
`0x03` is the maximum frequency, 1526 Hz; `0xFF` the minimum, 24 Hz.

> [!CAUTION]
> **`PRE_SCALE` is writable only while `SLEEP` is set.** Datasheet: *"The PRE_SCALE
> register can only be set when the SLEEP bit of MODE1 register is set to logic 1."*
> A write while running is silently ignored.

**At 50 Hz the prescale is 121** (`round(25e6 / (4096 * 50)) - 1 = 121`), derived
here from the formula above.

### 7.5 The oscillator is not a known quantity

The datasheet gives 25 MHz as *typical* and **states no minimum or maximum** in the
electrical characteristics -- the only frequency limits published are for the
external `fEXTCLK` input. Adafruit's example sketch is blunter about the spread:

> "In theory the internal oscillator (clock) is 25MHz but it really isn't that
> precise. You can 'calibrate' this by tweaking this number until you get the PWM
> update frequency you're expecting! The int.osc. for the PCA9685 chip is a range
> between about 23-27MHz"

Note that the shipped example calibrates to **27 MHz**, not 25. The library's own
API documentation admits the blindness: *"@returns The frequency the PCA9685 thinks
it is running at (it cannot introspect)"*.

**Consequence for protoArtoo:** pulse width accuracy is per-chip and needs a scope
to trim. If servo endpoints are calibrated per channel anyway -- which our dome
already does via NVS -- the oscillator error is absorbed into that calibration and
never needs measuring. That is the cheap path, and it should be a deliberate
decision rather than an accident.

### 7.6 The restart sequence

Quoted from the datasheet, because it carries a hard timing rule:

> 1. Read MODE1 register.
> 2. Check that bit 7 (RESTART) is a logic 1. If it is, clear bit 4 (SLEEP). Allow
>    time for oscillator to stabilize (500 us).
> 3. Write logic 1 to bit 7 of MODE1 register. All PWM channels will restart and
>    the RESTART bit will clear.
>
> Remark: The SLEEP bit must be logic 0 for at least 500 us, before a logic 1 is
> written into the RESTART bit.

Also worth knowing: *"if the user does an orderly shutdown of all the PWM channels
before setting the SLEEP bit, the RESTART bit will be cleared. If this is done the
contents of all PWM registers are invalidated and must be reloaded before reuse."*

### 7.7 Resolution at servo frequencies

Arithmetic, from the 12-bit counter and a 50 Hz frame:

```
step = 20000 us / 4096 = 4.883 us
```

Against protoArtoo's LEDC backend at 50 Hz and 16-bit
(`include/ledc_pwm.h:41-43`), `20000 / 65536 = 0.305 us` -- confirmed by that
header's own comment, *"~0.3us at 50Hz/16-bit"*. The expander is **16x coarser**.

Across protoArtoo's configured travel of 500-2500 us (`ledc_pwm.h:49-51`):

| Backend | Steps across travel | Degrees per step, 180 deg servo | 360 deg servo |
| --- | --- | --- | --- |
| LEDC, 16-bit | 6554 | 0.027 | 0.055 |
| PCA9685, 12-bit | **409.6** | **0.44** | **0.88** |

Half a degree on a dome panel is invisible. Record it as a real difference that
does not matter here, rather than pretending it is not there.

### 7.8 Addressing

Slave address is `1 A5 A4 A3 A2 A1 A0` -- base `0x40` with the six jumpers adding
their binary value. Adafruit: *"To program the address offset, use a drop of solder
to bridge the corresponding address jumper for each binary '1' in the address."*

**Reserved, and enabled at power-up:**

| Address | Purpose |
| --- | --- |
| `0x00` | Software reset (general call), followed by data byte `0x06` |
| `0x70` (7-bit) / `E0h` (8-bit) | LED All Call |

> [!CAUTION]
> **Never jumper a board to `0x70` and never put another device there.** The
> datasheet is explicit: *"The default LED All Call I2C-bus address (E0h or
> 1110 000X) must not be used as a regular I2C-bus slave address since this address
> is enabled at power-up. All the PCA9685s on the I2C-bus will acknowledge the
> address."* Adafruit's FAQ records the same trap from the user side.
>
> `0xE0` and `0x70` are the same address in 8-bit and 7-bit notation. Sources
> quote both; they do not disagree.

## 8. The libraries, and where they diverge from the datasheet

### 8.1 Adafruit-PWM-Servo-Driver-Library v3.0.3

The reference implementation, and the one most builders run. Four divergences,
each read from source this session:

> [!WARNING]
> **1. `writeMicroseconds()` can silently do the opposite of what you asked.**
> It computes `pulse` as a `double` and passes it straight into `setPWM()`, and
> **neither function clamps or range-checks**. Ask for a microsecond value too
> large for the current prescale and the computed tick crosses 4096, setting the
> **full-OFF** bit -- so an over-long pulse request turns the channel off instead.
> Verified by reading both functions directly.

**2. The frequency clamp is wrong for internal-oscillator use.** The code reads
`if (freq > 3500) freq = 3500;` with the comment *"Datasheet limit is
3052=50MHz/(4*4096)"*. But 50 MHz is the **EXTCLK** ceiling. On the internal
oscillator the real maximum is **1526 Hz** (Section 6), so the library will accept
a frequency it cannot produce and clamp the prescale instead.

**3. The 500 us oscillator-settle rule is honoured on one path only.**

| Path | Behaviour | Verdict |
| --- | --- | --- |
| `setPWMFreq()` | clears SLEEP, `delay(5)`, then sets RESTART | **Honoured** -- 10x the requirement |
| `setExtClk()` | `delay(5)` elapses *while still asleep*; SLEEP cleared and RESTART set in one write | not honoured |
| `reset()` | single write of `MODE1 = 0x80`, delay after rather than between | not honoured |
| `wakeup()` | clears SLEEP with **no delay at all** | not honoured |

The common servo path (`begin()` then `setPWMFreq()`) is safe. Runtime
sleep/wake is not -- insert your own delay after `wakeup()`.

**4. Errors are swallowed.** `setPWM()` returns `0` for success and `1` for a
failed I2C write -- inverted from the usual C idiom -- and both `setPin()` and
`writeMicroseconds()` discard the return value. A dropped bus transaction is
invisible on those paths.

Two more behaviours worth knowing: `begin()` forces **1000 Hz** unless passed a
prescale, and it heap-allocates a new `Adafruit_I2CDevice` on every call.
`setPin()` is the only clamping entry point and is the safe one to build on.

### 8.2 Reeltwo `ServoDispatchPCA9685` -- what our dome runs

Pinned as `https://github.com/reeltwo/Reeltwo#23.5.3` in the fork's
`platformio.ini:25`. **Not vendored on disk** -- there is no `.pio/libdeps` for
AstroPixelsPlus, so the source must be read from GitHub.

It is markedly more careful than Adafruit's, and its mitigations are a map of the
problems this chip actually causes:

| Mitigation | Detail |
| --- | --- |
| **Channel staggering** | `CHANNEL_OFFSET_STEP 50` us phase shift per channel, so 16 servos do not draw inrush on the same edge. **`DEFAULT_CHANNEL_STAGGERING false`** -- off unless asked for |
| **Per-chip clock calibration** | `setClockCalibration(const uint32_t clock[...])`, one value per board |
| **Prescale rounding fix** | Implements the datasheet's rounding step, citing Adafruit issue #40 by URL in a comment |
| **Temperature correction** | `DEFAULT_TEMPERATURE_CORRECTION 40` per 30 C, `setEnvironmentTemperatureCelsius()` |
| **De-energise after move** | `setPWMOff(channel)` once `offTime` passes, plus a global OE auto-off |

Adafruit issue #40 is worth reading in full: requesting 200 Hz produced prescale
29 instead of 30, an actual 210 Hz -- a 5 % frequency error landing straight on
pulse width.

**Its channel numbering is 1-based**, and this is the single most portable-code
hazard in the subject:

```
uint8_t chip    = (servoChannel - 1) / 16;
uint8_t channel = (servoChannel - 1) % 16;
```

with channel `0` reserved as the "unassigned" sentinel. Adafruit and FlthyHPs are
**0-based**. Porting a channel table between them shifts every servo by one.

**Board count comes from the array length, never from the pin numbers:**
`numServos / 16 + 1`. The example spells out the consequence: *"You do have to
fill up all the pins on any preceeding board... Only the last PCA9685 board can
have unused pins."*

### 8.3 Ramping is host-side work

Neither library gets ramping from the chip, because the chip has none. Reeltwo
interpolates on the MCU and re-transmits over I2C on a roughly 1 ms gate, with 32
Penner easing functions. That cost is the reason FlthyHPs' Maestro edition exists
(Section 11).

## 9. Wiring, addressing and bus budget

### 9.1 Pinout

Control header, 6 pins, duplicated on both edges -- *"Both sides of the pins are
identical!"* -- carrying `GND`, `OE`, `SCL`, `SDA`, `VCC`, `V+`.

Servo ports are four 3x4 blocks. Row order top to bottom is **PWM signal / V+ /
GND**: *"Be sure to align the plug with the ground wire (usually black or brown)
with the bottom row and the signal wire (usually yellow or white) on the top."*

### 9.2 Chaining

*"Multiple Drivers (up to 62) can be chained... the wiring is as simple as
connecting a 6-pin parallel cable from one board to the next."* Only the first
board needs a power terminal. Address by bridging jumpers: `0x40`, `0x41`
(bridge A0), `0x42` (A1), `0x43` (A0+A1), and so on.

### 9.3 Three channel-numbering conventions

| Convention | Used by | First channel |
| --- | --- | --- |
| Silkscreen | The printed board | `0` |
| 0-based firmware | Adafruit library, FlthyHPs | `0` |
| **1-based firmware** | **Reeltwo, hence our dome** | **`1`** |

Our fork's own note, `AstroPixelsPlus.ino:383-389`, states the conversion:
`pin = (n x 16) + physCh + 1`, where `n` = 0 for the `0x40` board and 1 for
`0x41`.

### 9.4 I2C bus time is a real budget

Derived here from the bus clock and the 5-byte `setPWM` transaction (address +
register + 4 data = 6 bytes at 9 bits each including ACK, plus START/STOP,
approximately 56 bit-times):

| Bus speed | Per channel | 13 channels | 19 channels |
| --- | --- | --- | --- |
| 100 kHz (Arduino default) | ~560 us | **~7.3 ms** | **~10.6 ms** |
| 400 kHz | ~140 us | ~1.8 ms | ~2.7 ms |
| 1 MHz (Fm+ max) | ~56 us | ~0.7 ms | ~1.1 ms |

> [!IMPORTANT]
> **Our dome never sets the bus clock.** `AstroPixelsPlus.ino:1222` calls bare
> `Wire.begin()` with no `setClock()`, so it runs at the 100 kHz default. At that
> speed a full 13-channel refresh costs about **a third of a 50 Hz tick**, and a
> 19-channel one costs over half. #286 puts ramping in firmware at 50 Hz, so this
> is a live constraint for protoArtoo rather than trivia -- and it is a one-line
> fix if it bites.

## 10. What our own dome has already proven

The richest evidence in this sheet, because it is ours. Two PCA9685 boards,
`0x40` panels and `0x41` holos, 19 slots.

### 10.1 Grinding under held PWM after a close

`FORK_IMPROVEMENTS.md:164` states the root cause in one line worth keeping:

> "PCA9685 is a dumb PWM emitter. After a close sequence, the servo holds position
> indefinitely under active PWM. Any mechanical conflict (misrouted wire, wiring
> offset, fighting sequence) grinds the motor until power is cut."

Found on a 2026-05-21 integration test with **no recovery short of a power cycle**.
The fix, `FORK_IMPROVEMENTS.md:225`:

> "`servoDispatch.setOutput(pin, false)` writes `LED_FULL_OFF_H` to the PCA9685
> output register -- this is the correct path for actually cutting hardware PWM, as
> opposed to `disable(i)` which only updates firmware state."

Section 7.3 is the datasheet backing: full-OFF is a hardware state, so the fix is
correct rather than a workaround. In code at `AstroPixelsPlus.ino:1911-1919`.

### 10.2 The silkscreen-versus-firmware off-by-one, shipped twice

`FORK_IMPROVEMENTS.md:680` is the builder-facing statement, and it is the reason
this belongs in a spec sheet at all:

> "you wire panel and holo servos to the PCA9685 boards, fire `:OP01`, and... the
> wrong panel moves. Or nothing moves. The only way to debug it physically is to
> lift the dome off, disconnect the slip-ring, work back through your carefully
> zip-tied servo bundles, and trace each suspect cable from the panel hinge all
> the way down to its PCA9685 header... **Every PCA9685-based R2 build hits this.**
> The fork has shipped *two* separate 'wrong by one channel' bugs to date -- once
> on panels, once on holos."

The holo one was a genuine chip-boundary bug (`FORK_IMPROVEMENTS.md:713`): pin 16
stays on `0x40` CH15 rather than crossing to `0x41` CH0, so the original
`{16,17,...,21}` was one short across all six holo axes. Corrected to
`{17,...,22}`; ADR 0004 carries the math.

> [!NOTE]
> **Our fork is ahead of upstream here.** Upstream `reeltwo/AstroPixelsPlus` still
> carries the `// Second PCA9685 controller` comment above pin 16. Our fork moved
> the live table to pins 17-22 and kept the old values commented out at
> `AstroPixelsPlus.ino:1276-1277`. If the Reeltwo dependency is ever bumped or the
> table re-synced from upstream, this regresses.

### 10.3 A third trap the ticket does not record

`AstroPixelsPlus.ino:393-396`: a firmware pin of `0` underflows
`fLastLength[channel-1]` in the `ServoDispatchPCA9685` constructor. Inactive slots
must carry a **non-zero** PROGMEM pin and be zeroed after construction via
`setServo(i, 0, ..., 0)`.

### 10.4 Pulse ranges differ between our two codebases

The fork ships **800-2200 us** per slot (`AstroPixelsPlus.ino:446-451`);
protoArtoo's LEDC defaults are **500-2500 us** (`ledc_pwm.h:49-51`). Not a
conflict, but a body-side PCA9685 backend should not silently inherit either.

## 11. How the three members differ

| | ESP32 LEDC | PCA9685 | Pololu Maestro |
| --- | --- | --- | --- |
| Outputs | 1 GPIO each | 16 per board, up to 62 boards | 6 / 12 / 18 / 24 per board |
| Extra pins | one per servo | **zero** (shares I2C) | one UART |
| Resolution at 50 Hz | 0.305 us | 4.883 us | 0.25 us |
| Ramping | firmware | **firmware** | **onboard hardware** |
| Output kill | per channel | per channel, plus board-wide `OE` | per channel |
| Startup behaviour | firmware | outputs default full-OFF | configurable per channel |
| Timing accuracy | crystal-derived | **per-chip oscillator trim** | crystal-derived |
| Autonomy | none | none | onboard scripting |

**The one-line difference is motion.** A PCA9685 is a dumb PWM emitter; a Maestro
has a motion engine. Pololu documents the units: *"Speed... in units of 0.25 us /
(10 ms)"*, *"Acceleration... in units of (0.25 us) / (10 ms) / (80 ms)"*.

The sharpest evidence is FlthyHPs shipping **both**, its Maestro edition's
changelog naming exactly what the swap bought:

> "Removed software servo easing - replaced with Maestro hardware speed and
> acceleration control (smoother, zero CPU cost)."
> "No OUTPUT_ENABLED_PIN needed - the Maestro handles servo power directly."

Both of those are PCA9685 problems disappearing, not Maestro features appearing.

**Channel economics.** Adafruit 815 at USD 14.95 is about USD 0.93 per channel;
generics are roughly USD 0.69. A Micro Maestro 6 is about USD 4.66 per channel, a
Mini Maestro 24 about USD 2.29. The expander is 2.5x to 5x cheaper per channel and
pushes ramping, timing accuracy and startup behaviour into our firmware.

> [!NOTE]
> **Maestro availability: corrected 2026-09-11.** This sheet previously recorded
> all eight Maestro SKUs as *"Out of stock"* with a Backorder button, read from
> Pololu's category page on 2026-09-10. **That was wrong.** Every product page
> carries `"availability":"InStock"` in its JSON-LD and `Status: Active and
> Preferred`.
>
> The category page emits a hidden
> `<div ... style='display: none'>Out of stock.</div>` for **every** product
> regardless of stock, with real stock injected by JavaScript, and the *"backorders
> allowed"* text is a static link to the ordering policy rather than a stock state.
> Any tool that converts the page to text reports the whole family as out of stock.
> **Read `"availability"` from the JSON-LD, never the rendered page.**
>
> The PCA9685 chip itself is Active at NXP and enrolled in Product Longevity
> (*"available for a minimum of 10 years"*). Maestro pricing and stock now live in
> [`pololu-maestro-servo-controller.md`](pololu-maestro-servo-controller.md)
> Section 2.

## 12. What protoArtoo would have to do

> [!IMPORTANT]
> **This part does not slot into an existing seam. It creates one.** Verified in
> source this session:
>
> | | State today | Evidence |
> | --- | --- | --- |
> | Servo backend interface | **none** -- `servoTask` calls `ledcPwmSetPulseWidth()` directly | `src/tasks/servo_task.cpp:101` |
> | Speed / accel / easing (#286) | **not implemented** | no ramp anywhere in `servo_task.cpp` |
> | Output Release (ADR 0043) | **target only** -- *"the code at `939ed705` implements none of it yet"* | ADR 0043 status line |
> | I2C bus | **never initialised**; no `Wire.` in `src/` or `include/` | grep |
> | I2C pins | declared on both targets, unused | `config.h:232-233`, `:325-326` |
>
> This is the same shape #303 recorded for drive -- *"no interface, no selector"*.

The work, in dependency order:

1. **Bring up an I2C bus.** First one on the body controller. Pick and set a clock
   explicitly (Section 9.4) rather than inheriting 100 kHz.
2. **Introduce the servo backend seam** that #302 and ADR 0042 imply, with LEDC as
   the first member and PCA9685 as the second. This is the bulk of the work and it
   is owed to #286 regardless of whether PCA9685 ships.
3. **Implement Output Release (ADR 0043) on both backends.** On PCA9685 that means
   writing `LED_FULL_OFF_H`, exactly as the dome fork does -- and the ADR's *"a bus
   drop is reported, not escalated"* clause becomes real code here, because a bus
   drop is now possible.
4. **Decide the channel numbering convention and write it down once.** Three exist
   (Section 9.3). Our dome is 1-based Reeltwo; a body implementation using the
   Adafruit library directly would be 0-based. Two conventions in one project is
   how this bug ships a third time.
5. **Do not use `writeMicroseconds()`** if building on the Adafruit library
   (Section 8.1). Convert to ticks and use the clamping path.
6. **Decide whether to trim the oscillator.** Section 7.5 -- if per-channel
   endpoint calibration exists anyway, the error is absorbed and no scope is
   needed. Make that a decision, not an accident.

**Component Protocol.** #303 already names it: **I2C PWM expander**. Nothing in
this research contradicts that, and it passes the "does it change the driver" test.

**Board Capability Gate.** Probably none needed -- I2C pins are declared on both
targets and cost zero additional pins. But unlike the Teeces case this is not free:
the bus does not exist yet, so the gate question is really *"has this board got a
working I2C bus"*, which is a firmware fact rather than a board fact. Confirm
rather than assume.

**Component Member.** This category would hold two selectable members once a
PCA9685 backend exists, which is exactly the condition ADR 0042 sets for a member
setting to appear.

## 13. Ecosystem survey (non-normative)

How the other astromech projects drive servos. Useful because it shows there is no
single hobby default, contrary to the usual assumption.

| Project | Servo hardware |
| --- | --- |
| MarcDuino master/slave | **AVR software PWM, 12 channels max**, Timer1 at 0.5 us |
| SHADOW / SHADOW_MD / Penumbra | **none** -- delegates entirely to MarcDuino over serial |
| ShadowRC | **none** -- only `:SE` sequences |
| Padawan360 (baseline) | **none for panels** -- its only servo file is marked `SERVO CODE NOT YET WORKING` |
| Padawan360 mega_maestro fork | Pololu Maestro at 9600 baud |
| Reeltwo / AstroPixelsPlus | **PCA9685** primary, `ServoDispatchDirect` fallback |
| FlthyHPs | **PCA9685 at 0x40**, with a parallel Maestro edition |

Three conclusions:

**By installed base the hobby default is MarcDuino's own AVR outputs**, not an
expander at all. Every SHADOW-family controller emits `:OPxx` strings and owns no
servo hardware.

**PCA9685 is the default for greenfield firmware** -- the Reeltwo ecosystem and
FlthyHPs. FlthyHPs' manual gives a reason specific to AVR that does not apply to
us: NeoPixel bit-banging disables interrupts and wrecks timer-driven servo PWM, so
offloading to I2C sidesteps it.

**Maestro is the deliberate upgrade after PCA9685 pain**, not an alternative
chosen first.

**Servo count for a full droid, from project sources:** 13 dome panels + 6 holo
axes = **19 in the dome alone**, plus roughly 10 body channels including two
utility arms -- about 29 total. Two facts follow: the dome alone needs two
PCA9685 boards, and `ServoDispatchDirect` on an ESP32-S3 offers only 8 LEDC
channels (6 on a C3), which is the quantitative case for an expander on modern
ESP32 parts.

### The droid I2C address landscape

| Address | Device |
| --- | --- |
| `0x00` | Software reset (general call) |
| `0x0a` | AstroPixelsPlus slave mode (optional, off by default) |
| `0x14` | IA-Parts Magic Panel |
| `0x15` (21) | R-Series LogicEngine |
| `0x16` | PSIPro |
| **`0x19` (25)** | **FlthyHPs, MarcDuino REON1, and Reeltwo `I2CReceiver` default -- a real three-way collision** |
| `0x1A`, `0x1B` | MarcDuino REON3 (top), REON2 (rear) |
| `0x20` | IA-Parts Periscope |
| `0x40`-`0x7F` | **PCA9685 range** |
| `0x70` | PCA9685 LED All Call -- reserved, enabled at power-up |

> [!NOTE]
> **The expander block never collides with the droid gadget block**, because every
> gadget above sits below `0x40`. The `0x19` collision is real and documented by
> FlthyHPs -- *"Both the front REON HP and the FlthyHP system use an I2C address of
> 0x19 (25) and this causes issues"* -- but it does not involve the PCA9685. Good
> news for protoArtoo: adding an expander to a body bus cannot collide with
> anything in this table.

## 14. Agent Lookup Quick Reference

- Field: Chip. Required value: NXP PCA9685, 16 channels, 12-bit.
- Field: Supply `VDD`. Required value: 2.3 V to 5.5 V.
- Field: Board VCC (Adafruit). Required value: 3 V to 5 V; also feeds the 10K I2C pull-ups.
- Field: Board V+ servo rail. Required value: 5-6 V; not used by the chip.
- Field: Output frequency range. Required value: 24 Hz to 1526 Hz, shared by all channels.
- Field: Prescale formula. Required value: `round(osc_clock / (4096 * update_rate)) - 1`.
- Field: Prescale at 50 Hz, 25 MHz. Required value: 121.
- Field: `PRE_SCALE` write precondition. Required value: `SLEEP` must be 1.
- Field: Oscillator. Required value: 25 MHz typical, **no min/max specified**; per-chip trim expected.
- Field: Step size at 50 Hz. Required value: 4.883 us.
- Field: MODE1. Required value: `00h`. MODE2: `01h`. PRE_SCALE: `FEh`.
- Field: Channel registers. Required value: `LEDn_ON_L = 06h + 4n`, through `45h`.
- Field: Full-ON bit. Required value: `LEDn_ON_H[4]`.
- Field: Full-OFF bit. Required value: `LEDn_OFF_H[4]`, **power-on default 1**.
- Field: Full-off via library. Required value: `setPWM(pin, 0, 4096)`.
- Field: Restart settle time. Required value: SLEEP low for at least **500 us** before writing RESTART.
- Field: Base I2C address. Required value: `0x40`, plus the six jumper bits.
- Field: Reserved addresses. Required value: `0x00` software reset, `0x70` all call.
- Field: Max devices on a bus. Required value: 62.
- Field: Max I2C speed. Required value: 1 MHz (Fm+).
- Field: Total output current. Required value: 400 mA across all 16 channels.
- Field: Ramping. Required value: **none in hardware**; host-side interpolation.
- Field: Channel numbering. Required value: project-specific -- Reeltwo 1-based, Adafruit and FlthyHPs 0-based.
- Field: Component Protocol. Required value: **I2C PWM expander**.

If a required value above cannot be proven for the unit in hand, status is
`UNKNOWN` and dependent implementation work should stop.

## 15. Open Items

1. **Board trace and terminal-block current rating.** Adafruit publishes none. The
   chip's 400 mA total is a *signal* limit; V+ is separate copper. Settled by the
   EagleCAD files plus the terminal block datasheet, or a thermal test.
2. **Whether to trim the oscillator per chip.** Section 7.5. A decision, not a
   lookup -- and probably answerable "no" if per-channel endpoint calibration
   exists.
3. **Which I2C clock protoArtoo should run.** Section 9.4 gives the budget; the
   choice interacts with #286's 50 Hz ramp and with any other future I2C device.
4. **Whether the servo backend seam is built for #286 or for #306.** It is owed to
   both. Which ticket carries it is a scheduling decision.
5. **Reeltwo `numberOfPCA9685Chips()` over-allocation.** `numServos / 16 + 1`
   returns 2 for exactly 16 servos, so setup writes MODE1 to a board that need not
   exist. Harmless in our 19-slot config; would matter at exactly 16. Unverified on
   hardware.
6. **Reeltwo bounds-guard inconsistency at the top channel.** The two `setPWM`
   overloads use `<` and `>` against the same array size, so the highest legal
   channel may update its cached length without emitting an I2C write. Derived from
   reading, not observed. A logic analyser would settle it.
7. **Servo signal level at 3.3 V VCC.** Accepted by most hobby servos, guaranteed
   by none. Worth a builder-doc sentence once a real servo set is chosen.
8. **Clone build quality versus Adafruit's.** No vendor or lab source documents it.
   Settled only by buying both and comparing.

## 16. Sources

**Normative (primary)**

- NXP PCA9685 data sheet, Rev. 4, 16 April 2015:
  https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf
- NXP PCA9685 product page (lifecycle status): https://www.nxp.com/products/PCA9685

**Vendor documentation**

- Adafruit product 815: https://www.adafruit.com/product/815
- Adafruit learn guide: https://learn.adafruit.com/16-channel-pwm-servo-driver
  (Pinouts, Assembly, Hooking it Up, Chaining Drivers, Library Reference, FAQ,
  Downloads)
- Waveshare Servo Driver HAT: https://www.waveshare.com/wiki/Servo_Driver_HAT
- Pololu Maestro: https://www.pololu.com/product/1350 and
  https://www.pololu.com/docs/0J40/4.b (speed and acceleration units)

**Library source (read this session)**

- `adafruit/Adafruit-PWM-Servo-Driver-Library` v3.0.3:
  https://github.com/adafruit/Adafruit-PWM-Servo-Driver-Library
- Adafruit issue #40, prescale rounding:
  https://github.com/adafruit/Adafruit-PWM-Servo-Driver-Library/issues/40
- `reeltwo/Reeltwo` `src/ServoDispatch.h`, `src/ServoDispatchPCA9685.h`,
  `src/ServoDispatchDirect.h`, `src/ServoEasing.h`, `src/ServoSequencer.h`:
  https://github.com/reeltwo/Reeltwo

**Our own deployment (primary, and the most valuable)**

- `mattiasbrandt/AstroPixelsPlus` -- `AstroPixelsPlus.ino`, `FORK_IMPROVEMENTS.md`,
  `docs/adr/0004-holo-servosettings-starts-at-pin-17.md`,
  `docs/HARDWARE_WIRING.md`

**Ecosystem survey (primary source, non-normative)**

- https://github.com/nhutchison/MarcDuinoMain (`servo.c`, `servo.h`)
- https://github.com/ryan-sondgeroth/FlthyHPs (both PCA9685 and Maestro editions)
- https://github.com/dankraus/padawan360
- https://github.com/Imperiallandm/Padawan360_mega_maestro_DYSV5W
- https://github.com/RealNobser/ShadowMD
- https://github.com/reeltwo/PenumbraShadowMD
- https://github.com/nhutchison/LogicEngine (`config.h`, I2C address 21)
