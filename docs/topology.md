# Topology - Classic MarcDuino vs protoR2

This document defines the wiring, signal ownership, and runtime control topology for protoR2.
It is intended as the practical architecture map that links hardware pinout, control flow,
and subsystem responsibility in one place.

## Table of Contents

- [Source of Truth Contract](#source-of-truth-contract)
- [High-Level Architecture](#high-level-architecture)
- [Component Families](#component-families)
- [Classic Baseline vs protoR2](#classic-baseline-vs-protor2)
- [Physical Topology](#physical-topology)
- [Body Controller Port Topology](#body-controller-port-topology)
- [RC Input Topology](#rc-input-topology)
- [Runtime Signal Ownership](#runtime-signal-ownership)
- [Control-Path Topology](#control-path-topology)
- [Safety Topology](#safety-topology)
- [Configuration and State Topology](#configuration-and-state-topology)
- [Integration Boundaries](#integration-boundaries)
- [Bring-Up and Verification Checklist](#bring-up-and-verification-checklist)

## Source of Truth Contract

Use the following precedence when topology details are needed:
1. docs/pin_map.md
2. include/config.h
3. include/component_registry.inc (each product's state: Supported, Tested, Roadmap)
4. docs/failsafe.md
5. docs/goal.md

If these sources diverge, reconcile them in the same change.

## High-Level Architecture

protoR2 is a body-controller-centered architecture with explicit subsystem ownership:
- The Body Controller owns the Foot Drive, RC input processing, safety enforcement,
  API/web control, body-side audio, the body's servos and lights, and Dome Rotation.
- The Dome Controller (AstroPixelsPlus-class stack) owns dome-local panels, lighting and
  animation behavior.
- Body and dome communicate over protoR2link: bidirectional serial through the slip ring,
  with WiFi as the fallback transport.

Each box below is a Component Family. The builder fits one member of it, and the
Body Controller reaches that member through the family's interface.

```
Operator (browser, Controller Console, or RC Radio)
        |
        v
Body Controller (protoR2 on the Artoo PCB or the FireBeetle 2)
    |- Foot Drive            -> wheel controller (UART)
    |- Sound                 -> sound module (UART)
    |- protoR2link          <-> Dome Controller (UART over slip ring; WiFi fallback)
    |- Body servo controller -> servos and LED strips on the board's Outputs (PWM),
    |                           or a PCA9685 (I2C)
    |- Dome Rotation         -> dome ESC (PWM)
```

## Component Families

From `include/component_registry.inc`. **Supported**: in the project and works.
**Tested**: has run on a real droid. **Roadmap**: planned. Products in a family are peers.

| Family | Supported | Roadmap | How the Body Controller reaches it |
|---|---|---|---|
| Body Controller | Artoo PCB (Tested); FireBeetle 2 (ESP32-P4) | | the running image is the answer |
| Radio Controller | HotRC DS-650 (Tested); RC Radio; RC Receiver - PWM; RC Receiver - SBUS; RC Receiver - ELRS (selectable, not read yet) | Xbox Controller | PWM or SBUS receiver input; none fitted is also an answer |
| Body servo controller | Body controller board GPIO (Tested); PCA9685 | Pololu Maestro | LEDC PWM on the board's Outputs; I2C |
| Dome Rotation | ISDT ESC70 (RC ESC) (Tested) | SyRen 10 | LEDC PWM |
| Dome Controller | AstroPixels Plus (Tested) | Teeces | protoR2link |
| Foot Drive | Hoverboard, hacked firmware | Sabertooth 2x25; Flipsky Mini V6 VESC | UART (Gen2.x 8-byte frames for the hoverboard) |
| Sound | DY-SV5W (Tested); MP3 Trigger; CHIRP Audio Trigger (Tested) | DFPlayer Mini | UART |

## Classic Baseline vs protoR2

| Aspect | Classic MarcDuino-style baseline | protoR2 topology |
|---|---|---|
| Body controller class | ATmega/Arduino body master patterns | ESP32 Body Controller: the Artoo PCB or the FireBeetle 2 |
| Dome serial model | Primarily one-way body-to-dome command direction | Bidirectional body-dome command and status flow (protoR2link) |
| Sound ownership | Commonly dome-side module ownership | Body-side audio authority |
| Drive transport | Sabertooth/SyRen ecosystems are common | A pluggable Foot Drive behind one interface; the hoverboard is Supported, Sabertooth and VESC are Roadmap |
| RC/control posture | Gamepad-centric and mixed legacy patterns | RC receivers plus browser-first operation, or the browser alone |

## Physical Topology

- Body board: one Body Controller - the Artoo PCB (artoo.uk, carrying a generic ESP32
  clone) or the FireBeetle 2 ESP32-P4 with its DFR1237 IO shield.
- Dome board: AstroPixelsPlus-class ESP32 controller.
- Body-dome interconnect: slip ring carrying at least TX, RX, and shared GND for serial.
- Motion peripherals:
	- The Foot Drive's wheel controller on a body-side UART.
	- The Dome Rotation ESC on a PWM output.
	- Servos (utility arms, doors) and LED strips on the board's Outputs, each named by what
	  the board prints beside it; a PCA9685 on I2C adds sixteen more.

## Body Controller Port Topology

Each board has its own serial allocation. `docs/pin_map.md` is the full map.

**Artoo PCB** (silkscreen headers):

| Header | Function | GPIO | Baud | Direction |
|---|---|---|---|---|
| S0 | USB debug | TX1 / RX3 | 115200 | Bidirectional |
| S1 | Foot Drive (hoverboard) | TX16 / RX17 | 115200 | Bidirectional transport, drive-owned protocol |
| S2 | Sound module | TX26 / RX35 | 9600 | TX-primary with optional status RX |
| S3 | protoR2link | TX33 / RX34 | 9600 | Bidirectional Marcduino-style serial |

- GPIO34 and GPIO35 are input-only.
- The chip has three UART controllers, so S3 and S2's RX share one: audio status queries
  run only while protoR2link is on its WiFi fallback, and S2's TX is a software UART.

**FireBeetle 2** (DFR1237 main-field rows):

| Rows | Function | GPIO | Baud | Direction |
|---|---|---|---|---|
| `20` + `21` | Foot Drive (UART1) | TX20 / RX21 | 115200 | Bidirectional transport, drive-owned protocol |
| `22` + `23` | protoR2link (UART2) | TX22 / RX23 | 9600 | Bidirectional Marcduino-style serial |
| `34` + `36` | Sound module (UART3) | TX34 / RX36 | 9600 | Hardware UART both directions |

- The ESP32-P4 has five UART controllers, so nothing is shared.

On both boards:
- Dome serial requires TX-RX cross-connection across the slip ring path.
- SBUS decoding uses RMT, avoiding UART port conflicts.

## RC Input Topology

The RC mode follows the Radio Controller and receiver picked on Configuration:

| Mode | Wiring (Artoo PCB / FireBeetle 2) | Intended use |
|---|---|---|
| standard_pwm | CH1-CH6 as PWM inputs (GPIO 15,13,2,4,12,27 / GPIO 28-33) | Conventional multi-channel PWM receivers |
| single_sbus | SBUS on CH1 (GPIO15 / GPIO28) | One receiver for core control |
| dual_sbus | SBUS1 on CH1, SBUS2 on CH2 (GPIO15 + GPIO13 / GPIO28 + GPIO29) | Split drive/dome control workflows |
| elrs | none read yet (#369) | Selectable; the droid behaves as with no receiver |
| not fitted | none | A droid driven from the web alone |

Default behavioral intent:
- SBUS1 carries drive-centric controls.
- SBUS2 carries dome/trigger-centric controls in dual-SBUS mode.
- Digital trigger-capable mappings can use SBUS channels 17/18.

## Runtime Signal Ownership

Ownership by subsystem is explicit to reduce ambiguity:

| Signal domain | Owner | Notes |
|---|---|---|
| Foot Drive command output | Body drive path | Safety-gated, speed-capped before transmit |
| Dome ESC output | Body PWM path | Receives mapped dome speed intent |
| Body audio playback | Body audio path | Body is authoritative sound source |
| Dome-local effects | Dome Controller | Managed dome-side by dome firmware |
| RC decode and mapping | Body RC path | Runtime mode + mapping profile driven |
| Browser control and configuration | Body web/API path | Persists config and updates runtime state |

## Control-Path Topology

### RC-to-drive path

```
RC receiver input (PWM or SBUS)
	-> RC decode/mapping
	-> drive intent (speed/steer)
	-> safety and limit gating
	-> Foot Drive frame output (hoverboard UART today)
```

### Browser-to-drive path

```
HTTP API request
	-> request validation
	-> drive intent update
	-> web-command timeout supervision
	-> safety and limit gating
	-> Foot Drive frame output (hoverboard UART today)
```

### Body-dome coordination path

```
Body command/status routing
	<-> protoR2link: bidirectional serial over the slip ring (WiFi fallback)
	<-> Dome Controller behavior/state
```

## Safety Topology

Drive safety is layered and converges on zero output behavior (`docs/failsafe.md`):
1. RC receiver hardware failsafe signaling (SBUS failsafe flag).
2. SBUS software watchdog timeout (200 ms default).
3. Web-drive command timeout (500 ms default).
4. ESP32 task watchdog reset (3 s); the next boot latches estop.
5. The Foot Drive's own timeout: the hoverboard firmware stops its motors after about
   500 ms without a valid frame, which is why the Body Controller never goes silent and
   keeps sending zero frames.

Layers 1 and 2 apply only while a Radio Controller is fitted; with none, the web-drive
timeout and the estop hold the feet. Layer 5 is the hoverboard's; another Foot Drive
brings its own.

Estop topology:
- Estop is latching and requires explicit clear action.
- Loss/recovery of input links does not automatically clear estop.

## Configuration and State Topology

Configuration model:
- Runtime configuration is persisted and applied without requiring rebuilds for normal operation.
- The fitted member of each Component Family, RC mode, mapping, calibration, and major
  subsystem settings are operator-editable via web surfaces.

State visibility model:
- Status and diagnostics API surfaces expose control-state and health context.
- RC diagnostics include source-link health and mapped-channel perspective.
- Failsafe state and trigger-source visibility are explicit for troubleshooting.

## Integration Boundaries

Boundary intent for long-term maintainability:
- Protocol-specific details remain isolated in their respective decode/driver paths.
- Web handler layer validates and routes intent; hardware actuation is executed by task/driver paths.
- Body and dome are coordinated peers with explicit transport contracts, not hidden side effects.

## Bring-Up and Verification Checklist

Use this checklist when validating a new build or wiring refresh:
1. Verify serial-port wiring against docs/pin_map.md for your board, including the dome TX-RX cross.
2. Verify selected RC mode wiring matches configured mode.
3. Confirm drive zero-output behavior under failsafe and estop states.
4. Confirm protoR2link bidirectional traffic and state transitions.
5. Confirm audio authority is body-side and commands route correctly.
6. Confirm status/diagnostic endpoints reflect expected live topology state.
