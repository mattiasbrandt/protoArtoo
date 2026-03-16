### SBUS Protocol Overview

The SBUS (Serial Bus) protocol is a digital serial communication standard developed by Futaba for remote control (RC) systems, such as those used in drones, airplanes, and other hobbyist vehicles. It enables efficient transmission of multiple channel commands (typically for servos or ESCs) over a single wire, supporting up to 18 channels in a compact frame. Unlike pulse-width modulation (PWM), SBUS uses a bus architecture, allowing daisy-chaining of devices. The protocol is inverted serial logic, meaning the signal is active-low (logical '1' is low voltage), and often requires an inverter for compatibility with standard UART interfaces.

#### Serial Configuration
- **Baud Rate**: 100,000 bps (100 kbps) for standard mode. A variant called "Fast SBUS" (not universally supported) uses 200,000 bps for higher refresh rates.
- **Data Bits**: 8
- **Parity**: Even (E)
- **Stop Bits**: 2
- **Format Notation**: Commonly described as 8E2 (8 data bits, even parity, 2 stop bits), with 1 start bit per byte.
- **Bit Order**: Most significant bit (MSB) first.
- **Logic Level**: Inverted (high voltage = logical 0, low voltage = logical 1). This requires hardware inversion (e.g., via a transistor or optoisolator) when interfacing with non-inverted UARTs.

#### Frame Structure
Each SBUS frame is 25 bytes long and transmitted at regular intervals:
- **Frame Interval**: Approximately 7 ms (high-speed mode, ~143 Hz) or 14 ms (analog/standard mode, ~71 Hz). Some implementations report 10 ms or 20 ms intervals, depending on the transmitter configuration.
- **Overall Frame Breakdown**:
  - **Byte 0 (Header/Start Byte)**: Fixed value `0x0F` (binary: 00001111).
  - **Bytes 1–22 (Channel Data)**: Packed data for 16 proportional channels (176 bits total, as each channel is 11 bits).
  - **Byte 23 (Flags/Status Byte)**: Contains digital channels and status indicators.
  - **Byte 24 (Footer/End Byte)**: Fixed value `0x00` (binary: 00000000).

The frame is sent periodically from the receiver to connected devices (e.g., flight controllers or servos).

#### Channel Data Encoding
- **Channels Supported**: 16 proportional (analog) channels + 2 digital channels, for a total of 18.
- **Proportional Channels (1–16)**: Each is an 11-bit unsigned integer (range: 0–2047). This provides high resolution for control (e.g., throttle, ailerons).
  - **Packing Mechanism**: Channels are bit-packed across the 22 data bytes without byte alignment. For example:
    - Channel 1: Bits 0–7 from Byte 1 + Bits 0–2 from Byte 2.
    - Channel 2: Bits 3–7 from Byte 2 + Bits 0–5 from Byte 3.
    - This continues sequentially, requiring bit-shifting and masking in code to extract values (e.g., `channel1 = (byte1 | (byte2 & 0x07) << 8)`).
  - **Typical Value Mapping**: Neutral position is around 1024 (midpoint). Full range often maps to ~172–1811 for ±100% travel, or 0–2047 for extended ±150% in some receivers (e.g., FrSky).
- **Digital Channels (17–18)**: Binary (on/off) states, encoded in the flags byte. Values are typically 0 or 2047 (full on/off). Not all servos or receivers support these.

#### Flags Byte (Byte 23)
This byte provides status and additional data:
- **Bit 0**: Channel 17 (digital; 0x01 if set).
- **Bit 1**: Channel 18 (digital; 0x02 if set).
- **Bit 2**: Frame lost (0x04 if a single frame was dropped between transmitter and receiver).
- **Bit 3**: Failsafe activated (0x08 if multiple consecutive frames lost, triggering receiver failsafe mode—e.g., throttle to zero).
- **Bits 4–7**: Reserved or unused (often 0).

In failsafe mode, the receiver may hold last-known positions or revert to predefined safe values.

#### Additional Technical Details
- **Voltage Levels**: Typically 3.3V or 5V logic, but the inverted nature means idle state is high (around VCC).
- **Error Handling**: Relies on parity for basic error detection per byte. Frame loss is explicitly flagged, but no built-in CRC—higher-level implementations may add checksums.
- **Compatibility**: Widely used in RC ecosystems (e.g., Futaba, FrSky receivers). SBUS decoders in software (e.g., Arduino libraries) parse the frame by reading the serial stream and unpacking bits.
- **Variants**: Some systems support telemetry feedback (e.g., SBUS2 adds bidirectional communication), but standard SBUS is unidirectional from receiver to controller.

For implementation, parsing code typically involves reading 25 bytes from a UART, checking the header/footer, unpacking channels via bitwise operations, and handling flags for safety checks. If you need sample code or further clarification on parsing, let me know.

### Sources
1. [SBUS Protocol - Embedded Flight Software - WARG](https://uwarg-docs.atlassian.net/wiki/spaces/ZP/pages/2238283817/SBUS+Protocol)
2. [SBUS - EmbeddedTS](https://docs.embeddedts.com/SBUS)
3. [Futaba S-BUS Introduction S-BUS protocol](https://cdck-file-uploads-europe1.s3.dualstack.eu-west-1.amazonaws.com/arduino/original/4X/3/6/a/36adfecc5ac5988048603b11d50132c3e5d79b49.pdf)
4. [README.md - bolderflight/sbus](https://github.com/bolderflight/sbus/blob/main/README.md)
5. [S-BUS protocol for MORSE](https://www.racom.eu/eng/support/prot/sbus/index.html)
6. [S-BUS Protocol Overview and Specifications | PDF](https://www.scribd.com/document/391656774/SBUS-Comm)
7. [SBUS protocol stucture - Programming](https://forum.arduino.cc/t/sbus-protocol-stucture/446591)
8. [SBUS RC PROTOCOL HOW IT WORKS - YouTube](https://www.youtube.com/watch?v=IqLUHj7nJhI)
9. [Futaba S.Bus System - Espruino](https://www.espruino.com/SBus)
10. [sbus_rs - Rust](https://docs.rs/sbus-rs)
12. [UART / SBUS Data Aqcuisition](https://community.st.com/t5/stm32-mcus-embedded-software/uart-sbus-data-aqcuisition/td-p/626023)
13. [SBUS-dat](https://w2.electrodragon.com/Network-dat/RC-dat/RC-protocols-dat/SBUS-dat/SBUS-dat.md)
14. [S-BUS protocol for MORSE](https://www.racom.eu/download/sw/prot/free/eng/sbus.pdf)
