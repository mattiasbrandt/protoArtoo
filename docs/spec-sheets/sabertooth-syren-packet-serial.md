# Sabertooth and SyRen Spec Sheet (Dimension Engineering Packet Serial)

The **Sabertooth 2x25** is a dual-channel brushed motor driver and the **SyRen 10**
a single-channel one, both from Dimension Engineering. The astromech hobby uses
the Sabertooth on the feet and the SyRen on the dome, and both speak the same
**packet serial** protocol, so one serial line and one implementation serve both.

Research date 2026-09-10. Every register, byte value and electrical figure was
read from a Dimension Engineering manual or from library source.
Claims that could not be sourced are marked `UNKNOWN` with the artefact or bench
test that would settle them.

> [!CAUTION]
> **The serial timeout is the safety-critical fact, and it is narrower than it
> looks.** It stops the motors when the host stops talking only on hardware and
> firmware that implement it, and only once it is armed. It is **off by
> default**, it **does not survive a power cycle**, and it is **not documented at
> all for the SyRen 10 or the original Sabertooth 2x25**. Section 7 is the whole
> story.

## 1. Scope

Covers packet serial, the four operating modes, DIP switch configuration, both
products' electrical limits, the safety mechanisms, the shared bus, the library,
and how the astromech hobby uses these.

Does not cover: the newer CRC-protected packet serial or plain-text serial used by
the USB Sabertooth and Sabertooth 2x32 (a different protocol and library, and no
astromech project surveyed uses it), the Kangaroo motion controller, or
Simplified Serial beyond Section 5.3 -- the hobby has deprecated it on safety
grounds.

## 2. Products Covered

| Product | Status | Notes |
| --- | --- | --- |
| Sabertooth 2x25 **V1** (July 2007) | superseded | Packet commands **0-13 only**. Autobaud |
| Sabertooth 2x25 **V2** (April 2012) | discontinued | Packet commands 0-17. EEPROM baud. 1 A BEC |
| Sabertooth 2x32 | current | The replacement DE points at for the 2x25 |
| SyRen 10 | **current, in production** | Packet commands **0-3 only** documented. Autobaud |

> [!IMPORTANT]
> **The two revisions of the 2x25 are not interchangeable for firmware purposes.**
> The V1 manual documents commands 0-13; commands 14 (serial timeout), 15 (baud),
> 16 (ramping) and 17 (deadband) appear first in the V2 manual. A builder cannot
> tell them apart from the DIP switches. The V2 spec block reads "Sabertooth 2X25
> V2"; the V1 has no 5 V BEC specification and a 24 V rating rather than 30 V.

> [!NOTE]
> **The 2x25 is discontinued.** Printed Droid's builder page says *"The 2x25A
> version will also work, but the 2x32A version is mostly cheaper and has more
> features."* DE's own 2x25 product page is marked **DISCONTINUED**, pointing at
> the 2x32. The protocol is identical, so one implementation serves all of them.

## 3. Sources Checked

| Source | URL | Extraction notes |
| --- | --- | --- |
| Sabertooth 2x25 V1 user's guide, July 2007 | https://www.dimensionengineering.com/datasheets/Sabertooth2x25.pdf | **Commands 0-13 only.** Packet format, checksum, autobaud, addressing, lithium cutoff |
| Sabertooth 2x25 V2 user's guide, April 2012 | https://www.dimensionengineering.com/datasheets/Sabertooth2x25v2.pdf | **Adds commands 14-17.** Serial timeout semantics, ramping, deadband, S2 e-stop |
| SyRen 10 / SyRen 25 user's guide, July 2007 | https://www.dimensionengineering.com/datasheets/SyRen10-25.pdf | **Commands 0-3 only.** 5 V terminal limits, S2 e-stop, R/C failsafe |
| SyRen 50 user's guide, February 2011 | https://www.dimensionengineering.com/datasheets/SyRen50.pdf | Also commands 0-3 -- confirms the SyRen line's command set is not an oversight in one document |
| SyRen 10 product page | https://www.dimensionengineering.com/products/syren10 | Current production status, voltage-derated current ratings, price |
| DE "Libraries for Arduino" | https://www.dimensionengineering.com/info/arduino | **The V1/V2 autobaud list.** Mode comparison table, the startup-transient FAQ |
| `SabertoothArduinoLibraries.zip` v1.5 | https://www.dimensionengineering.com/software/SabertoothArduinoLibraries.zip | The reference implementation. **Not on GitHub** -- the DE org has no such repo |
| Vendored copy in ShadowMD | https://github.com/RealNobser/ShadowMD (`lib/Sabertooth/`) | Same library, read locally; confirms checksum and command encoding |
| Printed Droid: SyRen 10 | https://www.printed-droid.com/kb/syren-10/ | Builder DIP settings, wiring, the 10k resistor, baud troubleshooting |
| Printed Droid: Sabertooth 2x32A | https://www.printed-droid.com/kb/sabertooth-2x32a/ | Builder DIP settings, 2x25-vs-2x32 guidance |

