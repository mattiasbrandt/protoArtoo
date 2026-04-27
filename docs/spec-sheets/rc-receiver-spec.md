### Arduino/ESP32 Communication to HOTRC or Regular Modern RC Receivers (Non-SBUS)

Non-SBUS communication with RC receivers typically uses PWM (Pulse Width Modulation) or PPM (Pulse Position Modulation) protocols. These are analog-style signals for transmitting channel data (e.g., throttle, steering) from the receiver to a microcontroller like Arduino or ESP32. HOTRC receivers (e.g., DS-600, DS-4A, F-08A) support PWM outputs in non-SBUS modes, with standard 20ms periods and 1000–2000µs pulses. Regular modern receivers (e.g., FlySky FS-iA6B, Turnigy iA6C) offer PWM/PPM options, compatible with Arduino's pulseIn() or ESP32's RMT for efficient reading. This setup is ideal for DIY drones, robots, or RC vehicles, allowing microcontrollers to interpret controls without flight controller intermediaries.

#### PWM Protocol Details
PWM is the simplest and oldest RC protocol, sending individual pulses per channel over separate wires.
- **Signal Characteristics**:
  - Pulse Width: 1000µs (min/low) to 2000µs (max/high); neutral/midpoint ~1500µs.
  - Period: 20ms (50Hz refresh rate); duty cycle varies with position (e.g., 50% at neutral for HOTRC analog mode).
  - Voltage: 3.3V–5V logic; ESP32 requires level shifting if receiver outputs 5V to avoid damage (use voltage divider or shifter).
  - Channels: One wire per channel (up to 6–8 typical); ground and power shared.
- **HOTRC-Specific**: In "analog mode" (blue LED off), PWM period is 20ms with 50% duty neutral. "Digital mode" (blue LED on) uses 3ms pulses (333Hz), but less common for standard RC—stick to analog for compatibility.
- **Reading on Microcontrollers**:
  - Arduino: Use `pulseIn(pin, HIGH, timeout)` to measure width; timeout ~25000µs to handle dropouts.
  - ESP32: Prefer RMT (Remote Control) peripheral for non-blocking, multi-channel reading; supports up to 8 channels simultaneously with raw pulse widths and frequencies.

#### PPM Protocol Details
PPM (also called CPPM or PPM-Sum) encodes multiple channels into a single wire, reducing wiring complexity.
- **Signal Characteristics**:
  - Frame: Series of pulses (one per channel + sync); total frame ~22.5ms.
  - Pulse Width: 300–500µs per channel separator; channel values as position from previous pulse (1000–2000µs offset).
  - Sync Pulse: >3000µs (often 5000–10000µs) to mark frame end/start.
  - Polarity: Positive (high pulses) or inverted (negative); auto-detectable in libraries.
  - Channels: Up to 8–12; e.g., channel 1 starts after sync.
- **HOTRC-Specific**: Limited PPM support; some models (e.g., Turnigy-compatible) output PPM on dedicated pin, but HOTRC primarily PWM—confirm model (e.g., iA6C has PPM).
- **Reading on Microcontrollers**:
  - Arduino: Use interrupts (e.g., pin change) to capture rising/falling edges; libraries like PPMReader handle decoding.
  - ESP32: RMT excels here—configures as RX mode to parse pulse trains; or use timers/interrupts for custom implementations.

#### Wiring
- **Basic Setup**: Connect receiver's signal pin(s) to microcontroller GPIO; share GND. Power receiver from 5V (Arduino) or 3.3V/5V (ESP32—check tolerance).
  - PWM: One GPIO per channel (e.g., Arduino pins 2–7; ESP32 any GPIO).
  - PPM: Single GPIO (e.g., Arduino pin 2/3 for interrupts; ESP32 GPIO 14).
  - HOTRC: Signal (white/orange), + (red, 4.5–6V), - (black). For 5V outputs, use 3.9k/6.8k divider for ESP32.
- **Power**: Receiver often powered by BEC (5V); avoid direct battery to prevent noise.
- **Failsafe**: If signal lost (>20ms no pulse), implement software timeout to neutral values.

#### Additional Technical Details
- **Latency/Resolution**: PWM/PPM ~20ms/frame; sufficient for most RC (50Hz). ESP32 RMT offers <1µs resolution.
- **Failsafe Implementation**: Monitor for pulses >25ms absent; set channels to neutral (1500µs) or safe (e.g., throttle 1000µs).
- **Libraries**: Arduino: PulseIn, PPMReader; ESP32: ESP-IDF RMT, or wrappers like bolderflight/sbus (adapt for PWM/PPM).
- **Compatibility**: Works with FlySky, Spektrum (DSM non-SBUS), Turnigy; HOTRC analog mode mirrors standard PWM.

### Sources
1. [Read PWM, Decode RC Receiver Input, and Apply Fail-Safe](https://projecthub.arduino.cc/kelvineyeone/read-pwm-decode-rc-receiver-input-and-apply-fail-safe-113bac)
2. [How to read PWM signal from RC receiver](https://forum.arduino.cc/t/how-to-read-pwm-signal-from-rc-receiver/78917)
3. [rewegit/esp32-rmt-pwm-reader](https://github.com/rewegit/esp32-rmt-pwm-reader)
4. [How to Use an RC Controller with an Arduino](https://www.partsnotincluded.com/how-to-use-an-rc-controller-with-an-arduino)
5. [Notes on ESP32 RMT Peripheral For Receiving RC PWM](https://newscrewdriver.com/2021/04/05/notes-on-esp32-rmt-peripheral-for-receiving-rc-pwm)
6. [How to generate PPM signal with ESP32 and Arduino](https://blog.quadmeup.com/2021/03/29/how-to-generate-ppm-signal-with-esp32-and-arduino)
7. [PPM Reader for Arduino](https://github.com/Nikkilae/PPM-reader)
8. [Reading RC Receiver PPM Signal Using Arduino](https://www.instructables.com/Reading-RC-Receiver-PPM-Signal-Using-Arduino)
9. [HotRC Transmitter & Receiver Manual](https://hackmd.io/@prooma/HyCKuGmGa)
10. [Modifying the HotRC DS-600](https://bithead942.wordpress.com/2023/01/21/modifying-the-hotrc-ds-600)
11. [RC Protocols Explained Simply (PWM, PPM, SBUS, CRSF, MAVLINK, SmartPort and others)](https://www.youtube.com/watch?v=6Xc2w7CU9uU)
12. [FPV Protocols Explained (CRSF, SBUS, DSHOT, ACCST, PPM, PWM and more)](https://oscarliang.com/rc-protocols)
13. [HOTRC receiver positive and negative signal wiring and servo switching](https://www.youtube.com/watch?v=r7OiDGQAULM)
14. [What does IBUS, SBUS, PPM, PWM mean? (2021)](https://www.youtube.com/watch?v=y7T4hBqOwxM)
15. [Radio Control Systems — Copter documentation](https://ardupilot.org/copter/docs/common-rc-systems.html)
