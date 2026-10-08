# ExpressLRS and CRSF Spec Sheet

**ExpressLRS** (ELRS) is open-source radio link firmware for RC handsets and
receivers. An ELRS receiver hands its channels to the vehicle over **CRSF**, the
Crossfire serial protocol TBS defined, so this sheet covers both: ELRS for what
a receiver does, CRSF for what arrives on the wire.

**Status: Roadmap.** The droid reads no ELRS input yet, so the receiver cannot
be picked; this sheet is the research a driver would start from.

Research date 2026-09-10. Every frame byte, channel constant, baud rate and
default in this document was read from the TBS CRSF specification, from
ExpressLRS firmware source, from library source, or from the astromech projects
that use it. Claims that could not be sourced are marked `UNKNOWN` with the
artefact or bench test that would settle them. Where the specification leaves a
choice, as it does for failsafe, ExpressLRS firmware source decides what a
receiver does; the specification decides the frame format.

> [!CAUTION]
> **CRSF has no failsafe flag.** On link loss the receiver stops sending the
> frame entirely, so there is nothing to read a flag from: silence is the only
> signal. A host failsafe built for SBUS, which reads a flag the receiver sets,
> has nothing to read here, and a frame-arrival watchdog becomes the only
> protection (Section 7.1).
>
> The protocol's own advice is slow. From the TBS specification: *"In case of a
> Failsafe, this frame will no longer be sent... **It is recommended to wait for
> 1 second before starting the FC failsafe routine.**"* That is written for
> aircraft, where cutting power is worse than gliding. A heavy ground vehicle
> among people is the opposite case. Section 7.3.

> [!NOTE]
> **CRSF's channel values are numerically identical to SBUS's** -- 172 / 992 /
> 1811 -- so a host that already reads SBUS channel values reads CRSF's with no
> conversion (Section 6.2). And CRSF is full duplex on the vehicle side, so the
> vehicle can report **back** to the operator's handset (Section 8).

## 1. Scope

Covers the CRSF frame format and CRC, the channel encoding and its value
domain, the electrical characteristics, the failsafe model, the telemetry
return path, ExpressLRS's configuration where it changes what arrives on the
wire, the host libraries, and what the astromech hobby has already built.

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
> to a single checked date. A complete setup is roughly USD 60-125 on top of a
> handset. Treat the table as "what shape of part to buy", not as a price list.

**2.4 GHz versus 900 MHz** matters more for a droid than for an aircraft. 900
MHz penetrates bodies and structures better, which is what a convention hall is
full of; 2.4 GHz gives lower latency and much smaller antennas. Both are legal
in most regions with the right firmware, and the choice is made **at flash
time** -- a builder cannot switch bands later without new hardware. `UNKNOWN`
which a droid should prefer; no astromech source compares them and the one
field-tested droid implementation (Section 10) uses 2.4 GHz.

## 3. Sources Checked

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
| **CHIRP Droid Control** | https://github.com/joymonkey/CHIRP, README | Its successor, and its stated goals -- including droid status over ELRS telemetry |

Two web research passes ran alongside this. Their claims were checked against
source before use. The most important one **corroborated** rather than
contradicted: the specification sentence about failsafe was found independently
by one pass and by a direct reading of `SerialCRSF.cpp`, from opposite directions.

One claim is recorded but not relied on: that ELRS *"enters failsafe if link
quality drops to 0, or 1 second has passed without a valid channels packet,
whichever comes first."* That is an ELRS-internal state, and it is **not** the
same thing as when the CRSF output stops -- Section 7.2 explains why the
distinction matters and marks the interaction `UNKNOWN`.

One pass reported that **CHIRP Droid Control could not be found** and that "no
public GitHub, website, documentation, or forum thread" for it exists. That is
wrong: it is `github.com/joymonkey/CHIRP`, and Section 10.5 quotes its README.
A search that fails to find a project is not evidence that it does not exist.

## 4. Electrical and the wire

### 4.1 Two configurations, and the vehicle side uses the second

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
SBUS, which is inverted, 8E2 and 100000 baud.

### 4.2 Three baud numbers, and which one is real

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
> measured failure at 420000 specifically, so it is Open Item 7 with the check
> that settles it -- not a claim that it will happen on every board.

## 5. The frame protocol

Every CRSF frame:

```
  <sync/addr> <len> <type> <payload...> <crc8>
```

