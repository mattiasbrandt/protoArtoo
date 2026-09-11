# Pololu Maestro Spec Sheet (Maestro serial)

Working spec for the **Pololu Maestro** servo controller family as a **Body servo
controller** lineup member
([#307](https://github.com/mattiasbrandt/protoArtoo/issues/307), minted from
[#303](https://github.com/mattiasbrandt/protoArtoo/issues/303)), reached over the
**Maestro serial** Component Protocol that #303 assigned it.

Research date 2026-09-11. Every command byte, unit, default, electrical value and
firmware revision below was read this session from the Pololu Maestro Servo
Controller User's Guide (0J40, 102 pages, `pdftotext -layout`), from Pololu's SDK
and Arduino library source, from the Linux configuration binaries, or from the
astromech projects on disk. Nothing is recalled. Claims that could not be sourced
are marked `UNKNOWN` with the artefact or bench test that would settle them.

> [!CAUTION]
> **The serial protocol is commands, never configuration.** The Maestro has
> exactly thirteen serial commands and **not one writes a stored setting**. Baud,
> device number, CRC, the serial timeout, each channel's mode, its travel limits,
> its 8-bit neutral and range, the servo period, and what every channel does **on
> startup or on error** are set once from a PC over USB and are invisible to
> firmware from then on. There is no command to read them back either. Pololu's own
> SDK header proves the split: serial commands are `enum uscCommand`, while
> configuration is `REQUEST_SET_PARAMETER` in `enum uscRequest`, *"values to put in
> to bRequest when making a setup packet for a **control transfer**"* (Section 7.2).
>
> This is the same shape as the VESC's and Sabertooth's timeout findings, and the
> strongest instance of it on the lineup: here the thing firmware cannot arm is
> **the servo failsafe itself** (Section 10.1).
>
> The one exception is easy to miss: **speed and acceleration *are* serial
> commands**. The motion engine is fully reachable at runtime. Everything else is a
> PC's business.

> [!NOTE]
> **Two pieces of good news that change the shape of the problem.**
>
> **The USB step can be a repo artefact.** Pololu ships `UscCmd` for Linux and
> Windows: `--configure FILE` *"load configuration file into device"*,
> `--getconf FILE` *"read device settings and write configuration file"*,
> `--program FILE` for the script. The settings file is versioned XML. So a
> Maestro's whole configuration can be checked in, applied with one command, and
> **diffed against what a builder's board actually holds** (Section 9.3).
>
> **The factory startup default is already ours.** *"On startup or error, the
> servos turn off (no pulses are sent)"* is exactly
> [ADR 0052](../adr/0052-a-parts-motion-is-set-in-time-not-in-rate.md)'s **limp by
> default** and
> [ADR 0043](../adr/0043-estop-releases-servo-outputs-rather-than-closing-them.md)'s
> **Output Release**. Of everything on this lineup, the Maestro is the only part
> whose out-of-box behaviour matches a decision we reached independently.

> [!WARNING]
> **This is the first part on the lineup that needs a level shifter.** Pololu,
> twice: *"it is not guaranteed to read 3.3V as high on the RX pin, so you should
> boost 3.3V TTL serial signals to above 4V"*, and its TX drives 0-5 V push-pull
> into whatever is listening. The VESC's logic level is *"why this one is easy"*
> and the Sabertooth's is *"3.3 V TTL serial on the same header"*; this one is
> neither (Section 5.3).

## Where this sits in the lineup

| Product | How it drives a servo | Status |
| --- | --- | --- |
| ESP32 GPIO (LEDC) | MCU peripheral, one timer channel per output | `supported` |
| PCA9685 | I2C expander, 16 outputs per board | `roadmap` |
| **Pololu Maestro** | Serial controller with an onboard motion engine and a script engine | `roadmap` |

These are peers with different shapes. The Maestro is not the PCA9685's upgrade
and the PCA9685 is not the Maestro's budget option: one costs a bus we already
have and gives no motion engine, the other costs the scarcest bus on the board and
gives a complete one. Which a droid carries is a fact about what the builder
bought.

The difference in kind is **where the ramp runs**. Under ADR 0052 protoArtoo put
its motion model in firmware; a Maestro arrives with one already built, in
different units, on a tick we do not run. Section 8 works that collision out and
Section 14.1 answers the architectural fork #307's body asks about.

## 0. Authority Contract

This document is an implementation authority for the Maestro serial protocol, its
configuration surface, and its motion engine's arithmetic.

Authority order:

1. **The Maestro Servo Controller User's Guide (0J40)** -- the product's own
   document -- for the protocol, the settings, the defaults and the errata.
2. **Pololu's shipped source and binaries** (`protocol.h`, `Sequence.cs`,
   `UscCmd`, `Usc.dll`) for what the tooling does. They are newer than the guide
   (package release 2026-08-10 against a 2022 guide) and carry facts it does not.
3. This document.
4. **Pololu's Arduino library** -- evidence of practice, **not** correctness.
   Section 12.1 lists three defects in it.
5. Astromech project source (Section 13).

If references conflict: prefer the guide for protocol and settings; prefer the
binaries for tooling; for the motion engine prefer measurement, and until someone
measures, prefer Section 8.2's derivation, which reproduces four of the five
published figures. Otherwise mark `UNKNOWN` and stop dependent work.

Agent requirements:

- MUST NOT look for a serial command that writes a setting. There are thirteen and
  none does (Section 7.2).
- MUST NOT assume firmware can arm the serial timeout (Section 10.1).
- MUST NOT send compact-protocol commands to a factory-fresh Maestro without first
  sending `0xAA`. Pololu's own library never sends it in compact mode, and a
  factory device is in detect-baud (Section 6.3).
- MUST NOT assume a commanded pulse width arrives. A factory device's limits are
  **992-2000 us** against protoArtoo's **500-2500 us** (Section 9.2).
- MUST NOT convert a travel time to a speed by dividing. Speed and acceleration
  jointly set the time, and below a threshold distance speed has **no effect at
  all** (Section 8.3).
- MUST NOT treat `Get Moving State` as available -- Mini only, and documented as
  buggy on the Micro (Section 11).
- MUST distinguish the variants. Three commands, the script size, the input types
  and the pulse range all differ between Micro and Mini (Section 2).

## 1. Scope

Covers the four variants, their electrical characteristics, the serial modes and
sub-protocols, the complete command set, the motion engine and its arithmetic, the
USB-only settings and the tooling that reaches them, the safety model, the
firmware errata, the host libraries, what the hobby has built, and what protoArtoo
would have to do.

Does not cover: the scripting language's full command reference (Section 8.5
covers only what a host needs), the USB SDK's native interface (protoArtoo is not
a PC), the Maestro as a USB-to-TTL adapter, the analog and digital input modes
beyond what `Get Position` returns, or `Set PWM` (`0x8A`) -- a Mini-only feature
with no role in this category.

## 2. Products Covered

Four variants, four firmwares (`usc02a`, `usc03a`, `usc03b`, `usc03c`), each sold
assembled or as a partial kit; the kit difference is connectors only. USB VID
`0x1FFB`, PIDs `0x0089`-`0x008C`, mini-B, cable not included.

| | Micro Maestro 6 | Mini Maestro 12 | Mini Maestro 18 | Mini Maestro 24 |
| --- | --- | --- | --- | --- |
| Item # assembled / kit | 1350 / 1351 | 1352 / 1353 | 1354 / 1355 | 1356 / 1357 |
| Price assembled / kit | 27.95 / 26.95 | 37.95 / 36.49 | 46.95 / 44.95 | 54.95 / 52.49 |
| Per channel (assembled) | USD 4.66 | USD 3.16 | USD 2.61 | USD 2.29 |
| Channels | 6 | 12 | 18 | 24 |
| Analog / digital inputs | 6 / 0 | 12 / 0 | 12 / 6 | 12 / 12 |
| Size (inches) | 0.85 x 1.20 | 1.10 x 1.42 | 1.10 x 1.80 | 1.10 x 2.30 |
| Weight, bare | 3.0 g | 4.2 g | 4.9 g | 6.0 g |
| Pulse rate | 33-100 Hz | 1-333 Hz | 1-333 Hz | 1-333 Hz |
| Pulse range | 64-3280 us | 64-4080 us | 64-4080 us | 64-4080 us |
| Script size | 1 KB | 8 KB | 8 KB | 8 KB |
| Script stack / call stack | 32 / 10 | 126 / 126 | 126 / 126 | 126 / 126 |
| `Set Multiple Targets` `0x9F` | **no** | yes | yes | yes |
| `Get Moving State` `0x93` | **buggy** | yes | yes | yes |
| `Set PWM` `0x8A` | no | yes | yes | yes |
| `ERR` / `TXIN` pins | **neither** | both | both | both |
| Latest firmware | **1.04** | 1.03 | 1.03 | 1.03 |

> [!IMPORTANT]
> **The Micro Maestro is a different device, not a smaller one.** It lacks the
> command that makes streaming affordable, its motion-complete query is broken, its
> script memory is an eighth the size, and it cannot be chained without an external
> AND gate. For a droid the decisive comparison is 6 channels against protoArtoo's
> twenty-three-Output model.

> [!NOTE]
> **Availability, checked 2026-09-11: all eight SKUs are in stock, and the
> opposite claim in a sheet already on `main` was wrong.** Every product page
> carries `"availability":"InStock"` in its JSON-LD and `Status: Active and
> Preferred`.
>
> **Pololu's server-rendered HTML carries no usable stock signal and reads as the
> opposite of the truth.** Two traps, both verified in the raw HTML this session:
> the category page emits
> `<div data-product-ui='short_order_form_out_of_stock_message' style='display: none'>Out of stock.</div>`
> for **every** product regardless of stock, with real stock injected by
> JavaScript -- so any tool that converts the page to text, a fetch-and-summarise
> agent included, strips the `style` and reports the whole family as out of stock;
> and the product page's *"backorders allowed"* is a static link to
> `/ordering#backorders`, an ordering policy rather than a stock state, which reads
> as corroboration of the first trap.
>
> Both fooled this research on its first pass. **Read `"availability"` from the
> JSON-LD, never the rendered page.**
> [`pca9685-servo-expander.md`](pca9685-servo-expander.md) Section 11 recorded the
> out-of-stock reading on 2026-09-10 and is corrected in the same change that adds
> this sheet.

