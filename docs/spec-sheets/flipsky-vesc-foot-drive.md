# Flipsky Mini V6 VESC Spec Sheet (VESC UART)

The **Flipsky Mini V6** is a brushless motor controller built on Benjamin
Vedder's open VESC 6 design: a field-oriented controller with a motor model the
builder has to teach it, one per motor. A host reaches it either over the
**VESC UART** binary packet protocol or by **R/C servo pulse** (PPM); this sheet
documents both.

Research date 2026-09-10. Every packet byte, scaling factor, default and pin in
this document was read from the VESC firmware source, from Vedder's own Flipsky
hardware definition, from the vendor's product data, or from library source.
Claims that could not be sourced are marked `UNKNOWN` with the artefact
or bench test that would settle them.

> [!CAUTION]
> **Two findings shape any integration.**
>
> 1. **The hobby does not use the serial protocol.** Every astromech build that
>    runs a Flipsky today does it as an **R/C servo PWM input**, one pin per foot,
>    mixed in software -- verified by reading six projects (Section 10). None
>    speaks VESC UART to a droid's feet.
> 2. **A starved VESC coasts.** The motor timeout is 1000 ms by default and its
>    timeout brake current is **0.0 A**, so the failure mode is a freewheeling
>    motor, not a stopped one. Unlike the Sabertooth 2x25 V2's command 14, **no
>    serial command exists to change it**: the timeout lives in the app config,
>    which a host can only rewrite wholesale. Section 7 is the whole story.

## 1. Scope

Covers the VESC binary packet protocol, its CRC, the command set relevant to a
drive path, the telemetry reply, the motor timeout and kill-switch mechanisms,
the R/C PPM input as the other way in, the Flipsky V6 family's electrical
limits and connectors, the host libraries, and what the astromech hobby actually
does.

Does not cover: CAN bus (a second transport for the same command set, relevant
only if two ESCs are chained -- noted where it changes an answer), motor
detection and FOC tuning (a builder activity performed in VESC Tool, not
something firmware does), LispBM packages, the bootloader and firmware upload
commands, or the IMU/balance features.

## 2. Products Covered

Flipsky's V6 series, read from `flipsky.net/collections/v6-series/products.json`
on 2026-09-10. Prices are USD, all seven were `available: true`:

| Product | Price | Hardware | Ships with | Note |
| --- | --- | --- | --- | --- |
| **Mini V6 MK5 with power button** | 67.00 | `VESC_6_MK5` | firmware **6.02** | Phase filter, smart power button. Listed 2023-11-28 |
| **Mini FSESC6.7 PRO 70A** | 57.00 | VESC 6.6 | firmware **5.2** | No phase filter, no button. Listed 2023-11-18 |
| Mini FSESC6.8 60V 100A | 76.00 | VESC 6 | `UNKNOWN` | Higher current, outside "Mini V6" naming |
| Mini V6.8 PLUS 60V 150A | 113.00 | VESC 6 | `UNKNOWN` | |
| FSESC 6.7 PRO (full size) | 107.00 | VESC 6 | `UNKNOWN` | Anti-spark switch; not a "Mini" |
| Dual FSESC6.7 Plus | 153.00 | VESC 6 | `UNKNOWN` | **Two motors on one board**, one UART for both |
| IP68 Mini FSESC6.8 Plus 150A | 123.00 | VESC 6 | `UNKNOWN` | Waterproof, for efoil |

> [!IMPORTANT]
> **"Mini V6" names two different products.**
> The literal *Mini V6* today is the **MK5**; the part builders have called a
> "mini V6" for years is the **Mini FSESC6.7 PRO**. They differ in ways that
> reach firmware: only the MK5 has the phase-filter circuit, they ship different
> firmware, and Vedder's tree builds two different `HW_NAME` strings from the
> same Flipsky sources (Section 6.4). Which string each product actually returns
> is Open Item 1, not established here. Both speak the identical protocol, so one
> implementation serves both; the price, the shipped firmware and the VESC Tool
> version they need are not the same.

> [!WARNING]
> **Flipsky tells 6.7 PRO owners not to update the firmware.** From that
> product's own page: *"It is recommended to keep firmware 5.2 from factory ship,
> the new firmware upgrade may damage the ESC."* A host therefore cannot assume
> current firmware in the field, and must not require a feature added after 5.2.
> Everything this sheet relies on predates 5.2.

## 3. Sources Checked