| Field | Value |
| --- | --- |
| Sync byte | **`0xC8`** on a serial link |
| `len` | bytes that follow, counting `type` and `crc8`, **not** counting sync and len |
| `type` | frame type (Section 5.2) |
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

### 5.1 Extended frames

Frame types `0x28` to `0x96` are *extended*: the payload begins with a
destination and an origin address byte, and those carry the routing. Everything
a vehicle needs for control, link health and telemetry is a standard frame; the
extended ones are configurator and parameter traffic.

### 5.2 Frame types worth knowing

From `crsf_protocol.h`, trimmed to what a droid touches:

| Type | Name | Direction |
| --- | --- | --- |
| **`0x16`** | `RC_CHANNELS_PACKED` | receiver to vehicle -- **the sticks and switches** |
| **`0x14`** | `LINK_STATISTICS` | receiver to vehicle -- RSSI, link quality, SNR |
| `0x08` | `BATTERY_SENSOR` | **vehicle to the handset** |
| `0x0C` | `RPM` | vehicle to the handset |
| `0x0D` | `TEMP` | vehicle to the handset |
| `0x02` | `GPS` | vehicle to the handset |
| `0x1E` | `ATTITUDE` | vehicle to the handset |
| `0x21` | `FLIGHT_MODE` | vehicle to the handset -- a free-text-ish status field |
| `0x2E` | `ELRS_STATUS` | ELRS-specific good/bad packet counts |
| `0x0B` | `HEARTBEAT` | either |

## 6. Channels

### 6.1 The packing

`0x16` carries **16 channels of 11 bits in 22 bytes**, little-endian bit-packed
with no padding -- `crsf_channels_s` is a struct of sixteen `unsigned ch : 11`
bit-fields. Frame length byte is `0x18` (22 payload + type + crc).

### 6.2 The value domain is SBUS's, exactly

From `crsf_protocol.h`:

| Constant | Value | Microseconds |
| --- | --- | --- |
| `CRSF_CHANNEL_VALUE_STD_MIN` | **172** | 988 us |
| `CRSF_CHANNEL_VALUE_1000` | 191 | 1000 us |
| `CRSF_CHANNEL_VALUE_MID` | **992** | 1500 us |
| `CRSF_CHANNEL_VALUE_2000` | 1792 | 2000 us |
| `CRSF_CHANNEL_VALUE_STD_MAX` | **1811** | 2012 us |
| `CRSF_CHANNEL_VALUE_EXT_MIN` / `_EXT_MAX` | 0 / 1984 | with extended limits |

**172 / 992 / 1811 are SBUS's own reference points** (`sbus-protocol.md`,
"Value Domain"). A host that already handles SBUS channel values can take a
CRSF channel array unchanged: no conversion and no new value domain.

> [!WARNING]
> **Extended limits break the assumption.** With E.Limits enabled on the
> handset the range widens to 0..1984, which is outside the SBUS range. It is a
> transmitter-side setting the receiver output does not announce. Treat
> 172/1811 as defaults to be calibrated against, not as guarantees.

### 6.3 Channels 15 and 16 are not yours

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
- **Gift**: a host gets link quality as an ordinary channel, with no telemetry
  parsing at all. That is a very cheap link-health indicator.

## 7. Failsafe

> [!CAUTION]
> This section is the reason the sheet exists. Read it before writing a driver.

### 7.1 There is no flag, because there is no frame

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
| A host failsafe that reads the flag | works | **has nothing to read** |

### 7.2 When the silence starts is not simply answered

Two different clocks exist and they are easy to conflate:

1. **The CRSF output** stops as soon as no over-the-air frame is available --
   there is no timer in `sendRCFrame()` at all.
2. **ELRS's own failsafe state** is entered on a longer horizon, reported as
   link quality reaching zero or about a second without a valid packet.

What the receiver puts on the wire in between -- nothing, or repeats of the
last channel data -- is **`UNKNOWN`** and is Open Item 1, because it decides
whether a vehicle can be moving on stale sticks during that window. The
conservative reading, and the one to build against, is that **silence may be
preceded by up to a second of stale-looking data**, so a watchdog on frame
arrival alone is not sufficient; the values themselves must also be gated by
arming (Section 7.4).

### 7.3 The specification's failsafe advice is aircraft advice

> *"It is recommended to wait for 1 second before starting the FC failsafe
> routine."*

