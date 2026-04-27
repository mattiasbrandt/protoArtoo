# SBUS Protocol

## 0. Authority Contract

This document is an implementation authority for SBUS frame parsing and validation behavior.

Authority order for agent decisions:

1. Vendor or standards references listed in Sources.
2. This document.
3. Secondary community references.

If references conflict:

- Prefer vendor/standards references.
- If still unresolved, mark value as `UNKNOWN` and stop implementation changes that depend on it.
- Request new evidence instead of trial-and-error tuning.

Agent requirements when using this document:

- MUST treat sections labeled protocol as normative.
- MUST label heuristics as heuristics in code comments and notes.
- MUST NOT invent byte layouts, bit assignments, or timing constants not present here.
- MUST NOT silently switch between inverted and non-inverted assumptions.

## 1. Overview

SBUS is a serial protocol used in RC systems to transport multiple channels over one signal line.

- Capacity: 16 proportional channels + 2 digital channels.
- Transport: UART-like framing with fixed-length messages.
- Typical use: receiver-to-controller/actuator command transport.

## 2. UART and Electrical Characteristics

For Futaba-style SBUS, serial configuration is:

- Baud rate: `100000` (non-standard UART baud)
- Frame format: `8E2`
  - 1 start bit
  - 8 data bits
  - 1 even parity bit
  - 2 stop bits
- Logic level convention: inverted relative to normal UART TTL representation.

Normative parser baseline for this spec:

- Assume `100000`, `8E2`, inverted signaling.
- Treat alternative electrical variants as profile overrides, not default behavior.

Parity detail:

- Even parity means the total number of `1` bits in the data+parity set is even.
- If data bits contain an odd number of `1` values, parity bit is `1`; otherwise `0`.

Note on receiver ecosystems:

- Futaba documentation and reverse-engineered references describe inverted signaling.
- Some FrSky-related references describe non-inverted variants.

Implementation note:

- On MCUs with UART inversion support, inversion can be enabled in UART advanced settings instead of external hardware inversion.

## 3. Message Structure

An SBUS message is 25 bytes:

- Byte 0: header (`0x0F`)
- Bytes 1-22: packed channel payload (16 x 11-bit channels)
- Byte 23: flag byte
- Byte 24: footer (`0x00` in baseline references)

Normative frame acceptance rules:

- MUST require exact frame length of 25 bytes.
- MUST require header byte `0x0F`.
- MUST decode flags from byte 23 using Section 5 bit assignments.
- MUST treat footer handling as profile-configurable when hardware evidence shows accepted variants.

Timing commonly documented in public references:

- On-wire transmit duration: about 3 ms per message at 100 kbaud with 8E2.
- Repetition interval: around 14 ms (analog mode) or 7 ms (high-speed mode).

Bit transmission order:

- UART transmits bits least-significant-bit first on the wire.
- Keep this in mind when comparing oscilloscope captures to decoded byte values.

## 4. Channel Packing

Packing properties:

- CH1-CH16 are unsigned 11-bit fields.
- Data is densely packed across bytes 1-22 (not byte-aligned).
- Least-significant channel bits are placed first in the packed stream.

Common extraction pattern examples:

- `ch1 = ((b1 | (b2 << 8)) & 0x07FF)`
- `ch2 = (((b2 >> 3) | (b3 << 5)) & 0x07FF)`
- `ch3 = (((b3 >> 6) | (b4 << 2) | (b5 << 10)) & 0x07FF)`
- `ch4 = (((b5 >> 1) | (b6 << 7)) & 0x07FF)`

Where `b1..b22` correspond to frame bytes 1..22.

Complete reference unpacking formulas (CH1-CH16):

- `ch1  = ((b1       | (b2  << 8))                 & 0x07FF)`
- `ch2  = (((b2>>3)  | (b3  << 5))                 & 0x07FF)`
- `ch3  = (((b3>>6)  | (b4  << 2) | (b5  << 10))   & 0x07FF)`
- `ch4  = (((b5>>1)  | (b6  << 7))                 & 0x07FF)`
- `ch5  = (((b6>>4)  | (b7  << 4))                 & 0x07FF)`
- `ch6  = (((b7>>7)  | (b8  << 1) | (b9  << 9))    & 0x07FF)`
- `ch7  = (((b9>>2)  | (b10 << 6))                 & 0x07FF)`
- `ch8  = (((b10>>5) | (b11 << 3))                 & 0x07FF)`
- `ch9  = ((b12      | (b13 << 8))                 & 0x07FF)`
- `ch10 = (((b13>>3) | (b14 << 5))                 & 0x07FF)`
- `ch11 = (((b14>>6) | (b15 << 2) | (b16 << 10))   & 0x07FF)`
- `ch12 = (((b16>>1) | (b17 << 7))                 & 0x07FF)`
- `ch13 = (((b17>>4) | (b18 << 4))                 & 0x07FF)`
- `ch14 = (((b18>>7) | (b19 << 1) | (b20 << 9))    & 0x07FF)`
- `ch15 = (((b20>>2) | (b21 << 6))                 & 0x07FF)`
- `ch16 = (((b21>>5) | (b22 << 3))                 & 0x07FF)`