## 4. Electrical

### 4.1 Sabertooth 2x25

| Parameter | V1 (2007) | V2 (2012) |
| --- | --- | --- |
| Input voltage | 6-24 V nominal, **30 V absolute max** | 6-30 V nominal, **33.6 V absolute max** |
| Continuous current | 25 A per channel | same |
| Peak current | 50 A per channel "for a few seconds" | same |
| 5 V BEC | not specified | **1 A continuous, 1.5 A peak**, switching |
| NiMH / NiCd | 5-18 cells | 5-20 cells |
| Lithium | 2s-6s | 2s-8s |
| Size | 65 x 80 x 20 mm | 65 x 80 x 21 mm |
| Weight | 96 g | 90 g |
| Switching frequency | 32 kHz | 32 kHz |

Mounting hole centres form a 1.75 x 2.25 in rectangle, 0.125 in diameter.

> [!WARNING]
> *"All batteries must be capable of maintaining a steady voltage when supplying
> 20+ amps (AA or 9V batteries aren't going to cut it! An 18Ah wire-acid battery
> is a good starting point)."*

Reversing needs no stop: *"there is no need for the Sabertooth to stop before
being commanded to reverse. You can go from full forward immediately to full
reverse."*

### 4.2 SyRen 10

| Parameter | Value |
| --- | --- |
| Input voltage | 6-24 V nominal, 30 V absolute max |
| Continuous current | 10 A up to 18 V; **8 A at 24 V** (10 A with added heatsinking/airflow) |
| Peak current | 15 A "for a few seconds" |
| **5 V terminal** | **100 mA if source is 12.6 V or less; 10 mA above 12.6 V** |
| Size | 35 x 57 x 14 mm |
| Weight | 26 g |
| Lithium | 2s-6s |
| Price | USD 49.99 |

The manual's ratings caveat: *"These ratings are for input voltages up to 18v in
still air without additional heatsinking."*

> [!CAUTION]
> **The SyRen's 5 V terminal cannot power a controller.** 100 mA below 12.6 V,
> and **10 mA above it** -- a droid on a 12 V wire-acid pack sits right on that
> boundary. Do not hang an ESP32 off it. DE says *"If more power is needed, we
> recommend using a ParkBEC or SW05."* The Sabertooth 2x25 V2's 1 A BEC is a
> different proposition entirely.

### 4.3 Protection and reporting

Both carry dual temperature sensors and overcurrent sensing, and both shut down on
depleted battery, overheating, overcurrent or overvoltage.

> [!IMPORTANT]
> **Every fault is reported on an LED and nowhere else.** Packet serial is
> one-way, so the host cannot read battery voltage, temperature, current, fault
> state, or even whether the controller is present.

**Max-voltage hard brake**, relevant on a bench supply: *"If the driver detects an
input voltage above the set limit, it will put the motor into a hard brake until
the voltage drops below the set point again."*

### 4.4 Lithium cutoff is a DIP switch, and the hobby recipe gets it wrong

Switch 3 selects battery type. **Down** = lithium: auto-detects series cell count
at startup and sets a **3.0 V per cell** cutoff. **Up** = NiCd, NiMH, alkaline,
wire-acid, or a power supply.