That is sound for an aircraft: a momentary dropout at altitude is survivable,
and cutting the motors is not. **A droid is the mirror image.** It is heavy, it
is at ground level, it is surrounded by people, and coasting for a second is
the hazard rather than the mitigation.

What other projects and libraries choose:

| Source | Timeout | Vehicle |
| --- | --- | --- |
| Betaflight, `RACE_PRO` | 10 ms | aircraft |
| Betaflight, default | 100 ms | aircraft |
| `AlfredoCRSF` packet timeout | 100 ms | library |
| `AlfredoCRSF` failsafe stage 1 | 300 ms | library |
| TBS specification recommendation | **1000 ms** | aircraft |
| **ArduPilot Rover `FS_TIMEOUT` default** | **1000 ms** | **ground vehicle** |

The ArduPilot Rover row is the uncomfortable one: it is a **ground vehicle**
project, and it also defaults to a full second. That is a default chosen for
outdoor rovers with room to coast, not for a machine worked among standing
people, and it should not be read as endorsement.

### 7.4 Arming is not optional here

Because the only signal is absence, an arming gate carries more weight in CRSF
than in SBUS. ShadyRC does exactly this and Section 10.3 records how.

## 8. Telemetry: the return path

CRSF on the vehicle side is **full duplex**, which SBUS and PWM receiver outputs
are not. That buys two things.

### 8.1 Link statistics, free

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
hall that is a different class of information.

### 8.2 The vehicle can report to the handset

The standard telemetry frames carry what a droid's drive already measures:

| CRSF frame | Carries |
| --- | --- |
| `0x08 BATTERY_SENSOR` | battery voltage, current, capacity, remaining |
| `0x0D TEMP` | a temperature |
| `0x0C RPM` | wheel or motor speed |
| `0x21 FLIGHT_MODE` | a short status string, e.g. estop, armed, speed preset |

This is not theoretical. ShadyRC already sends battery voltage up the link
(Section 10.4), and CHIRP Droid Control's stated goal is *"Send system status
and audio file details to the operators RC radio via ExpressLRS
telemetry packets."*

## 9. Libraries

### 9.1 `AlfredoCRSF`

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
libraries surveyed here. It does not block, it bounds its buffer, it checks the
CRC, and it exposes both a link-state primitive and a telemetry send path. Two things to note rather than to fix:

- `getChannel()` returns **microseconds**, not the 172-1811 raw domain. A host
  that wants the raw domain (Section 6.2) reads `getChannelsPacked()` and skips
  the conversion.
- `checkLinkDown()` uses a fixed 300 ms. A host with a shorter link-loss budget
  keeps its own watchdog rather than adopting the library's.

### 9.2 Others

| Library | Note |
| --- | --- |
| `CRSFforArduino` (ZZ-Cat) | **Archived May 2026 and no longer maintained**, and licensed **AGPL-3.0**, which carries obligations for any firmware that links it. Not read in source this session |
| Betaflight `src/main/rx/crsf.c` | Not usable as a library; the best production reference. Byte-at-a-time from an ISR, 64-byte frame buffer, bad-CRC frames dropped silently with an error counter |
| ArduPilot `AP_RCProtocol_CRSF.cpp` | Same status. Notable because **ArduPilot Rover supports CRSF officially** -- the only ground-vehicle precedent of any weight |
| CRServoF | `AlfredoCRSF`'s ancestor |

### 9.3 CRSF v3 negotiated baud: leave it alone

The specification defines a speed-negotiation frame that can move the link to
1 Mbaud and beyond. It is implemented in Betaflight and **defaults to off**
(`crsf_use_negotiated_baud = 0`), with random-failsafe reports behind that
decision.

For a droid it buys nothing -- 420000 baud already carries a 22-byte channel
frame in well under half a millisecond -- and it adds a negotiation state
machine to a safety path. **Stay at the fixed default.**

## 10. How the hobby actually uses these (non-normative)

**A field-tested astromech CRSF droid exists, on an ESP32, with readable
source.**

### 10.1 ShadyRC Crossfire

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

with software serial for the MP3 trigger and the dome: hardware UARTs for the
things that need them, bit-banged serial for the slow ones. That is how three
UARTs on a classic ESP32 carry CRSF.

### 10.2 The control grammar

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

### 10.3 The link-loss handler

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

Five properties of that handler:

1. **`isLinkUp()` is the primitive** -- a derived link state, because there is
   no flag to read.
