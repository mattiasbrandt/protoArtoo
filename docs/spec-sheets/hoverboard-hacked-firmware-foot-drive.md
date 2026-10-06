# Hoverboard Hacked-Firmware Spec Sheet

A **hacked hoverboard** is the dual-motor controller out of a self-balancing
scooter, with its stock firmware erased and replaced by a community firmware
that takes speed and steering commands over a serial cable. Two firmware
families do this: EFeru's FOC firmware for Gen 1 boards and RoboDurden's
Gen2.x firmware for Gen 2 boards.

Research date 2026-09-12. There is no vendor for this product. The "product" is
a **salvaged dual-motor controller out of a self-balancing scooter, with its
stock firmware erased and replaced**, so the firmware source *is* the
specification and there is nothing else to read. Every frame byte, define,
default, timeout and pin below was read from
[`EFeru/hoverboard-firmware-hack-FOC`](https://github.com/EFeru/hoverboard-firmware-hack-FOC)
at `main`, from
[`RoboDurden/Hoverboard-Firmware-Hack-Gen2.x-GD32`](https://github.com/RoboDurden/Hoverboard-Firmware-Hack-Gen2.x-GD32)
at `main`, from their ancestors
([`flo199213`](https://github.com/flo199213/Hoverboard-Firmware-Hack-Gen2)),
or from those projects' own wikis. Claims that could not be sourced are marked
`UNKNOWN` with the artefact or bench test that would settle them.

> [!CAUTION]
> **Five facts a host author gets wrong first.**
>
> 1. **"Gen2" does not name the wire.** The 8-byte, `0xABCD`-start,
>    XOR-checksummed command frame is **EFeru's Gen1 protocol**. The Gen2
>    lineage's *own* protocol (flo199213 -> krisstakos -> RoboDurden
>    `REMOTE_UART`) is `'/'`-framed, CRC-16/CCITT, **19200 baud**, and shares
>    nothing with it. RoboDurden's `REMOTE_ROS2` borrows EFeru's frame for Gen 2
>    hardware. Section 4.
> 2. **On a RoboDurden board exactly one build option speaks the `0xABCD`
>    frame, and it ships no binary.** Only `#define REMOTE_ROS2` produces it.
>    `REMOTE_UART` -- the option whose name a builder would reach for -- does
>    not. No prebuilt `REMOTE_ROS2` image exists in either RoboDurden
>    repository; the board must be built from source in Keil. Section 4.2.
> 3. **One command value means two different things.** On EFeru's default
>    `VLT_MODE` the command is a percentage of motor voltage (+/-1000 = +/-100 %).
>    On RoboDurden `REMOTE_ROS2` the units are **RPM**, and 600 RPM is close to
>    the motor's ~640 RPM no-load ceiling. Section 6.
> 4. **Starvation is not "holds the last command", and it is not the same rule
>    twice.** EFeru holds for `SERIAL_TIMEOUT` (0.8 s) then drops to open-loop
>    zero and **coasts**; RoboDurden holds for 500 ms then ramps down under
>    power (**soft brake**) and disables the bridge. Section 7.
> 5. **The reported board temperature is not always a measurement.** RoboDurden
>    `REMOTE_ROS2` transmits a **hardcoded 250** (`boardTemp = 250; // Dummy
>    value`), which decodes as 25.0 degC forever. EFeru's is a real ADC reading
>    but its calibration constants are per-board and its own config says "very
>    inaccurate without calibration (up to 45 degC)". Section 10.

## 1. Scope

Covers: what a builder is actually buying and how to tell a usable board from an
unusable one; the two firmware families that take serial commands and the build
options within them; the command and feedback frames byte by byte; what the
command units mean on each firmware; every timeout and what the wheels do when
it fires; the electrical and connector facts needed to wire an ESP32 to the
board; the flashing procedure including readout-protection removal; what the
feedback frames measure and what they do not; and how the astromech hobby
drives feet instead.

Does not cover: the balancing/gyro function of the original scooter (erased with
the stock firmware and never recovered); the sideboard daughter-board firmware
projects, except where they contend for the same cable; the FOC control
mathematics; battery pack construction or charging; CAN, iBUS, PPM, PWM, Nunchuk
or CRSF input variants, except to say they exist and which cable they claim.

## 2. What you are actually buying

### 2.1 There is no product, there is a donor vehicle

A hacked hoverboard is not something a builder can buy new from a manufacturer.
The bill of materials is:

| Part | Where it comes from | Typical cost |
| --- | --- | --- |
| Controller board(s) | inside a self-balancing scooter, or an AliExpress replacement board | USD 10-50 for a broken scooter; USD 15-40 for a board |
| Two hub motors | the same scooter's wheels | included |
| Battery | the same scooter, 10S Li-ion | included, often the reason it was scrapped |
| **Firmware** | **a GitHub project, compiled and flashed by the builder** | free, plus an ST-Link (~USD 5) |

The firmware is the part that makes it a serial motor controller, and it is the
part the builder supplies. A hoverboard with its stock firmware cannot be
commanded over serial at all: the stock board only ever obeys its own tilt
sensors. The donor vehicle is the recognisable object; the thing on the wire is
a bare PCB the builder has already modified.

### 2.2 Two board generations, and only one of them has a "mainboard"

**Gen 1 -- one mainboard, two motors.** A single PCB with an
**STM32F103RCT6**, STM32F103RET6 or **GD32F103RCT6** (LQFP-64, 256 KB flash,
64 KB RAM, Cortex-M3 at 72 MHz) running both phase bridges, with two 4-pin
"sensor cables" running out to the gyro sideboards. This is EFeru's target.
(`wiki/Firmware-Compatibility`.)

**Gen 2 -- no mainboard at all.** Two independent boards, one bolted into each
wheel well, each carrying its own BLDC bridge and its own MCU
(**GD32F130C8**, GD32F103/STM32F103, GD32E230, MM32SPIN0x or LKS32), talking to
each other over a serial link in a **MASTER/SLAVE** pair. RoboDurden's own
header states it plainly: *"These new hoverboards have no mainboard anymore.
They consist of two Sensorboards which have their own BLDC-Bridge per Motor and
an ARM Cortex-M3 processor GD32F130C8."*
(`HoverBoardGigaDevice/Inc/comms.h`.)

That structural difference is why there are two firmware families rather than
one firmware with a board flag, and why the two families were written by
different people and never converged.

| | Gen 1 | Gen 2 |
| --- | --- | --- |
| Boards | 1 mainboard | 2, one per wheel |
| MCU | STM32F103RCT6 / GD32F103RCT6 | GD32F130C8 and four other targets |
| Flash / RAM | 256 KB / 64 KB | 64 KB / 8 KB (GD32F130C8) |
| Firmware | EFeru FOC | RoboDurden Gen2.x |
| Toolchain | PlatformIO or `arm-none-eabi-gcc` + Make | **Keil MDK v6** |
| Roles | one image | **MASTER**, **SLAVE**, or **SINGLE** |
| Gyro sideboards | separate daughter boards | integrated on each motor board |

**Boards neither family supports.** Artery **AT32F403RCT6 / AT32F413RCT7** are
*"the cheapest and more common ones available on Aliexpress now"* and are not
compatible with EFeru's FOC firmware; separate forks exist
(`cloidnerux/hoverboard-firmware-hack`, `thanek/hoverboard-firmware-hack`), and
neither fork is covered by this sheet. A builder who buys the cheapest
replacement board on AliExpress has a good chance of getting one of these.
(`wiki/Firmware-Compatibility`, "Other boards".)

RoboDurden also maintains a `BEWARE of these hoverboards/` directory in the
Gen2.x repository -- at the time of reading, the "H6 Eco" board.

### 2.3 The motors

Read from `wiki/Hoverboard-Wheels`, which is the only characterised source for
these.

| Property | Value |
| --- | --- |
| Type | 3-phase sensored BLDC hub motor, direct drive |
| Pole pairs | **15** (all sizes) |
| Winding | wye / star |
| Hall sensors | 3, **120 deg apart**, 4 deg resolution |
| Sensorless operation | **not supported by either firmware** |
| kV (star) | **~16 rpm/V** -> ~640 rpm no-load at 40 V |
| kV (delta rewired) | ~28 rpm/V -> ~1120 rpm no-load (measured by the wiki author) |
| Sizes | 4.5", 6.5", 8", 8.5", 10" |
| Power rating | 180 W (4.5") to 350 W (10"); 6.5" is 200-300 W |

RoboDurden's `config.h` carries a slightly different community figure in a
comment: *"Hoverboard motor: 14 rpm/V * 50V = 700 rpm"*. Take 14-16 rpm/V as the
band; nobody has published a measurement with a torque curve.

**What that means for a droid.** At 36 V nominal a 6.5" wheel (165 mm, 0.518 m
circumference) turning 576 rpm covers 4.97 m/s -- **about 18 km/h**. A droid
walking with a crowd wants roughly 1.4 m/s (5 km/h), which is **under 30 % of
the motor's no-load speed**. The gearing is fixed: the motor *is* the wheel, so
the only way down is the command value. A command of 600 (60 % on EFeru's
default mode) is not a slow setting; Section 6 works the numbers per firmware.

**The known low-speed problem.** These motors commutate off three hall sensors
at 4 deg resolution, and the hobby's consistent complaint is cogging and
stuttering near zero rpm -- which is exactly the regime a droid mingling in a
crowd lives in. EFeru's own troubleshooting page treats cogging as a phase or
hall mapping problem; the wider robotics community treats it as inherent to
hall-commutated hub motors at walking pace. No measurement of it on a droid has
been published (Open Item 1). It is the single most likely unpleasant surprise
on a first full drive test, and it is a property of the motor, not of any host
code.

### 2.4 Buying advice, from the people who wrote the firmware

EFeru's `wiki/Buying-a-used-hoverboard` is short and worth quoting in full for a
builder:

- *"It's very hard to know if an hoverboard has a controller supported by this
  firmware, there are too many brands/models. Usually the older models have
  single/mainboards."*
- Ask the seller for **the chip model** before buying, and check it against the
  compatibility list.
- If it powers on, tilt-test both wheels; if it does not spin, the sideboard is
  probably dead.
- If it does not power on, the battery is usually the fault -- try it on the
  charger to prove the controller lives.
- **Spin the wheels by hand.** Cogging means two MOSFETs of one phase are
  shorted. Wheels that will not turn at all mean water damage: *"you can throw
  it away."*

## 3. Sources Checked

Read in full or in the named part:

| Source | What it settled |
| --- | --- |
| `EFeru/.../Inc/config.h` (55.9 KB) | every variant, limit, threshold and default in Sections 4.1, 6, 7 |
| `EFeru/.../Inc/util.h` | `SerialCommand` and `SerialSideboard` layouts |
| `EFeru/.../Src/main.c` | `SerialFeedback` layout, feedback rate, enable gate, poweroff and beep codes |
| `EFeru/.../Src/util.c` | the timeout safe state, checksum validation, the mixer |
| `EFeru/.../Arduino/hoverserial/hoverserial.ino` | the reference host's framing and 100 ms send rate |
| `EFeru` wiki: Firmware-Compatibility, Hoverboard-Wheels, Buying-a-used-hoverboard, How-to-Unlock-MCU-Flash | pin map, shunts, motor characteristics, unlock procedure |
| `RoboDurden/...-Gen2.x` `README.md` | the code move, the board-numbering scheme, the layout list |
| `RoboDurden/...-Gen2.x-GD32` `Inc/config.h`, `Inc/defines.h` | remotes, driving modes, cell thresholds, `TIMEOUT_MS` |
| `RoboDurden/...-Gen2.x-GD32` `Inc/RemoteROS2.h`, `Src/RemoteROS2.c` | **the 26-byte `REMOTE_ROS2` frames** |
| `RoboDurden/...-Gen2.x-GD32` `Inc/remoteUart.h`, `Arduino Examples/TestSpeed/hoverserial.h` | the `REMOTE_UART` protocol, which is a different wire |
| `RoboDurden/...-Gen2.x-GD32` `Src/bldc.c`, `Src/main.c`, `Src/it.c` | the soft-brake path and the two timeout layers |
| `flo199213/.../Src/commsSteering.c` | the Gen2 lineage's original `'/'` + CRC-16 protocol |
| `Candas1/hoverboard-firmware-hack-FOC` `Src/main.c` | confirmed 18-byte feedback; no current fields |
| GitHub code search, three queries | no upstream other than `RemoteROS2` emits the 26-byte frame |
| Source trees of `ShadowMD`, `Padawan360_mega_maestro_DYSV5W`, `r2d2-astromech-simulator` | the negative result in Section 11 |

**Three secondary-source claims were contradicted by the source and are recorded
so nobody repeats them:**

1. *"The hoverboard serial link is 9 data bits at 26315 baud."* That is the
   **stock** gyro-sideboard-to-mainboard link, reverse-engineered by Hackaday in
   2016. Both hacked firmwares run the sensor cables at **115200 8N1**
   (`USART2_BAUD` / `USART3_BAUD`, `Inc/config.h:658,664`). The 26315 figure has
   no bearing on a host talking to a hacked board.
2. *"On serial timeout EFeru sets `pwml = 0; pwmr = 0; enable = 0`."* That code
   exists but is inside `#ifdef VARIANT_TRANSPOTTER` (`Src/main.c:415-424`) and
   is the GameTrak variant's general-input timeout. The real serial-timeout path
   is `Src/util.c:1031-1036` and does something different (Section 7.1).
3. *"Hoverboard motors are ~16 kV, so ~20,700 rpm no-load at 36 V."* The figure
   is 16 **rpm per volt**, not kV in the RC sense, giving ~640 rpm (Section 2.3).
   A hub motor turning 20,700 rpm would be a turbine.

## 4. The two firmwares, and the protocols that look alike

This is the section the rest of the document depends on. There are **two**
firmware families. They are separate projects, for separate hardware, written
by separate people, and they agree on a wire format only because one of them
deliberately copied the other for one build option.

### 4.1 EFeru FOC -- the common one

[`EFeru/hoverboard-firmware-hack-FOC`](https://github.com/EFeru/hoverboard-firmware-hack-FOC),
GPL-3.0, default branch `main`, ~1.8k stars, **no tagged releases** (builders
work from `main`). Targets Gen 1 mainboards. Descended from
NiklasFauth's original hack; the headline difference is Field Oriented Control
in place of block commutation, which is why it is quieter and smoother at low
speed than its ancestor.

**Input variants**, one selected at build time in `Inc/config.h`. Each claims
particular cables, which is why they are mutually exclusive:

| Variant | Input | Cable it claims |
| --- | --- | --- |
| `VARIANT_ADC` | two potentiometers | left sensor cable (ADC1/ADC2) |
| **`VARIANT_USART`** | **serial commands** | left cable by default; right is an option |
| `VARIANT_NUNCHUK` | Wii Nunchuk over I2C | right cable |
| `VARIANT_PPM` | R/C PPM-sum | either cable |
| `VARIANT_PWM` | two R/C servo channels | either cable |
| `VARIANT_IBUS` | FlySky iBUS, 14 channels | right cable (USART3) |
| `VARIANT_HOVERCAR` | two pedals + sideboard | both |
| `VARIANT_HOVERBOARD` | the two stock gyro sideboards | both |
| `VARIANT_TRANSPOTTER` | GameTrak follow-me | both |
| `VARIANT_SKATEBOARD` | R/C PWM, torque mode | right cable |

`VARIANT_USART` as shipped (`Inc/config.h`):

```c
#define CONTROL_SERIAL_USART2  0    // left sensor board cable
#define FEEDBACK_SERIAL_USART2      // left sensor board cable
#define PRI_INPUT1             3, -1000, 0, 1000, 0     // TYPE, MIN, MID, MAX, DEADBAND
#define PRI_INPUT2             3, -1000, 0, 1000, 0
```

`CONTROL_SERIAL_USARTn` makes the board obey commands on that cable;
`FEEDBACK_SERIAL_USARTn` makes it send feedback frames on it. They are separate
defines, so a board built with only the first drives and never answers. The
right-cable pair is `CONTROL_SERIAL_USART3 0` and `FEEDBACK_SERIAL_USART3`.
In `PRI_INPUTn`, `TYPE 3` is auto-detect, the range is +/-1000, and `DEADBAND 0`
leaves deadband to the host. PlatformIO offers each variant as an environment of
the same name, `VARIANT_USART` among them.

Note the default is **USART2, the left cable** -- and USART2 is the one that is
**not 5 V tolerant**. EFeru's own Arduino example recommends the opposite:
*"Option 1: Serial on Right Sensor cable (short wired cable) - recommended,
since the USART3 pins are 5V tolerant."* An ESP32 is a 3.3 V part so either
works electrically, but a builder following the config default and a builder
following the example end up on different cables. Section 8.2.

Other build options change what arrives on the wire and are off by default:

- `ENABLE_ODOMETRY` (`Inc/config.h:117`) **adds two int16 fields to the middle
  of the feedback frame**, making it 22 bytes (Section 5.2). It is what a
  builder enables for wheel distance, and ROS integration guides turn it on.
- `DUAL_INPUTS` adds a sideboard as an auxiliary input with priority switching.
- `DEBUG_SERIAL_USART2` / `DEBUG_SERIAL_USART3` print ASCII debug output on
  that cable; the config file's own comments say to disable debug on a cable
  that carries serial control.
- `TANK_STEERING` bypasses the mixer (Section 6.1).

Defining a second `VARIANT_*` either fails the config file's own build checks
or leaves two variants claiming the same cable.

### 4.2 RoboDurden Gen2.x -- the Gen 2 one

The lineage, which matters because each step changed the protocol:

```
flo199213/Hoverboard-Firmware-Hack-Gen2        (2018, GD32F130C8, '/' + CRC-16)
        |
        v
krisstakos/Hoverboard-Firmware-Hack-Gen2.1     (2022)
        |
        v
RoboDurden/Hoverboard-Firmware-Hack-Gen2.x     (binaries, wiki, board catalogue)
        |
        v
RoboDurden/Hoverboard-Firmware-Hack-Gen2.x-GD32  <-- THE CODE LIVES HERE NOW
```

> [!WARNING]
> **`RoboDurden/Hoverboard-Firmware-Hack-Gen2.x` no longer contains the
> firmware.** Its `README.md` says: *"code moved to
> Hoverboard-Firmware-Hack-Gen2.x-GD32 !!"*. What remains at that URL is
> prebuilt binaries, board photographs, a wiki with an interactive board finder,
> and vendor manuals. A citation of the protocol should point at
> `Hoverboard-Firmware-Hack-Gen2.x-GD32`, ideally at
> `HoverBoardGigaDevice/Src/RemoteROS2.c`, since the repository has eight
> remotes and only one of them is this frame.

Both repositories are GPL-3.0. `Gen2.x` has ~172 stars, `Gen2.x-GD32` ~48; both
were pushed to within the last ten days of this research date.

**Board numbering.** Since 2024-02-18 boards are `Gen2.t.v`: target, then
version. `Gen2.1.4xf` reads as *split board, target 1 (GD32F130), layout 4,
64 KB flash, FOC-capable*. Targets are the Keil targets: 1 = GD32F130,
2 = GD/STM32F103, 3 = GD32E230, 4 = MM32SPIN0x, 5 = LKS32. The target is
selected in the Keil project's target dropdown, not in the config file. Roughly
thirty layouts are catalogued; seven were listed "ready to use (99%)" at last
README update.

> [!CAUTION]
> **`#define LAYOUT x` must match the physical board** (and `LAYOUT_SUB` where
> the layout has one). RoboDurden's README: *"Make sure your board is one of the
> supported layouts! **Wrong pin assignments can shortcut the battery and kill
> the mosfets!!**"* This is the one step in the whole procedure that destroys
> hardware rather than just failing. Identify the board with the interactive
> finder in the `Gen2.x` wiki first.

**Roles.** `MASTER`, `SLAVE` or `SINGLE` (`Inc/config.h`). A two-wheel droid is
a MASTER board (which talks to the host) plus a SLAVE board (which talks only
to the master), one image each. A `SINGLE` build drives one wheel and reports
zero for the other.

**Remotes** -- how the board is commanded. Exactly one is compiled in:

| Remote | Protocol on the wire | Baud | Speaks the `0xABCD` frame |
| --- | --- | --- | --- |
| `REMOTE_DUMMY` | none; sweeps -300..300 on its own | -- | no (and it moves by itself) |
| `REMOTE_UART` | `'/'` start byte, **CRC-16/CCITT** | **19200** | **no** |
| `REMOTE_UARTBUS` | `'/'` + slave id, CRC-16/CCITT | 19200 | no |
| `REMOTE_CRSF` | CRSF (an R/C link protocol) | -- | no |
| **`REMOTE_ROS2`** | **`0xABCD`, XOR checksum** | **115200** | **yes -- the only one** |
| `REMOTE_ADC` | two potentiometers | -- | no |
| `REMOTE_AUTODETECT` | a hall-pin discovery tool | -- | no (a diagnostic build) |
| `REMOTE_OPTIMIZEPID` | a PID tuning build | -- | no |

`REMOTE_ROS2` exists because someone wanted RoboDurden boards to work with the
existing ROS `hoverboard-driver`, which was written against EFeru. So it speaks
**EFeru's dialect** -- `0xABCD`, XOR, 115200 -- on Gen2 hardware. Its own header
says so: the `Pilot()` function carries the comment *"Reverse of
hoverboard-driver code"*, citing `hoverboard-robotics/hoverboard-driver`.
`REMOTE_BAUD` is 115200 inside `Inc/RemoteROS2.h` and needs no edit.

That is why one host parser can serve both firmware families. It is not a
convergence of the two projects; it is one build option in one of them,
borrowing the other's frame.

`REMOTE_ROS2` will not even compile unless two other settings agree
(`Src/RemoteROS2.c`):

```c
#ifndef SPEED_AsRevsPerSec
  #error "RemoteROS2 assumes SPEED_AsRevsPerSec is defined"
#endif
#if DRIVING_MODE != 1
  #error "RemoteROS2 assumes DRIVING_MODE == 1"
#endif
```

`DRIVING_MODE 1` is **speed mode, in revs/s * 1024**. This is where the unit
difference in Section 6 comes from, and it is not optional: the firmware refuses
to build any other way. The feedback code multiplies by 60 to report RPM.

> [!IMPORTANT]
> **No prebuilt `REMOTE_ROS2` binary exists.** `BinariesReadyToFlash/` in the
> `Gen2.x` repository carries images named `master Dummy`, `master Uart`,
> `slave` and `single uartBus` for each layout. `BinariesToTest/` adds
> `autodetect` builds. Neither directory carries a ROS2 image, and
> `Gen2.x-GD32/BinariesToFlash/` holds a single `GD32F130.bin`. A builder
> who wants the `0xABCD` frame on a RoboDurden board must **build from source
> in Keil MDK v6** with `REMOTE_ROS2`, `DRIVING_MODE 1` and `SPEED_AsRevsPerSec`
> set, and the correct `LAYOUT`. That is a materially higher bar than the EFeru
> path, which is PlatformIO and one `make flash`. There is no PlatformIO or GCC
> path for this firmware.

### 4.3 The three protocols that all start near `0xABCD`

Read this table before writing any code that says "the hoverboard protocol".

| | EFeru `VARIANT_USART` | RoboDurden `REMOTE_ROS2` | RoboDurden `REMOTE_UART` |
| --- | --- | --- | --- |
| Host -> board start | `0xABCD` (2 bytes) | `0xABCD` (2 bytes) | `'/'` (**1 byte**, 0x2F) |
| Host -> board size | **8** bytes | **8** bytes | **9** bytes |
| Board -> host start | `0xABCD` | `0xABCD` | `0xABCD` |
| Board -> host size | **18** bytes (22 with odometry) | **26** bytes | **22** bytes (36 with IMU) |
| Checksum | **XOR** of all uint16 words | **XOR** of all uint16 words | **CRC-16/CCITT**, poly 0x1021, init 0 |
| Baud | 115200 | 115200 | **19200** |
| Command units | PWM / voltage percent (default) | **RPM** | PWM |

A builder who flashes `master Uart` -- the obvious-sounding prebuilt binary --
gets a board that is listening at 19200 for a CRC-16-checked `'/'` frame. A host
transmitting 115200 XOR-checked `0xABCD` frames to it gets no movement and no
reply, and nothing on either side reports an error.

## 5. The wire (normative)

Little-endian throughout, 8N1. `0xABCD` appears on the wire as `CD AB`.

### 5.1 Command frame -- host to board (8 bytes)

Identical in EFeru `VARIANT_USART` (`Inc/util.h:38-43`) and RoboDurden
`REMOTE_ROS2` (`Src/RemoteROS2.c`):

```c
typedef struct {
   uint16_t start;     // 0xABCD
   int16_t  steer;
   int16_t  speed;
   uint16_t checksum;  // start ^ steer ^ speed
} SerialCommand;
```

| Offset | Size | Field | Notes |
| --- | --- | --- | --- |
| 0 | 2 | `start` | `0xABCD` |
| 2 | 2 | `steer` | signed; see Section 6 for units |
| 4 | 2 | `speed` | signed; see Section 6 for units |
| 6 | 2 | `checksum` | `start ^ steer ^ speed` |

**`steer` comes before `speed`.** A host API that takes `(speed, steer)` has to
swap them onto the wire. A transposition here turns a throttle command into a
spin and fails no build.

Validation is identical on both firmwares: start word must match, then the XOR
must match, or the frame is silently discarded and the timeout counter keeps
running (`EFeru Src/util.c:1295-1300`, `RoboDurden Src/RemoteROS2.c`
`RemoteCallback()`).

The reference hosts send at **10 Hz**: EFeru's `hoverserial.ino` with
`TIME_SEND 100`, and RoboDurden's examples with `SEND_INTERVAL_MS 100`. At
115200 baud an 8-byte frame occupies ~0.7 ms, so even a 50 Hz stream loads the
line ~3.5 % outbound.

### 5.2 Feedback frame -- EFeru, 18 bytes

`Src/main.c:125-139`, transmitted every **10 ms (100 Hz)** by DMA
(`Src/main.c:522`, `main_loop_counter % 2` against a 5 ms loop).

| Offset | Field | Type | Unit / meaning |
| --- | --- | --- | --- |
| 0 | `start` | uint16 | `0xABCD` |
| 2 | `cmd1` | int16 | normalized input 1 (**steer**) echoed back |
| 4 | `cmd2` | int16 | normalized input 2 (**speed**) echoed back |
| 6 | `speedR_meas` | int16 | right motor, **RPM** (`rtY_Right.n_mot`) |
| 8 | `speedL_meas` | int16 | left motor, **RPM** |
| 10 | `batVoltage` | int16 | **V x 100**, calibrated (`batVoltageCalib`) |
| 12 | `boardTemp` | int16 | **degC x 10** (`board_temp_deg_c`) |
| 14 | `cmdLed` | uint16 | sideboard LED word |
| 16 | `checksum` | uint16 | XOR of the eight words above |

There are **no current fields**. A host that shows current for an EFeru board
has nothing to show.

With `ENABLE_ODOMETRY` defined, `wheelR_cnt` and `wheelL_cnt` are inserted at
offsets 10 and 12 and everything after shifts by 4 bytes, giving a **22-byte**
frame. A host that accepts only 18 and 26 bytes never decodes it.

### 5.3 Feedback frame -- RoboDurden `REMOTE_ROS2`, 26 bytes

`Src/RemoteROS2.c`, transmitted every **100 ms (10 Hz)**
(`SEND_INTERVAL_MS 100`, `Inc/RemoteROS2.h`).

| Offset | Field | Type | Unit / meaning |
| --- | --- | --- | --- |
| 0 | `start` | uint16 | `0xABCD` |
| 2 | `cmd1` | int16 | `bldc_inputFilterPwm` -- *"Not used by ROS2"* |
| 4 | `cmd2` | int16 | `bldc_outputFilterPwm` -- *"Not used by ROS2"* |
| 6 | `speedR_meas` | int16 | **RPM**, from the SLAVE board, sign negated |
| 8 | `speedL_meas` | int16 | **RPM**, from this (MASTER) board |
| 10 | `wheelR_cnt` | int16 | hall odometer, wraps at `ENCODER_MAX` = **9000** |
| 12 | `wheelL_cnt` | int16 | hall odometer, wraps at 9000 |
| 14 | `left_dc_curr` | int16 | **A x 100**, DC-link current, this board |
| 16 | `right_dc_curr` | int16 | **A x 100**, DC-link current, slave board |
| 18 | `batVoltage` | int16 | **V x 100** |
| 20 | `boardTemp` | int16 | **hardcoded 250** -- see Section 10.2 |
| 22 | `cmdLed` | uint16 | `revs32` -- *"Not used by ROS2"* |
| 24 | `checksum` | uint16 | XOR of the twelve words above |

On a `SINGLE` build (one board, one wheel) `speedR_meas`, `wheelR_cnt` and
`right_dc_curr` are **hardcoded to 0**, so a one-wheel bench rig reports a dead
right motor rather than an absent one.

### 5.4 Checksum

Both feedback formats are validated the same way and it is worth stating
precisely because it is unusual: the checksum field is itself included, so
**XOR of every uint16 word in the frame equals zero** for a valid frame -- over
9 words for the 18-byte frame and 13 for the 26-byte frame. It is
arithmetically equivalent to the firmware's "XOR all fields except the checksum,
compare", and it is the cheaper form.

An XOR checksum detects any single-bit error and any odd number of bit errors in
a column, but **not** a swapped pair of words, and it will pass a random frame
once in 65536. It is not a CRC and should not be described as one. A host that
tries the 18-byte layout before the 26-byte one can therefore mistake a 26-byte
frame whose first nine words XOR to zero for an 18-byte one; latching the format
after the first valid frame confines that to the first frame after a reset.

## 6. What a command value means, and why it is not one answer

Both firmwares take `speed` and `steer` as signed 16-bit values with a nominal
range of +/-1000. What arrives at the wheels for a given value differs by
firmware.

### 6.1 EFeru: a percentage, softened twice

EFeru's defaults (`Inc/config.h:157-164`):

```c
#define CTRL_TYP_SEL    FOC_CTRL        // COM_CTRL, SIN_CTRL, FOC_CTRL (default)
#define CTRL_MOD_REQ    VLT_MODE        // OPEN_MODE, VLT_MODE (default), SPD_MODE, TRQ_MODE
#define I_MOT_MAX       15              // [A] per motor
#define I_DC_MAX        17              // [A] DC link
#define N_MOT_MAX       1000            // [rpm]
```

In the default `VLT_MODE`, the command is a **voltage/duty request**: +/-1000
maps to +/-100 %. So a command of 600 asks for 60 % of motor voltage, giving
roughly 0.6 x 640 = **~380 rpm no-load**, about 12 km/h on a 6.5" wheel. A
command of 200 is about 4 km/h, which is walking pace.

`SPD_MODE` (closed-loop rpm) and `TRQ_MODE` (current) exist and are **FOC-only**.
A builder who selects `SPD_MODE` changes what every host command value means,
and nothing in the firmware announces it. `I_MOT_MAX` and `N_MOT_MAX` are the
board's own ceilings (15 A, 1000 rpm by default); lowering `N_MOT_MAX` caps the
top speed on the board itself, whatever the host sends.

Two filters sit between the frame and the motor and they are not optional
(`Src/main.c:328-334`):

```c
rateLimiter16(input1[inIdx].cmd, rate, &steerRateFixdt);
rateLimiter16(input2[inIdx].cmd, rate, &speedRateFixdt);
filtLowPass32(steerRateFixdt >> 4, FILTER, &steerFixdt);
filtLowPass32(speedRateFixdt >> 4, FILTER, &speedFixdt);
```

With `DEFAULT_RATE 480` (30.0 units per 5 ms loop) a step from 0 to 1000 takes
**about 167 ms** to arrive, and `DEFAULT_FILTER 6553` (0.1) smooths it further.
The board has its own ramp. A host stream at 10-50 Hz is well inside it, and an
instantaneous zero command still takes the wheels a fraction of a second to
unwind -- which is a property of the board, not of the host.

The mixer (`Src/util.c:1706-1723`), with `DEFAULT_SPEED_COEFFICIENT 16384` (1.0)
and `DEFAULT_STEER_COEFFICIENT 8192` (0.5):

```
speedR = speed - 0.5 * steer
speedL = speed + 0.5 * steer
```

so **positive steer turns toward the right wheel slowing**. `TANK_STEERING` is a
build option that bypasses the mixer entirely and maps `steer` -> left,
`speed` -> right; a host that sends mixed speed/steer to such a board spins
where it should drive, silently.

### 6.2 RoboDurden `REMOTE_ROS2`: RPM, with no percentage anywhere

The struct comments are unambiguous (`Src/RemoteROS2.c`):

```c
typedef struct {
   uint16_t start; // START_FRAME 0xABCD
   int16_t  steer; // Difference between left and right wheel speed. Unit: RPM
   int16_t  speed; // Average of left and right wheel speed. Unit: RPM
   uint16_t checksum;
} SerialCommand;
```

and the mixer:

```c
void Pilot(int16_t* pPwmMaster, int16_t* pPwmSlave)
{
    *pPwmMaster = ( (speed + (steer/2)) *1024) / 60;
    *pPwmSlave  = ( (speed - (steer/2)) *1024) / 60;
}
```

MASTER is the board reporting `speedL_meas`, so master = left. The steering sign
therefore **agrees with EFeru**: left = speed + steer/2, right = speed - steer/2.
That is a genuine convenience and worth stating, because it is the one thing
about these two firmwares that does line up.

What does not line up is the scale. `speed` is a **target RPM** fed to a
closed-loop speed controller, so:

| Command value | EFeru (`VLT_MODE`) | RoboDurden (`REMOTE_ROS2`) |
| --- | --- | --- |
| 200 | 20 % voltage, ~130 rpm, ~4 km/h | **200 rpm** commanded, ~6 km/h |
| 350 | 35 % voltage, ~220 rpm, ~7 km/h | **350 rpm** commanded, ~11 km/h |
| 600 | 60 % voltage, ~380 rpm, ~12 km/h | **600 rpm** commanded, ~19 km/h |

The RPM column is a *target the controller will work to reach*, so it does not
sag under load the way the voltage column does -- it will pull harder instead.
Speeds assume a 6.5" wheel; scale by wheel diameter.

> [!WARNING]
> **On a RoboDurden board, a command of 600 is approximately the motor's
> no-load ceiling.** 600 rpm against a ~640 rpm free-running motor is not a 60 %
> setting, it is a ~94 % setting, and the closed loop will try to hold it. The
> same host configuration moved from an EFeru board to a RoboDurden board gives
> a materially faster droid, and neither firmware tells the host which one it
> is.

## 7. Starvation, timeouts, and what the wheels actually do

When the command stream stops, both firmwares first keep acting on the last
valid command, and then diverge.

### 7.1 EFeru: 0.8 s of holding, then an open-loop coast

`SERIAL_TIMEOUT` is **160** loop iterations at 5 ms = **0.8 s**
(`Inc/config.h:654`, whose own comment does the arithmetic). Until it fires,
nothing zeroes the command: the last valid `speed`/`steer` keep the motors
turning. When it fires (`Src/util.c:1031-1036`):

```c
// In case of timeout bring the system to a Safe State
if (timeoutFlgADC || timeoutFlgSerial || timeoutFlgGen) {
  ctrlModReq  = OPEN_MODE;   // Request OPEN_MODE. This will bring the motor power to 0 in a controlled way
  input1[inIdx].cmd  = 0;
  input2[inIdx].cmd  = 0;
}
```

`OPEN_MODE` with zero command means **no drive and no braking**: the wheels
free-wheel. A starved droid rolls; it does not stop. Note also that `enable` is
**not** cleared here, so the moment valid frames resume the board drives again
without any re-arming handshake.

Three low-pitch beeps on the buzzer signal a serial timeout
(`Src/main.c:581`). That is the only outward sign, and many boards have no
buzzer fitted (`wiki/Firmware-Compatibility`: *"Missing on some of the
boards"*).

At boot EFeru arms the motors only if both inputs are near zero (`< 50`,
`Src/main.c:270`), and beeps twice when it does; a host sending zero commands
from power-up satisfies that.

### 7.2 RoboDurden: 0.5 s of holding, then a soft brake, then 2 s hard off

Two layers, both real:

**Layer 1 -- `LOST_CONNECTION_STOP_MILLIS` = 500 ms** (`Inc/RemoteROS2.h`).
`RemoteUpdate()` runs every 10 ms and checks:

```c
if (millis() - iTimeLastRx > LOST_CONNECTION_STOP_MILLIS)
{
    bRemoteTimeout = 1;
    speed = steer = 0;
}
```

`bRemoteTimeout` clears `enable` in the main loop (`Src/main.c:436`), which
reaches `bldc.c:220-228`:

```c
if (currentDC > DC_CUR_LIMIT  || bldc_enable == RESET  || timedOut == SET)
{
    iDrivingModeOverride = bldc_inputFilterPwm = iBldcInput = 0;
    SetFilter(14);  // soft brake
    if (ABS(bldc_outputFilterPwm)<100)
    {
        timer_automatic_output_disable(TIMER_BLDC);
    }
}
```

The PWM output filter is switched to a slow decay and the demand set to zero, so
the wheels **ramp down under power** and the bridge is only released once the
output has nearly died. That is a gentler and more predictable stop than EFeru's
coast. (Read from source; not measured.)

**Layer 2 -- `TIMEOUT_MS` = 2000 ms** (`Inc/defines.h:242`, *"Time in
milliseconds without steering commands before pwm emergency off"*), a 1 kHz
timer interrupt that sets `timedOut` and forces the same disable path
unconditionally.

The same `bldc.c` condition also fires on `currentDC > DC_CUR_LIMIT` (15 A,
`Inc/config.h`), so a stalled droid soft-brakes itself rather than cooking the
bridge.

### 7.3 The two starvation rules side by side

| | EFeru | RoboDurden ROS2 |
| --- | --- | --- |
| Command held for | 800 ms | 500 ms |
| Then | open-loop **coast** | **soft brake**, then bridge off |
| Hard cutoff | none (open mode is the cutoff) | 2000 ms, emergency off |

A host that streams commands faster than either timeout -- a zero frame
included -- has a zero command reach the wheels in one frame period, rather than
waiting for the board's own timeout to notice silence.

### 7.4 Protections the board keeps to itself

Neither firmware tells the host any of this; the host finds out by the wheels
changing behaviour.

| Protection | EFeru | RoboDurden |
| --- | --- | --- |
| Undervoltage poweroff | `BAT_DEAD` = 3.37 V/cell, `BAT_DEAD_ENABLE 1`, only while `speedAvgAbs < 20` | `CELL_LOW_DEAD` 3.0 V/cell, but `BATTERY_LOW_SHUTOFF` **commented out** by default |
| Low-battery warning | `BAT_LVL1` 3.5 V/cell (fast beep, enabled), `BAT_LVL2` 3.6 V/cell (disabled) | `CELL_LOW_LVL1` 3.5, `CELL_LOW_LVL2` 3.3, `BATTERY_LOW_BEEP` enabled |
| Cells assumed | `BAT_CELLS 10` | `BAT_CELLS 10` |
| Overtemperature | `TEMP_POWEROFF` 65.0 degC -- **`TEMP_POWEROFF_ENABLE 0`** | none found |
| Temperature warning | `TEMP_WARNING` 60.0 degC -- **`TEMP_WARNING_ENABLE 0`** | none found |
| Motor current | `I_MOT_MAX` 15 A, `I_DC_MAX` 17 A | `DC_CUR_LIMIT` 15 A, current chopping |
| Inactivity poweroff | `INACTIVITY_TIMEOUT` **8 minutes** | `INACTIVITY_TIMEOUT` **8 minutes** |
| Motor fault | one low beep, `enable = 0` | -- |

> [!CAUTION]
> **A hoverboard board powers itself off after 8 minutes of standing still.**
> Both firmwares default to `INACTIVITY_TIMEOUT 8` minutes, measured on the
> *motor command* (EFeru: `speedAvgAbs`; RoboDurden: `ABS(pwmMaster) > 50 ||
> ABS(pwmSlave) > 50`). A droid parked for a photo line or standing on static
> display will latch its own power off and need its button pressed. A host
> streaming zero commands cannot see it coming and cannot prevent it over the
> serial line. `UNKNOWN`: whether the board announces it (EFeru calls
> `poweroff()`, which beeps). Settled by a bench test, not by reading more
> source (Open Item 2).

> [!NOTE]
> **Both temperature protections are disabled by default** and EFeru's config
> says why: *"It is very inaccurate without calibration (up to 45 degC). So only
> enable this funcion after calibration!"* A drive board therefore has no
> working thermal cutout unless the builder calibrated it, which is a fact about
> the hardware and not something a host can supply.

## 8. Electrical and wiring

### 8.1 Gen 1 mainboard, from EFeru's pin map

`wiki/Firmware-Compatibility`, "Pin Mapping". Board Variant 0 unless noted.

| Signal | Pin | Note |
| --- | --- | --- |
| SWCLK / SWDIO | PA14 / PA13 | programming |
| TX / ADC1 / PWM | PA2 | **left** sensor cable |
| RX / ADC2 / PWM / PPM / IBUS | PA3 | **left** sensor cable |
| TX / SCL / PWM | PB10 | **right** sensor cable |
| RX / SDA / PWM / PPM / IBUS | PB11 | **right** sensor cable |
| Hall left U/V/W | PB5 / PB6 / PB7 | |
| Hall right U/V/W | PC10 / PC11 / PC12 | |
| Phase left U/V/W high | PC6 / PC7 / PC8 | |
| Phase right U/V/W high | PA8 / PA9 / PA10 | |
| `LEFT_DC_CUR` / `RIGHT_DC_CUR` | PC0 / PC1 | **3.5 mOhm or 2 x 7 mOhm shunt**; *"Missing on some of the boards, so no dc current limitation"* |
| `LEFT_U_CUR` etc. | PA0, PC3, PC4, PC5 | phase current via low-side MOSFET RDSon as the shunt |
| `DCLINK` | PC2 (PA1 on variant 1) | battery voltage through a divider |
| `OFF` | PA5 (PC15) | holds the power latch; released to power off |
| `BUTTON` | PA1 (PB9) | power, calibration, on-the-go limits |
| `BUZZER` | PA4 (PC13) | diagnostic beeps; *"Missing on some of the boards"* |
| `LED` | PB2 | *"Missing on some of the boards"* |
| `CHARGER` | PA12 (PA11) | *"Not used in the firmware at the moment"* |

Reverse-engineered schematics exist in that repository as
`docs/20150722_hoverboard_sch.pdf`.

### 8.2 The sensor cables, and the 5 V trap

Each sensor cable carries GND, a supply, and a UART pair.

> [!CAUTION]
> **USART2 (left cable) is not 5 V tolerant. USART3 (right cable) is.** EFeru's
> Arduino example states both. An ESP32 is a 3.3 V part so either is safe for
> it, but note that EFeru's `VARIANT_USART` *ships pointed at USART2* while the
> example recommends USART3 -- a builder following one and a guide written from
> the other will disagree about which cable to land on.

> [!CAUTION]
> **The red wire on a sensor cable is 12-15 V, not 5 V.** Every variant block in
> EFeru's `config.h` repeats the warning. On some boards the *black* wire is
> live too -- verify with a multimeter before assuming a ground. And
> `wiki/Firmware-Compatibility` adds: on boards with a 6-wire hall cable
> (an extra white wire), *"The sideboard on the right hand side is powered with
> battery voltage on those boards"* -- 36-42 V on a connector that looks like
> the other one.

Wire color convention as documented (GND black, supply red, TX green, RX blue)
is a convention, not a standard. `UNKNOWN`: the connector part number. These are
generic 4-pin JST-style housings and no source names a manufacturer part
(Open Item 3).

### 8.3 RoboDurden: which header the host lands on

`Inc/config.h` sets, for `REMOTE_UART`, `REMOTE_UARTBUS`, `REMOTE_CRSF` and
`REMOTE_ROS2`:

```c
#define REMOTE_USART  0   //  1 is usually PA2/PA3 and the original master-slave 4pin header
                          //  0 is usually PB6/PB7 and the empty header close to the flash-header
                          //  2 is usually PB10/PB11 on stm32f103 boards
```

So on a Gen2 board the controller does **not** connect to the obvious 4-pin
master-slave header -- that one is reserved for the MASTER-SLAVE link between
the two motor boards (`MASTERSLAVE_USART 1`). The host goes to **the empty
header near the flash header, PB6/PB7**. This is per-layout; the
`defines_2-x-y.h` for the specific board is authority.

And a warning that applies to the ADC variant but is worth carrying: *"DO NOT
use the 5V/15V pin of the masterslave header for the potentiometers!!!!!!!!!"*
Treat every supply pin on these headers as 15 V until measured.

### 8.4 The host side

The host needs three wires: its TX to the board's RX, its RX to the board's TX,
and a common GND. The board runs from its own drive battery, and the drive
battery never needs to reach the host. Isolation is not present on either side
and none of these boards provide any -- a fault on the 36 V side has a path to
the host controller through that shared ground.

## 9. Flashing

### 9.1 Readout protection comes first

Most boards ship locked. EFeru's `wiki/How-to-Unlock-MCU-Flash` is unusually
blunt about the cost:

> **It's not possible to dump the original firmware and flash it later. This
> procedure will erase the stock MCU firmware irreversibly!**

So the first step is one-way: the scooter stops being a scooter. Also:

> Make sure you hold the power button while unlocking the chip, as the STM will
> release the power latch and switch itself off.

**Linux path:**

```bash
openocd -f interface/stlink.cfg -f target/stm32f1x.cfg \
        -c init -c "reset halt" -c "stm32f1x unlock 0"
```

If that fails, the wiki gives a longer sequence writing the flash unlock keys
(`0x45670123`, `0xCDEF89AB`) to `FLASH_KEYR`/`OPTKEYR` and the protection
half-word `0x5AA5` to `0x1ffff800`, then retrying the unlock. Both forms are in
the wiki; use them verbatim rather than from memory.

**Windows path:** ST-Link Utility, `Target` -> `Option Bytes` -> Read Out
Protection -> **Disable** -> `Apply`, then reconnect.

**GD32 caveat.** The wiki records that a partly-successful unlock produces
*"funny, unexpected behavior from your board after flashing"*, and the fix is to
re-lock, then unlock again with **all** user-configuration option-byte boxes
checked.

### 9.2 Connecting

Three wires: **GND, SWDIO, SWCLK**. The wiki adds: *"Connect the 3V3 pin only if
your MCU is not powered externally."* Powering a mainboard from a programmer's
3.3 V rail is a documented way to kill one.

### 9.3 Building and flashing

**EFeru (Gen 1):**

```bash
git clone https://github.com/EFeru/hoverboard-firmware-hack-FOC.git
# edit Inc/config.h: select VARIANT_USART
make                                     # or: platformio run -e VARIANT_USART
st-flash --reset write build/hover.bin 0x8000000
```

PlatformIO carries a pre-configured environment per variant; `arm-none-eabi-gcc`
plus `stlink-tools` is the Make path.

**RoboDurden (Gen 2):**

1. Identify the layout using the interactive board finder in the `Gen2.x` wiki.
2. Open the Keil project in **MDK-ARM v6**, select the target (1 = GD32F130 etc.).
3. In `Inc/config.h` set `LAYOUT` (and `LAYOUT_SUB` where the layout has one),
   `MASTER` or `SINGLE`, `REMOTE_ROS2`, `DRIVING_MODE 1`, and define
   `SPEED_AsRevsPerSec`. The last two are enforced by `#error` at build time.
4. Build the slave image separately with `SLAVE` and flash the other board.
5. Flash with ST-Link. The repository ships `st-flash.exe` and an
   `auto-flash.bat` for the prebuilt images; the same tool takes a locally built
   binary.

> [!CAUTION]
> **Test a new layout on a current-limited supply.** RoboDurden repeats it for
> every "worth testing" layout: *"with a 2A cc constant current power supply /
> dcdc-step-down converter | 1.5A 42V charger"*. A wrong `LAYOUT` puts the
> battery across a half-bridge.

### 9.4 Recovery

There is none beyond reflashing. The stock bootloader does not survive the
unlock, the stock firmware cannot be backed up, and a bricked board is
recovered only over SWD. Keep the programmer. A builder with one droid and one
board should expect to buy a second board before they expect to need it.

## 10. Telemetry: what is measured and what is not

| Feedback field | EFeru | RoboDurden `REMOTE_ROS2` |
| --- | --- | --- |
| **Battery** | real, `batVoltageCalib`, but see 10.1 | real, `batteryVoltage * 100` |
| **Board temperature** | real ADC, **uncalibrated by default** | **hardcoded 250 -> always 25.0 degC** |
| **Speed R / L** | real, RPM, from the motor model | real, RPM; **R is 0 on a `SINGLE` build** |
| **Current L / R** | **none** (no such field) | real, DC-link amps per board |

### 10.1 Battery voltage needs a per-board calibration

EFeru's `Inc/config.h:70-76`:

```c
/* Battery voltage calibration: connect power source. ...
 * Write debug output value nr 5 to BAT_CALIB_ADC. make and flash firmware. */
#define BAT_CALIB_REAL_VOLTAGE  3970      // input voltage measured by multimeter (x100)
#define BAT_CALIB_ADC           1492      // adc-value measured by mainboard
```

The shipped constants are one person's board. A builder who did not calibrate
gets a reading scaled by whatever their divider actually is. The number is
usually close enough to be useful and is **not** trustworthy to a tenth of a
volt.

### 10.2 The temperature field

On RoboDurden it is not a reading at all (`Src/RemoteROS2.c`):

```c
feedback.boardTemp = 250; // Dummy value (250 => 25 degrees)
```

Divided by 10 it reads 25.0 degC, unchanging, on a board that may be at any
temperature. On EFeru it is a real measurement of the **MCU's internal sensor**
-- not the MOSFETs, not the motors -- and EFeru's own comment puts it at *"up to
45 degC"* off without calibration. Neither is a number to act on without
knowing which firmware sent it.

### 10.3 What the frames carry beyond these

`wheelR_cnt` / `wheelL_cnt` at offsets 10 and 12 of the RoboDurden frame are a
hall-step odometer wrapping at 9000, and the same pair appears in EFeru's frame
when `ENABLE_ODOMETRY` is built in (Section 5.2). They are the only distance
measurement either firmware offers.

Motor current, the load signal a host would use to tell that the wheels are
being held, exists **only on RoboDurden boards**. On an EFeru board there is no
current field, so anything built on current reads zero there and never fires.

## 11. How the hobby actually drives feet (non-normative)

### 11.1 The negative result, recorded so nobody repeats the search

Six droid control stacks were checked for hoverboard support. **None has any.**

| Project | Foot drive it supports | Hoverboard |
| --- | --- | --- |
| `reeltwo/Reeltwo` | Sabertooth packetized serial, PWM | no |
| SHADOW / SHADOW_MD | Sabertooth 2x32 (feet), SyRen 10 (dome) | no |
| Padawan360 | Sabertooth 2x32 or PWM, SyRen 10 | no |
| Penumbra | Sabertooth serial | no |
| Marcduino | does not drive feet at all | n/a |
| ShadowRC lineage | Sabertooth, SyRen 10 | no |

Checked in the source as well as on GitHub: `grep -ri hoverboard` across the
source trees of `ShadowMD`, `Padawan360_mega_maestro_DYSV5W` and
`r2d2-astromech-simulator` returns **nothing**.

The astromech simulator, a design tool for astromech builders, puts the foot
drive question at question 7 and offers exactly two answers: *"Sabertooth
(serial) vs Flipsky/hub (PWM 44/45)"* (`src/js/config/hardware.js:29`). The tool
that exists to help a builder choose does not know hoverboards are an option.
No upstream droid community has hit a hoverboard bug first.

### 11.2 What the hobby does instead

The standard stack is a **Sabertooth 2x32 or 2x25 driving brushed scooter
motors** (MY6812-class, 12 V or 24 V), with a SyRen 10 on the dome, commanded
over Dimension Engineering packet serial from an Arduino Mega. Roboteq
controllers appear on heavier builds. The reasons builders give are consistent
and they are all about the *motor*, not the controller: a brushed gearmotor has
torque at a standstill, is quiet, and does not care that it is being asked to
crawl.

### 11.3 Why a hoverboard is nonetheless a reasonable choice

- **Cost.** A broken scooter is two motors, two wheels, a battery, a charger and
  a dual controller for the price of a takeaway. The traditional stack is
  several hundred dollars of controller before any motors.
- **Integration.** Motor, wheel, tyre and bearing arrive as one assembly that
  already fits a foot shell.
- **Feedback.** It reports per-wheel speed *and* (on Gen 2) per-wheel current
  back to the host.

The costs, stated plainly: no droid community has debugged this path; the
firmware is the builder's responsibility; low-speed cogging is a real and
unmeasured risk; and the board will switch itself off after eight idle minutes.

### 11.4 Robot-side code that does exist

Outside the droid hobby, this protocol is well travelled. The ROS
`hoverboard-driver` family (`hoverboard-robotics/hoverboard-driver`,
`alex-makarov/hoverboard-driver`, and ROS2 ports) speaks exactly the frames in
Section 5 -- and is the reason RoboDurden's `REMOTE_ROS2` exists. RoboDurden also
ships ESP32 examples (`Arduino Examples/Esp32_PPM`, `TestSpeed`,
`Test_4Wheeler`, and a PlatformIO CYD/LVGL dashboard) that are directly readable
reference hosts.

## 12. Quick Reference

| Field | Value | Source |
| --- | --- | --- |
| Start marker | `0xABCD`, on the wire as `CD AB` | both firmwares |
| Command frame size | 8 bytes | `Inc/util.h`, `RemoteROS2.c` |
| Command field order | `start, steer, speed, checksum` | same |
| Command checksum | `start ^ steer ^ speed` | same |
| Feedback size, EFeru | **18** bytes (22 with `ENABLE_ODOMETRY`) | `Src/main.c:125` |
| Feedback size, RoboDurden ROS2 | **26** bytes | `Src/RemoteROS2.c` |
| Feedback checksum test | XOR of **all** uint16 words == 0 | Section 5.4 |
| Baud | 115200 8N1 | `Inc/config.h:658`, `Inc/RemoteROS2.h` |
| Feedback rate, EFeru | 100 Hz | `Src/main.c:522` |
| Feedback rate, RoboDurden | 10 Hz | `Inc/RemoteROS2.h` |
| Reference host send rate | 10 Hz | `hoverserial.ino`, RoboDurden examples |
| Command range | +/-1000 nominal | `Inc/config.h` `PRI_INPUTn` |
| Command units, EFeru default | voltage percent (`VLT_MODE`) | `Inc/config.h:158` |
| Command units, RoboDurden ROS2 | **RPM** | `RemoteROS2.c` struct comment |
| Steering sign, both | left = speed + steer/2, right = speed - steer/2 | `util.c:1706`, `RemoteROS2.c` `Pilot()` |
| Starved: holds for | 800 ms (EFeru) / 500 ms (RoboDurden) | `Inc/config.h:654`, `Inc/RemoteROS2.h` |
| Starved: then | coast (EFeru) / soft brake (RoboDurden) | `util.c:1032`, `bldc.c:220` |
| Hard cutoff | none / 2000 ms | `Inc/defines.h:242` |
| Motor enable gate, EFeru | both inputs `< 50` at boot | `Src/main.c:270` |
| RoboDurden build that speaks `0xABCD` | **`REMOTE_ROS2` only** | Section 4.2 |
| RoboDurden prebuilt ROS2 image | **none exists** | Section 4.2 |
| Which cable, Gen 1 | left = USART2 (**not 5 V tolerant**), right = USART3 (5 V tolerant) | `hoverserial.ino` header |
| Which header, Gen 2 | PB6/PB7, the empty header near the flash header | `Inc/config.h` `REMOTE_USART 0` |
| Red wire on sensor cable | **12-15 V**, sometimes battery voltage | `Inc/config.h`, wiki |
| MCU, Gen 1 | STM32F103RCT6 / RET6, GD32F103RCT6 | wiki |
| MCU, Gen 2 | GD32F130C8 + four other targets | `Inc/comms.h`, README |
| Unsupported chips | AT32F403RCT6 / AT32F413RCT7 (Artery) | wiki |
| Motor | 15 pole pairs, wye, halls 120 deg, ~16 rpm/V | `wiki/Hoverboard-Wheels` |
| No-load speed | ~640 rpm at 40 V (star) | same |
| Battery | 10S Li-ion, 42 V full, 3.37 V/cell dead | `Inc/config.h:78,88` |
| Current limits | 15 A per motor, 17 A DC (EFeru); 15 A DC (RoboDurden) | `Inc/config.h` both |
| Inactivity poweroff | **8 minutes**, both | `Inc/config.h:184`, RoboDurden `Inc/config.h` |
| Temperature protections | **disabled by default**, both | `Inc/config.h:107,109` |
| Licence, both firmwares | GPL-3.0 | both repos |

## 13. Open Items

| # | Item | How it gets settled |
| --- | --- | --- |
| 1 | Low-speed cogging on a loaded droid at walking pace | Bench: drive a loaded droid at a low command and observe. Cannot be settled by reading. |
| 2 | Whether the 8-minute poweroff announces itself | Bench: leave a powered board idle 9 minutes with a serial console attached. |
| 3 | Sensor-cable connector part number | `UNKNOWN`. No source names one; would need a caliper and a connector catalogue. |
| 4 | Measured no-load rpm per volt | Read `speedR_meas` / `speedL_meas` at a known command with the wheels off the ground. Settles the 14-vs-16 rpm/V band. |

## 14. Sources

**Firmware source:**

- `https://raw.githubusercontent.com/EFeru/hoverboard-firmware-hack-FOC/main/Inc/config.h`
- `https://raw.githubusercontent.com/EFeru/hoverboard-firmware-hack-FOC/main/Inc/util.h`
- `https://raw.githubusercontent.com/EFeru/hoverboard-firmware-hack-FOC/main/Inc/defines.h`
- `https://raw.githubusercontent.com/EFeru/hoverboard-firmware-hack-FOC/main/Src/main.c`
- `https://raw.githubusercontent.com/EFeru/hoverboard-firmware-hack-FOC/main/Src/util.c`
- `https://raw.githubusercontent.com/EFeru/hoverboard-firmware-hack-FOC/main/Arduino/hoverserial/hoverserial.ino`
- `https://raw.githubusercontent.com/RoboDurden/Hoverboard-Firmware-Hack-Gen2.x-GD32/main/HoverBoardGigaDevice/Src/RemoteROS2.c`
- `https://raw.githubusercontent.com/RoboDurden/Hoverboard-Firmware-Hack-Gen2.x-GD32/main/HoverBoardGigaDevice/Inc/RemoteROS2.h`
- `https://raw.githubusercontent.com/RoboDurden/Hoverboard-Firmware-Hack-Gen2.x-GD32/main/HoverBoardGigaDevice/Inc/config.h`
- `https://raw.githubusercontent.com/RoboDurden/Hoverboard-Firmware-Hack-Gen2.x-GD32/main/HoverBoardGigaDevice/Inc/defines.h`
- `https://raw.githubusercontent.com/RoboDurden/Hoverboard-Firmware-Hack-Gen2.x-GD32/main/HoverBoardGigaDevice/Inc/comms.h`
- `https://raw.githubusercontent.com/RoboDurden/Hoverboard-Firmware-Hack-Gen2.x-GD32/main/HoverBoardGigaDevice/Inc/remoteUart.h`
- `https://raw.githubusercontent.com/RoboDurden/Hoverboard-Firmware-Hack-Gen2.x-GD32/main/HoverBoardGigaDevice/Src/main.c`
- `https://raw.githubusercontent.com/RoboDurden/Hoverboard-Firmware-Hack-Gen2.x-GD32/main/HoverBoardGigaDevice/Src/bldc.c`
- `https://raw.githubusercontent.com/RoboDurden/Hoverboard-Firmware-Hack-Gen2.x-GD32/main/HoverBoardGigaDevice/Src/it.c`
- `https://raw.githubusercontent.com/RoboDurden/Hoverboard-Firmware-Hack-Gen2.x-GD32/main/Arduino%20Examples/TestSpeed/hoverserial.h`
- `https://raw.githubusercontent.com/RoboDurden/Hoverboard-Firmware-Hack-Gen2.x-GD32/main/Arduino%20Examples/TestROS2/ros2serial.h`
- `https://raw.githubusercontent.com/flo199213/Hoverboard-Firmware-Hack-Gen2/master/HoverBoardGigaDevice/Src/commsSteering.c`
- `https://raw.githubusercontent.com/Candas1/hoverboard-firmware-hack-FOC/master/Src/main.c`

**Wikis and READMEs:**

- `https://github.com/EFeru/hoverboard-firmware-hack-FOC/wiki/Firmware-Compatibility`
- `https://github.com/EFeru/hoverboard-firmware-hack-FOC/wiki/Hoverboard-Wheels`
- `https://github.com/EFeru/hoverboard-firmware-hack-FOC/wiki/Buying-a-used-hoverboard`
- `https://github.com/EFeru/hoverboard-firmware-hack-FOC/wiki/How-to-Unlock-MCU-Flash`
- `https://github.com/RoboDurden/Hoverboard-Firmware-Hack-Gen2.x` (README, binaries, board catalogue)
- `https://github.com/RoboDurden/Hoverboard-Firmware-Hack-Gen2.x-GD32` (README)

**Related projects, named not read in full:**

- `https://github.com/NiklasFauth/hoverboard-firmware-hack` -- the original; the
  GitHub API returns 404 for it at this research date, so it is cited as
  ancestry only.
- `https://github.com/bipropellant/bipropellant-hoverboard-firmware` -- a
  different protocol entirely; not covered by this sheet.
- `https://github.com/EFeru/hoverboard-sideboard-hack-STM`,
  `https://github.com/EFeru/hoverboard-sideboard-hack-GD` -- the sideboard
  companions.
- `https://github.com/cloidnerux/hoverboard-firmware-hack`,
  `https://github.com/thanek/hoverboard-firmware-hack` -- the Artery forks.
- `https://github.com/hoverboard-robotics/hoverboard-driver`,
  `https://github.com/alex-makarov/hoverboard-driver` -- the ROS drivers
  `REMOTE_ROS2` was written for.