> [!CAUTION]
> The recipe repeated across ShadowMD, SHADOW and other sketches is *"Dip
> Switches: 1 and 2 Down, All Others Up"* -- which puts switch 3 **up**, i.e.
> lithium protection **off**. That is correct for the SLA packs most droids use
> and wrong for anyone on lithium. DE's warning: *"a lithium battery pack
> discharged below 3.0v per cell will lose capacity and batteries discharged below
> 2.0v per cell may not ever recharge... it is important to unplug the battery
> from the Sabertooth promptly once the cutoff is reached."*
>
> Padawan360's README is the only project doc that catches it. On a 2x32 both the
> polarity and the default differ again, so a 2x25 recipe copied onto a 2x32 is
> wrong twice.

## 5. Operating modes and DIP switches

### 5.1 The four modes

Switches 1 and 2 select the mode:

| Mode | SW1 | SW2 | Wire format |
| --- | --- | --- | --- |
| Analog | UP | UP | 0-5 V, 2.5 V = stop |
| R/C | DOWN | UP | 1000-2000 us servo pulse, 1500 us = stop |
| Simplified Serial | UP | DOWN | single byte, 8N1 |
| **Packetized Serial** | **DOWN** | **DOWN** | 4-byte packet, 8N1 |

**Update-rate ceiling, both serial modes:** *"The maximum update speed on the
Sabertooth is approximately 2000 commands per second. Sending characters faster
than this will not cause problems, but it will not increase the responsiveness of
the controller either."*

### 5.2 Addressing

Switches 4, 5 and 6 set the address: *"Addresses start at 128 and go to 135."*
Weighting is `address = 128 + 1*(SW4 down) + 2*(SW5 down) + 4*(SW6 down)`.

**The hobby convention, confirmed across five independent projects and both
Printed Droid pages:**

| Role | Address | DIP (Printed Droid wording) |
| --- | --- | --- |
| Foot Sabertooth | **128** | `1, 2 off / 3, 4, 5, 6 on` |
| Dome SyRen | **129** | `1, 2, 4 off / 3, 5, 6 on` |

Only switch 4 differs -- exactly the address LSB. Do not invent a different
convention; a builder's existing droid is wired to this one.

> [!WARNING]
> **Community DIP tables are unreliable; the DE manual is authoritative.**
> Shadow-RC ships two documents that contradict each other on the SyRen's address
> 129, and neither matches DE -- one of them treats switch 3 as an address bit
> when it is the lithium cutoff. Always resolve against the manual's switch-4/5/6
> chart.

### 5.3 Simplified Serial, and why the hobby abandoned it

Single-byte. On a Sabertooth, bytes 1-127 drive motor 1 (64 = stop) and 128-255
drive motor 2 (192 = stop); **byte 0 shuts down both motors**. On a SyRen the whole
byte drives one motor -- and **byte 0 is an extreme speed, not a stop**.

ShadowMD's header records the outcome plainly:

> "NOTE: Support for SyRen Simple Serial has been removed, due to problems. Please
> contact DimensionEngineering to get an RMA to flash your firmware."

Padawan360's README is blunter about the hazard:

> "a chance that the dome could start spinning if power is lost to the arduino but
> not the syren... I would say now that we know of this possibility, do not use
> this option if people, especially children, will be close enough to the droid to
> be injured."

**Use packet serial.** This sheet documents Simplified Serial only so nobody
rediscovers it as an option.

## 6. Packet serial (normative)

### 6.1 Frame

Exactly four bytes, 8N1, no reply:

```
[0] address   128..135
[1] command   0..127
[2] data      0..127
[3] checksum  0..127
```

> "Address bytes have value greater than 128, and all subsequent bytes have values
> 127 or lower. This allows multiple types of devices to share the same serial
> line."

(The manual's "greater than 128" is loose -- 128 is a valid address and is the
library default. Read it as "at least 128".)

### 6.2 Checksum

> "Checksum = address byte + command byte + data byte ... added with all unsigned
> 8 bit integers, and then ANDed with the mask 0b01111111 (decimal 127)"

The library agrees exactly:

```c
void Sabertooth::command(byte command, byte value) const
{
  port().write(address());
  port().write(command);
  port().write(value);
  port().write((address() + command + value) & B01111111);
}
```

Manual's worked example: address 130, command 0, speed 64 gives
`130+0+64 = 194 = 0b11000010`, masked to `0b01000010`.

### 6.3 Commands