2. **Stop the feet and the dome**, not just the feet.
3. **Overwrite the cached stick values with centre.** Stale channel data cannot
   be reused on reconnect. This is the mitigation for Section 7.2's `UNKNOWN`.
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

### 10.4 Telemetry, already working

```cpp
static void sendRxBatteryTelem(float voltage, float current, float capacity, float remaining) {
  crsf_sensor_battery_t crsfBatt = { 0 };
  ...
  crsf.queuePacket(CRSF_SYNC_BYTE, CRSF_FRAMETYPE_BATTERY_SENSOR, &crsfBatt, sizeof(crsfBatt));
}
```

fed from a real ADC measurement of the droid's battery, with an honest comment
that the other three fields are *"made up"*. The handset shows the droid's
battery voltage. That is the loop Section 8.2 describes, running on somebody's
droid today.

### 10.5 CHIRP Droid Control, the successor

`joymonkey/CHIRP` -- the same author, and the same repository as the
**CHIRP Audio Trigger**. Its README describes the control half as *"An
evolution of the previously mentioned ShadyRC system. The intent is to use a
microcontroller and ExpressLRS radio receiver to send signals/commands to the
various motion, sound and lighting systems of an Astromech droid."*

Its stated goals:

> - "Send system status and audio file details to the operators radio
>   transmitter via ExpressLRS telemetry packets"
> - "Don't require re-programming just to update sounds"
> - "Be controller agnostic; initially supporting several different EdgeTX based
>   radio transmitters such as the Radiomaster Zorro"
> - "**Be safe but also easy to pick up; the system should police itself to some
>   extent**"
> - "Be expandable"

> [!IMPORTANT]
> **The Droid Control half has no published source yet.** The `joymonkey/CHIRP`
> repository tree at its current tip contains only `CHIRP_Audio_Trigger/` --
> sketches, firmware `.uf2` images, sounds and board photographs. The Droid
> Control goals are in the README; the code is not in the repository. So the
> working CRSF implementation to read is still ShadyRC (Section 10.1), not this.

### 10.6 Three generations, and only the oldest has code

Checked across the author's repositories on 2026-09-10, because it is easy to
assume the newest name is the one to study:

| Project | ELRS/CRSF | State |
| --- | --- | --- |
| `dEvolution` -- **ShadyRC Crossfire** | **yes, working** | **field-tested source**, read in full for this sheet |
| `sentinel` -- *"An updated droid control system for use with EdgeTX/ExpressLRS radios"* | announced | **placeholder** -- README and LICENSE only, no code |
| `CHIRP` -- Droid Control | announced, goals documented | **no Droid Control source published**; the Audio Trigger half is complete |

The ambition has been restated three times and implemented once. That is worth
knowing before anyone goes looking for a modern reference implementation.

> [!NOTE]
> No other astromech project surveyed supports CRSF. The SHADOW family is PS3
> Navigation over Bluetooth, Padawan360 is Xbox over USB, Reeltwo and MarcDuino
> do not own an input layer. **ELRS in a droid is joymonkey's line of work**,
> and it starts from TBS Crossfire rather than from ExpressLRS.

## 11. Quick Reference