## 5. Flag Byte Definition (Byte 23)

Bit assignment:

- Bit 0: digital channel 17
- Bit 1: digital channel 18
- Bit 2: frame lost
- Bit 3: failsafe active
- Bits 4-7: reserved

Equivalent bit view often shown in references:

`[0 0 0 0 failsafe frame_lost ch18 ch17]`

Common decoding:

- `ch17 = (flags & 0x01) ? 1 : 0`
- `ch18 = (flags & 0x02) ? 1 : 0`
- `frame_lost = (flags & 0x04) != 0`
- `failsafe = (flags & 0x08) != 0`

Bit-label caveat:

- Some documents present equivalent semantics in reversed capture-oriented bit labels.
- For decoded logical bytes, bits 0..3 mapping above is the common parser convention.

## 6. Value Domain

- Raw proportional channel representation: `0..2047` (11-bit).
- Common operational windows referenced by flight-controller ecosystems include:
  - about `192..1792` on wire for nominal travel
  - mappings to control units like `1000..2000`
- Other implementations also use common reference points near `172 / 992 / 1811`.

Normative interpretation:

- Parser output domain is `0..2047`.
- Any mapping to PWM/us/percent is an application-layer transform and out of scope for frame decode.
- Agents MUST NOT hardcode control mapping ranges into parser logic unless explicitly requested.

## 7. Synchronization Notes from Public Decoder References

- Synchronization should rely on frame boundaries and inter-message idle behavior.
- Payload-content-only synchronization is insufficient because fixed-like patterns can appear inside packed channel bits.
- Many decoders treat malformed header/footer/timing as frame abort conditions and resynchronize on the next frame boundary.

Typical parser state-machine behavior:

1. Wait for header byte (`0x0F`).
2. Buffer until full 25-byte frame.
3. Validate footer and flags layout.
4. Decode channels and status bits.
5. Abort and resync on malformed frames.

Timing health heuristics found in public implementation notes:

- Expect an inter-frame idle gap before each new message.
- Expect small byte-to-byte spacing within a valid frame.
- Large in-frame timing gaps are treated as framing loss and trigger resynchronization.

These are implementation heuristics, not extra wire-format fields.

Normative sync safety rules:

- MUST fail closed: reject malformed frame candidates and resynchronize.
- MUST NOT accept a frame on payload plausibility alone without structural checks.
- MUST make frame validity checks deterministic and repeatable.

## 8. Representation Caveat

Some references display captured data in inverted/bit-order views where the start byte can appear as `0xF0`. In logical decoded SBUS framing, header is treated as `0x0F`.

## 9. Agent Lookup Quick Reference

Use this table first when implementing or reviewing parser behavior.

- Field: Frame length. Required value: 25 bytes.
- Field: Header. Required value: `0x0F`.
- Field: Channel payload bytes. Required value: bytes 1-22 packed 16 x 11-bit LSB-first.
- Field: Flags byte index. Required value: 23.
- Field: Footer byte index. Required value: 24.
- Field: CH17 bit. Required value: flags bit 0.
- Field: CH18 bit. Required value: flags bit 1.
- Field: lost_frame bit. Required value: flags bit 2.
- Field: failsafe bit. Required value: flags bit 3.
- Field: Raw channel range. Required value: `0..2047`.

If a required value above cannot be proven for the target hardware profile, status is `UNKNOWN` and implementation work should stop pending clarification.

## 10. Sources

- UWARG SBUS Protocol page: https://uwarg-docs.atlassian.net/wiki/spaces/efs/pages/2238283817/SBUS+Protocol
- RPG Quadrotor Control wiki SBUS page: https://github.com/uzh-rpg/rpg_quadrotor_control/wiki/SBUS-Protocol#for-sbus-a-serial-port-has-to-be-configured-as-follows
- sigrok protocol decoder page: https://sigrok.org/wiki/Protocol_decoder:Sbus_futaba
- Ordinoscope SBUS notes and decoder example: https://www.ordinoscope.net/index.php/Electronique/Protocoles/SBUS
- mbed notebook (Futaba S-BUS controlled by mbed): https://os.mbed.com/users/Digixx/notebook/futaba-s-bus-controlled-by-mbed/
- mbed SBUS library source (FutabaSBUS.cpp): https://os.mbed.com/users/Digixx/code/SBUS-Library_16channel/file/83e415034198/FutabaSBUS/FutabaSBUS.cpp/