| Cmd | Name | Data | Sabertooth 2x25 | SyRen 10 |
| --- | --- | --- | --- | --- |
| 0 | Drive forward motor 1 | 0-127 | V1, V2 | **yes** |
| 1 | Drive backward motor 1 | 0-127 | V1, V2 | **yes** |
| 2 | Min voltage | 0-120 | V1, V2 | **yes** |
| 3 | Max voltage | 0-127 | V1, V2 | **yes** |
| 4 | Drive forward motor 2 | 0-127 | V1, V2 | n/a |
| 5 | Drive backward motor 2 | 0-127 | V1, V2 | n/a |
| 6 | Drive motor 1, 7-bit | 0-127 | V1, V2 | not documented |
| 7 | Drive motor 2, 7-bit | 0-127 | V1, V2 | n/a |
| 8 | Drive forward mixed | 0-127 | V1, V2 | n/a |
| 9 | Drive backward mixed | 0-127 | V1, V2 | n/a |
| 10 | Turn right mixed | 0-127 | V1, V2 | n/a |
| 11 | Turn left mixed | 0-127 | V1, V2 | n/a |
| 12 | Drive forward/back 7-bit | 0-127 | V1, V2 | n/a |
| 13 | Turn 7-bit | 0-127 | V1, V2 | n/a |
| **14** | **Serial timeout** | 0-127 | **V2 only** | **not documented** |
| 15 | Baud rate | 1-5 | **V2 only** | **not documented** |
| 16 | Ramping | 0-80 | **V2 only** | **not documented** |
| 17 | Deadband | 0-127 | **V2 only** | **not documented** |

7-bit forms (6, 7, 12, 13) use 0 = full reverse, 64 = stop, 127 = full forward.

> [!IMPORTANT]
> **Mixed mode needs both values before it does anything.** *"the Sabertooth
> requires valid data for both drive and turn before it will begin to operate...
> You should design your code to either use the independent or the mixed commands.
> Switching between the command sets will cause the vehicle to stop until new data
> is sent for both motors."*
>
> Consequence: `Sabertooth::stop()` sends commands 0 and 4 -- **independent** mode
> -- so calling it while in mixed mode does not simply stop, it forces a mode
> switch and requires both mixed values to be re-sent before the droid moves
> again.

**Power-save:** commanding speed 0 puts that motor into power save *"after
approximately 4 seconds"*, implying the bridge stays energised until then.

**Ramping (16)** and **deadband (17)** persist in EEPROM **and apply in every
mode, including R/C and analog**. DE's own example warns: *"The Sabertooth
remembers this command between restarts AND in all modes."* A value a host
writes follows the board into every other mode and outlives a reflash of the
host. Deadband semantics: `127-command < motors off < 128+command`, default `124 < off < 131`.

**Min voltage (2)** does *not* persist: *"This value is cleared at startup, so must
be set each run."* Units are per-model; on a 2x25, `value = (desired volts - 6) * 5`.

### 6.4 Baud and autobaud

Valid rates: 2400, 9600, 19200, 38400. (115200 exists on the 2x60 only.)

> [!CAUTION]
> **The droid's standard pair are different protocol generations on one wire.**
> DE's FAQ lists the V1 (autobauding) drivers as: *"SyRen 10, SyRen 25, Sabertooth
> 2X5, Sabertooth 2X10, Sabertooth 2X25 V1"*, and *"V2 and newer motor drivers
> store their baud rate in an EEPROM setting (the factory default is 9600)."*
>
> So a **SyRen 10 autobauds** and a **Sabertooth 2x25 V2 does not**. Move the host
> to 19200 and the SyRen follows while the Sabertooth stays at 9600 and goes deaf.
> The correct order is: talk to the Sabertooth at 9600, send command 15 to move it,
> *then* change the host, and let the SyRen autobaud at the new rate.
>
> This is almost certainly why both Printed Droid pages tell builders to *"try
> 2400, 9600, 19200 (most likely), 38400"* when a motor will not respond. That is
> troubleshooting advice for a sequencing problem.

**Autobaud mechanics (V1):** send `0xAA` (170, `0b10101010`) before any command.
*"It is not possible to change the baud rate once the bauding character has been
sent."* Until then *"the driver will accept no commands"*. DE requires **a
two-second delay between applying power and sending the bauding character**.