| Source | URL or path | Extraction notes |
| --- | --- | --- |
| VESC firmware `comm/packet.c` | https://github.com/vedderb/bldc/blob/master/comm/packet.c | **The framing, definitively.** Start byte doubles as header length; CRC16 over payload only; stop byte 3; the shift-and-retry resynchroniser |
| VESC firmware `util/crc.c` | .../util/crc.c | CRC-16/CCITT table, polynomial `0x1021`, init 0, no final XOR |
| VESC firmware `comm/commands.c` | .../comm/commands.c | Every command handler. **Which commands reset the timeout** (485-535, 700-702, 1212-1216); the `COMM_GET_VALUES` field order and scalings (384-480) |
| VESC firmware `datatypes.h` | .../datatypes.h | `COMM_PACKET_ID` (938+), `mc_fault_code` (145+), `app_use` (601-612), `KILL_SW_MODE` (899-910) |
| VESC firmware `timeout.c` / `.h` | .../timeout.c | **The safety story.** Defaults, the 10 ms timeout thread, what it does on expiry, the independent 12 ms IWDG |
| VESC firmware `applications/appconf_default.h` | .../applications/appconf_default.h | `APPCONF_TIMEOUT_MSEC 1000`, `APPCONF_TIMEOUT_BRAKE_CURRENT 0.0`, `APPCONF_APP_TO_USE APP_UART`, `APPCONF_UART_BAUDRATE 115200`, the PPM block |
| VESC firmware `applications/app.c` | .../applications/app.c | **Which app starts which UART port** (101-130). The finding in Section 8.3 |
| VESC firmware `applications/app_uartcomm.c` | .../applications/app_uartcomm.c | `BAUDRATE 115200`; three named UART ports |
| VESC firmware `applications/app_ppm.c` | .../applications/app_ppm.c | Safe start, the PPM path's own timeout handling |
| **Flipsky hardware definition** | .../hwconf/flipsky_official/flipsky_v6/ | `HW_NAME`, `V_REG 3.3`, USART3 on PB10/PB11, servo capture on PB6, `HW_LIM_VIN 6.0, 57.0`. **Vedder's tree carries Flipsky's own board files** |
| Flipsky V6 collection data | https://flipsky.net/collections/v6-series/products.json | Product names, prices, stock, publish dates, full spec bodies. Read as JSON rather than scraped |
| Flipsky Mini V6 MK5 wiring diagram | product image, read visually | **The COMM connector pinout** -- the one fact no text source carries |
| Flipsky Mini FSESC6.7 PRO wiring diagram | product image, read visually | The 6.7 PRO's different COMM pin 1 |
| `SolidGeek/VescUart` | https://github.com/SolidGeek/VescUart | The library everybody uses. Four defects in Section 9 |
| Padawan360 Flipsky fork | https://github.com/Imperiallandm/Padawan360_mega_maestro_DYSV5W, read locally | **The only astromech Flipsky implementation found.** R/C PWM, `mixHubDrive()`, the calibration mode |
| `Imperiallandm/motorpolecounter` | https://github.com/Imperiallandm/motorpolecounter | A sketch whose whole purpose is counting hub-motor poles "so you can then enter the correct value in your VESC software" |
| Astromech project clones | Reeltwo, PenumbraShadowMD, Shadow-RC, padawan360, ShadowMD | Cloned and grepped locally. **All five: zero hits for vesc, flipsky, brushless or hub motor** |
| R2-D2 Astromech Simulator | https://github.com/mikeeddington-lgtm/r2d2-astromech-simulator, read locally | Models the Flipsky foot drive as a catalogue row with its own wire settings |

Two web research passes were run alongside this and their findings were checked
against the sources above before use. Three of their claims did not survive:

- *"Default motor timeout 500 ms"* -- the firmware says **1000 ms**
  (`appconf_default.h:27-28`).
- *"VESC firmware has a bug where serial buffers over 64 bytes get truncated"* --
  the firmware buffers `PACKET_MAX_PL_LEN + 8` = **520 bytes**
  (`packet.h:27-31`). The 64-byte limit is `SERIAL_RX_BUFFER_SIZE` on AVR
  Arduinos, a host-side constraint that does not apply to an ESP32.
- *"The UART port remains available regardless of the selected app"* -- true only
  of the **permanent NRF port**, not of the COMM header a builder would wire to
  (Section 8.3).

## 4. Electrical

### 4.1 The board

From the vendor spec bodies (both Mini models are identical here) and
`hw_flipsky_60_core.h`:

| Parameter | Value | Source |
| --- | --- | --- |
| Input voltage | **14 - 60 V** nominal, 4-13S; vendor says *"safe for 4S to 12S, voltage spikes may not exceed 60V"* | vendor |
| Firmware input limits | `HW_LIM_VIN 6.0, 57.0` | `hw_flipsky_60_core.h:434` |
| Continuous current | 70 A | vendor |
| Burst current | 200 A | vendor |
| Absolute current fault | `MCCONF_L_MAX_ABS_CURRENT 150.0` A | `hw_flipsky_60_core.h:415` |
| Max ERPM | 150 000 | vendor |
| BEC / 5 V out | **5 V at 1 A** | vendor |
| Logic rail | **3.3 V** (`V_REG 3.3`) | `hw_flipsky_60_core.h:188` |
| Size | 67 x 39 x 18.7 mm including heatsink | vendor, confirmed on the dimensioned photo |
| Weight | 130 g | vendor |
| Motor and power wire | 12 AWG | vendor |
| MCU | STM32F4, DRV8301 gate driver, 3 shunts | `hw_flipsky_60_core.h:35-37` |