- Field: Receiver output protocol. Value: **CRSF**.
- Field: Frame. Value: `<0xC8> <len> <type> <payload> <crc8>`.
- Field: Sync byte. Value: **`0xC8`** -- a sync byte on a serial link, **not** an address.
- Field: Length semantics. Value: bytes following `len`, **including** `type` and `crc8`, excluding sync and len.
- Field: CRC. Value: **CRC-8, polynomial `0xD5`**, over `type` and payload.
- Field: Max frame. Value: `CRSF_MAX_PACKET_LEN` = **64**; minimum 4.
- Field: RC channels frame type. Value: **`0x16`**, 22-byte payload.
- Field: Channel packing. Value: **16 channels x 11 bits**, little-endian bit-packed.
- Field: Channel min / centre / max. Value: **172 / 992 / 1811** -- identical to SBUS's reference points.
- Field: Channel extended limits. Value: 0 to 1984, enabled transmitter-side.
- Field: Channels 15 and 16. Value: **overwritten with uplink link quality and RSSI** except in full-resolution 16-channel mode.
- Field: Link statistics frame type. Value: **`0x14`**, 10-byte payload, link quality is a **0-100 %** field.
- Field: Telemetry frames a droid can send. Value: `0x08` battery, `0x0C` RPM, `0x0D` temperature, `0x21` flight mode, `0x02` GPS.
- Field: Extended frame range. Value: types **`0x28` to `0x96`** carry destination and origin bytes.
- Field: Vehicle-side UART. Value: **two-wire full duplex, 8N1, non-inverted, 3.0-3.3 V**.
- Field: Baud. Value: **420000** for ExpressLRS receivers by default (`rcvr-uart-baud`); TBS spec defaults are 400000 half-duplex and 416666 full-duplex.
- Field: Inversion. Value: **not inverted** on the vehicle side. `PROTOCOL_INVERTED_CRSF` exists as a receiver option and would present as a dead link.
- Field: Failsafe signalling. Value: **none -- the frame stops being sent.** There is no failsafe flag.
- Field: Specification failsafe advice. Value: *"wait for 1 second"* -- **written for aircraft.**
- Field: `AlfredoCRSF` timeouts. Value: packet 100 ms, failsafe stage 1 **300 ms**.
- Field: Host peripheral. Value: **a UART** at the receiver's baud, 8N1, non-inverted.
- Field: Arming. Value: **none in the protocol.** ShadyRC arms on a switch channel evaluated every loop, not a toggle (Section 10.3).

## 12. Open Items

1. **What does the receiver put on the wire between real signal loss and
   declared failsafe?** Section 7.2. Settled on a bench: power the handset off
   mid-stream and capture the receiver's UART, looking for whether frames stop
   at once or repeat stale channel data for up to a second first. **This decides
   whether a frame-arrival watchdog is sufficient on its own.**
2. **Should a frame-arrival watchdog scale with the packet rate?** At 50 Hz
   packet rate a frame is due every 20 ms and a 200 ms watchdog is ten missed
   frames; at 500 Hz it is a hundred. `UNKNOWN` whether the threshold should
   scale with the configured rate, or stay fixed as a wall-clock safety number.
3. **How do 2.4 GHz and 900 MHz compare in a crowded hall?** No astromech source
   compares them, and the band is chosen at flash time.
4. **Does ELRS at 2.4 GHz interfere with a WiFi AP on the same droid?** Both are
   2.4 GHz. ELRS documents frequency hopping and claims tolerance of WiFi noise,
   but nobody has measured the reverse -- what an ELRS receiver does to an ESP32
   AP a metre away. Bench test: run both and watch link quality and AP
   throughput together.
5. **Does `getChannelsPacked()` give the raw domain unmodified?** Section 9.1
   assumes so from the struct type. Confirm before relying on it, or unpack the
   frame directly.
6. **What is `CRSFforArduino` like?** Not read this session (Section 9.2), and
   it is archived and AGPL-licensed, so it is a fallback rather than a
   candidate. Only matters if `AlfredoCRSF` is rejected.
7. **Does a 420 kbaud UART stay locked while WiFi is running?** Section 4.2's
   warning. Settled on a bench: run the AP and a CRSF stream together for an
   extended period and count framing and CRC errors, with and without any
   power-management setting that can move the APB clock.
8. **Can an ELRS link always be recovered without a power cycle?** Several
   ExpressLRS issues report failsafes that did not clear until the transmitter
   or receiver was restarted. For an aircraft that is a bad day; for a droid in
   a hall it decides whether an operator can recover on the floor or has to
   carry the droid out. `UNKNOWN` how current firmware behaves; worth asking
   the ELRS community rather than testing blind.
9. **Is half-duplex CRSF always inverted?** Sources disagree. The TBS
   specification says half-duplex is *"inverted or non-inverted"*, ExpressLRS
   exposes `PROTOCOL_CRSF` and `PROTOCOL_INVERTED_CRSF` as separate options,
   and the CRSF working group's physical-layer wiki was reported this session
   as saying half-duplex is inverted. **It does not affect the vehicle side**,
   which is full-duplex and non-inverted in every source -- but the sheet should
   not pretend the sources agree.

## 13. Sources

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
- **CHIRP Droid Control**: https://github.com/joymonkey/CHIRP (README)
- [`docs/spec-sheets/xbox-controller-input.md`](xbox-controller-input.md) for
  the gamepad grammar Section 10.2 compares against

**Vendor documentation**

- ExpressLRS documentation: https://www.expresslrs.org/ -- packet rates, switch
  modes, telemetry ratio, binding, the web UI, regional firmware