## 3. Project Integration

- **[`include/config.h`](../../include/config.h)** -- the UART budget this part
  competes for: artoo-esp32's three controllers (`:146-148`, dome and audio sharing
  one), firebeetle2's five (`:265-267`) with `:257` recording *"UART4 unclaimed by
  the firmware"*.
- **[`include/ledc_pwm.h`](../../include/ledc_pwm.h)** -- `SERVO_PULSE_MIN_US 500`
  / `_MAX_US 2500` / `_NEUTRAL_US 1500` (`:49-51`), the range a factory Maestro
  does not have (Section 9.2).
- **[`src/drivers/audio_soft_uart_tx.h`](../../src/drivers/audio_soft_uart_tx.h)**
  -- the existing bit-banged TX and its measured cost, *"~1.04 ms per byte"* in a
  Core 0 critical section (Section 14.3).
- **[`src/tasks/servo_task.cpp`](../../src/tasks/servo_task.cpp)** -- `:101`,
  `ledcPwmSetPulseWidth()` called directly. The seam this part would need does not
  exist (Section 14.7).
- **[`docs/pin_map.md`](../pin_map.md)** -- artoo-esp32's GPIO table and the
  CH3-CH6 pins unused in SBUS modes (`:177`); firebeetle2's *"15 usable GPIOs
  against a demand of 14 -- one spare"* (`:307-310`).
- **ADR 0041 / 0042 / 0043 / 0052** -- Endpoint Pair, Component Member, Output
  Release, Motion Profile. ADR 0052 explicitly rejects *"Maestro units, as the
  reference stores them"*; Section 8.4 is the conversion that rejection implies.
- **`CONTEXT.md`** -- **Motion Profile**, **Output Release**, **Cadence Floor**,
  **Servo Output**. The Cadence Floor entry is load-bearing for Section 14.1.

## 4. Sources Checked

| Source | What it gave |
| --- | --- |
| Maestro User's Guide 0J40, (c) 2001-2022 -- https://www.pololu.com/docs/0J40 | **Authoritative.** Command set, serial modes, sub-protocols, CRC-7, motion units, every factory default, the error register, firmware history, pulse-rate and baud limits |
| Product pages 1350-1357 and the RC servo controller category | Prices, specifications, `"availability":"InStock"`, and the stock-markup trap |
| `maestro-linux` package, release 2026-08-10 | **Downloaded and unpacked.** `UscCmd`'s option list, the Mono prerequisite, the udev rule, the XML settings format, and errata strings the guide lacks |
| `pololu/maestro-arduino`, commit `bcbd0c06` | **Read in full.** Three defects; confirmation that compact mode never emits `0xAA` |
| `pololu/pololu-usb-sdk` -- `Maestro/protocol.h`, `Maestro/Sequencer/Sequence.cs` | The command/request/parameter split that proves configuration is USB-only, and the script generator's `return` trap |
| `Padawan360_mega_maestro_DYSV5W` (local) | The hobby's real grammar: eight buttons, eight `restartScript(n)`, SoftwareSerial at 9600 |
| `r2d2-astromech-simulator/arduino/MaestroPCA` (local) | A reimplementation of the protocol and kinematics with an ESP32 backend: independent confirmation of the motion units, and a documented parser exploit |
| `BitMarkus/ESP32_Hexapod` | The only working ESP32-plus-Maestro integration found, and a field confirmation of Section 5.3 |
| `ShadowMD`, `CHIRP`, `AstroPixelsPlus`, MarcDuino (local and cloned) | **Negative results**, each grep validated against a known positive (Section 13.2) |

## 5. Electrical

### 5.1 Two supplies, and the Maestro regulates neither

*"The processor and the servos can have separate power supplies."* Processor power
is USB or **5-16 V** on `VIN`, and *"if the external supply falls below 5 V,
correct operation is not guaranteed, even if USB is also connected."* Supply
current is 30 mA (Micro) / 40 mA (Mini), about 10 mA more with USB attached.

Servo power *"is passed directly to the servos without going through a regulator,
so the only restrictions on your servo power supply are that it must be within the
operating range of your servos and provide enough current"* -- Pololu's sizing note
being *"a ballpark figure for the current draw of an average straining servo is
1 A"*. The Maestro does not regulate, sense, limit or report servo current, and
USB cannot power servos: *"A USB port might only be capable of supplying 100 mA,
which is less than what you need for a single servo."*

