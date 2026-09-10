# ExpressLRS and CRSF Spec Sheet

Working spec for **ExpressLRS** as a Radio Controller member
([#311](https://github.com/mattiasbrandt/protoArtoo/issues/311)), reached over
the **CRSF** Component Protocol that
[#303](https://github.com/mattiasbrandt/protoArtoo/issues/303) assigned it.

Research date 2026-09-10. Every frame byte, channel constant, baud rate and
default in this document was read from the TBS CRSF specification, from
ExpressLRS firmware source, from library source, or from the astromech projects
on disk, this session. Claims that could not be sourced are marked `UNKNOWN`
with the artefact or bench test that would settle them.

> [!CAUTION]
> **The safety model inverts, and our existing one has a layer that stops
> existing.**
>
> `rc_input.cpp` documents two layers: *"Layer 1: SBUS receiver hardware
> failsafe flag (data.failsafe)"* and *"Layer 2: SBUS software watchdog
> (SBUS_TIMEOUT_MS = 200 ms)"*. **CRSF has no failsafe flag.** On link loss the
> receiver stops sending the frame entirely, so there is nothing to read a flag
> from. Layer 1 evaporates and Layer 2 becomes the only protection there is.
>
> Worse, the protocol's own advice is slower than our invariant. From the TBS
> specification: *"In case of a Failsafe, this frame will no longer be sent...
> **It is recommended to wait for 1 second before starting the FC failsafe
> routine.**"* That is written for aircraft, where cutting power is worse than
> gliding. **A droid among people is the opposite case and should decline the
> advice.** Section 8.

> [!NOTE]
> **The good news is unusually good.** CRSF's channel values are numerically
> identical to SBUS's -- 172 / 992 / 1811 -- so every binding, calibration and
> normalisation protoArtoo already has applies unchanged (Section 7.2). And
> CRSF is bidirectional, so for the first time in this lineup the droid can
> report **back** to the operator's handset (Section 9).

## Where this sits in the lineup

| Category | Product | Role | Status |
| --- | --- | --- | --- |
| **Radio Controller** | RC Transmitter -- ELRS | the radio the droid is driven with | `roadmap` |

#311's body is right that this is a **sub-choice of the generic RC Transmitter
entry**, beside Standard PWM and SBUS, both of which already ship
(`docs/pin_map.md:166-172`). Under `CONTEXT.md`'s test -- *"if it changes the
driver it is a protocol; if it does not, it is configuration"* -- CRSF is
unambiguously a protocol: a different wire format, a different baud, a different
transport, and a different failsafe model.

ELRS is not a competitor to the HotRC gear we support; it is the same job done
with an open-source link, better telemetry and a handset a builder programs
themselves. Which one a droid carries is a builder's choice about radios, not a
verdict on either.

## 0. Authority Contract

This document is an implementation authority for CRSF and for ExpressLRS's
behaviour as a CRSF source.

Authority order for agent decisions:

1. **The TBS CRSF specification**, `tbs-fpv/tbs-crsf-spec`, for the wire format.
   It is the protocol's own document and ExpressLRS's source cites it by URL.
2. **ExpressLRS firmware source**, for what a real receiver actually does --
   especially where the specification leaves a choice, as it does for failsafe.
3. This document.
4. **Library source** (`AlfredoCRSF`) -- evidence of practice, and the source of
   the timeout numbers in Section 10.
5. Astromech project source (Section 11).

If references conflict:

- Prefer ExpressLRS source over the specification for **receiver behaviour**.
  The specification describes what CRSF permits; the firmware decides what an
  ELRS receiver does.
- Prefer the specification over any library for the **frame format**.
- If still unresolved, mark `UNKNOWN` and stop dependent work.

Agent requirements when using this document:

- MUST NOT look for a failsafe flag. There is none (Section 8.1).
- MUST NOT adopt the specification's 1-second recommendation for a ground
  vehicle (Section 8.3).
- MUST NOT assume channels 15 and 16 are the operator's. In every mode except
  full-resolution 16-channel, ELRS overwrites them with link quality and RSSI
  (Section 7.3).
- MUST NOT assume CRSF can share the SBUS decode path. SBUS costs no UART
  because it is decoded on RMT; **CRSF needs a real UART** (Section 12.1).
- MUST treat the baud rate as a receiver setting, not a constant. Three
  different defaults exist in the wild (Section 5.2).

## 1. Scope

Covers the CRSF frame format and CRC, the channel encoding and its value
domain, the electrical characteristics, the failsafe model, the telemetry
return path, ExpressLRS's configuration where it changes what arrives on the
wire, the host libraries, what the astromech hobby has already built, and what
protoArtoo would have to do.

Does not cover: the ExpressLRS over-the-air modulation, binding phrases and RF
hopping beyond what reaches the CRSF stream, the TX-module side of the link,
MSP-over-CRSF and the configurator traffic (frame types `0x7A`-`0x7C`),
CRSF-over-MAVLink, or the ELRS "airport" serial-bridge mode.

## 2. Products Covered

**ExpressLRS is firmware, not a product.** A builder buys two pieces of
hardware that both run it:

| Side | What it is |
| --- | --- |
| **Transmitter** | a module in, or plugged into, an EdgeTX handset -- or a handset with ELRS built in |
| **Receiver** | a small board on the droid that outputs CRSF on a UART |

Representative receivers a ground vehicle would want -- external antenna rather
than the ceramic chip antennas the tiny FPV parts carry, and diversity where
available:

| Vendor | Model | Band | Notes |
| --- | --- | --- | --- |
| BetaFPV | SuperP 14CH Diversity | 2.4 GHz | external U.FL, diversity, about USD 30 |
| RadioMaster | RP4TD | 2.4 GHz | true diversity, external antennas |
| Matek | R24-D | 2.4 GHz | true diversity |
| Happymodel | ES900RX | 900 MHz | external antenna, long range |

> [!NOTE]
> Prices and stock move constantly across a dozen vendors and were not pinned
> to a single checked date the way the Flipsky and Xbox sheets pin theirs. A
> complete setup is roughly USD 60-125 on top of a handset. Treat the table as
> "what shape of part to buy", not as a price list.

**2.4 GHz versus 900 MHz** matters more for a droid than for an aircraft. 900
MHz penetrates bodies and structures better, which is what a convention hall is
full of; 2.4 GHz gives lower latency and much smaller antennas. Both are legal
in most regions with the right firmware, and the choice is made **at flash
time** -- a builder cannot switch bands later without new hardware. `UNKNOWN`
which a droid should prefer; no astromech source compares them and the one
field-tested droid implementation (Section 11) uses 2.4 GHz.

## 3. Project Integration

- **[`src/tasks/rc_input.cpp`](../../src/tasks/rc_input.cpp)** -- the RC task.
  Lines 14-18 carry the channel domain and the two safety layers this sheet is
  mostly about; lines 50-56 record that SBUS is RMT-based and costs no UART.
- **[`include/rc_binding_types.h`](../../include/rc_binding_types.h)** --
  `RC_SBUS_DEFAULT_MIN/CENTER/MAX` = 172 / 992 / 1811, which Section 7.2 shows
  are already CRSF's numbers.
- **[`include/config.h`](../../include/config.h)** -- `SBUS_MIN`, `SBUS_MAX`,
  `SBUS_TIMEOUT_MS` (200 ms), and the UART allocation comments for both boards
  (133-148 and 245-262).
- **[`include/robot_state.h`](../../include/robot_state.h)** -- the eight `hb_*`
  drive telemetry fields that Section 9.2 maps onto CRSF telemetry frames.
- **[`tasks/rc_diagnostics_contract.md`](../../tasks/rc_diagnostics_contract.md)**
  -- the per-channel `raw` / `normalized` / `mapped` contract, which CRSF fits
  better than any other roadmap protocol.
- **[`docs/spec-sheets/sbus-protocol.md`](sbus-protocol.md)** -- the sibling
  protocol sheet. This one deliberately mirrors its structure where the two
  protocols are comparable.
- **ADR 0029** (Board Capability Gates), **ADR 0042** (Component Families).

## 4. Sources Checked

| Source | URL or path | Extraction notes |
| --- | --- | --- |
| **TBS CRSF specification** | https://github.com/tbs-fpv/tbs-crsf-spec/blob/main/crsf.md | **The protocol's own document.** The failsafe sentence, the two UART configurations and their baud defaults, the frame layout |
| ExpressLRS `crsf_protocol.h` | https://github.com/ExpressLRS/ExpressLRS/blob/master/src/include/crsf_protocol.h | CRC polynomial, sync byte, frame size limits, **the channel value constants**, the frame-type and address enums, the 11-bit channel struct, the link-statistics struct |
| ExpressLRS `SerialCRSF.cpp` | .../src/src/rx-serial/SerialCRSF.cpp | **The failsafe answer.** `if (!frameAvailable) return;` -- and the ch15/ch16 substitution |
| ExpressLRS `SerialSBUS.cpp` | .../src/src/rx-serial/SerialSBUS.cpp | The contrast: `effectivelyFailsafed`, `FAILSAFE_NO_PULSES`, and a `FAILSAFE_SET_POSITION` mode |
| ExpressLRS `SerialIO.h` | .../src/src/rx-serial/SerialIO.h | The design intent in the base class: a protocol may *"set a flag in the serial messages, or stop sending over serial port"* |
| ExpressLRS `rx_main.cpp` | .../src/src/rx_main.cpp | Baud and framing per output protocol, and **which protocols are inverted** |
| ExpressLRS `options.cpp` | .../src/lib/OPTIONS/options.cpp | `rcvr-uart-baud` defaults to **420000** |
| `AlfredoCRSF` | https://github.com/AlfredoSystems/AlfredoCRSF | Read in full: the two timeout constants, `isLinkUp()`, the telemetry send path, the bounded receive buffer |
| **ShadyRC Crossfire** | `joymonkey/dEvolution`, sketch fetched and read | **The field-tested astromech CRSF droid.** Its 16-channel map, its arming model, its link-loss handler, its battery telemetry |
| **CHIRP Droid Control** | `~/Documents/GitHub/CHIRP/README.md`, local | Its successor, and its stated goals -- including droid status over ELRS telemetry |
| protoArtoo RC path | `src/tasks/rc_input.cpp`, `include/rc_binding_types.h`, `include/config.h` | The two safety layers, the channel domain, the UART and RMT budgets |

Two web research passes ran alongside this. Their claims were checked against
source before use. The most important one **corroborated** rather than
contradicted: the specification sentence about failsafe was found independently
by one pass and by my own reading of `SerialCRSF.cpp`, from opposite directions.

One claim is recorded but not relied on: that ELRS *"enters failsafe if link
quality drops to 0, or 1 second has passed without a valid channels packet,
whichever comes first."* That is an ELRS-internal state, and it is **not** the
same thing as when the CRSF output stops -- Section 8.2 explains why the
distinction matters and marks the interaction `UNKNOWN`.

One pass reported that **CHIRP Droid Control could not be found** and that "no
public GitHub, website, documentation, or forum thread" for it exists. That is
wrong: it is `github.com/joymonkey/CHIRP`, it is **cloned on this machine**, and
Section 11.5 quotes its README. Where this project has already recorded that
something exists -- `CONTEXT.md` names it explicitly -- an agent's failure to
find it is not evidence of absence.

## 5. Electrical and the wire

### 5.1 Two configurations, and we want the second

The specification defines both:

| | **Single-wire half-duplex** | **Full-duplex, two wires** |
| --- | --- | --- |
| Where used | *"usually... the TX modules side"* | *"usually used on the flying platform side"* |
| Default baud | **400 kbaud** | **416666 baud** |
| Format | 8N1 | 8N1 |
| Inversion | *"inverted or non-inverted"* | **"Only non-inverted (regular) UART is supported"** |
| Level | 3.3 V | 3.0 to 3.3 V |

**The vehicle side is the easy one.** Two wires, non-inverted, 3.3 V, 8N1 --
which is a plain ESP32 UART with no inverter and no level shifting. Compare
SBUS, which is inverted, 8E2 and 100000 baud, and which protoArtoo therefore
decodes on RMT rather than on a UART at all.

### 5.2 Three baud numbers, and which one is real

| Number | Where it comes from |
| --- | --- |
| 400000 | TBS spec default for **half-duplex** |
| 416666 | TBS spec default for **full-duplex** |
| **420000** | **what ExpressLRS receivers actually default to** |

From `options.cpp`: `firmwareOptions.uart_baud = doc["rcvr-uart-baud"] | 420000;`
-- and `rx_main.cpp` uses that value directly for CRSF output. ShadyRC's sketch
hardcodes the same number with a comment that says why:

> `#define CRSF_BAUDRATE 420000 // by default crossfire runs at 420000 baud, if you want to slow it down the receiver and transmitter may need a firmware reflash`

So **420000 is the number to build against**, it is a per-receiver setting
rather than a constant, and a builder who changes it must change both ends.

> [!NOTE]
> **420000 baud on an ESP32 is proven by the other end of the same wire.**
> ExpressLRS receivers are themselves ESP8266- and ESP32-based -- `rx_main.cpp`
> branches on `PLATFORM_ESP8266` and `PLATFORM_ESP32` -- so the rate is being
> generated by the same family of UART that would receive it. This is stronger
> evidence than a divisor calculation.

> [!WARNING]
> **One ESP32-specific hazard is worth a bench check anyway.** The ESP32 UART
> selects its clock source by rate -- `REF_TICK` below roughly 250 kHz and the
> **APB clock** above it. At 420000 baud the UART is therefore APB-derived, and
> the APB frequency is not constant on a board that runs WiFi and may use
> dynamic frequency scaling or light sleep. A shifting APB clock under a
> fixed divisor is a baud error, and a baud error at 420 kbaud is a dead link
> rather than a degraded one.
>
> This is a reported class of problem on ESP32 at high rates rather than a
> measured failure at 420000 specifically, so it is Open Item 9 with the check
> that settles it -- not a claim that it will happen to us.

### 5.3 What it costs us that SBUS does not

`rc_input.cpp:50-56`: *"SBUS receiver objects -- RMT-based, no hardware UART
consumed... UART1 is now exclusively owned by DriveTask; UART2 by
DomeLinkTask."*

That single design choice is why SBUS is free on both boards and CRSF is not.
An asynchronous 420 kbaud serial stream is UART work; it cannot be decoded on
the RMT peripheral the way an SBUS pulse train can. Section 12.1 is the
consequence.

## 6. The frame protocol

Every CRSF frame:

```
  <sync/addr> <len> <type> <payload...> <crc8>
```

| Field | Value |
| --- | --- |
| Sync byte | **`0xC8`** on a serial link |
| `len` | bytes that follow, counting `type` and `crc8`, **not** counting sync and len |
| `type` | frame type (Section 6.2) |
| `crc8` | CRC-8 over `type` and `payload`, polynomial **`0xD5`** |

Sizes from `crsf_protocol.h`: `CRSF_MIN_PACKET_LEN 4`, `CRSF_MAX_PACKET_LEN 64`,
and `CRSF_FRAME_NOT_COUNTED_BYTES 2` -- the sync and len fields.

> [!IMPORTANT]
> **`0xC8` is a sync byte here, not an address.** The address enum also defines
> `CRSF_ADDRESS_FLIGHT_CONTROLLER = 0xC8`, which invites the wrong reading.
> ExpressLRS's own source settles it in a comment repeated twice in
> `SerialCRSF.cpp`: *"CRSF on a serial port `_always_` has 0xC8 as a sync byte
> rather than the device_id"*, citing the specification. A parser should match
> `0xC8` and not try to route on it.
>
> `AlfredoCRSF` says the same in its own words: *"Byte 0 of a CRSF frame is a
> sync byte, not routing information: standard frames (type below 0x28) have
> meaning purely by their type, and extended frames (0x28-0x96) carry their
> routing in destination/origin header bytes."*

### 6.1 Extended frames

Frame types `0x28` to `0x96` are *extended*: the payload begins with a
destination and an origin address byte, and those carry the routing. Everything
protoArtoo would need to drive a droid is a standard frame; the extended ones
are configurator and parameter traffic.

### 6.2 Frame types worth knowing

From `crsf_protocol.h`, trimmed to what a droid touches:

| Type | Name | Direction |
| --- | --- | --- |
| **`0x16`** | `RC_CHANNELS_PACKED` | receiver to us -- **the one that drives the droid** |
| **`0x14`** | `LINK_STATISTICS` | receiver to us -- RSSI, link quality, SNR |
| `0x08` | `BATTERY_SENSOR` | **us to the handset** |
| `0x0C` | `RPM` | us to the handset |
| `0x0D` | `TEMP` | us to the handset |
| `0x02` | `GPS` | us to the handset |
| `0x1E` | `ATTITUDE` | us to the handset |
| `0x21` | `FLIGHT_MODE` | us to the handset -- a free-text-ish status field |
| `0x2E` | `ELRS_STATUS` | ELRS-specific good/bad packet counts |
| `0x0B` | `HEARTBEAT` | either |

## 7. Channels

### 7.1 The packing

`0x16` carries **16 channels of 11 bits in 22 bytes**, little-endian bit-packed
with no padding -- `crsf_channels_s` is a struct of sixteen `unsigned ch : 11`
bit-fields. Frame length byte is `0x18` (22 payload + type + crc).

### 7.2 The value domain is SBUS's, exactly

This is the finding that makes the integration cheap. From `crsf_protocol.h`:

| Constant | Value | Microseconds |
| --- | --- | --- |
| `CRSF_CHANNEL_VALUE_STD_MIN` | **172** | 988 us |
| `CRSF_CHANNEL_VALUE_1000` | 191 | 1000 us |
| `CRSF_CHANNEL_VALUE_MID` | **992** | 1500 us |
| `CRSF_CHANNEL_VALUE_2000` | 1792 | 2000 us |
| `CRSF_CHANNEL_VALUE_STD_MAX` | **1811** | 2012 us |
| `CRSF_CHANNEL_VALUE_EXT_MIN` / `_EXT_MAX` | 0 / 1984 | with extended limits |

And from protoArtoo, `include/rc_binding_types.h`:

```cpp
static constexpr uint16_t RC_SBUS_DEFAULT_MIN = 172;
static constexpr uint16_t RC_SBUS_DEFAULT_CENTER = 992;
static constexpr uint16_t RC_SBUS_DEFAULT_MAX = 1811;
```

**They are the same three numbers.** `rc_input.cpp`'s own header already
describes `ch[]` as *"range SBUS_MIN(172)..SBUS_MAX(1811), center ~992"*. So a
CRSF decoder can hand its channel array to every existing consumer --
`RcBindingConfig`, the calibration, the normalisation, the diagnostics contract
-- with no conversion and no new value domain. Of everything on the Radio
Controller roadmap, this is the member that fits our existing model best.

> [!WARNING]
> **Extended limits break the assumption.** With E.Limits enabled on the
> handset the range widens to 0..1984, which is outside what our defaults
> expect. It is a transmitter-side setting we cannot see. Treat 172/1811 as
> defaults to be calibrated against, not as guarantees -- which is what
> `RcBindingConfig`'s per-channel min/max already allows.

### 7.3 Channels 15 and 16 are not yours

`SerialCRSF::sendRCFrame()` fills channels 1-14 from the received data and then:

```cpp
    // In 16ch mode, do not output RSSI/LQ on channels
    if (OtaIsFullRes && OtaSwitchModeCurrent == smHybridOr16ch) { ...ch14, ch15 = real data... }
    else {
        // Not in 16-channel mode, send LQ and RSSI dBm
        PackedRCdataOut.ch14 = UINT10_to_CRSF(fmap(linkStats.uplink_Link_quality, 0, 100, 0, 1023));
        PackedRCdataOut.ch15 = UINT10_to_CRSF(map(constrain(rssiDBM, ...), ..., 0, 1023));
    }
```

So unless the builder is in a full-resolution 16-channel mode, **channel 15
carries uplink link quality and channel 16 carries RSSI**, scaled into the
normal channel range.

Both a trap and a gift:

- **Trap**: a builder who binds an action to channel 15 gets an input that moves
  with radio conditions rather than with their switch.
- **Gift**: protoArtoo gets link quality as an ordinary channel, readable
  through the binding model with no telemetry parsing at all. For a first
  implementation that is a very cheap link-health indicator.

## 8. Failsafe

> [!CAUTION]
> This section is the reason the sheet exists. Read it before writing a driver.

### 8.1 There is no flag, because there is no frame

`SerialCRSF::sendRCFrame()` opens with:

```cpp
    if (!frameAvailable)
        return DURATION_IMMEDIATELY;
```

No frame received over the air, no frame emitted on the wire. `SerialCRSF` does
not override the base class's `setFailsafe()` to do anything at all.

The base class documents this as a deliberate per-protocol choice
(`SerialIO.h`): setting the failsafe flag *"allows the serial protocol
implementation to react accordingly. e.g. if it needs to set a flag in the
serial messages, **or stop sending over serial port**."*

And the specification says the same from the other side: *"In case of a
Failsafe, this frame will no longer be sent (when the failsafe type is set to
'cut')."*

**The contrast with SBUS is stark**, and both live in the same ExpressLRS
codebase. `SerialSBUS::sendRCFrame()` computes
`effectivelyFailsafed = failsafe || (!connectionHasModelMatch) || (!teamraceHasModelMatch)`,
stops only when the configured mode is `FAILSAFE_NO_PULSES`, and otherwise
**keeps transmitting** -- because SBUS has a flag byte to put the news in. It
even carries a `FAILSAFE_SET_POSITION` mode.

| | SBUS | CRSF |
| --- | --- | --- |
| On link loss | keeps sending, sets the failsafe bit | **sends nothing** |
| Configurable | yes -- no-pulses or last-position | not on the CRSF path |
| What the host reads | an explicit flag | **silence** |
| protoArtoo Layer 1 | works | **does not exist** |

### 8.2 When the silence starts is not simply answered

Two different clocks exist and they are easy to conflate:

1. **The CRSF output** stops as soon as no over-the-air frame is available --
   there is no timer in `sendRCFrame()` at all.
2. **ELRS's own failsafe state** is entered on a longer horizon, reported as
   link quality reaching zero or about a second without a valid packet.

What the receiver puts on the wire in between -- nothing, or repeats of the
last channel data -- is **`UNKNOWN`** and is Open Item 1, because it decides
whether a droid can be driving on stale sticks during that window. The
conservative reading, and the one to build against, is that **silence may be
preceded by up to a second of stale-looking data**, so a watchdog on frame
arrival alone is not sufficient; the values themselves must also be gated by
arming (Section 8.4).

### 8.3 The specification's own advice is wrong for us

> *"It is recommended to wait for 1 second before starting the FC failsafe
> routine."*

That is sound for an aircraft: a momentary dropout at altitude is survivable,
and cutting the motors is not. **A droid is the mirror image.** It is heavy, it
is at ground level, it is surrounded by people, and coasting for a second is
the hazard rather than the mitigation.

protoArtoo's existing number is **200 ms** (`SBUS_TIMEOUT_MS`), which is five
times faster than the specification's advice and is the right order for this
machine. `AlfredoCRSF` sits in between with `CRSF_PACKET_TIMEOUT_MS = 100` and
`CRSF_FAILSAFE_STAGE1_MS = 300`.

| Source | Timeout | Vehicle |
| --- | --- | --- |
| Betaflight, `RACE_PRO` | 10 ms | aircraft |
| Betaflight, default | 100 ms | aircraft |
| `AlfredoCRSF` packet timeout | 100 ms | library |
| **protoArtoo `SBUS_TIMEOUT_MS`** | **200 ms** | **droid** |
| `AlfredoCRSF` failsafe stage 1 | 300 ms | library |
| TBS specification recommendation | **1000 ms** | aircraft |
| **ArduPilot Rover `FS_TIMEOUT` default** | **1000 ms** | **ground vehicle** |

The ArduPilot Rover row is the uncomfortable one: it is a **ground vehicle**
project, and it also defaults to a full second. That is a default chosen for
outdoor rovers with room to coast, not for a machine worked among standing
people, and it should not be read as endorsement.

**Recommendation: keep 200 ms and document the departure.** The specification is
giving aircraft advice; the sheet records that we declined it deliberately
rather than by oversight.

### 8.4 Arming is not optional here

Because the only signal is absence, an arming gate carries more weight in CRSF
than in SBUS. ShadyRC does exactly this and Section 11.3 records how.

## 9. Telemetry: the return path

CRSF on the vehicle side is **full duplex**, which no protocol currently in this
project's Radio Controller category is. That buys two things.

### 9.1 Link statistics, free

Frame `0x14` carries a 10-byte payload (`crsfLinkStatistics_t`):

| Field | Meaning |
| --- | --- |
| `uplink_RSSI_1`, `uplink_RSSI_2` | RSSI per antenna, dBm negated |
| **`uplink_Link_quality`** | **link quality, 0-100 %** |
| `uplink_SNR` | signal-to-noise, dB |
| `active_antenna` | which antenna is selected |
| `rf_Mode` | the current packet rate |
| `uplink_TX_Power` | handset transmit power |
| `downlink_RSSI_1`, `downlink_Link_quality`, `downlink_SNR` | the return direction |

SBUS gives one bit that says "it already failed". CRSF gives a continuously
varying percentage that says "it is about to". For a droid worked in a crowded
hall that is a different class of information, and it is exactly what the
Status Plate's RC-link chip should show.

### 9.2 The droid can report to the handset -- and our data already fits

The telemetry frames map onto what protoArtoo already holds in `RobotState`:

| CRSF frame | protoArtoo already has |
| --- | --- |
| `0x08 BATTERY_SENSOR` | `hb_batteryRaw` |
| `0x0D TEMP` | `hb_boardTempRaw` |
| `0x0C RPM` | `hb_speedL`, `hb_speedR` |
| `0x21 FLIGHT_MODE` | a short status string -- estop, armed, speed preset |

This is not theoretical. ShadyRC already sends battery voltage up the link
(Section 11.4), and CHIRP Droid Control's stated goal is *"Send system status
and audio file details to the operators radio transmitter via ExpressLRS
telemetry packets."*

> [!NOTE]
> **This is the first time the operator's own handset could display droid
> state.** It is out of scope for a first CRSF driver and it is the most
> interesting thing in this sheet. Recorded so it is not lost.

## 10. Libraries

### 10.1 `AlfredoCRSF`

The library ShadyRC uses, forked from CRServoF. Read in full this session.

| | |
| --- | --- |
| Repository | https://github.com/AlfredoSystems/AlfredoCRSF |
| Receive path | `update()` calls `handleSerialIn()` -- **fed from the caller's loop, non-blocking** |
| Buffer | `_rxBuf[CRSF_MAX_PACKET_LEN + 3]`, bounded |
| CRC | validated, polynomial `0xd5` set in the constructor |
| Link state | `isLinkUp()`, cleared by `checkLinkDown()` when `millis() - _lastChannelsPacket > CRSF_FAILSAFE_STAGE1_MS` |
| Timeouts | `CRSF_PACKET_TIMEOUT_MS = 100`, `CRSF_FAILSAFE_STAGE1_MS = 300` |
| Channels | `getChannel(ch)` -- **1-indexed**, returns microseconds; `getChannelsPacked()` for raw |
| Telemetry out | **yes** -- `queuePacket(addr, type, payload, len)` |
| Extras | `getLinkStatistics()`, `isArmed()`, `getChannelsStatus()`, `sendHeartbeat()`, `setDeviceName()` |

**Verdict: structurally sound for a real-time path**, which is unusual among the
libraries these sheets have surveyed. It does not block, it bounds its buffer,
it checks the CRC, and it exposes both a link-state primitive and a telemetry
send path. Two things to note rather than to fix:

- `getChannel()` returns **microseconds**, not the 172-1811 raw domain. For
  protoArtoo the raw form is the one that matches everything (Section 7.2), so
  a driver should read `getChannelsPacked()` and skip the conversion.
- `checkLinkDown()` uses 300 ms, which is longer than our 200 ms invariant. A
  protoArtoo driver should keep its own watchdog rather than adopt the
  library's.

### 10.2 Others

| Library | Note |
| --- | --- |
| `CRSFforArduino` (ZZ-Cat) | **Archived May 2026 and no longer maintained**, and licensed **AGPL-3.0** -- a licence this project would have to think about rather than accept by default. Not read in source this session |
| Betaflight `src/main/rx/crsf.c` | Not usable as a library; the best production reference. Byte-at-a-time from an ISR, 64-byte frame buffer, bad-CRC frames dropped silently with an error counter |
| ArduPilot `AP_RCProtocol_CRSF.cpp` | Same status. Notable because **ArduPilot Rover supports CRSF officially** -- the only ground-vehicle precedent of any weight |
| CRServoF | `AlfredoCRSF`'s ancestor |

### 10.3 CRSF v3 negotiated baud: leave it alone

The specification defines a speed-negotiation frame that can move the link to
1 Mbaud and beyond. It is implemented in Betaflight and **defaults to off**
(`crsf_use_negotiated_baud = 0`), with random-failsafe reports behind that
decision.

For a droid it buys nothing -- 420000 baud already carries a 22-byte channel
frame in well under half a millisecond -- and it adds a negotiation state
machine to a safety path. **Stay at the fixed default.**

## 11. How the hobby actually uses these (non-normative)

This is the strongest ecosystem section in any of these sheets, because unlike
the VESC and the Xbox controller, **a field-tested astromech CRSF droid exists,
on an ESP32, with source we can read.**

### 11.1 ShadyRC Crossfire

`joymonkey/dEvolution`, sketch `ShadyRC_Crossfire_250211`. Its own header:

> "an update to ShadyRC_dEvolution, using Crossfire/ELRS radios. Can be used
> with most EdgeTX transmitters using an ExpressLRS or Crossfire radio.
> **Heavily tested with a Radiomaster Zorro** (running EdgeTX v2.9.0), internal
> ExpressLRS 2.4Ghz radio running ExpressLRS v3.3.0. Using a DIY 2.4Ghz
> receiver on the droid side"

Its wiring, on a classic ESP32:

```
//  ESP32 UART0 (Serial)  RX=GPIO3  TX=GPIO1  debug over USB
//  ESP32 UART1 (Serial1) RX=GPIO26 TX=GPIO27 reserved for the Syren and Rome-A-Dome
//  ESP32 UART2 (Serial2) RX=GPIO16 TX=GPIO17 talk to the ELRS receiver (using Crossfire protocol)
```

with software serial for the MP3 trigger and the dome. **That is the same UART
squeeze protoArtoo has, solved the same way protoArtoo already solves it** --
hardware UARTs for the things that need them, bit-banged serial for the slow
ones. Section 12.1 returns to this.

### 11.2 The control grammar

A droid's worth of function mapped onto a handset, from the sketch's own table:

| Ch | Function | Zorro control |
| --- | --- | --- |
| 1 | Direction | Right stick X |
| 2 | Throttle | Right stick Y |
| 3 | AutoDome | Left stick Y |
| 4 | Dome rotate | Left stick X |
| **5** | **Arm** | **SE (2-way switch)** |
| 7 | Speed | SB (3-way) |
| 9 | Play tune | SA (momentary) |
| 10 | Play beep | SD (momentary) |
| 12 | Sound select | S1 (dial) |
| 13 | Volume | S2 (dial) |
| 14 | Play tune 2 | SG |

Worth comparing with Padawan360's Xbox grammar in
[the Xbox sheet](xbox-controller-input.md): the gamepad carries forty actions
through button-plus-modifier combinations, while the handset carries a dozen
through **dedicated physical switches and dials**. Neither is better; they suit
different operators. But the handset's dials are something a gamepad has no
equivalent of -- a sound-select dial and a volume dial that hold their position
and can be read at a glance.

### 11.3 The link-loss handler, which is the best in the hobby

```cpp
    crsf.update();
    if (crsf.isLinkUp()==false) {
      if (isArmed==true) {
        //oh balls, we've just lost connected to the transmitter, stop movement and assume that foot and motor sticks are centered
        stopFeet();
        SyR->motor(0);
        txChannels[1]=1500; //foot direction
        txChannels[2]=1500; //foot throttle
        txChannels[4]=1500; //dome rotation stick
      }
      isArmed=false; //change armed state asap
      speedRange=0;  //once we arm after re-connecting, we want to make sure we start in slow mode
    }
```

Five properties worth copying, and every one of them is a decision protoArtoo
faces:

1. **`isLinkUp()` is the primitive** -- a derived link state, because there is
   no flag to read.
2. **Stop the feet and the dome**, not just the feet.
3. **Overwrite the cached stick values with centre.** Stale channel data cannot
   be reused on reconnect. This is the mitigation for Section 8.2's `UNKNOWN`.
4. **Disarm immediately** -- *"change armed state asap"*.
5. **Reset the speed range**, so reconnecting always comes back in slow mode.

And the arming model itself:

```cpp
      if (txChannels[5]>1500) {
        isArmed=true;
        if (prevArmed==false) speedRange=1; //always start in slow mode after arming
        ...
      } else { isArmed=false; speedRange=0; }
```

Arming is a **physical switch position**, evaluated every loop -- not a button
press that toggles a latch. The switch's position *is* the armed state, so
there is no way for the droid and the operator to disagree about it. On
disarm it goes further and **detaches the servo outputs entirely**
(`leftFootSignal.detach()`), stopping pulses rather than sending neutral ones.

### 11.4 Telemetry, already working

```cpp
static void sendRxBatteryTelem(float voltage, float current, float capacity, float remaining) {
  crsf_sensor_battery_t crsfBatt = { 0 };
  ...
  crsf.queuePacket(CRSF_SYNC_BYTE, CRSF_FRAMETYPE_BATTERY_SENSOR, &crsfBatt, sizeof(crsfBatt));
}
```

fed from a real ADC measurement of the droid's battery, with an honest comment
that the other three fields are *"made up"*. The handset shows the droid's
battery voltage. That is the loop Section 9.2 describes, running on somebody's
droid today.

### 11.5 CHIRP Droid Control, the successor

`joymonkey/CHIRP` -- the same author, and the same repository that gives us the
**CHIRP Audio Trigger** we already support. Its README describes the control
half as *"An evolution of the previously mentioned ShadyRC system. The intent is
to use a microcontroller and ExpressLRS radio receiver to send signals/commands
to the various motion, sound and lighting systems of an Astromech droid."*

Its stated goals, quoted because two of them are things protoArtoo also wants:

> - "Send system status and audio file details to the operators radio
>   transmitter via ExpressLRS telemetry packets"
> - "Don't require re-programming just to update sounds"
> - "Be controller agnostic; initially supporting several different EdgeTX based
>   radio transmitters such as the Radiomaster Zorro"
> - "**Be safe but also easy to pick up; the system should police itself to some
>   extent**"
> - "Be expandable"

`CONTEXT.md` already records what this is: *"a peer body controller... which a
builder would choose **instead of** protoArtoo rather than alongside it."* That
framing is right, and it makes CHIRP the closest thing this project has to a
direct comparator on the radio side.

> [!IMPORTANT]
> **The Droid Control half has no published source yet.** The `joymonkey/CHIRP`
> repository tree at its current tip contains only `CHIRP_Audio_Trigger/` --
> sketches, firmware `.uf2` images, sounds and board photographs. The Droid
> Control goals are in the README; the code is not in the repository. So the
> working CRSF implementation to read is still ShadyRC (Section 11.1), not this.

### 11.6 Three generations, and only the oldest has code

Checked across the author's repositories on 2026-09-10, because it is easy to
assume the newest name is the one to study:

| Project | ELRS/CRSF | State |
| --- | --- | --- |
| `dEvolution` -- **ShadyRC Crossfire** | **yes, working** | **field-tested source**, read in full for this sheet |
| `sentinel` -- *"An updated droid control system for use with EdgeTX/ExpressLRS radios"* | announced | **placeholder** -- README and LICENSE only, no code |
| `CHIRP` -- Droid Control | announced, goals documented | **no Droid Control source published**; the Audio Trigger half is complete |

The ambition has been restated three times and implemented once. That is worth
knowing before anyone goes looking for a modern reference implementation --
**and it means protoArtoo would not be following a finished peer, it would be
arriving at roughly the same time as one.**

> [!NOTE]
> No other astromech project surveyed supports CRSF. The SHADOW family is PS3
> Navigation over Bluetooth, Padawan360 is Xbox over USB, Reeltwo and MarcDuino
> do not own an input layer. **ELRS in a droid is joymonkey's line of work**,
> and it starts from TBS Crossfire rather than from ExpressLRS.

## 12. What protoArtoo would have to do

### 12.1 The UART, and whether artoo-esp32 is really excluded

#311's body says *"only firebeetle2 has one spare -- UART4"*, and `config.h`
confirms both halves: artoo-esp32's three controllers are all committed, with
the dome and audio already sharing one and audio TX already bit-banged;
firebeetle2's UART4 is *"unclaimed by the firmware"*.

So the straightforward reading is **firebeetle2 only**, and that is probably
the right answer. But it deserves one honest challenge before it becomes a Gate.

ShadyRC runs CRSF on a **classic ESP32 with three UARTs**, exactly as many as
artoo-esp32 has, by putting the slow devices on software serial. protoArtoo
already does the same thing for the same reason -- `audio_soft_uart_tx.h` exists
because *"the one shared controller's TX is committed to the dome link"*.

That does not make artoo-esp32 viable: our UART0 is a real console, the dome
link is bidirectional and timing-sensitive, and CRSF at 420 kbaud is the least
suitable thing on the board to bit-bang. But the honest statement is **"no spare
UART for CRSF, and freeing one would cost the dome link or the console"**,
rather than "impossible" -- and that distinction is the difference between a
Gate that is a fact and a Gate that is an assumption.

### 12.2 The decoder, and how little else changes

Because of Section 7.2, a CRSF driver is unusually contained:

| Piece | Work |
| --- | --- |
| Frame sync, length, CRC-8 `0xD5` | new, small |
| 11-bit unpack into `ch[16]` | new, small |
| Channel value domain | **none -- already 172/992/1811** |
| `RcBindingConfig`, calibration, deadband, reverse | **none** |
| Diagnostics `raw`/`normalized`/`mapped` | **none** -- `raw` is already this domain |
| Failsafe Layer 1 | **removed** -- there is no flag |
| Failsafe Layer 2 | **becomes the only layer** |
| RC input mode plumbing | a fourth `rc_mode`, beside PWM, single and dual SBUS |

Compare the Xbox controller, which needs a whole new binding shape. **CRSF is
the cheapest member on the Radio Controller roadmap**, and the reason is that
TBS chose SBUS's numbers.

### 12.3 The failsafe design, which is the real work

1. **Keep the 200 ms watchdog** and record that we declined the specification's
   1-second advice, with the reason (Section 8.3).
2. **Zero the cached channel values on link loss**, do not merely stop acting on
   them -- ShadyRC's third property, and the mitigation for Section 8.2.
3. **Require an arm channel.** With no failsafe flag, the arming gate is doing
   safety work rather than convenience work. A switch position evaluated every
   tick, not a toggle.
4. **Come back disarmed and slow.** ShadyRC resets the speed range on every
   disconnect so a reconnect cannot resume at speed.
5. **Feed the existing `FailsafeGate`** rather than inventing a parallel path;
   an RC link loss is the same class of event as an SBUS watchdog trip.

### 12.4 What to do with link quality

Two options, and the cheap one is genuinely good:

- **Free**: read channel 15, which already carries uplink link quality as a
  0-100 % value scaled into the channel range (Section 7.3). No telemetry
  parsing at all, and it arrives through the binding model.
- **Proper**: parse frame `0x14` and get RSSI, SNR, antenna and RF mode as well.

Either way, this is the first RC member that can tell the operator *the link is
getting worse* rather than only *the link has failed*, and the Status Plate's
RC-link chip is where it belongs.

### 12.5 Costs to state plainly

- **A UART**, which only firebeetle2 has spare (Section 12.1).
- **A new decoder**, though a small one (Section 12.2).
- **A failsafe redesign**, because Layer 1 stops existing (Section 8).
- **An arm channel**, which is a change to how a builder drives the droid.
- **A builder-side provisioning story**: binding phrase, packet rate, switch
  mode, telemetry ratio and the receiver's baud, all set outside protoArtoo.

## 13. Agent Lookup Quick Reference

- Field: Component Protocol. Required value: **CRSF**.
- Field: Frame. Required value: `<0xC8> <len> <type> <payload> <crc8>`.
- Field: Sync byte. Required value: **`0xC8`** -- a sync byte on a serial link, **not** an address.
- Field: Length semantics. Required value: bytes following `len`, **including** `type` and `crc8`, excluding sync and len.
- Field: CRC. Required value: **CRC-8, polynomial `0xD5`**, over `type` and payload.
- Field: Max frame. Required value: `CRSF_MAX_PACKET_LEN` = **64**; minimum 4.
- Field: RC channels frame type. Required value: **`0x16`**, 22-byte payload.
- Field: Channel packing. Required value: **16 channels x 11 bits**, little-endian bit-packed.
- Field: Channel min / centre / max. Required value: **172 / 992 / 1811** -- identical to protoArtoo's SBUS constants.
- Field: Channel extended limits. Required value: 0 to 1984, enabled transmitter-side.
- Field: Channels 15 and 16. Required value: **overwritten with uplink link quality and RSSI** except in full-resolution 16-channel mode.
- Field: Link statistics frame type. Required value: **`0x14`**, 10-byte payload, link quality is a **0-100 %** field.
- Field: Telemetry frames a droid can send. Required value: `0x08` battery, `0x0C` RPM, `0x0D` temperature, `0x21` flight mode, `0x02` GPS.
- Field: Extended frame range. Required value: types **`0x28` to `0x96`** carry destination and origin bytes.
- Field: Vehicle-side UART. Required value: **two-wire full duplex, 8N1, non-inverted, 3.0-3.3 V**.
- Field: Baud. Required value: **420000** for ExpressLRS receivers by default (`rcvr-uart-baud`); TBS spec defaults are 400000 half-duplex and 416666 full-duplex.
- Field: Inversion. Required value: **not inverted** on the vehicle side. `PROTOCOL_INVERTED_CRSF` exists as a receiver option and would present as a dead link.
- Field: Failsafe signalling. Required value: **none -- the frame stops being sent.** There is no failsafe flag.
- Field: Specification failsafe advice. Required value: *"wait for 1 second"* -- **aircraft advice; protoArtoo declines it.**
- Field: protoArtoo watchdog. Required value: **`SBUS_TIMEOUT_MS` = 200 ms**, retained for CRSF.
- Field: `AlfredoCRSF` timeouts. Required value: packet 100 ms, failsafe stage 1 **300 ms**.
- Field: protoArtoo Layer 1 (receiver failsafe flag). Required value: **does not exist for CRSF**.
- Field: Decode peripheral. Required value: **a hardware UART**. CRSF cannot use the RMT path SBUS uses.
- Field: Board with a spare UART. Required value: **firebeetle2 only** (UART4).
- Field: Arming. Required value: **required** -- a channel evaluated every tick, not a toggle.

If a required value cannot be proven for the receiver in hand, status is
`UNKNOWN` and dependent work stops. For anything in Section 8, that means the
droid does not drive.

## 14. Open Items

1. **What does the receiver put on the wire between real signal loss and
   declared failsafe?** Section 8.2. Settled on a bench: power the handset off
   mid-stream and capture the receiver's UART, looking for whether frames stop
   at once or repeat stale channel data for up to a second first. **This decides
   whether a frame-arrival watchdog is sufficient on its own.**
2. **Is the 200 ms watchdog right for CRSF's packet cadence?** At 50 Hz packet
   rate a frame is due every 20 ms and 200 ms is ten missed frames; at 500 Hz it
   is a hundred. `UNKNOWN` whether the threshold should scale with the
   configured rate, or stay fixed as a wall-clock safety number.
3. **Could artoo-esp32 free a UART?** Section 12.1. Decides whether the Board
   Capability Gate is `{firebeetle2}` as a fact or as an assumption.
4. **Which band should the lineup recommend, 2.4 GHz or 900 MHz?** No astromech
   source compares them for a crowded hall, and it is chosen at flash time.
5. **Does ELRS at 2.4 GHz interfere with our own WiFi AP?** Both are 2.4 GHz and
   both are on the droid. ELRS documents frequency hopping and claims tolerance
   of WiFi noise, but nobody has measured the reverse -- what an ELRS receiver
   does to an ESP32 AP a metre away. Bench test: run both and watch link quality
   and AP throughput together.
6. **Does `getChannelsPacked()` give the raw domain unmodified?** Section 10.1
   assumes so from the struct type. Confirm before relying on it, or unpack the
   frame directly.
7. **What is `CRSFforArduino` like?** Not read this session (Section 10.2), and
   it is archived and AGPL-licensed, so it is a fallback rather than a
   candidate. Only matters if `AlfredoCRSF` is rejected.
8. **Should protoArtoo send telemetry at all in a first implementation?**
   Section 9.2 says the data already exists. It is scope, not research.
9. **Does a 420 kbaud UART stay locked while WiFi is running?** Section 5.2's
   warning. Settled on a bench: run the AP and a CRSF stream together for an
   extended period and count framing and CRC errors, with and without any
   power-management setting that can move the APB clock.
10. **Can an ELRS link always be recovered without a power cycle?** Several
    ExpressLRS issues report failsafes that did not clear until the transmitter
    or receiver was restarted. For an aircraft that is a bad day; for a droid in
    a hall it decides whether an operator can recover on the floor or has to
    carry the droid out. `UNKNOWN` how current firmware behaves; worth asking
    the ELRS community rather than testing blind.
11. **Is half-duplex CRSF always inverted?** Sources disagree. The TBS
    specification says half-duplex is *"inverted or non-inverted"*, ExpressLRS
    exposes `PROTOCOL_CRSF` and `PROTOCOL_INVERTED_CRSF` as separate options,
    and the CRSF working group's physical-layer wiki was reported this session
    as saying half-duplex is inverted. **It does not affect us** -- the vehicle
    side is full-duplex and non-inverted in every source -- but the sheet should
    not pretend the sources agree.

## 15. Sources

**Normative (primary): the protocol specification**

- TBS CRSF specification: https://github.com/tbs-fpv/tbs-crsf-spec/blob/main/crsf.md
  (the failsafe sentence, the two UART configurations, the frame layout)

**Normative (primary): ExpressLRS firmware source**

- Repository: https://github.com/ExpressLRS/ExpressLRS
- `src/include/crsf_protocol.h` -- CRC polynomial, sync byte, channel constants,
  frame types, addresses, the channel and link-statistics structs
- `src/src/rx-serial/SerialCRSF.cpp` -- the failsafe behaviour and the ch15/ch16
  substitution
- `src/src/rx-serial/SerialSBUS.cpp` -- the contrasting SBUS behaviour
- `src/src/rx-serial/SerialIO.h` -- the base class's documented intent
- `src/src/rx_main.cpp` -- per-protocol baud, framing and inversion
- `src/lib/OPTIONS/options.cpp` -- the 420000 baud default

**Normative (primary): library source**

- `AlfredoCRSF`: https://github.com/AlfredoSystems/AlfredoCRSF

**Ecosystem survey (primary source, non-normative)**

- **ShadyRC Crossfire**, read in full:
  https://github.com/joymonkey/dEvolution/blob/master/sketches/ShadyRC_Crossfire_250211/ShadyRC_Crossfire_250211.ino
- **CHIRP Droid Control**: https://github.com/joymonkey/CHIRP, read locally at
  `~/Documents/GitHub/CHIRP/`
- [`docs/spec-sheets/xbox-controller-input.md`](xbox-controller-input.md) for
  the gamepad grammar this section compares against

**Project documentation**

- ExpressLRS documentation: https://www.expresslrs.org/ -- packet rates, switch
  modes, telemetry ratio, binding, the web UI, regional firmware