> [!WARNING]
> **A V1 driver latches onto the first edge pattern it sees, including host boot
> noise.** DE's FAQ: *"There is a startup transient on the Uno that the Sabertooth
> sees as a very fast autobaud character. As a result it autobauds at its maximum
> baud rate, 38400."* The same holds on an ESP32 or any other host: a TX pin that
> floats or toggles during boot can mis-baud a V1 controller until it is power
> cycled.
>
> Mitigations: drive TX to idle-high before the controller powers up; power the
> controller after the host UART is initialised; keep the TX line off pins with
> a boot-strapping role. Padawan360's README offers the blunt fix -- *"Send the
> syren to Dimension Engineering to be flashed with the Ver. 2 firmware"*.

## 7. The safety story

The most important section in this sheet. Three independent mechanisms exist and
they are not equivalent.

### 7.1 The serial timeout (command 14)

Quoted in full from the Sabertooth 2x25 **V2** manual:

> "This setting determines how long it takes for the motor driver to shut off if it
> has not received a command recently. **Serial Timeout is off by default.** A
> command of 0 will disable the timeout if you had previously enabled it. **The
> timeout scales 1 unit per 100ms of timeout**, so a command of 10 would make a
> timeout of 1000ms. **This setting does not persist through a power cycle** or in
> any mode other than packet Serial."

| Property | Value |
| --- | --- |
| Default | **OFF** |
| Granularity | 100 ms, rounded up |
| Range | 100 ms to 12.7 s |
| Disable | data byte 0 |
| Persistence | **none** -- re-arm on every controller power-up |
| Readback | **impossible** -- the bus is one-way |

> [!CAUTION]
> **Four ways this bites.**
>
> 1. **Off by default.** It must be sent on every boot *and* after any controller
>    brownout or power cycle, which the host cannot detect on a one-way bus.
> 2. **`setTimeout(0)` disables rather than minimises.** A computed value that
>    reaches 0 silently removes the failsafe.
> 3. **Not documented for the SyRen 10.** Its manual, and the SyRen 50's, document
>    commands 0-3 only. Whether SyRen firmware acts on command 14 is `UNKNOWN`.
> 4. **Not documented for the Sabertooth 2x25 V1.** Commands 14-17 appear first in
>    the V2 manual.
>
> **What a starved controller does:** an armed V2 shuts the motors off. A V1, or
> any controller whose timeout is not armed, has no documented serial failsafe;
> the V1 manual's own words for the analogous R/C case are *"the Sabertooth will
> continue to drive the motor according to the last command until another command
> is given... this can be dangerous due to the robot not stopping."* A held
> command does not decay.

**Bench test to settle it** (needs a powered controller with a motor on it):
address the device, `setTimeout(500)`, run the motor at modest power, stop
transmitting, and time the stop. Past roughly 600 ms means no serial failsafe on
that unit.

### 7.2 The S2 hardware emergency stop -- present on both, used by nobody

Verbatim, and near-identical in the Sabertooth 2x25 V2 and SyRen 10 manuals:

> "In Packetized Serial mode, the S2 input is configured as an active-low emergency
> stop. It is pulled high internally, so if this feature isn't needed, it can be
> ignored. If an emergency stop is desired, **all the S2 inputs can be tied
> together**. Pulling the S2 input low will cause the driver to shut down. **This
> should be tied to an emergency stop button if used in a device that could
> endanger humans.**"

> [!IMPORTANT]
> **This is the strongest safety mechanism these controllers offer, and no
> astromech project surveyed wires it.** One GPIO, pulled low, hardware-stops the
> feet *and* the dome simultaneously, with no dependence on serial state, firmware
> liveness, or an armed timeout. It works when the MCU has hung.

> [!WARNING]
> **A community wiring note appears to conflict with this.** Printed Droid's
> tables show `S2` connected to `Serial2 - Rx2 (Pin 17)` for Padawan Shadow. In
> packet serial S2 is the e-stop input. An MCU RX pin is high-impedance, so S2
> stays pulled high and nothing happens -- harmless but useless. It becomes
> *harmful* if that pin is ever driven low (misconfiguration, or boot state), which
> would e-stop both controllers. Do not copy that wiring without understanding it.
> Whether it serves some other purpose in that build is `UNKNOWN`.

### 7.3 The floating-S1 pull-down