The rails can be joined -- a solder bridge on the Micro, the included
**`VSRV=VIN` shorting block** on the Mini. That does not change
`docs/pin_map.md`'s existing advice for the FireBeetle 2's servo field (*"Power
servos and ESCs from a separate BEC"*); it moves where the servo rail terminates.

### 5.2 The 5 V output is not a power supply

| | Regulator | Board draws | Left for you | Signal-pin total |
| --- | --- | --- | --- | --- |
| Micro Maestro 6 | 50 mA | 30 mA | *"about 20 mA"* | 60 mA, 220 ohm series |
| Mini Maestro 12/18/24 | 100 mA | 50 mA | *"about 50 mA"* | 200 mA |

Enough for a level shifter and nothing else. When the on-board regulator is the
source, signal-pin current out is capped at the regulator's spare rather than the
pin total. (The guide states the 220 ohm protection for the Micro only and is
silent for the Mini; Pololu publishes no schematic -- Open Item 6.)

### 5.3 Logic level, where this part is genuinely harder than its peers

Pololu says it twice, identically, once per board family:

> "Note that the Maestro will probably be able to receive 3.3V TTL serial bytes,
> but it is not guaranteed to read 3.3V as high on the RX pin, so you should boost
> 3.3V TTL serial signals to above 4V if you want to ensure reliable operation."

Section 5.b sets the window -- *"a logic-level (0 to 4.0-5 V, or 'TTL'),
non-inverted serial signal... should not go below 0 V and should not exceed 5 V"*
-- so **the specified input-high band starts at 4.0 V** and 3.3 V is outside it.
The return path is the mirror image: *"The Maestro provides logic-level (0 to 5 V)
serial output on its serial transmit line, TX."*

**TX is push-pull, in every mode.** The guide never uses the words, but guide section 5.g is
decisive: *"the TX outputs are driven high when not sending data, so they cannot
simply be wired together. Instead, you can use an AND gate."* A wire-OR-able
output would need neither that sentence nor the Mini's built-in `TXIN` gate.

So correct wiring is **asymmetric**:

| Direction | Problem | Fix |
| --- | --- | --- |
| ESP32 TX -> Maestro RX | 3.3 V may not read high | boost above 4 V: a real shifter, or open-drain with a pull-up to 5 V |
| Maestro TX -> ESP32 RX | 5 V into a non-tolerant pin | divide, or shift down |

Two consequences. **A resistor divider is only half a solution** -- it handles
Maestro-to-ESP32 and cannot raise a voltage, so a bidirectional link needs a real
shifter. And **not wiring the return path removes half the problem**: *"If you
aren't interested in receiving TTL serial bytes from the Maestro, you can leave the
TX line disconnected"*, at the cost of `Get Position`, `Get Errors`,
`Get Moving State` and `Get Script Status` (Sections 10.4, 10.5, 14.3).

> [!NOTE]
> Pololu's Arduino README makes the same point about a 3.3 V board: the Due
> *"should not be directly connected to the Maestro's 5 V TX line."* The vendor is
> not claiming this works and being wrong; it is telling you it does not. Section
> 13.3 is a builder who found out independently.

### 5.4 `RST`, `ERR` and `TXIN`

**`RST`** is *"internally pulled high, so it is safe to leave this pin
unconnected"*, and pulling it low is *"roughly equivalent to powering off the
Maestro; it will not reset any of the configuration parameters stored in
non-volatile memory."* Pololu's guidance is to *"have the I/O line tri-stated or at
5 V when you want the Maestro to run and drive it low temporarily"* -- so it is the
one Maestro signal a 3.3 V host can own directly, open-drain, with no shifter.

**`ERR`** (Mini only) *"is driven high when the red LED is on, and it is a pulled
low through the red LED when the red LED is off. Since the ERR line is never driven
low, it is safe to connect the ERR line of multiple Mini Maestros together."*
Driven high means **5 V**, so it needs the same divider as TX -- but it buys an
error indication with **no serial traffic and no return path**, which Section 10.4
argues is the best value on the board.

**`TXIN`** (Mini only): *"Any serial bytes received on this line will be buffered
through an AND gate and transmitted on the TX line"* -- the built-in replacement
for the external gate a response-capable chain otherwise needs.

## 6. Serial modes

### 6.1 Three modes, and only one is ours

| Mode | RX / TX carry | Needs a PC? |
| --- | --- | --- |
| USB Dual Port | a general-purpose serial passthrough; commands arrive over USB | yes |
| USB Chained | Command Port bytes; RX is forwarded to the PC, **not parsed as commands** | yes |
| **UART** | *"commands to the Maestro and responses from it"* | **no** |

The command set is identical in all four (UART splits into detect-baud and
fixed-baud); what changes is where commands arrive. In UART mode an attached USB
cable still gives an RX-only Command Port *"available on the PC for debugging"* --
a genuinely useful bench affordance, since a builder can watch what protoArtoo
sends, on the Maestro's own USB, while the droid runs.

Fixed baud spans *"300 - 200,000 bps"*, detect *"300 - 115,200 bps"*.

### 6.2 Detect-baud, and the byte that must come first

> "If your Maestro's serial mode is 'UART, detect baud rate', you must first send
> it the baud rate indication byte 0xAA on the RX line before sending any commands.
> The 0xAA baud rate indication byte can be the first byte of a Pololu protocol
> command."

Until it arrives the device is not listening, and says so: *"the yellow LED will
blink slowly. During this time the Maestro does not transmit any servo pulses."*
Good diagnostic, bad default -- **a factory-fresh Maestro never answers a compact
command until something sends `0xAA`.**

### 6.3 The library never sends it, and that is the trap

```cpp
void Maestro::writeCommand(uint8_t commandByte)
{
  if (_deviceNumber != deviceNumberDefault)
  {
    writeByte(baudRateIndication);      // 0xAA -- Pololu protocol only
    write7BitData(_deviceNumber);
    write7BitData(commandByte);
  }
  else
  {
    writeByte(commandByte);             // compact -- no 0xAA, ever
  }
}
```

`deviceNumberDefault` is `255` and it is the constructor's default, so the
idiomatic `MiniMaestro maestro(Serial1);` -- what every example and every Padawan360
fork writes -- **selects compact and therefore never emits the baud-detect byte.**
Against a factory device nothing happens, with no error.

Three ways out:

1. **Pass a device number.** `MiniMaestro(Serial1, noResetPin, 12)` uses the Pololu
   protocol, which begins every packet with `0xAA`, so detect-baud resolves on the
   first command. Costs two bytes per command.
2. **Set a fixed baud over USB.** Removes the problem at the device; costs a
   provisioning step and makes the baud invisible to firmware.
3. **Open with one complete Pololu-protocol command**, then use compact. Exactly
   what the guide describes, and the factory device number is 12, so
   `0xAA 0x0C 0x22` (Go Home) both resolves the baud and puts every channel in a
   known state.

   A **bare** `0xAA` with no packet behind it is *not* established as safe: it
   leaves the Maestro expecting a device-number byte, and what it does with the
   high-bit command byte that follows is `UNKNOWN`. The errata's *"an unfinished
   command packet is interrupted by another command packet"* suggests it resyncs
   and flags bit 4, but whether the interrupting command is **executed or
   discarded** is not stated (Open Item 3).

**protoArtoo should do 1 or 3, not 2.** A driver that only works against a
specifically pre-configured device fails in the field with no diagnosis, and
Section 9.3 exists precisely because we cannot assume the provisioning step
happened correctly. Option 1 is the least clever: pay two bytes and never think
about baud detection again.

## 7. The command protocol (normative)

### 7.1 Three sub-protocols, spoken simultaneously

*"The Maestro identifies the Pololu, Compact, and Mini-SSC protocols on the fly;
you do not need to use a configuration parameter... and you can freely mix
commands in the three protocols."*

| | Frame | Addressed? | CRC? |
| --- | --- | --- | --- |
| **Compact** | `cmd`, data... | no | if enabled |
| **Pololu** | `0xAA`, `device`, `cmd & 0x7F`, data... | yes, 0-127, default **12** | if enabled |
| **Mini SSC** | `0xFF`, `channel`, `target8` | via Mini SSC offset | **never** |

Framing: *"Command bytes always have their most significant bits set (128-255...)
while data bytes always have their most significant bits cleared (0-127...)"*,
Mini SSC's target excepted. Fourteen-bit values go **low seven bits first**:
`target & 0x7F` then `(target >> 7) & 0x7F`.

**`Get Position`'s reply does not follow that rule** -- *"the position is
formatted as a standard little-endian two-byte unsigned integer"*. Two encodings
in one protocol; a decoder that reuses the command packer on the reply is wrong by
a value-dependent factor.

### 7.2 Thirteen commands, and none of them configures

| Command | Compact | Args | Reply | Variants |
| --- | --- | --- | --- | --- |
| Set Target | `0x84` | ch, target14 | -- | all |
| Set Target (Mini SSC) | `0xFF` | ch, target8 | -- | all |
| Set Multiple Targets | `0x9F` | count, first ch, target14 x count | -- | **Mini only** |
| Set Speed | `0x87` | ch, speed14 | -- | all |
| Set Acceleration | `0x89` | ch, accel14 | -- | all |
| Set PWM | `0x8A` | ontime14, period14 | -- | **Mini only** |
| Get Position | `0x90` | ch | 2 B, little-endian | all |
| Get Moving State | `0x93` | -- | 1 B | **Mini only**, buggy on Micro |
| Get Errors | `0xA1` | -- | 2 B, **and clears** | all |
| Go Home | `0xA2` | -- | -- | all |
| Stop Script | `0xA4` | -- | -- | all |
| Restart Script at Subroutine | `0xA7` | sub | -- | all |
| ...with Parameter | `0xA8` | sub, param14 | -- | all |
| Get Script Status | `0xAE` | -- | 1 B | all |

> [!IMPORTANT]
> **Read that table for what is absent**: no `Set Baud`, no `Set Device Number`,
> no `Set Channel Mode`, no `Set Min`/`Max`, no `Set Startup Behaviour`, no
> `Set Period`, no `Set Timeout`, no `Enable CRC`, and no `Get Settings` of any
> kind. A host commands the device and reads four runtime values. It cannot
> configure it and cannot discover how it is configured.
>
> **Pololu's own SDK header proves the split.** `Maestro/protocol.h` carries two
> separate enumerations: `uscCommand`, the serial commands above, and
> `uscRequest`, introduced as *"values to put in to bRequest when making a setup
> packet for a **control transfer**"* -- which is where `REQUEST_SET_PARAMETER =
> 0x82`, `REQUEST_ERASE_SCRIPT`, `REQUEST_WRITE_SCRIPT` and `REQUEST_REINITIALIZE`
> live. **Configuration is USB, by construction.**
>
> The near-exception: **speed and acceleration have two homes** -- a stored startup
> value (USB) and a runtime value (`0x87`/`0x89`). The runtime value does not
> survive a reset. Nothing else has a runtime home.

Two command behaviours worth pulling out:

- **`Get Errors` clears the register**: *"the error bits are cleared"* after the
  reply. A poll is destructive and two readers cannot share the link.
- **`Go Home` is settings-dependent**: *"For servos and outputs set to 'Ignore',
  the position will be unchanged."* With any channel on `Go to` it is a mass move,
  not a release (Section 10.3).

### 7.3 The framing gives free resynchronisation

Because data bytes are seven-bit, **a high-bit byte can only be a command byte**.
A decoder seeing one mid-packet knows the packet was truncated -- by noise, a
dropped byte, a slip ring -- and should abandon it rather than eat it as an
argument and corrupt both. `MaestroPCA` names the principle: *"This self-resync is
exactly what the protocol's 7-bit data encoding exists to allow."* The guide is
consistent -- a *"Serial protocol error (bit 4)"* covers *"an unfinished command
packet... interrupted by another command packet"*. **A protoArtoo decoder must do
the same.**

### 7.4 CRC-7, and why it stays off

Optional, off by default, enabled only from the Control Center. Polynomial
**`0x91`**, computed **LSB first** *"to match the order in which the bits are
transmitted"* -- the guide notes this matches the jrk and qik but **differs from
the TReX**. Appended to every packet except Mini SSC; a wrong CRC sets bit 3 and
the command is ignored. *"The Maestro does not append a CRC byte to the data it
transmits"*, so replies are unprotected either way.

**Leave it off.** Firmware cannot tell whether it is on, and a mismatch is silent
in one direction (device expects CRC, host sends none: every command fails) and
invisible in the other (host appends one, device ignores it as a stray data byte
and the packets still work). If it is ever enabled, enable it in the checked-in
settings file so the two ends cannot disagree.

## 8. The motion engine

The Maestro's motion engine has no analogue elsewhere on the lineup, and it
collides with a decision protoArtoo has already made.

### 8.1 The units

| | Unit | Range | Zero means |
| --- | --- | --- | --- |
| Target | quarter-microseconds | 0-16383 | **stop pulsing** -- limp |
| Speed | (0.25 us) / (10 ms) | 0-16383 | unlimited |
| Acceleration | (0.25 us) / (10 ms) / (80 ms) | **0-255** | unlimited |

Fixed beyond argument by the guide's own conversions: speed 140 *"corresponds to
a speed of 3.5 us/ms"* (140 x 0.25/10 ms = 3.5 us/ms), and acceleration 4 gives
*"1250 us/s every second"* (4 x 0.25 us per 10 ms per 80 ms).

> [!WARNING]
> **The units change if the servo period is not 20 ms** (Mini only): at
> `T = 3-19 ms` speed is `(0.25 us)/T` and acceleration `(0.25 us)/T/(8T)`; above
> 20 ms, `(0.25 us)/(T/2)` and `(0.25 us)/(T/2)/(4T)`. **Period is a USB-only
> setting**, so firmware cannot read it and cannot know which row it is in. Every
> number below assumes the factory 20 ms. A builder who raised the pulse rate for
> faster digital servos silently rescaled every speed and acceleration we send.

### 8.2 What a move takes, derived

The guide gives no formula, only worked examples. One is needed because ADR 0052
stores a **time** and the Maestro takes **rates**.

Let `D` be distance in quarter-us, `S` the speed setting, `A` the acceleration
setting, at the factory 20 ms period. Acceleration raises velocity by `A`
(quarter-us per 10 ms) every 80 ms, so `t_acc = 80*S/A` ms and the ramp covers
`4*S^2/A` quarter-us. The profile is symmetric, so full speed is reached only if
`D >= 8*S^2/A`.

```
trapezoidal (D >= 8*S^2/A):   T = 80*S/A + 10*D/S              ms
triangular  (D <  8*S^2/A):   T = 160*sqrt(D/(8*A))            ms
speed only  (A = 0):          T = 10*D/S                       ms
```

Checked against every published number that exists for this engine:

| Source | Setting | Distance | Published | Derived |
| --- | --- | --- | --- | --- |
| 0J40, Set Speed | S=1, A=0 | 4000 qus | *"40 seconds"* | **40.0 s** |
| 0J40, Set Speed | S=140, A=0 | 1400 qus | *"100 ms"* | **100 ms** |
| ADR 0052 / `CONTEXT.md` | S=80, A=10 | 2752 qus | *"~344 ms speed alone"* | **344 ms** |
| ADR 0052 / `CONTEXT.md` | S=80, A=10 | 2752 qus | *"really ~940 ms"* | **938 ms** |
| 0J40, Set Acceleration | S=0, A=1 | 4000 qus | *"about 3 seconds"* | 3.58 s |

Four land exactly; the fifth is the guide's loosest wording, against a derivation
that reproduces the reference's independently **measured** 940 ms to within 0.2 %.

> [!NOTE]
> **Confirmed against a second implementation.** `MaestroPCA` reimplements these
> kinematics independently: `src/MaestroPCA.cpp:571` is
> `int32_t a = (int32_t)accel << 5;` with the comment *"accel x 256 / 8
> ticks-per-80ms"* -- velocity rising by `A/8` per 10 ms tick, i.e. `A` per 80 ms.
> Its decel envelope at `:586`, `128 * isqrt32(accel * dq)`, is `sqrt(2ad)` in
> 1/256 units for the same `a`.

Remaining uncertainty is quantisation: whether the engine updates velocity every
10 ms or every servo period is not stated, so treat the formulas as accurate to
about one tick until someone scopes a channel.

### 8.3 Acceleration binds, and speed can do nothing at all

In the triangular case **`S` does not appear**. Below `8*S^2/A` the servo never
reaches its speed limit, so raising speed changes nothing. That is the general
form of the lesson `CONTEXT.md` records as the reference's bench finding --
*"speed 80 with acceleration 10 reads as ~344 ms and is really ~940 ms"*.
Increasing speed past `sqrt(A*D/8)` buys exactly nothing; with `A = 10` and
`D = 2752` that ceiling is **59**.

For droid panels this is the normal case, not the corner.

> [!CAUTION]
> **A raw Maestro speed slider lies to the operator over most of its range.**
> ADR 0052 chose times over rates to avoid exactly this -- *"mis-reading a rate as
> a time is the specific error the prior art paid for"*. Any surface exposing raw
> rates must show the derived time beside them.

### 8.4 Converting ADR 0052's times into Maestro rates

Given throw distance `D` (from the **Endpoint Pair**), a full-throw time
`T_throw` and an acceleration time `T_acc`:

```
S = 10*D / (T_throw - T_acc)        A = 80*S / T_acc
```

valid only while `T_throw >= 2*T_acc`; below that the move is triangular, `S` is
inert, and the solve is `A = 3200*D / T_throw^2`. Clamp `S` to 0-16383 and **`A`
to 0-255** -- acceleration is *"a value from 0 to 255"*, so short acceleration
times on long throws saturate, and a driver that clamps silently delivers a slower
move than was asked for. The surface must say so, as ADR 0052 makes a degraded
overshoot say so.

Three consequences a driver must handle rather than discover:

1. **Recalibration changes both numbers.** `D` comes from the Endpoint Pair, and
   ADR 0052 requires the stored time to survive a recalibration -- *"the door
   should still take about a second"*. On a Maestro that means recomputing and
   re-sending `S` and `A` whenever an endpoint moves. On LEDC it is free.
2. **`S` and `A` are device state, lost on reset.** *"On startup, there are no
   speed or acceleration limits."* A Maestro that browns out mid-show returns with
   every channel **unlimited** -- full-speed moves on parts whose profile said
   otherwise. The whole per-channel profile must be re-sent after any reset, and
   detecting one without the return path is inference.
3. **`soft` and `overshoot` do not exist on the device.** The Maestro offers one
   shape, the trapezoid, which is ADR 0052's `none`. The other two require
   streaming -- the architectural fork, arriving from a direction #307 does not
   mention.

### 8.5 The script engine, as a host sees it

Four commands reach it: `Stop Script` (`0xA4`), `Restart Script at Subroutine`
(`0xA7`), the same with a parameter (`0xA8`), and `Get Script Status` (`0xAE`,
`0` running / `1` stopped). Scripts are a stack language *"very similar to
FORTH"*, compiled to bytecode. **No serial command loads one** -- writing is
`REQUEST_ERASE_SCRIPT` / `REQUEST_WRITE_SCRIPT` over USB, or `UscCmd --program`.

Three host-visible constraints:

- **Subroutine numbers are positional** -- *"numbered in the order they are
  defined in your script, starting with 0"* -- so inserting one renumbers the rest,
  and a host holding slot numbers holds a fragile contract against a file it
  cannot read.
- **Only the first 128 subroutines are serially reachable**, on every variant.
- **A serially-started subroutine must not `RETURN`**: *"they should contain
  infinite loops or end with a QUIT command."* Getting this wrong underflows the
  call stack (bit 7) -- and Pololu's own tooling gets it wrong (Section 13.6).

**One script runs at a time**, and a new `Restart Script at Subroutine` pre-empts
whatever is playing. `Get Script Status` plus `0xA7` is the guide's own idiom for
"start a motion and wait for it to finish", including as the workaround for the
Micro's broken `Get Moving State` -- and it needs the return path.

## 9. The settings that exist only over USB

### 9.1 The list

All stored in non-volatile memory, all set only from a PC, none readable by
firmware. Parameter names are from Pololu's `protocol.h`.

| Setting | Why it matters to us |
| --- | --- |
| Serial mode (`SERIAL_MODE`) | must be **UART** or nothing we send is a command |
| Baud: detect or fixed (`SERIAL_FIXED_BAUD_RATE`) | decides whether `0xAA` is required |
| Device number, default **12** (`SERIAL_DEVICE_NUMBER`) | the Pololu protocol's address |
| Mini SSC offset (`SERIAL_MINI_SSC_OFFSET`) | the Mini SSC protocol's address base |
| CRC enable (`SERIAL_ENABLE_CRC`) | a silent mismatch either way (Section 7.4) |
| **Serial timeout** (`SERIAL_TIMEOUT`) | **the servo failsafe** (Section 10.1) |
| Never sleep (`SERIAL_NEVER_SUSPEND`) | only relevant on USB power |
| Channel mode (`IO_MASK_*` / channel modes) | a channel in the wrong mode ignores targets |
| **Min / Max** (`SERVOn_MIN`/`_MAX`) | **992 / 2000 us by default** (Section 9.2) |
| **Startup/error behaviour** (`SERVOn_HOME`) | the whole safety story (Section 10.2) |
| Startup speed / acceleration (`SERVOn_SPEED`/`_ACCELERATION`) | in force before firmware speaks |
| 8-bit neutral and range (`SERVOn_NEUTRAL`/`_RANGE`) | what a Mini SSC target means |
| Period and multiplier (`SERVO_PERIOD`) | **rescales speed and acceleration** (Section 8.1) |
| Run script at startup (`SCRIPT_DONE`) | *"if 0, run the bytecode on restart"* |
| The user script | what `0xA7` triggers |

**Two encodings in that table are traps**, read from the same header:

- `SERVOn_MIN`/`_MAX` are *"1 byte... (x2^6)"* -- travel limits quantise to
  **64 quarter-us = 16 us** steps. Both factory defaults land exactly on the grid
  (992 us = 62 x 64 qus; 2000 us = 125 x 64), confirming it. Harmless for a fence,
  but it is a clamp, not a setpoint.
- The stored speed is *"1 byte (5 mantissa, 3 exponent) **us per 10ms**"* and the
  stored acceleration *"speed changes that much every **10ms**"* -- different
  encodings and apparently different base units from the serial commands'
  quarter-us and 80 ms. See the warning in Section 11.

### 9.2 The factory defaults, and the two that collide with protoArtoo

Prefaced by Pololu's own reassurance: *"Without using USB, you will not be able to
change the Maestro's settings, but you can use the default settings which are
suitable for many applications."*

UART **detect baud rate**; device number **12**; Mini SSC offset **0**; timeout
and CRC **disabled**; all channels servos, **min 992 us, max 2000 us**; 8-bit
neutral **1500 us**, range **476.25 us**; **on startup or error the servos turn
off**; no speed or acceleration limits; period **20 ms**; script empty.

> [!CAUTION]
> **The default pulse range is narrower than ours, and the mismatch is silent.**
> `include/ledc_pwm.h:49-50` sets 500-2500 us. A factory Maestro clamps at
> **992-2000 us**. Most of the range ADR 0041's **Endpoint Pair** calibration can
> record is unreachable until the settings file widens it.
>
> Whether an out-of-range target is **clamped or rejected** is `UNKNOWN` -- the
> guide says only that *"Min and Max specify the allowed values"*.
> `MaestroPCA` clamps (`src/MaestroPCA.cpp:294-297`), which is what the hobby
> believes, not what the device does. **Bench test:** target 2000 qus (500 us) on a
> factory device, then `Get Position`. 3968 means clamp; the old position plus
> protocol-error bit 4 means reject.

> [!NOTE]
> **The startup default is already the decision we made.** *"On startup or error,
> the servos turn off"* is ADR 0052's **limp by default** and ADR 0043's **Output
> Release**, for free. Nothing else on this lineup arrives that way. **Do not
> change it to `Go to`** without a decision: `Go to` turns every Maestro error into
> a mass servo move, which is the brownout condition the **Cadence Floor** exists
> to prevent.

### 9.3 `UscCmd`: the USB step can be an artefact, not a wiki page

Pololu ships a command-line configuration utility for Windows and Linux. The Linux
package was downloaded and unpacked this session; its `README.txt` gives a
**release date of 2026-08-10**, so this tooling is current, and it now ships a
`default.nix`. The options below are read from the shipped binary's own string
table:

```
  --list         --configure FILE    --getconf FILE      --restoredefaults
  --program FILE --status            --bootloader
  --stop  --start  --restart  --step  --sub NUM[,PARAM]
  --servo NUM,TARGET   --speed NUM,SPEED   --accel NUM,ACCEL   --device SERIAL
```

The settings file is **versioned XML** (`Usc.dll`: *"Unrecognized settings file
version"*), and it carries the script as well as the settings. So a Maestro's
entire configuration can live in this repository, be applied with one command, and
-- via `--getconf` -- be **diffed against what a builder's board actually holds**.
That is the difference between "configure it over USB somehow" and a provisioning
step with a reviewable artefact and a verification command.

Three limits:

- **One settings file per variant.** A 24-channel file on a 6-channel device
  discards the extras; a 6-channel file on a 24-channel device leaves the rest at
  factory (*"initialized with default settings"*). Both are warnings, not errors.
- **It is a Mono application** -- `mono-runtime`, WinForms, `libgdiplus`, GTK2,
  plus a udev rule. A real ask on a modern Linux desktop.
- **`UscCmd` was not run this session** -- no Maestro is attached. Its capabilities
  are read from the shipped binary, which is a primary source for what it accepts
  but not proof that an invocation succeeds.

### 9.4 Where the Control Center does not run

Windows XP through 11, and Linux. Two exclusions matter:

- **No macOS software at all.** *"The Maestro must be initially configured from a
  Windows or Linux computer, but after that it can be controlled from a Mac."* On
  macOS 10.11+ the virtual serial ports also need **firmware 1.03 or later**.
- **Not on a Raspberry Pi.** *"On ARM-based Linux machines such as the Raspberry
  Pi, the Maestro's graphical configuration program... does not work"*, blamed on
  Mono's WinForms. That is scoped to the **GUI**; whether `UscCmd`, which is not a
  WinForms application, works on ARM is `UNKNOWN` and decides whether a Pi-only
  builder can provision a Maestro at all.

## 10. Safety

### 10.1 The serial timeout is the failsafe we want, and firmware cannot arm it

> "Timeout: This parameter specifies the duration before which a Serial timeout
> error will occur. This error can be used as a safety measure to ensure that your
> servos and digital outputs go back to their default states whenever the software
> sending commands to the Maestro stops working."

Resolution 0.01 s, maximum 655.35 s, **0.00 disables it and 0.00 is the factory
default**. On firing it sets bit 5 and, by the general error rule, *"the Maestro
sends all of its servos and digital outputs to their home positions"* -- which with
the factory `Off` means **every servo goes limp**.

That is ADR 0043's Output Release executed by the device, covering what firmware
cannot: a crashed controller, a wedged task, a cut wire, a dead board.

**And there is no serial command for it.** The command set is Section 7.2;
`Timeout` appears in the guide only in the Control Center description, the
error-bit list and the defaults. Firmware cannot enable it, set it, or read
whether it is on.

> [!CAUTION]
> **The same finding as the VESC's and the Sabertooth's, on a part where it
> matters more.** On a drive controller an unarmable timeout means the motors keep
> their last command. Here it means **the servo failsafe either exists or does
> not, decided months earlier by whoever set the board up, and the droid cannot
> find out.**
>
> 1. **Ship it enabled in the settings file** (Section 9.3), with `--getconf` as
>    the check. This makes that file a **safety artefact**.
> 2. **Infer it at commissioning**: stop sending for slightly longer than the
>    configured timeout, then read `Get Errors` -- bit 5 says the watchdog is armed.
>    Costs the return path and a deliberate dropout.

One behavioural consequence decides how a driver must run: **with the timeout
armed, a quiet link is a fault, not a rest state.** A driver that goes silent while
nothing moves will trip the watchdog and drop every panel. It must keep the link
alive inside the timeout window -- `Get Errors` qualifies, and Pololu recommends
polling it anyway -- rather than lengthening the timeout until it is useless. Same
shape as the drive lane's zero-frame continuity rule.

### 10.2 On startup or error: three modes

| Mode | At startup | On error |
| --- | --- | --- |
| **Off** (factory) | no pulses -- limp | no pulses -- limp |
| **Ignore** | no pulses -- limp | **unchanged** -- keeps driving |
| **Go to** | drives to the stored position | drives to the stored position |

Note `Ignore`'s asymmetry, and the guide's note that `Go to` *"will smoothly
transition to the specified position on an error, but not during start-up, since
it has no information about the previous position"*.

**`Off` is right for this project and is already the factory default.** `Go to`
turns an error into simultaneous motion on every channel, and `CONTEXT.md` records
the cost: *"overlapping servo inrush is what browns a controller out."*

`Ignore` has one legitimate use -- a part held against gravity, exactly the case
`MaestroPCA` warns about from the other side: *"ONLY safe where the part rests in
place on its own -- a servo holding against gravity will drop."* That is a
per-channel decision, made in the settings file.

### 10.3 Release is native, and cheaper here than anywhere else

*"A target value of 0 tells the Maestro to stop sending pulses to the servo."* One
command, four bytes, per channel. ADR 0043's **Output Release** is a
`Set Target 0`. Compare the PCA9685, where release means writing `LED_FULL_OFF_H`
and where ADR 0043 had to add *"a bus drop is reported, not escalated"* because the
release itself can fail.

The Maestro improves on that in one respect -- its own timeout releases everything
if the link dies -- **provided the timeout was armed**, which is Section 10.1.

Two routes, not equivalent:

- **`Set Target 0` per channel** -- explicit and selective.
- **`Go Home` (`0xA2`)** -- all channels, obeying each one's stored startup mode.
  With factory settings that is a full release in two bytes; with any channel on
  `Go to` it is a mass move. **What `Go Home` does depends on a setting firmware
  cannot read.**

For estop, prefer explicit `Set Target 0` -- or `Set Multiple Targets` with every
target 0 on a Mini, which is one packet -- precisely because its behaviour does not
depend on an invisible setting.

### 10.4 Errors are readable, and reading them costs the return path

Nine bits: serial signal (0), overrun (1), buffer full (2), CRC (3), protocol (4),
**timeout (5)**, script stack (6), call stack (7), program counter (8). Mini
Maestros additionally show **performance flags** -- *"the processor missed a
deadline for performing a servo control task"* -- in the Control Center; whether
they are reachable over serial is `UNKNOWN`.

Reading any of it needs the Maestro's TX wired back, a divider, and a spare input
pin -- which Section 14.3 shows may not exist. **The `ERR` pin (Mini only) is the
cheap substitute**: one digital input says *some* error is latched, with no return
path at all. It cannot say which, but it beats the failure mode `MaestroPCA`
names: *"A Maestro that ignores serial looks identical to a dead droid."*

### 10.5 `Get Moving State` answers a question ADR 0043 had to estimate

ADR 0043 caps a release delay at 30 s *"because a PCA9685 gives no motion-complete
signal"*. A Mini Maestro has one: `0x93` returns *"1 as long as there is at least
one servo that is limited by a speed or acceleration setting still moving."*

Three limits: **Mini only** (Section 11); **global, not per channel**, so it cannot
say which output arrived; and it needs the return path. It also reports on the
*commanded ramp*, not the servo -- with no limits set it can only answer 0, and a
servo that cannot keep up is reported as done while still travelling.

So it upgrades the release timer from "estimate and cap" to "the device is idle",
and no further. **For per-Output arrival, Section 8.2's formula is better**:
firmware computes the move time from the numbers it sent, needs no return path,
and is per channel by construction.

## 11. Firmware versions and errata

| Version | Date | Applies to | Change |
| --- | --- | --- | --- |
| 1.00 | -- | all | original |
| 1.01 | 2009-11-19 | **Micro** | *"makes 'Ignore' mode servos behave correctly at startup"* |
| 1.02 | 2013-06-20 | all | *"the position of a servo with an acceleration limit would never settle to the target value"*, plus error handling and the Mini's yellow LED |
| 1.03 | 2016-05-06 | all | macOS 10.11 compatibility |
| 1.04 | 2019-06-10 | **Micro** | see below |

**Current: 1.04 on the Micro, 1.03 on the Minis** -- seven years old, while the
host software was refreshed last month.

> [!CAUTION]
> **Firmware 1.04 is mandatory if a Micro Maestro is specified, and the trigger is
> our own traffic**: *"receiving a serial command could potentially interfere with
> the Maestro's servo update routine, causing position updates for some servos to
> happen too soon or to be skipped entirely. **Repeated serial commands at high
> baud rates could also trap the Maestro in the servo update routine, causing it to
> become unresponsive.**"* Checking the version needs the Control Center, and
> upgrading **wipes every setting** -- another argument for Section 9.3's file.

**`Get Moving State` on the Micro**: *"it has a bug that could make this command
return 0 when it should return 1... we do not recommend using this command on a
Micro Maestro."* **No firmware version is given and no release fixes it** -- treat
it as permanent on the Micro.

**The first movement after startup ignores speed and acceleration limits.** From
the product FAQ: *"the servos could be in any position, and the Maestro has no way
of determining what position they are in... it will instead command the servo to
immediately go to the target position."* The same rule appears in the `Go Home`
note and in the `On startup or error` description, and **it is the rule ADR 0052
states independently** (*"the first move is a jump rather than a ramp"*). It also
fires after every release, because `Set Target 0` makes the device forget the
position -- so on a Maestro **an Output Release costs the next move its ramp**
(Section 14.6).

> [!WARNING]
> **Pololu's SDK header and its guide describe the stored and serial motion units
> differently, and nothing reconciles them.** `protocol.h` annotates
> `SERVOn_ACCELERATION` as *"speed changes that much every **10ms**"* and
> `SERVOn_SPEED` as *"(5 mantissa, 3 exponent) **us per 10ms**"*, while the serial
> commands are quarter-us per 10 ms and per **80 ms**.
>
> These are probably different quantities rather than a contradiction -- but that
> is worse: **the number a builder types into the Control Center and the number
> firmware sends over serial are not interchangeable**, and no Pololu document says
> so. The serial units are settled (Section 8.2 reproduces four published
> figures); the stored units are `UNKNOWN`. **Bench test:** `--configure` a known
> stored acceleration, power-cycle, time a throw; then send the same number with
> `--accel` and time it again.

**Baud must be derated at high pulse rates** on the Minis -- UART/chained
200 kbps at 10-100 Hz but **115.2 kbps at 111-333 Hz**; dual-port halves both --
*"Assuming bytes are not sent and received simultaneously, as required for the
Pololu protocol."* **Pulse range narrows too**: a Micro at 100 Hz with 6 servos is
capped at 1616 us, and on the Mini the restrictions *"apply to all servo channels,
even if some are set to a lower pulse rate using the Period multiplier feature."*
At the factory 50 Hz none of this binds -- but `Period` is the same invisible
setting that rescales the motion units. One setting, three consequences.

## 12. Libraries

### 12.1 `pololu/maestro-arduino`, read in full -- and three defects

Last code commit **`bcbd0c06`, 2020-12-17** (the repository's 2026 `updated_at` is
metadata; `pushed_at` is 2020). Not archived, zero open issues: frozen but
officially supported. It implements all thirteen commands and all three
sub-protocols, and the framing helpers are correct.

**Defect 1 -- every read is an unbounded spin.** All four read paths are
`while (_stream->available() < N);` with no timeout parameter anywhere in the
library. **On a droid that is a hang, not a slowdown**: an unplugged TX wire, a
device that has not detected its baud rate, a CRC mismatch or a wrong device
number, and the calling task blocks forever. On protoArtoo that would be a Core 1
real-time task, which AGENTS.md forbids outright. **A protoArtoo driver must not
use this read path** -- and arguably not the library at all; the write side is
thirty lines we would rather own.

**Defect 2 -- `_CRCByte` is never initialised.** The constructor sets `_stream`,
`_deviceNumber`, `_resetPin` and `_CRCEnabled` and never touches it; `writeCRC()`
zeroes it only *after* sending. Every packet but the first is correct. Harmless
for the file-scope object the examples declare (statically zero-initialised),
**wrong for a heap or stack instance** -- which is what a driver behind a runtime
Component Member would naturally build.

**Defect 3 -- compact mode never sends `0xAA`** (Section 6.3). Combined with the
factory default, this is the likeliest reason a first Maestro integration does
nothing at all.

One thing it gets right, worth copying: `reset()` drives the pin low **before**
making it an output, then returns it to high-impedance rather than driving high --
correct open-drain discipline against an internally pulled-up line, with
`delay(200)` for the reboot.

### 12.2 The alternatives

**No library targets ESP32.** The field is Arduino-AVR and Python.

| Library | Last commit | Maintained | Protocols | `0xAA`? | Reads |
| --- | --- | --- | --- | --- | --- |
| `pololu/maestro-arduino` | 2020-12-17 | frozen, official | all three | Pololu only | **unbounded spin** |
| `austin-bowen/pololu-maestro` (Py) | 2024-12-06 | **yes** | Pololu only | always, as header | `TimeoutError` |
| `FRC4564/Maestro` (Py) | 2020-02-15 | no | Pololu only | always, as header | unbounded |
| `mpiannucci/MiniMaestro` (Py2) | 2015-04-20 | no | Pololu | **yes, standalone** | 5 s timeout |
| `omcaree/node-pololumaestro` | 2014-09-30 | no | compact + Mini SSC | **no** | async |
| `pololu/pololu-usb-sdk` (C#) | current | official | **USB control transfers, not TTL** | n/a | n/a |

Two are worth reading before writing ours: `austin-bowen/pololu-maestro`, the only
one with real timeouts and a typed error enum, and the USB SDK, which is the
authority on the configuration surface even though it speaks a transport we will
never use.

> [!NOTE]
> **Reported, not verified.** Every row but the official Arduino library comes
> from a research agent's survey and was not read line by line this session; the
> `maestro-arduino` dates were confirmed against the GitHub API. Treat the table as
> a map, not an authority. `maestro-servo` on PyPI could not be fetched and is
> `UNKNOWN`. The ESP Component Registry query **failed with a tool error**, so
> "no ESP-IDF component exists" is untested, not negative.

## 13. How the hobby actually uses these (non-normative)

### 13.1 Padawan360: the whole grammar is eight `restartScript` calls

`~/Documents/GitHub/Padawan360_mega_maestro_DYSV5W/` (Steve Baudains /
Imperiallandm fork). Both sketches are identical in their Maestro handling:

```cpp
SoftwareSerial maestroSerial(10, 11);        //tx pin 11
//MiniMaestro maestro(Serial3);              //hardware serial
MiniMaestro maestrosserial(maestroSerial);   //software serial
maestroSerial.begin(9600);
...
  if (Xbox.getButtonPress(R2, 0)) {
    if (Xbox.getButtonPress(UP, 0)) {
      maestrosserial.restartScript(0);
```

`restartScript(0)` through `(7)`, on `R2`/`L2` plus a d-pad direction. **Grepping
the same files for `setTarget`, `setSpeed`, `setAcceleration`, `getPosition`,
`setMultiTarget`, `goHome` or `stopScript` returns nothing.** Three bytes per
gesture; every frame of choreography lives on the Maestro.

- **9600, compact, `SoftwareSerial`** -- with `Serial3` commented out on a Mega
  that has it free. The convention is imitation, not analysis.
- **It cannot work against a factory-default Maestro**: compact emits no `0xAA`,
  so the device must have been set to fixed 9600 over USB. Nothing in the sketch or
  its README says so.
- **It never reads**, so it never meets the blocking spin. The hazard is latent.
- **The triggers are level-sensitive** -- all eight use `getButtonPress` (true
  while held) with no `millis()` guard, while the same file uses `getButtonClick`
  thirteen times elsewhere, so the author knows the difference. A held button
  restarts the subroutine every pass of `loop()`. That the symptom is a sequence
  which starts and never advances is **inference**, not a report against Padawan --
  but Pololu staff diagnosed exactly this pattern on another builder's R2 sketch,
  where a 16-second Cantina sequence moved one panel and halted.

### 13.2 Negative results, recorded so nobody repeats the search

| Project | Maestro? | How checked |
| --- | --- | --- |
| `ShadowMD` | **none** | 0 hits in tree **and** `git log --all -S "Maestro"`. Validated: 0 hits for `servo` too, because it owns no servo code |
| `AstroPixelsPlus` | **none** | 0 hits; `servo` hits five files |
| `CHIRP` | **none** | 0 hits; `servo` hits `CHIRP_Audio.ino` |
| MarcDuino (Main, Client, BetterDuino) | **none** | 0 `maestro`/`pololu` across all three; `servo` hits `servo.c` in each |

ShadowMD is MarcDuino end to end -- *"BODY PANEL OPTIONS ASSUME SECOND MARCDUINO
MASTER BOARD ON MEGA ADK SERIAL #3"* -- commanding panels as **numbered
functions**, not positions.

> [!IMPORTANT]
> **Both of the hobby's ecosystems converged on the same architecture, and it is
> not ours.** MarcDuino carries its own keyframe sequencer -- *"The sequence is
> held on a matrix, each line representing a 'frame' or step. The first element of
> the frame is the length of the step in 1/100 sec. Then the position of all servos
> follows"* -- with `seq_startsequence()` and sequences in `PROGMEM`.
>
> So MarcDuino function code *n* and Maestro `restartScript(n)` are **the same
> architecture with different wire formats**: numbered choreography on a
> co-processor, triggered by a host that then forgets about it. protoArtoo's
> decided model (ADR 0052, #286, #318, #319) is the other one. Section 14.1 is that
> collision.

### 13.3 The one real ESP32 integration, and it confirms Section 5.3 in a builder's own words

`BitMarkus/ESP32_Hexapod` (2022-11-01), 18 DOF on a Mini Maestro 18 -- the only
working ESP32-plus-Maestro project found. Its wiring comments are worth more than
its code:

```cpp
//Da nur Daten vom ESP32 zum Maestro Servo Controller gesendet werden,
//ist nur der TX-Pin wichtig (transmit)
//VORSICHT! Der ESP32 arbeitet mit 3,3V Logik, der Maestro Servo Controller mit 5V Logik
//Das Senden von Signalen vom ESP32 zum Maestro funktioniert, aber eigentlich
//ist die Schwelle zum HIGH bei 4,0V
//Beim Senden von Daten vom Maestro zum ESP32 kann der ESP32 beschaedigt werden!!!!
#define RXD2 12  //Orange: Dummy-Port
#define TXD2 17  //Gelb: TX2 des ESP32 -> geht zu RX-Port des Maestro
//ACHTUNG! Baudrate muss auch in der Maestro Software eingestellt werden
Serial2.begin(115200, SERIAL_8N1, RXD2, TXD2);
```

Translated: only TX matters, because data only goes one way; the ESP32 is 3.3 V
and the Maestro 5 V; sending to the Maestro works *"but the threshold for HIGH is
really at 4.0 V"*; **"when sending data from the Maestro to the ESP32, the ESP32
can be damaged!!!!"**; and the baud *"must also be set in the Maestro software"*.

No level shifter appears anywhere in the repository. So the only working precedent
runs exactly the configuration Section 14.3 recommends -- **write-only, fixed high
baud at 115200, 3.3 V straight into RX** -- and treats the return path as
dangerous. It streams positions rather than triggering scripts, which is right for
the reason Section 14.1 gives: **gaits are computed, not authored.**

### 13.4 `MaestroPCA`: the protocol answered from the other side

`~/Documents/GitHub/r2d2-astromech-simulator/arduino/MaestroPCA/` (Mike Eddington)
reimplements Maestro *behaviour* on a PCA9685 or ESP32 LEDC, and its `MaestroLink`
**answers the Maestro's own serial protocol**, so a Padawan host needs no changes.
It is the most useful artefact here because it is a *reader* of the protocol and
everything else in the hobby is a writer.

It implements every command in Section 7.2. Its divergences are documented and
semantic: `Set PWM` accepted and discarded, `Get Errors` always zero (*"nothing
here latches errors"*), `0xA8`'s parameter dropped, and Mini SSC scaled across
min..max rather than the Maestro's 8-bit neutral +/- range.

The case for a co-processor, stated better than anywhere else:

> "if the host stalls on `Usb.Task()` or a `delay()`, a single-board animation
> stalls with it."

And the limit of a real one: *"A real Maestro runs **one** script."* Concurrent
behaviour -- a holoprojector idling while panels fire -- is not expressible on one,
which is why `MaestroPCA` added four tracks, a per-channel release timer,
oscillator and wander generators, and per-channel easing. Read as a list, those are
**a point-by-point catalogue of what ADR 0052 and ADR 0043 decided protoArtoo needs
and a Maestro does not have.**

### 13.5 The `0x9F` parser trap, ours the moment we ever listen

`Set Multiple Targets` is **the only command whose length comes off the wire**.

> "a count above `MPCA_LINK_MAX_MULTI` is refused outright rather than believed,
> because `2 + count*2` in a uint8_t is 0 for a count of 127 and a parser that
> trusts it decides the command is complete on its first argument byte and then
> reads a hundred targets out of whatever follows `_arg` in memory. **Two bytes of
> noise, `0x9F 0x7F`, is the whole exploit.**"

The arithmetic checks: `2 + 127*2 = 256`, truncating to **0**. Its tests make the
point that this is a noise story, not an attacker story -- the protocol's own
self-resync (Section 7.3) is what turns one dropped byte on a shared line into
exactly that sequence.

**protoArtoo is a writer, so this is not our bug today.** It is recorded because it
is the one place the protocol is genuinely dangerous, and because #307 leaves open
whether protoArtoo might ever present a Maestro-compatible interface. If it does:
**refuse an oversized count, do not clamp it.**

### 13.6 Pololu's own sequencer export emits the script its own errata warns against

The Control Center's **"Copy All Sequences to Script"** is how a builder turns a
choreographed sequence into something `restartScript(n)` can trigger. Its
generator, `Maestro/Sequencer/Sequence.cs` in Pololu's SDK:

```csharp
string script = "# " + name + "\n" + "sub " + nice_name + "\n";
script += generateScript(enabled_channels, needed_channel_lists);
script += "  return\n";
```

It terminates the subroutine with **`return`**. The guide's errata, on Script call
stack error (bit 7):

> "An underflow will occur if you run a subroutine using the 'Restart Script at
> Subroutine' serial command and the subroutine terminates with a return command
> rather than a quit command or an infinite loop."

**The exported script, plus the library call it exists to be driven by, is exactly
the combination Pololu's own documentation warns against** -- a contradiction
across three of its own artefacts. It is reported in the wild: an R2 builder
animating dome panels on a Mini Maestro 18 hit a stack overflow and an infinitely
looping script, fixed by adding `sub main` before `begin` and `quit` before
`repeat`.

**If protoArtoo ever ships Maestro builder documentation, this is the first line of
it:** replace the generator's trailing `return` with `quit`.

## 14. What protoArtoo would have to do

### 14.1 The architectural fork, answered

#307's body poses it: *"Whether we use the Maestro as a dumb output or as a
sequencer is this ticket's call."* It frames the question as a collision with
#286. **It is a collision with five decisions, and four of them are not about
motion at all.**

| Decision | What the sequencer model does to it |
| --- | --- |
| ADR 0052 -- easing is `none`, `soft`, `overshoot` | the Maestro has only the trapezoid, which is `none`. The other two cannot exist on-device (Section 8.4) |
| ADR 0052 -- speed and acceleration are **times** | a script stores rates, authored on a PC, on a device whose period firmware cannot read (Section 8.1) |
| `CONTEXT.md` **Cadence Floor** | moves into the Maestro's script, where the Sequence Coordinator cannot hold it -- and *"keeping the safe pace out of the browser is the reason the Floor lives in firmware at all"* |
| ADR 0043 -- Output Release scheduled **from arrival** | firmware stops knowing when arrival is; `Get Moving State` is global and Mini-only (Section 10.5) |
| #301 / #318 -- the builder edits Outputs in protoArtoo's UI | the choreography lives in an XML on a PC. Two editors, one droid, no sync |

The last row decides it, and it is not a motion argument. **A sequencer Maestro
moves authorship out of protoArtoo.** Everything this epic builds -- the output
table, the timeline, the Gesture model, the Parts catalog -- assumes the body owns
what a part does. A droid choreographed in the Maestro Control Center has two
sources of truth for one behaviour, and protoArtoo's is the one that cannot see
the other.

**The counter-argument is real and should be recorded, not dismissed.**
`MaestroLink` puts it best: *"if the host stalls on `Usb.Task()` or a `delay()`, a
single-board animation stalls with it."* That is why the hobby converged here,
twice, independently. **It does not apply to us.** Padawan360 is a single-threaded
AVR `loop()` that must poll a USB host shield; protoArtoo is a dual-core FreeRTOS
system whose servo work already runs as its own 50 Hz task with the drive lane
fenced onto Core 1. **The isolation a Maestro sells is isolation the scheduler
already bought.**

**So: dumb output.** Firmware keeps the ramp and streams positions; the Maestro's
speed and acceleration are left at 0 so its engine cannot fight the ramp we
computed; the script engine goes unused.

> [!NOTE]
> **One exception worth leaving open.** The engine is the right tool for exactly
> one job: a move that must complete correctly **while the host is not talking**,
> such as a slow return during a fault. Nothing on the roadmap asks for that today.
> If something does, Sections 8.2 and 8.4 are the conversion, and it can be adopted
> per-Output rather than per-droid.

### 14.2 The UART, and a collision with #311

| | UARTs | Committed to | Spare |
| --- | --- | --- | --- |
| artoo-esp32 | 3 | console, drive, dome (audio shares the dome's controller) | **none** |
| firebeetle2 | 5 | console, drive, dome, audio | **UART4**, *"unclaimed by the firmware"* |

> [!IMPORTANT]
> **Two roadmap parts want the same single spare UART.**
> [`elrs-crsf-radio.md`](elrs-crsf-radio.md) Section 12.1 already claims UART4 for
> CRSF, because CRSF at 420 kbaud is *"the least suitable thing on the board to
> bit-bang"*. A Maestro wants it too.
>
> **They are not equally stuck.** CRSF is bidirectional, fast and must be a real
> UART; a Maestro link is one-way, slow and tolerant, so it has three options
> (Section 14.3) where CRSF has one. **If a builder wants both, CRSF takes UART4
> and the Maestro goes elsewhere.** Better decided here than discovered by
> collision.

For artoo-esp32 the honest statement is the ELRS sheet's: **no spare UART, and
freeing one would cost the dome link or the console.** The Sabertooth sheet's
escape -- *"replace, never append"*, one lane with a runtime-chosen device -- **does
not work here**, and the difference is worth stating: drive is one category whose
two products both want a UART, so the lane can be shared by replacement. Body
servo controller's supported member is **LEDC, which costs no UART at all**. There
is nothing for a Maestro to replace.

### 14.3 Three ways to reach it, and where each stops

**(a) A hardware UART** -- firebeetle2's UART4, if #311 has not taken it. Full baud
range, no CPU cost, supports either architecture.

**(b) A software UART TX.** protoArtoo already ships one:
`src/drivers/audio_soft_uart_tx.h`, 9600 baud, bit-banged inside a `portMUX`
critical section, whose header states both the cost and its justification --
*"Duration: ~1.04 ms per byte... Audio commands are infrequent (at most a few per
second), so this is safe for the application."* That justification transfers to a
gesture-triggered Maestro and categorically not to a streamed one:

| Traffic | Bytes | Core 0 blocked | Verdict |
| --- | --- | --- | --- |
| `restartScript(n)`, compact | 2 | 2.1 ms | fine |
| `Set Target`, one channel | 4 | 4.2 ms | fine at gesture rate |
| `Set Multiple Targets`, 23 channels | 49 | **51 ms** | impossible |
| ...at the 50 Hz `ServoTask` tick | -- | **2.55 s blocked per second** | impossible |

So the soft-UART route supports only the architecture Section 14.1 just rejected.
**Recorded as a real option that the design decision closes**, not one nobody
thought of.

**(c) Freeing a second hardware UART by a trade** -- on artoo-esp32 this costs the
console or the dome link. Not acceptable.

**Pin budget, if a link is wired at all:**

- **artoo-esp32**: `docs/pin_map.md:177` records **GPIO 2, 4, 12 and 27 unused in
  SBUS modes** (the CH3-CH6 headers); one can carry TX. Two are classic-ESP32
  strapping pins, so **confirm which before choosing** -- the pin map does not flag
  them, and a pin held low at boot by an attached device changes how the chip
  starts. Check GPIO 4 and 27 first.
- **firebeetle2**: `docs/pin_map.md:307-310` measured *"15 usable GPIOs against a
  demand of 14 -- one spare, and it is itself an avoid-list pin"*. A **write-only**
  link fits that last pin. A bidirectional one does not: the return path needs a
  sixteenth GPIO that does not exist. **On this board, reading the Maestro back is
  a design change, not a wiring choice.**

### 14.4 The level shifter, a new class of part for this project

- A **write-only** link needs one shifter channel -- or, following the only working
  ESP32 precedent (Section 13.3), none, accepting Pololu's *"not guaranteed"* on a
  3.3 V RX. That is what the hobby does and it is out of specification.
- A **bidirectional** link needs shifting both ways; a divider will not do, because
  one direction needs the voltage raised.
- The Maestro's `5V (out)` can supply the shifter (Section 5.2), so no third rail.
- **This is the first part on the lineup to require one.** The VESC sheet's
  electrical section is titled *"why this one is easy"*; the Sabertooth is *"3.3 V
  TTL serial on the same header"*. Any builder documentation that says "wire the
  signal and a ground" is wrong for this part.

### 14.5 The provisioning step, made checkable

A Maestro cannot be configured over serial, so **"plug it into a PC once" is a
hard requirement**. Section 9.3 makes it a reviewable one: author the settings in
the Control Center, export XML, **commit it to this repository** (one file per
variant), `UscCmd --configure` it, then `--getconf` and diff to prove it took.

| Setting | Value | Why |
| --- | --- | --- |
| Serial mode | UART, **fixed** baud (or detect, with a Pololu-protocol driver) | Section 6.3 |
| Baud | 115200 or higher | Section 14.6 |
| **Serial timeout** | non-zero | **the servo failsafe** (Section 10.1) |
| Min / max per channel | widened to our range, on the 16 us grid | Sections 9.1, 9.2 |
| Startup / error | **Off**, except parts held against gravity | Sections 9.2, 10.2 |
| Startup speed / acceleration | **0** | firmware owns the ramp (Section 14.1) |
| Period | leave at 20 ms | changing it rescales the motion units invisibly |
| CRC | off | Section 7.4 |

**The timeout row makes this file a safety artefact**, which is the argument for
treating it like `docs/droid-parts.yaml` rather than like a wiki page.

### 14.6 Release, the first move, and the frame budget

**Release** is `Set Target 0` per channel (Section 10.3), and ADR 0043's *"a bus
drop is reported, not escalated"* applies unchanged with the UART in place of the
I2C bus.

**But release costs the next move its ramp.** A released channel has no position,
so the next `Set Target` is a jump at full speed (Section 11). On LEDC this is
harmless because firmware holds the last commanded value; here the device genuinely
does not know. So after a release firmware must **re-establish the position before
ramping** -- command the last known target, let it snap, then ramp -- or accept a
snap on the first move of every part after every estop. `SERVO_PULSE_NEUTRAL_US` is
not a safe re-establish value; the last commanded position is.

**The frame budget**, at the 50 Hz tick with twenty-three Outputs:

| Baud | `Set Multiple Targets`, 23 ch (49 B) | Fits a 20 ms tick? |
| --- | --- | --- |
| 9600 | 51.0 ms | **no, 2.5x over** |
| 38400 | 12.8 ms | yes, 64 % occupancy |
| 115200 | 4.3 ms | yes, comfortable |
| 200000 | 2.5 ms | yes |

**Run 115200 or faster and use `Set Multiple Targets`.** The hobby's 9600 cannot
carry a streamed body -- which is the quantitative reason the hobby does not
stream. A dedupe against the last sent value drops a resting droid's traffic to
Section 10.1's keep-alive.

### 14.7 Registry, Gate, Member

**Component Protocol.** #303's **Maestro serial** stands. It passes `CONTEXT.md`'s
test decisively: a different wire format, a different transport class from both
peers, and a device holding state firmware cannot read.

**Board Capability Gate.** **Needed, and unlike the PCA9685's it is a real board
fact** -- *"has this board a UART, or a spare GPIO, to reach a Maestro"*, answered
differently per board and entangled with #311. It is not the same gate as #311's,
so the two tickets should agree on one rather than mint two.

**Component Member.** A member appears once a second backend exists, which is
ADR 0042's condition. The seam does not exist yet:
`src/tasks/servo_task.cpp:101` still calls `ledcPwmSetPulseWidth()` directly.
**That seam is owed to #286 and #306 regardless of whether a Maestro is ever
built**, and it is the bulk of the work for either roadmap member -- so neither
part's ticket should carry it alone.

### 14.8 Costs to state plainly

- **A level shifter enters the bill of materials** -- a real one for a
  bidirectional link. First time on this lineup.
- **A PC provisioning step becomes mandatory**, on Windows or x86 Linux. macOS
  cannot do it; a Raspberry Pi probably cannot run the GUI.
- **The scarcest bus on the board is spent**, and on firebeetle2 it is contested
  with ELRS.
- **Reading the device back is unaffordable on firebeetle2's shield**, so
  `Get Errors`, `Get Position` and `Get Moving State` are unavailable in the
  realistic wiring, and with them every diagnostic the device offers. A Mini's
  `ERR` pin is the one cheap substitute.
- **Two settings we depend on are invisible to firmware** -- the serial timeout and
  the channel limits -- so the operator UI cannot honestly report whether the
  droid's servo failsafe is armed, only that the settings file says it should be.
- **The motion engine is bought and not used** under Section 14.1. That is not
  waste -- it is what makes this a peer of the PCA9685 rather than a replacement for
  firmware we are building anyway -- but a builder paying USD 2.29 to 4.66 per
  channel should be told plainly what they get: channel count off a single wire, a
  hardware failsafe, and per-channel startup behaviour.
## 15. Agent Lookup Quick Reference

- Field: Serial command count. Required value: **13**, none of which writes a stored setting.
- Field: Configuration transport. Required value: **USB control transfers only** (`REQUEST_SET_PARAMETER = 0x82`).
- Field: Factory serial mode. Required value: UART, **detect baud rate** -- `0xAA` required first.
- Field: Factory device number. Required value: **12**.
- Field: Factory channel limits. Required value: **min 992 us, max 2000 us** (protoArtoo's LEDC range is 500-2500 us).
- Field: Factory startup/error behaviour. Required value: **Off** -- no pulses, servos limp.
- Field: Factory serial timeout. Required value: **disabled** (0.00).
- Field: Factory CRC. Required value: **disabled**. Polynomial when enabled: `0x91`, LSB-first.
- Field: Factory period. Required value: **20 ms**.
- Field: Target units. Required value: **quarter-microseconds**; `0` = stop pulsing = limp.
- Field: Speed units. Required value: (0.25 us)/(10 ms), 0-16383, 0 = unlimited.
- Field: Acceleration units. Required value: (0.25 us)/(10 ms)/(80 ms), **0-255**, 0 = unlimited.
- Field: Move time, trapezoidal. Required value: `T = 80*S/A + 10*D/S` ms, valid when `D >= 8*S^2/A`.
- Field: Move time, triangular. Required value: `T = 160*sqrt(D/(8*A))` ms; **`S` has no effect**.
- Field: Set Target. Required value: `0x84`, channel, target low 7, target high 7.
- Field: Set Multiple Targets. Required value: `0x9F`, count, first channel, targets -- **Mini only**.
- Field: Get Position reply encoding. Required value: **little-endian uint16**, not 7-bit packed.
- Field: Get Errors. Required value: `0xA1`, 2-byte reply, **clears the register**.
- Field: Release one output. Required value: `Set Target` with target `0`.
- Field: RX logic level. Required value: **0 to 4.0-5 V**; 3.3 V is not guaranteed to read high.
- Field: TX logic level. Required value: **0-5 V push-pull**; not safe into an ESP32 pin.
- Field: Micro Maestro minimum firmware. Required value: **1.04**.
- Field: Prices (assembled). Required value: 6 ch USD 27.95, 12 ch 37.95, 18 ch 46.95, 24 ch 54.95.
- Field: Configuration tool. Required value: `UscCmd --configure FILE` / `--getconf FILE`, versioned XML, Mono.

## 16. Open Items

| # | Item | How to settle it |
| --- | --- | --- |
| 1 | Does an out-of-range target **clamp or reject**? | `Set Target` 2000 qus on a factory device, then `Get Position` and `Get Errors` (Section 9.2) |
| 2 | Are the **stored** speed/acceleration units the same as the serial ones? | `--configure` a known stored value, power-cycle, time a throw; compare with the same number sent by `--accel` (Section 11) |
| 3 | Is a bare `0xAA` followed by a compact command safe? | Send it, then `Set Target`, then `Get Position` + `Get Errors` (Section 6.3) |
| 4 | Does the derived move-time formula match the hardware to within a tick? | Scope one channel across the trapezoidal and triangular branches (Section 8.2) |
| 5 | Does `UscCmd` run on ARM Linux? | Run it on a Pi. Decides whether a Pi-only builder can provision a Maestro (Section 9.4) |
| 6 | Are the Mini's signal lines series-protected like the Micro's 220 ohm? | Guide states it only for the Micro; Pololu publishes no schematic. Measure, or ask Pololu (Section 5.2) |
| 7 | Are **performance flags** readable over serial? | Not in the command set; Control Center only as far as the guide says. Confirm (Section 10.4) |
| 8 | Which of artoo-esp32's free GPIO 2/4/12/27 are boot-strapping pins? | Read against the ESP32 datasheet and record in `docs/pin_map.md` (Section 14.3) |
| 9 | Does an ESP-IDF Maestro component exist? | The registry query failed with a tool error, so this is **untested, not negative** (Section 12.2) |
| 10 | Does #311 or #307 get firebeetle2's UART4? | A decision for the two tickets, not a measurement (Section 14.2) |

## 17. Sources

**Primary -- Pololu**

- Maestro Servo Controller User's Guide, document 0J40, (c) 2001-2022 Pololu Corporation -- https://www.pololu.com/docs/0J40
- Product pages 1350/1351/1352/1353/1354/1355/1356/1357 and the RC servo controller category -- https://www.pololu.com/category/12/rc-servo-controllers
- `maestro-linux` binary package, release 2026-08-10 -- https://www.pololu.com/file/0J315/maestro-linux-220509.tar.gz (`UscCmd`, `Usc.dll`, `README.txt`, `99-pololu.rules`)
- `pololu/maestro-arduino` -- https://github.com/pololu/maestro-arduino (`PololuMaestro.h`, `PololuMaestro.cpp`, commit `bcbd0c06`, 2020-12-17)
- `pololu/pololu-usb-sdk` -- `Maestro/protocol.h` (the `uscCommand` / `uscRequest` / `uscParameter` enumerations) and `Maestro/Sequencer/Sequence.cs` (the script generator)

**Astromech projects, read locally**

- `~/Documents/GitHub/Padawan360_mega_maestro_DYSV5W/` -- Steve Baudains / Imperiallandm
- `~/Documents/GitHub/r2d2-astromech-simulator/arduino/MaestroPCA/` -- Mike Eddington
- `~/Documents/GitHub/ShadowMD/`, `~/Documents/GitHub/AstroPixelsPlus/`, `~/Documents/GitHub/CHIRP/` -- negative results

**Third party**

- `BitMarkus/ESP32_Hexapod` -- https://github.com/BitMarkus/ESP32_Hexapod
- `renesh/DroidControl`, `nhutchison/MarcDuinoMain`, `RealNobser/BetterDuinoFirmwareV4`
- `austin-bowen/pololu-maestro`, `FRC4564/Maestro`, `mpiannucci/MiniMaestro`, `omcaree/node-pololumaestro`
- Pololu forum threads 4935, 5754, 20952, 22121, 22146, 22997, 25124, 25175, 25521, 25827 -- practice, and where quoted, Pololu staff

**protoArtoo**

- `include/config.h`, `include/ledc_pwm.h`, `src/tasks/servo_task.cpp`, `src/drivers/audio_soft_uart_tx.h`, `docs/pin_map.md`, `CONTEXT.md`
- ADR 0041, 0042, 0043, 0052
- [`pca9685-servo-expander.md`](pca9685-servo-expander.md), [`elrs-crsf-radio.md`](elrs-crsf-radio.md), [`sabertooth-syren-packet-serial.md`](sabertooth-syren-packet-serial.md), [`flipsky-vesc-foot-drive.md`](flipsky-vesc-foot-drive.md)
