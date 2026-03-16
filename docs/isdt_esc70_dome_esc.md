### ISDT ESC70 Overview

The ISDT ESC70 is a waterproof, Bluetooth-enabled brushed electronic speed controller (ESC) designed for RC vehicles, particularly crawlers and scale models (1/8 or 1/10 scale). It supports 540/550/775 brushed motors and features app-based configuration via the ISD Go APP for real-time monitoring, parameter adjustments, and OTA firmware updates. The ESC uses standard RC PWM for throttle control from the receiver, while Bluetooth handles communication for setup and telemetry. It's positioned as a budget-friendly alternative to models like the Hobbywing 1080, with advanced features like adjustable BEC, PWM frequency tuning, and specialized modes for crawling (e.g., slope anti-skid lock).

#### Technical Specifications
- **Continuous/Peak Current**: 70A / 120A
- **Motor Compatibility**: 540/550/775 brushed motors
- **Battery Support**: 2-3S LiPo or 6-8 cell NiMH
- **BEC Output**: Adjustable 5.0V-7.5V (0.1V steps), up to 3A continuous; supports high-voltage servos
- **Dimensions**: 38.6 × 31.6 × 17.15 mm (excluding wires)
- **Weight**: ~49g (ESC) + ~4.5g (switch module)
- **Wires**: 16AWG, 200mm length (no plugs included)
- **Waterproofing**: Fully waterproof (IP67 equivalent; clean and dry after immersion to prevent corrosion)
- **Operating Temperature**: Cuts power above 90°C/194°F

#### Communication and Control Protocols
- **Throttle Control Protocol**: Standard RC PWM signal from the receiver's throttle channel (typically 1000-2000µs pulse width). Connects via a 3-wire cable (signal, VCC from BEC, GND). No proprietary serial protocol for runtime control—relies on PWM for forward, brake, and reverse operations.
- **Configuration and Telemetry Protocol**: Bluetooth Low Energy (BLE) for wireless connection to the ISD Go APP. The integrated Bluetooth module allows:
  - Real-time data querying (e.g., voltage, temperature, current, RPM).
  - Parameter programming (e.g., modes, curves, protections).
  - OTA firmware upgrades.
- **Bluetooth Pairing**: In low-power state (ESC powered but switched off), long-press the switch button until the blue LED flashes. Pair via app; steady blue LED indicates connection.
- **App Communication**: Uses proprietary Bluetooth protocol (not publicly detailed; app handles all interactions). Supports iOS/Android; no open-source specs available for the BLE service/characteristics.
- **Switch Module**: Electronic switch with LED indicators:
  - White: Power/status.
  - Green: On (no errors).
  - Red: On (with errors, e.g., low voltage).
  - Blue: Bluetooth pairing (flashing) or connected (steady).
- **Failsafe**: If throttle signal lost, ESC enters protection mode (e.g., no output).

#### Wiring and Connections
- **Motor**: Two output wires (no polarity requirement; swap for direction reversal or adjust in app).
- **Receiver**: Throttle line to receiver's throttle channel; BEC powers receiver/servos (do not add external power to avoid damage).
- **Battery**: Polarity-sensitive input (red +, black -); connect with switch off and verify with wheels elevated.
- **Switch Module**: Integrated for power control and Bluetooth; long-press (1s) to power on/off.

#### App Features and Settings (ISD Go APP)
- **Throttle Calibration**: Mandatory; guided process with beeps—set max, min, and neutral positions. Red exclamation in app if pending.
- **Running Modes**:
  - Forward with Brake: Only forward and brake.
  - Forward/Reverse with Brake: Double-tap reverse (brake first, then reverse if stopped).
  - Forward/Reverse: Immediate reverse on throttle input.
- **Cutoff Voltage**: Auto (per battery type) or manual 5.0V-12.0V.
- **BEC Voltage**: 5.0V-7.5V adjustable.
- **Motor Rotation**: Clockwise/counter-clockwise.
- **PWM Frequency**: Adjustable (lower for torque, higher for smoothness/efficiency).
- **Starting/Braking Force**: Controls response speed (higher = faster).
- **Active Drag Brake**: Applies reverse force at neutral throttle (adjustable levels; prevents rolling on slopes).
- **Slope Anti-Skid Lock**: Enhances drag brake for hill-holding in crawl modes.
- **Active Brake**: Boosts braking when throttle < -50%.
- **Throttle/Brake Curves**: Stepless adjustment; presets like Novice (70% max), Standard, Violent, Custom.
- **Startup Sound**: Customizable.
- **Mode Presets**: On-road, Drift, Off-road, Rock Crawler, Custom.
- **Other**: Real-time status, OTA updates, error alerts.

#### Protections
- **Low Voltage**: Cuts output below threshold to prevent battery damage.
- **Over-Temperature**: Shuts down above 90°C.
- **Throttle Loss**: No output if signal absent.
- **BEC Over/Under Voltage**: Protects connected devices.
- **Short-Circuit/Reverse Polarity**: Insulation checks recommended; reverse connection damages ESC.

#### Additional Technical Details
- **Synchronous Rectification**: Improves throttle linearity and temperature performance.
- **Firmware**: Upgradable via app for new features.
- **Compatibility**: Works with standard RC transmitters/receivers; app available for iOS/Android.
- **Variants**: Similar to ESC90 (higher current: 90A/180A).

For implementation in custom projects, Bluetooth integration would require reverse-engineering the app protocol, as no official API is provided. Standard PWM parsing libraries (e.g., in Arduino/ESP32) can handle throttle input.

### Sources
1. [ISDT ESC70 PDF Manual](https://www.isdt.co/down/pdf/ESC70.pdf)
2. [ESC70 APP Menu Guide - ISDT](https://www.isdt.co/english-esc70-app-menu-guide.html?lang=en)
3. [ISDT ESC70 Manual (Manual.nz)](https://www.manual.nz/isdt/esc70/manual)
4. [ISDT ESC70 Product Page](https://www.isdt.co/esc70.html?lang=en)
5. [ISDT ESC70 on ISDT Shop](https://isdtshop.com/products/isdt-esc70)
6. [FCC Report PDF for ESC70](https://fcc.report/FCC-ID/2A3R7ESC706080/5678937.pdf)
7. [RC Groups Thread on ISDT ESC70](https://www.rcgroups.com/forums/showthread.php?4032873=)
8. [YouTube Review: A Better Brushed Crawler ESC? ISDT ESC70](https://www.youtube.com/watch?v=vRB3SwvVNuQ)
9. [YouTube Review: iSDT ESC70](https://www.youtube.com/watch?v=jKbERTPDcWc)
10. [YouTube Unboxing: ISDT ESC70](https://www.youtube.com/watch?v=q5NCn7Fl5AA)

(Note: Sources are based on official manuals and community reviews. No open protocol specs for Bluetooth were found; it's proprietary.)