Three independent sources say the same thing. Printed Droid: *"Put a 10k resistor
between 0V and S1"* if the dome moves without input. `ShadowMD:41`: *"Some place a
10K ohm resistor between S1 & GND on the SyRen 10 itself"*. Padawan360's README:
*"In some cases, we've noticed that the dome may behave eratically after starting
up. If this is the case plugging a 10k resister between the S1 and 0V screw
terminals."*

This is the host TX line floating while the MCU is in reset. **Treat it as
required wiring, not folklore** -- and note it interacts with the autobaud hazard
in 6.4, since a floating line is exactly what produces a spurious bauding
character.

## 8. The shared bus

### 8.1 It is real, vendor-endorsed, and the hobby default

> "Packetized serial is a one-direction only interface. The transmit line from the
> host is connected to S1. **The host's receive line is not connected to the
> Sabertooth.** Because of this, multiple Sabertooth 2x25 motor drivers can be
> connected to the same serial transmitter. **It is also possible to use SyRen and
> Sabertooth motor drivers together from the same serial source.**"

Up to **8 devices** per line, across both product families -- the address byte is
the only selector, so it is 8 total, not 8 of each. DE's own `SharedLine.ino`
notes: *"Autobaud is for the whole serial line -- you don't need to do it for each
individual motor driver."*

Confirmed in the wild by ShadowMD (`Sabertooth *ST=new Sabertooth(128, Serial2);`
and `*SyR=new Sabertooth(129, Serial2);`), Shadow-RC, Penumbra, and Printed
Droid's wiring tables (both boards' `S1` from `Serial2 Tx2`).

**Counter-example worth knowing:** Padawan360 does *not* share -- separate UARTs,
and **both devices at address 128**. That is safe only because they are on
different lines; merging them would silently break it.

### 8.2 Bus time is a real budget, and it is the reason the hobby runs slow

Derived here: a 4-byte packet at 8N1 is 40 bit-times.

| Baud | One packet | Drive (2 packets) | Drive + dome (3) |
| --- | --- | --- | --- |
| 2400 | 16.7 ms | 33.3 ms | 50.0 ms |
| **9600** | **4.17 ms** | **8.33 ms** | **12.5 ms** |
| 19200 | 2.08 ms | 4.17 ms | 6.25 ms |
| 38400 | 1.04 ms | 2.08 ms | 3.13 ms |

At the hobby's customary 9600, one drive-plus-dome update takes **12.5 ms**: 62 %
of a 20 ms (50 Hz) control period. At 38400 it takes 3.13 ms.

This is not theoretical. ShadowMD hit it and documented the fix:

> "Constantly sending commands to the SyRen (Dome) is causing foot motor delay.
> Lets reduce that chatter by trying 3 things: 1.) Eliminate a constant stream of
> "don't spin" messages... 2.) Add a delay between commands sent to the SyRen...
> 3.) Switch to real UART on the MEGA (Likely the *CORE* issue and solution) 4.)
> Reduce the timout of the SyRen"

SHADOW's `HISTORY.md` records the same defect being traced: *"Latency for the foot
motors has been traced down to a conflict with the dome code flooding the serial
channels"*. ShadowMD's answer is `serialLatency = 25` ms, rate-limiting feet to
40 Hz and dome to 20 Hz.

A faster baud is the other answer. Its cost on a line that mixes V1 and V2
controllers is the baud-sequencing order in Section 6.4, a one-time step.

## 9. The library, and where it contradicts itself

The reference implementation is `SabertoothArduinoLibraries.zip` v1.5 (2013),
ISC-style licence. **It is not on GitHub** -- the DE org has only a Kangaroo repo.
ShadowMD vendors a copy at `lib/Sabertooth/`, which is what the astromech
projects actually run.

Command encoding, verified identical in the vendored copy and the manual:

```c
void Sabertooth::motor(byte motor, int power) const
{
  if (motor < 1 || motor > 2) { return; }
  throttleCommand((motor == 2 ? 4 : 0) + (power < 0 ? 1 : 0), power);
}
void Sabertooth::drive(int power) const { throttleCommand(power < 0 ? 9 : 8, power); }
void Sabertooth::turn(int power)  const { throttleCommand(power < 0 ? 11 : 10, power); }
void Sabertooth::setTimeout(int ms) const { command(14, (byte)((constrain(ms, 0, 12700) + 99) / 100)); }
```