> [!IMPORTANT]
> **A 12 V droid cannot use this part.** 14 V is the vendor's floor and 4S is the
> minimum cell count. The astromech norm is a 12 V or 24 V brushed drive
> (Printed Droid's foot-motor page names MY6812 scooter motors at 12 V or 24 V),
> so choosing a VESC is choosing the pack as well: 6S or 7S lithium, or a 24 V
> SLA pair. A Sabertooth 2x25, by contrast, starts at 6 V.

### 4.2 Logic level -- and why this one is easy

The VESC's serial pins are 3.3 V, because the whole board runs on a 3.3 V rail
(`V_REG 3.3`). An ESP32 or ESP32-P4 UART connects **directly, both directions,
with no level shifting**.

The one warning that exists points the other way, at 5 V hosts:

> "Do not connect a 5 V UART directly without a level shifter, as this will
> damage the hardware." -- VESC project UART documentation

So the electrical risk for a 3.3 V host such as an ESP32 is nil, and for an
Arduino Mega host it is real. Ground must be common, as always.

### 4.3 Connectors

Read from the vendor's own wiring drawings. Pin order is as printed, top to
bottom on the drawing.

**COMM connector, Mini V6 MK5 -- 8 pins:**

| Pin | Label | Note |
| --- | --- | --- |
| 1 | `SWITCH` | Power button line; MK5 only |
| 2 | `ADC2` | |
| 3 | `TX/SCL` | **USART3 TX, PB10** |
| 4 | `RX/SDA` | **USART3 RX, PB11** |
| 5 | `ADC1` | |
| 6 | `GND` | |
| 7 | `3.3V` | |
| 8 | `5V` | The 1 A BEC |

**COMM connector, Mini FSESC6.7 PRO -- 8 pins:** identical except pin 1 is
`ADC3` rather than `SWITCH`.

Other headers on both: **CAN** (`CAL`, `CAH`, `5V`), **SENSE** (`H3`, `H2`, `H1`,
`TMP`, `5V` -- hall sensors and motor thermistor), **SWD** (`3.3V`, `CLK`, `GND`,
`DIO`, `NRST`), and **USB**.

> [!NOTE]
> **The `TX/SCL` and `RX/SDA` labels are not decoration.** PB10 and PB11 are
> shared between USART3 and I2C2 (`HW_I2C_SCL_PIN 10`, `HW_I2C_SDA_PIN 11`), and
> the firmware calls `hw_stop_i2c()` immediately before starting the COMM header
> UART (`app.c:112-113`). **A VESC cannot have both an I2C accessory and serial
> control.** For a host using serial control alone that costs nothing; for a
> builder who fitted an IMU for balance features, it is a straight conflict.

**PPM input:** the firmware captures servo pulses on **PB6, TIM4 channel 1**
(`hw_flipsky_60_core.h:259-266`). Which physical pin exposes it is `UNKNOWN`:
the two vendor drawings show the receiver harness (`GND`, `5V`, `SIN`) plugging
into different connectors, and neither COMM pin list names a signal input.
Settled by a continuity check from each connector pin to PB6, or by driving a
servo pulse into a candidate pin and watching VESC Tool's decoded PPM display.

## 5. The packet protocol

Read from `comm/packet.c`. This is the whole framing; there is nothing else.

```
  <start> <length...> <payload...> <crc-hi> <crc-lo> <0x03>
```

**The start byte is also the header length.** `packet.c` reads
`data_start = buffer[0]` and uses it directly as the payload offset:

| Start | Payload length | Header bytes | Used when |
| --- | --- | --- | --- |
| `0x02` | one byte | 2 | length 1 - 255 |
| `0x03` | two bytes, big-endian | 3 | length 256 - 65535 |
| `0x04` | three bytes, big-endian | 4 | length above 65535 |

Rules the firmware enforces, each a rejection path a host must not trip:

- A zero-length packet is invalid.
- A packet **must use the shortest header that fits**. A 16-bit header carrying a
  length below 255 is rejected outright, as is a 24-bit header below 65535.
- Payload above `PACKET_MAX_PL_LEN` (512) is rejected.
- The last byte must be `0x03`.
- The CRC must match, or the packet is dropped silently -- **there is no NAK**.

**CRC-16/CCITT over the payload only**, not over the header or the stop byte.
Table-driven in `util/crc.c`, polynomial `0x1021`, initial value 0, no final XOR,
transmitted **big-endian** (`crc >> 8` then `crc & 0xFF`).

**The receiver resynchronises by itself.** `packet_process_byte()` retries
`try_decode_packet()` at successive offsets, advancing one byte at a time on a
structural failure. A host does not have to frame perfectly for the ESC to
recover, and the ESC's own buffer is 520 bytes -- neither of which is true of
the popular host library (Section 9).

## 6. Commands and telemetry

Command ids from `datatypes.h:938+`; behaviour from `commands.c`. The payload's
first byte is the command id; multi-byte integers are **big-endian**.

### 6.1 The five ways to command a motor

| Command | Id | Payload | Scaling | Result |
| --- | --- | --- | --- | --- |
| `COMM_SET_DUTY` | 5 | `int32` | value / **100000** | duty cycle, -1.0 to 1.0 |
| `COMM_SET_CURRENT` | 6 | `int32` | value / **1000** | motor current in amps |
| `COMM_SET_CURRENT_BRAKE` | 7 | `int32` | value / **1000** | braking current in amps |
| `COMM_SET_RPM` | 8 | `int32` | value as-is | closed-loop electrical RPM |
| `COMM_SET_POS` | 9 | `int32` | value / **1000000** | position in degrees |
| `COMM_SET_HANDBRAKE` | 10 | `float32` | scale 1e3 | holding current at standstill |
| `COMM_SET_CURRENT_REL` | **84** | `float32` | scale 1e5 | current as a fraction of the configured max |

The first eight ids have been stable since the protocol was written;
`COMM_SET_CURRENT_REL` at 84 was appended later, so on firmware older than 5.2
take its id from that tree's own `datatypes.h` rather than from this table.

**Which one a droid should use is a real choice, not a detail.** Duty cycle is
open loop and behaves like a throttle; current control is torque control and
will happily spin a lifted wheel to its ERPM limit; RPM control is closed loop
and needs the motor model to be right. The hobby's PPM path (Section 8) uses
whatever `PPM_CTRL_TYPE` the builder set, which defaults to `PPM_CTRL_TYPE_NONE`
-- that is, nothing at all until configured.

### 6.2 Keeping the motor alive

| Command | Id | Payload | Note |
| --- | --- | --- | --- |
| `COMM_ALIVE` | 30 | none | Resets the timeout and nothing else. **Three bytes plus framing** |

### 6.3 Telemetry

`COMM_GET_VALUES` (id 4) returns every field; `COMM_GET_VALUES_SELECTIVE`
(id 50) takes a `uint32` bitmask and returns only the requested fields, echoing
the mask first. Fields in mask-bit order, with the scaling each is encoded at
(`commands.c:396-478`):

| Bit | Field | Encoding |
| --- | --- | --- |
| 0 | FET temperature | `float16`, scale 1e1 |
| 1 | Motor temperature | `float16`, scale 1e1 |
| 2 | Average motor current | `float32`, scale 1e2 |
| 3 | Average input current | `float32`, scale 1e2 |
| 4, 5 | Average `id`, `iq` | `float32`, scale 1e2 |
| 6 | Duty cycle now | `float16`, scale 1e3 |
| 7 | RPM | `float32`, scale 1e0 |
| 8 | **Input voltage** | `float16`, scale 1e1 |
| 9 - 12 | Amp hours, charged; watt hours, charged | `float32`, scale 1e4 |
| 13, 14 | Tachometer, absolute tachometer | `int32` |
| 15 | **Fault code** | one byte, `mc_fault_code` |
| 16 | PID position | `float32`, scale 1e6 |
| 17 | Controller id | one byte |
| 18 | Three MOSFET temperatures | three `float16`, scale 1e1 |
| 19, 20 | Average `vd`, `vq` | `float32`, scale 1e3 |
| 21 | **Status byte** | bit 0 = timeout active, bit 1 = kill switch active |

Two consequences:

**Bit 21 is the field that matters most.** The ESC will tell you it has timed
out, and will tell you its kill switch is asserted. A Sabertooth, whose bus is
write-only, cannot say anything at all about its own safety state.

**Bits 2, 3, 4, 5, 19 and 20 are destructive reads.** They call
`mc_interface_read_reset_avg_*()`, which resets the averaging accumulator. Two
consumers polling the same ESC corrupt each other's numbers. A selective mask
that omits them is the correct default for a status page.

### 6.4 Identification

`COMM_FW_VERSION` (id 0) replies with firmware major, firmware minor, the
**`HW_NAME` string, NUL-terminated**, then the 12-byte STM32 UUID
(`commands.c:231-243`). For these boards the firmware source sets:

- `"Flipsky_60_MK5"` when built with `HW60_IS_MK5`
- `"Flipsky_60"` when built with `HW60_IS_MK1`

The vendor's own spec body says the MK5 reports `60_MK5`, which is the same name
without the vendor prefix. Which exact string a shipped unit returns is
`UNKNOWN` -- Flipsky builds from their own tree -- and it is worth reading once
on a real board before any code matches on it. Match on the prefix, or do not
match at all.

## 7. Safety

> [!CAUTION]
> This section is the reason the sheet exists. Read it before writing a host
> implementation.

### 7.1 The motor timeout stops nothing

`timeout.c` runs a 10 ms thread. When `chVTTimeElapsedSinceX(last_update_time)`
exceeds `timeout_msec`, it unlocks the motor interface and calls
`mc_interface_set_brake_current(timeout_brake_current)` on both motor threads.

The two numbers that decide what that means:

| Setting | Firmware default | Source |
| --- | --- | --- |
| `timeout_msec` | **1000 ms** | `appconf_default.h:27-28` |
| `timeout_brake_current` | **0.0 A** | `appconf_default.h:30-31` |

A brake current of zero is not a brake. The motor is released and the droid
freewheels -- **one full second after the host stops talking**, and then
indefinitely. On a slope, or with any momentum, that is a runaway.

This is the opposite of an armed Sabertooth 2x25 V2, whose serial timeout stops
the motors.

`timeout_msec` of 0 disables the timeout entirely, which is worse again.

### 7.2 Telemetry does not feed the watchdog

Only these reset the timeout, verified by reading every `timeout_reset()` call
site in `commands.c`:

`COMM_SET_DUTY`, `COMM_SET_CURRENT`, `COMM_SET_CURRENT_BRAKE`, `COMM_SET_RPM`,
`COMM_SET_POS`, `COMM_SET_HANDBRAKE`, `COMM_SET_CURRENT_REL`, `COMM_ALIVE`, and
`COMM_SET_DETECT` in one branch.

`COMM_GET_VALUES` does not. A status page polling telemetry at 1 Hz keeps no
motor alive, and a host that stops sending setpoints has one second before the
ESC lets go.

### 7.3 No serial command sets the timeout

There is **no `COMM_SET_TIMEOUT`**. `timeout_msec` and `timeout_brake_current`
live in `app_configuration`, reachable only through `COMM_SET_APPCONF` (id 16),
which rewrites the entire app config -- a serialised blob whose layout changes
between firmware versions and which carries every other app setting with it.
There is no `COMM_SET_APPCONF_TEMP`; the temporary-write variants
(ids 48, 49) exist for the **motor** config only.

A Sabertooth 2x25 V2 arms its timeout over the wire with command 14, in four
bytes; the VESC has no equivalent command. **The timeout is a builder's setting
in VESC Tool, not something a host can arm over serial.** A host has to treat
the ESC's timeout as an unknown it inherits, read what it is if it can
(`COMM_GET_APPCONF`), and never depend on it.

### 7.4 The kill switch

`KILL_SW_MODE` (`datatypes.h:899-910`) lets the ESC watch a pin itself and stop
on it, independently of the serial link. Ten modes:

| Mode | Watches |
| --- | --- |
| `KILL_SW_MODE_DISABLED` | nothing -- the default |
| `..._PPM_LOW` / `..._PPM_HIGH` | the PPM input pin (PB6) |
| `..._ADC2_LOW` / `..._ADC2_HIGH` | ADC2, threshold 1.65 V |
| `..._ADC3_LOW` / `..._ADC3_HIGH` | ADC3, threshold 1.65 V |
| `..._SWDIO_LOW` / `..._SWDIO_HIGH` | PA13 |
| `..._SWCLK_LOW` / `..._SWCLK_HIGH` | PA14 |

When asserted, the timeout thread applies the brake current *and* calls
`mc_interface_ignore_input_both(20)`, so commands arriving over serial are
ignored rather than merely overridden. It is reported back on status bit 21.

**This is a hardware estop the ESC enforces and host firmware cannot defeat.**
It costs one GPIO per ESC and one VESC Tool setting. It is also, notably, the
only VESC safety mechanism that does not depend on the serial link being
healthy.

### 7.5 Faults are reported, not just latched

`mc_fault_code` (`datatypes.h:145-174`) has 29 values, among them
`FAULT_CODE_OVER_VOLTAGE`, `UNDER_VOLTAGE`, `DRV`, `ABS_OVER_CURRENT`,
`OVER_TEMP_FET`, `OVER_TEMP_MOTOR`, `MCU_UNDER_VOLTAGE`,
`BOOTING_FROM_WATCHDOG_RESET` and `UNBALANCED_CURRENTS`. Bit 15 of the telemetry
reply carries the current one.

`FAULT_CODE_BOOTING_FROM_WATCHDOG_RESET` deserves a note: the ESC tells you it
rebooted from its own watchdog, which a host can treat the way it treats a
watchdog reset of its own.

### 7.6 The ESC's independent watchdog

Separate from all of the above, `timeout_init()` arms the STM32 IWDG with a
reload of 140 at prescaler 4 -- **a 12 ms window** (33 ms worst case at the
slowest LSI). It is fed only if the monitored motor, CAN and timer threads all
report progress. A wedged VESC resets itself in well under a tenth of a second.
Nothing a host does affects this, and a host cannot rely on it either: a reset
ESC comes back with its outputs off and its serial app restarting.

## 8. Two ways in: VESC UART and R/C PPM

The two are different control interfaces with different costs, and Section 10
shows the hobby uses the second.

### 8.1 VESC UART

Full binary protocol, 115200 baud by default (`app_uartcomm.c:32`,
`appconf_default.h:229-230`), 8N1. Bidirectional: commands in, telemetry out.
One wire pair per ESC, or one pair to the first ESC and `COMM_FORWARD_CAN`
(id 34) to reach the second over CAN.

**What it buys:** per-wheel setpoints in real units, battery voltage, motor and
FET temperature, RPM, fault codes, and the ESC's own timeout and kill-switch
state. A Sabertooth reports nothing, and a hoverboard controller on hacked
firmware reports no fault codes or safety state.

**What it costs:** a UART per ESC (or CAN), a request/response discipline on a
real-time loop, and a builder who has configured the app in VESC Tool.

### 8.2 R/C PPM

The ESC decodes standard servo pulses on PB6. Defaults from
`appconf_default.h:86-129`:

| Setting | Default |
| --- | --- |
| `PPM_CTRL_TYPE` | `PPM_CTRL_TYPE_NONE` -- **disabled until configured** |
| Pulse start / centre / end | 1.0 / 1.5 / 2.0 ms |
| Hysteresis | 0.15 |
| Median filter | on |
| **Safe start** | **on** |
| Ramp time up / down | 0.4 s / 0.2 s |
| PID max ERPM | 15000 |

Safe start is worth naming: `app_ppm.c:336-386` refuses to drive the motor until
it has seen `MIN_PULSES_WITHOUT_POWER` consecutive idle pulses after startup, a
timeout or a fault. A droid whose transmitter is already at throttle when the
ESC powers up does not lurch.

The PPM path applies the **same** `timeout_get_brake_current()` on pulse loss
(`app_ppm.c:171-186`), so Section 7.1's finding applies identically: losing the
signal releases the motor unless a brake current was configured.

**What it buys:** one GPIO per foot, a standard servo pulse any MCU's PWM
peripheral generates, no protocol, no round trips, and the mixing stays in the
host's software.

**What it costs:** everything in Section 6.3. No telemetry, no fault codes, no
identification, no way to know the ESC is even connected.

### 8.3 They are not freely combinable

`app.c:101-130` starts the COMM header UART for exactly three of the nine
`app_use` values:

| `app_to_use` | COMM header UART started? |
| --- | --- |
| `APP_UART` | yes |
| `APP_PPM_UART` | yes, **and** PPM |
| `APP_ADC_UART` | yes, and ADC |
| `APP_PPM`, `APP_ADC`, `APP_NUNCHUK`, `APP_PAS`, `APP_ADC_PAS`, `APP_NONE` | **no** |

So:

- A builder who set the app to `APP_PPM` -- the natural thing to do when
  commanding by servo pulse -- has **no serial port at all**. A host cannot read
  telemetry, identify the board, or detect its presence.
- `APP_PPM_UART` gives both, but PPM writes a setpoint on every one of its own
  iterations, so **PPM wins**: serial setpoints are overwritten rather than
  merged. Useful for telemetry beside a PPM drive; useless for control.
- The `permanent_uart_enabled` setting that stays on regardless
  (`appconf_default.h:60-61`) applies to `UART_PORT_BUILTIN` --
  **the NRF/Bluetooth module port**, on PC11/PC12 (`app.c:167-170`,
  `hw_flipsky_60_core.h:247-255`), not the COMM header.

**The choice of control interface therefore reaches the builder's VESC Tool
settings**: serial control needs `APP_UART` (or `APP_PPM_UART` /
`APP_ADC_UART`) set there, alongside the baud rate.

## 9. Libraries

### 9.1 `SolidGeek/VescUart` -- the one everybody uses

Version 1.0.1. Its packing, its CRC and its scalings are correct and match the
firmware exactly (duty x100000, current x1000, RPM as-is). Its receive path is
not safe to build a drive loop on, for four separate reasons, all read in source
this session.

**Defect 1 -- it blocks, by spinning.** `receiveUartMessage()` is
`while (millis() < timeout && messageRead == false)` around a
`while (serialPort->available())`, with a default `_TIMEOUT` of **100 ms**
(`VescUart.h:52`). Every getter -- `getVescValues()`, `getFWversion()` -- is a
busy-wait of up to 100 ms with no yield: one unanswered read spans five periods
of a 50 Hz control loop.

**Defect 2 -- the timeout wraps.** `uint32_t timeout = millis() + _TIMEOUT;`
compared with `millis() < timeout`. In the 100 ms before `millis()` rolls over
at 49.7 days, `timeout` wraps to a small value and the loop exits immediately,
so every read fails until the rollover completes. A droid left powered does hit
this.

**Defect 3 -- it writes past the end of its buffer.** `messageReceived[256]`,
and `endMessage = messageReceived[1] + 5`, which reaches **260**. The bounds
check `if (counter >= sizeof(messageReceived)) break;` runs *after*
`messageReceived[counter++] = serialPort->read()` and only breaks the inner
loop, so the outer loop re-enters and writes indices 256, 257, 258, 259 -- and
then `messageReceived[endMessage] = 0` writes 260. Reachable from any reply
longer than 251 payload bytes, or from a desynchronised stream. `COMM_GET_VALUES`
at about 75 bytes never triggers it; `COMM_GET_MCCONF` would.

**Defect 4 -- it cannot read a long packet and does not resynchronise.** Start
byte `3` is an unimplemented `// ToDo`: it prints a message and leaves
`endMessage` at its initial 256 and `lenPayload` at 0, so the parser desyncs. An
unrecognised start byte prints "Unvalid start bit" and keeps appending. The
firmware's own decoder shifts one byte and retries (Section 5); this one does
not.

A fifth, cosmetic: `packSendPayload()` uses `if (lenPay <= 256)` to choose the
one-byte length header, so a 256-byte payload writes a length byte of 0 -- which
the firmware rejects as `len < 1`. The library never builds one, so it is an
off-by-one waiting for a caller rather than a live bug.

**Verdict:** correct as a specification of the wire format, unusable as-is in a
real-time drive path. Its send path is fine and is worth copying; its receive
path should be rewritten as a byte-at-a-time state machine fed from a
non-blocking drain, mirroring `packet_process_byte()`.

### 9.2 Others

| Library | URL | Note |
| --- | --- | --- |
| `gitcnd/VescUartLite` | https://github.com/gitcnd/VescUartLite | Interrupt-driven receive, timestamps replies rather than blocking. Closer to the right shape; less used. `UNKNOWN` whether it repeats defects 3 and 4 -- not read this session |
| `LiamBindle/PyVESC` | https://github.com/LiamBindle/PyVESC | Python. Useful as a second reading of the protocol, not as an implementation |
| `RollingGecko/VescUartControl` | https://github.com/RollingGecko/VescUartControl | The ancestor of `SolidGeek/VescUart` |

None of the libraries read here offers a non-blocking receive path ready to use
in a real-time loop (`VescUartLite`'s is unread). The framing is about 40 lines
and the CRC is one 256-entry table, so a receive state machine is a small job.

## 10. How the hobby actually uses these (non-normative)

### 10.1 The negative result, recorded so nobody repeats the search

Five astromech controller projects were cloned and grepped for `vesc`,
`flipsky`, `brushless` and `hub motor`, plus the local ShadowMD checkout:

| Project | Foot drive it supports | VESC hits |
| --- | --- | --- |
| `dankraus/padawan360` | Sabertooth, serial | **none** |
| `reeltwo/Reeltwo` | `SabertoothDriver.h`, `CytronSmartDriveDuoDriver.h`, `TB9051FTGMotorCarrier.h` | **none** |
| `reeltwo/PenumbraShadowMD` | Sabertooth | **none** |
| `ProtocolCosplay/Shadow-RC` | Sabertooth | **none** |
| `RealNobser/ShadowMD` | Sabertooth | **none** |
| MarcDuino | lights and sound only; no foot drive | n/a |

**No mainstream astromech controller speaks VESC UART, and none has a brushless
foot drive at all.** The hobby's foot controllers are Sabertooth-shaped.

### 10.2 The one implementation that exists, read in full

`Imperiallandm/Padawan360_mega_maestro_DYSV5W`, on disk locally, header comment
*"edits by STeven Sloan to accomodate the Flipsky esc's and brushless hub
motors"*. It commands them as **servo PWM**, and it is worth reading because it is
the only evidence of what a Flipsky droid actually does:

- `#define FOOT_CONTROLLER 1  //0 = Sabertooth Serial or 1 = individual ESC for
  PWM (HUB) motors` -- one compile-time switch, the whole selection mechanism.
- `Servo leftFootSignal; Servo rightFootSignal;` on pins **44 and 45**, marked
  `(R/C mode)`.
- `mixHubDrive(stickX, stickY, maxDriveSpeed)` mixes to two per-wheel servo
  angles: 90 is stop, 180 full forward, 0 full reverse. The comment credits
  *"KnightShade's SHADOW code with contributions for PWM Motor Controllers by
  JoyMonkey/Paul Murphy and Brad/BHD"* -- so the mixing is the SHADOW lineage's,
  not new.
- Software ramping (`RAMPING` per iteration toward the stick value) and a
  deadzone with a `RampingDeadzoneDelay` before it writes 90.
- `stopFeet()` writes 90 to both. That is the entire failsafe.
- **A calibration mode**: holding L1+L2+R1+R2 at drive speed 3 calls
  `mixHubDrive(..., CalibrationSpeed)` with `CalibrationSpeed = 127`, which maps
  to the servo endpoints, so the ESC can learn its throttle range. The
  repository's README describes the ritual, and its own header calls the whole
  path *"Experimental Hub Drive Code. Use at your own risk and test operation
  fully before going out in public."*

Its companion, `Imperiallandm/motorpolecounter`, exists solely to count a hub
motor's poles *"so you can then enter the correct value in your VESC software"*
-- which is the clearest single statement of what a VESC costs a builder that
the hobby has produced.

### 10.3 What the simulator already models

The R2-D2 Astromech Simulator carries the Flipsky answer as one catalogue row
holding its own wire settings (`src/js/config/hardware.js:242-243`):

> `{id:'flipsky', label:'Flipsky FSESC + hub motors', sim:'full', note:'Brushless
> hub motors, one ESC each, R/C-mode PWM on pins 44 and 45, mixed in software.
> FOOT_CONTROLLER 1.'}`

with `buildFootPWM(b)` as a one-word predicate every consumer asks, and a wiring
sheet that draws two PWM rows rather than one serial row
(`src/js/app/wiring.js:99-105`).

### 10.4 Why builders go brushless, and what bites them

Reasons, from build logs and the ODrive-for-astromech thread: no brush wear,
better power-to-weight in a foot shell, and -- the practical one -- gearbox
motors shear teeth under a heavy droid, so hub motors solve a mechanical failure
rather than an electrical one.

Costs: motor detection and FOC tuning in VESC Tool, cogging at the creeping
speeds a droid actually uses in a crowd, and pole counts a builder has to
measure. Documented builds run 4-inch hub motors on an 18 V tool pack, and Q85
e-bike hub motors modified for R2 feet.

> [!NOTE]
> **Even the one filmed build is not of this product.** The R2 brushless build
> series uses a **Flipsky 4.2**, an older and lower-specified controller, not a
> V6. Combined with Section 10.1, that leaves **zero** documented astromech
> builds of a Flipsky V6.

## 11. Quick Reference

- Field: Framing. Value: `<start> <len> <payload> <crc-hi> <crc-lo> 0x03`.
- Field: Start byte. Value: **2** for payload 1-255, **3** for 256-65535, **4** above. The start byte is also the header length in bytes.
- Field: Shortest-header rule. Value: a 16-bit length below 255, or a 24-bit length below 65535, is **rejected**.
- Field: CRC. Value: **CRC-16/CCITT, polynomial `0x1021`, init 0, no final XOR, over the payload only, transmitted big-endian**.
- Field: Max payload. Value: `PACKET_MAX_PL_LEN` = **512**; receive buffer 520.
- Field: Error signalling. Value: **none**. A bad packet is dropped silently; there is no NAK.
- Field: Baud. Value: **115200**, 8N1, configurable in VESC Tool.
- Field: Logic level. Value: **3.3 V both directions**. Direct ESP32 connection; a 5 V host needs a level shifter.
- Field: COMM connector, Mini V6 MK5. Value: `SWITCH, ADC2, TX/SCL, RX/SDA, ADC1, GND, 3.3V, 5V`.
- Field: COMM connector, Mini FSESC6.7 PRO. Value: `ADC3, ADC2, TX/SCL, RX/SDA, ADC1, GND, 3.3V, 5V`.
- Field: Serial pins. Value: **USART3, PB10 TX, PB11 RX** -- shared with I2C2, which the firmware stops before starting the UART.
- Field: PPM capture pin. Value: **PB6, TIM4 channel 1**. Which connector pin exposes it: **UNKNOWN**.
- Field: BEC. Value: 5 V at 1 A.
- Field: Input voltage. Value: **14-60 V, 4-13S**; vendor advises 4S-12S.
- Field: `COMM_SET_DUTY`. Value: id **5**, `int32`, value / 100000.
- Field: `COMM_SET_CURRENT`. Value: id **6**, `int32`, value / 1000 amps.
- Field: `COMM_SET_RPM`. Value: id **8**, `int32`, electrical RPM.
- Field: `COMM_ALIVE`. Value: id **30**, no payload, resets the timeout.
- Field: `COMM_GET_VALUES`. Value: id **4**; selective variant id **50** takes a `uint32` mask and echoes it.
- Field: `COMM_FW_VERSION`. Value: id **0**; replies major, minor, `HW_NAME` NUL-terminated, 12-byte UUID.
- Field: `HW_NAME`. Value: `Flipsky_60_MK5` (MK5) or `Flipsky_60` (MK1) in Vedder's tree; what a shipped unit reports is **UNKNOWN** -- match on prefix.
- Field: Telemetry status bit. Value: mask **bit 21**; bit 0 = timeout active, bit 1 = kill switch active.
- Field: Destructive telemetry fields. Value: mask bits **2, 3, 4, 5, 19, 20** reset their averaging accumulators on read.
- Field: Motor timeout. Value: **1000 ms** default, and **it cannot be set over the serial protocol**.
- Field: Timeout brake current. Value: **0.0 A** default -- the motor is **released**, not braked.
- Field: Commands that reset the timeout. Value: the seven `COMM_SET_*` motor commands and `COMM_ALIVE`. **`COMM_GET_VALUES` does not.**
- Field: Kill switch. Value: `KILL_SW_MODE`, ten modes on the PPM, ADC2, ADC3, SWDIO or SWCLK pins; **disabled by default**.
- Field: ESC independent watchdog. Value: STM32 IWDG, **12 ms** window, fed by thread progress.
- Field: App required for serial control. Value: **`APP_UART`**, `APP_PPM_UART` or `APP_ADC_UART`. With `APP_PPM` the COMM header UART is **not started**.
- Field: PPM defaults. Value: control type **`NONE`** (disabled), 1.0/1.5/2.0 ms, safe start **on**, ramp 0.4 s up / 0.2 s down.

## 12. Open Items

1. **What does a shipped Flipsky unit report as `HW_NAME`, and what app is it
   pre-configured for?** Flipsky builds from their own tree and ships a
   configuration Vedder's defaults do not describe. Settled by
   `COMM_FW_VERSION` and `COMM_GET_APPCONF` on a real board -- which is also the
   cheapest possible first bench test.
2. **Which COMM pin carries PB6 (PPM)?** The two vendor drawings disagree
   (Section 4.3). Settled by continuity from the connector to the MCU pin, or by
   injecting a servo pulse and watching VESC Tool's decoded PPM.
3. **What `timeout_brake_current` is safe for a droid?** Setting it non-zero
   turns a starved ESC from coasting into braking, which is what a droid wants --
   but it is a builder-side VESC Tool setting, and a wrong value brakes hard.
   `UNKNOWN` what value the hobby would recommend; no astromech source discusses
   it because no astromech source uses UART.
4. **Does `gitcnd/VescUartLite` repeat the buffer defect?** Not read
   (Section 9.2). Matters to anyone adopting that library rather than writing the
   framing.
5. **Hub motor and pack figures for a droid.** No astromech source states current
   draw, pole count or KV for a specific R2 build. `motorpolecounter` exists
   because builders measure poles themselves. An astromech.net brushless thread
   or a build log would settle it.

## 13. Sources

**Normative (primary): VESC firmware source**

- Repository: https://github.com/vedderb/bldc
- `comm/packet.c`, `comm/packet.h` -- framing, CRC placement, resynchronisation
- `comm/commands.c` -- every command handler, telemetry layout, timeout resets
- `util/crc.c` -- the CRC-16 table and polynomial
- `util/buffer.c` -- the scaled integer encodings
- `datatypes.h` -- `COMM_PACKET_ID`, `mc_fault_code`, `app_use`, `KILL_SW_MODE`
- `timeout.c`, `timeout.h` -- the motor timeout, the kill switch, the IWDG
- `applications/app.c` -- which app starts which UART port
- `applications/appconf_default.h` -- every default quoted in this sheet
- `applications/app_uartcomm.c`, `applications/app_ppm.c`
- `hwconf/flipsky_official/flipsky_v6/hw_flipsky_60_core.h` -- **Flipsky's own
  board definition, in Vedder's tree**

**Normative (primary): vendor**

- Flipsky V6 series: https://flipsky.net/collections/v6-series
- Mini V6 MK5:
  https://flipsky.net/products/flipsky-mini-v6-mk5-with-power-button-base-on-vesc_6_mk5-with-aluminum-anodized-heat-sink
- Mini FSESC6.7 PRO 70A: https://flipsky.net/products/mini-fsesc6-7-pro-70a
- Product data read as JSON:
  https://flipsky.net/collections/v6-series/products.json
- VESC Tool and firmware downloads: https://vesc-project.com/

**Library source**

- https://github.com/SolidGeek/VescUart (read in full)
- https://github.com/gitcnd/VescUartLite
- https://github.com/LiamBindle/PyVESC
- https://github.com/RollingGecko/VescUartControl

**Ecosystem survey (primary source, non-normative)**

- https://github.com/Imperiallandm/Padawan360_mega_maestro_DYSV5W -- the only
  astromech Flipsky implementation found, read locally
- https://github.com/Imperiallandm/motorpolecounter
- https://github.com/dankraus/padawan360
- https://github.com/reeltwo/Reeltwo
- https://github.com/reeltwo/PenumbraShadowMD
- https://github.com/ProtocolCosplay/Shadow-RC
- https://github.com/RealNobser/ShadowMD
- https://github.com/mikeeddington-lgtm/r2d2-astromech-simulator -- the R2-D2
  Astromech Simulator, read locally

**Builder documentation**

- Printed Droid, foot motors: https://www.printed-droid.com/kb/foot-motor/
- Printed Droid, control systems:
  https://www.printed-droid.com/kb/control-systems-overview/
- Q85 hub motor conversion: https://r2djp.co.uk/2022/07/02/budget-q85-drive/
- "Brushless HUB Motors and ESC Setup - R2D2 Full Build", parts 4a and 4b:
  https://www.youtube.com/watch?v=L8AFCddUKxU and
  https://www.youtube.com/watch?v=9ywOSxZ97Vo