Three divergences to know before building on it:

> [!WARNING]
> **1. It clamps throttle to `+/-126`, while every doc comment says `+/-127`.**
> `throttleCommand` does `constrain(power, -126, 126)`. `motor(1, 127)` transmits
> **126**. Full scale is unreachable through the public API; use `command()`
> directly if it matters.

**2. `setTimeout()` takes milliseconds, but the manual's units are 100 ms -- and
the hobby has confused the two.** `ShadowMD:1993` calls `ST->setTimeout(10)` with
the comment *"multiples of 100ms"*. The library computes `(10+99)/100 = 1`, giving
**100 ms**, not the 1000 ms the comment implies. `SyR->setTimeout(20)` also yields
**100 ms**. The two different-looking values are identical. Both fail *safe*, but
the intent was ten times longer.

**3. An unrecognised baud silently falls through to 9600** in `setBaudRate()`
rather than erroring.

Also: `autobaud()` blocks for **2000 ms** by default (`delay(1500)`, write,
`delay(500)`) -- which correctly implements DE's two-second rule but is
unacceptable inside a FreeRTOS task. Use the static form with `dontWait = true`
and own the timing.

## 10. How the hobby actually uses these (non-normative)

| Project | Topology | Addresses | Baud | Driver-side safety |
| --- | --- | --- | --- | --- |
| ShadowMD | shared `Serial2` | 128 / 129 | 9600 | `setTimeout` 100 ms both; `setDeadband(10)` feet |
| SHADOW | shared `Serial2` | 128 / 129 | 9600 | `setTimeout(300)` both |
| Penumbra Shadow MD | shared soft-serial, TX only | 128 / 129 | 9600 | `setTimeout` 100 ms; **autobaud deliberately removed** |
| Shadow-RC | shared `Serial2` | 128 / 129 | 9600 | **none** -- 5 ms software frame instead |
| Padawan360 | **two UARTs** | **128 / 128** | 9600 | `setTimeout(950)` both |
| Reeltwo | either | 128 (both, by default) | 9600 | `setTimeout(950)`, `setRamping(80)` |

Four observations:

**Everyone uses packet serial.** Simplified Serial is deprecated on safety
grounds; R/C mode appears only as an escape hatch for non-DE foot controllers.

**Nobody sets `setRamping()` except Reeltwo** -- because it persists in EEPROM and
applies in all modes. The SHADOW lineage ramps in software instead.

**Two mixing philosophies.** ShadowMD uses the Sabertooth's own mixed mode
(`drive()` + `turn()`); Reeltwo mixes in software and sends
`motor(1,x)` / `motor(2,x)`. Pick one and never switch at runtime (Section 6.3).

**Penumbra removed autobaud on purpose:** *"Don't use autobaud(). It is flaky and
causes delays."* On an ESP32 with a V2 controller that is correct. With a V1 SyRen
it is not optional.

## 11. Quick Reference

- Field: Frame. Value: `address, command, data, checksum`, 4 bytes, 8N1.
- Field: Checksum. Value: `(address + command + data) & 0b01111111`.
- Field: Address range. Value: 128 to 135, set by DIP 4/5/6.
- Field: Foot Sabertooth address. Value: **128** (hobby convention).
- Field: Dome SyRen address. Value: **129** (hobby convention).
- Field: Mode DIP. Value: packet serial = SW1 DOWN, SW2 DOWN.
- Field: Battery DIP. Value: SW3 UP = non-lithium, DOWN = lithium 3.0 V/cell.
- Field: Direction. Value: **one-way, transmit only**. No ACK, no telemetry.
- Field: Devices per line. Value: 8 total across both product families.
- Field: Baud rates. Value: 2400, 9600, 19200, 38400.
- Field: Autobaud character. Value: `0xAA` (170), V1 devices, after a 2 s power-up delay.
- Field: V1 devices (autobaud). Value: SyRen 10, SyRen 25, Sabertooth 2x5, 2x10, 2x25 V1.
- Field: V2 devices (EEPROM baud). Value: everything newer; factory default 9600.
- Field: Drive mixed. Value: cmd 8 forward, 9 backward, 10 turn right, 11 turn left.
- Field: Motor independent. Value: cmd 0/1 motor 1, 4/5 motor 2.
- Field: Mixed-mode precondition. Value: **both** drive and turn before any motion.
- Field: Serial timeout. Value: cmd 14, **off by default**, 100 ms units, does not persist.
- Field: Serial timeout on SyRen 10. Value: **not documented; UNKNOWN.**
- Field: Serial timeout on Sabertooth 2x25 V1. Value: **not documented; UNKNOWN.**
- Field: Hardware e-stop. Value: **S2 active-low**, both products, tie all S2 together.
- Field: Ramping and deadband. Value: cmd 16 / 17, **persist in EEPROM, apply in all modes**.
- Field: Library throttle clamp. Value: **+/-126**, not the documented +/-127.
- Field: Required wiring. Value: **10k pull-down from S1 to 0V**.

## 12. Open Items

1. **Does SyRen 10 firmware honour command 14?** Not documented in any SyRen
   manual (10/25 in 2007, 50 in 2011). Bench test in 7.1. **This gates whether the
   dome has any hardware failsafe at all.**
2. **Does Sabertooth 2x25 V1 firmware honour commands 14-17?** Same test, same
   stakes.
3. **Is a timeout stop a coast or a brake?** Every source says only "shut off".
   Indirect evidence points at brake (the library's deadband doc mentions an *"idle
   brake state"*), but no document states it. Matters for a droid on a slope. Test:
   with the driver powered and the timeout expired, compare shaft resistance by
   hand against the driver unpowered.
4. **What Printed Droid's `S2 -> Rx2` wiring is for.** In packet serial S2 is the
   e-stop input. Harmless as drawn, harmful if that pin is ever driven low.
5. **Foot motor current draw.** No project states figures for NPC-2212 or scooter
   motors; the only proxy is Padawan360's wire-gauge advice (12 AWG feet, 14 AWG
   dome). The NPC datasheet or an astromech.net drive-motor thread would settle it.
6. **Brownout on motor inrush.** Not mentioned in any project source read.
   `UNKNOWN` whether it is a real problem in these builds.

## 13. Sources

**Normative (primary): vendor manuals**

- Sabertooth 2x25 V1, July 2007:
  https://www.dimensionengineering.com/datasheets/Sabertooth2x25.pdf
- Sabertooth 2x25 V2, April 2012:
  https://www.dimensionengineering.com/datasheets/Sabertooth2x25v2.pdf
- SyRen 10 / SyRen 25, July 2007:
  https://www.dimensionengineering.com/datasheets/SyRen10-25.pdf
- SyRen 50, February 2011:
  https://www.dimensionengineering.com/datasheets/SyRen50.pdf
- Sabertooth 2x32:
  https://www.dimensionengineering.com/datasheets/Sabertooth2x32.pdf
- DE "Libraries for Arduino" (V1/V2 list, mode comparison, autobaud FAQ):
  https://www.dimensionengineering.com/info/arduino
- SyRen 10 product page: https://www.dimensionengineering.com/products/syren10

**Normative (primary): library source**

- `SabertoothArduinoLibraries.zip` v1.5:
  https://www.dimensionengineering.com/software/SabertoothArduinoLibraries.zip
- Vendored copy in ShadowMD, read locally: https://github.com/RealNobser/ShadowMD (`lib/Sabertooth/`)

**Builder documentation**

- Printed Droid, SyRen 10: https://www.printed-droid.com/kb/syren-10/
- Printed Droid, Sabertooth 2x32A:
  https://www.printed-droid.com/kb/sabertooth-2x32a/

**Ecosystem survey (primary source, non-normative)**

- https://github.com/RealNobser/ShadowMD
- https://github.com/knightshade23/SHADOW (and its `HISTORY.md`)
- https://github.com/reeltwo/PenumbraShadowMD
- https://github.com/ProtocolCosplay/Shadow-RC
- https://github.com/dankraus/padawan360
- https://github.com/reeltwo/Reeltwo (`src/motor/SabertoothDriver.h`,
  `src/drive/TankDriveSabertooth.h`, `src/drive/DomeDriveSabertooth.h`)
- https://github.com/PrintedDroid/ShadowMD-AstroCan-AstroComms
