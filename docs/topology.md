# Topology - Classic MarcDuino vs protoArtoo

This document defines the wiring, signal ownership, and runtime control topology for protoArtoo.
It is intended as the practical architecture map that links hardware pinout, control flow,
and subsystem responsibility in one place.

## Table of Contents

- [Source of Truth Contract](#source-of-truth-contract)
- [High-Level Architecture](#high-level-architecture)
- [Classic Baseline vs protoArtoo](#classic-baseline-vs-protoartoo)
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
3. docs/failsafe.md
4. docs/goal.md

If these sources diverge, reconcile them in the same change.

## High-Level Architecture

protoArtoo is a body-controller-centered architecture with explicit subsystem ownership:
- Body controller (Artoo PCB + ESP32) owns drive, RC input processing, safety enforcement,
	API/web control, and body-side audio.
- Dome controller (AstroPixelsPlus-class stack) owns dome-local lighting/animation behavior.
- Body and dome communicate over a bidirectional serial link through the slip ring.

```
Operator (browser or RC TX)
				|
				v
Body Controller (protoArtoo on Artoo PCB)
	|- Drive control -> Hoverboard controller (UART)
	|- Audio control -> Audio module (UART)
	|- Dome link    <-> Dome controller (UART over slip ring)
	|- Servo/ESC    -> Body servos + dome ESC (PWM)
```

## Classic Baseline vs protoArtoo

| Aspect | Classic MarcDuino-style baseline | protoArtoo topology |
|---|---|---|
| Body controller class | ATmega/Arduino body master patterns | ESP32 body controller on Artoo PCB |
| Dome serial model | Primarily one-way body-to-dome command direction | Bidirectional body-dome command and status flow |
| Sound ownership | Commonly dome-side module ownership | Body-side audio authority |
| Drive transport | Sabertooth/SyRen ecosystems are common | Hoverboard UART contract is primary target |
| RC/control posture | Gamepad-centric and mixed legacy patterns | RC receiver modes plus browser-first operation |

## Physical Topology

- Body board: Artoo Controller PCB v1.1/v1.2 with ESP32 D1 Mini form factor module.
- Dome board: AstroPixelsPlus-class ESP32 controller.
- Body-dome interconnect: slip ring carrying at least TX, RX, and shared GND for serial.
- Motion peripherals:
	- Hoverboard motor controller on body-side UART link.
	- Dome motor ESC on PWM output.
	- Utility arms and AUX channels on PWM outputs.

## Body Controller Port Topology

Serial headers and ownership:

| Header | Function | GPIO | Baud | Direction |
|---|---|---|---|---|
| S0 | USB debug | TX1 / RX3 | 115200 | Bidirectional |
| S1 | Hoverboard drive | TX16 / RX17 | 115200 | Bidirectional transport, drive-owned protocol |
| S2 | Sound module | TX26 / RX35 | 9600 | TX-primary with optional status RX |
| S3 | Dome link | TX33 / RX34 | 9600 | Bidirectional Marcduino-style serial |

Important electrical/topology notes:
- GPIO34 and GPIO35 are input-only.
- Dome serial requires TX-RX cross-connection across the slip ring path.
- SBUS decoding uses RMT on GPIO15 and GPIO13, avoiding UART port conflicts.

## RC Input Topology

protoArtoo supports three mutually exclusive runtime RC modes:

| Mode | Wiring | Intended use |
|---|---|---|
| standard_pwm | CH1-CH6 as PWM inputs (GPIO 15,13,2,4,12,27) | Conventional multi-channel PWM receivers |
| single_sbus | SBUS on CH1 / GPIO15 | One receiver for core control |
| dual_sbus | SBUS1 on CH1 / GPIO15 and SBUS2 on CH2 / GPIO13 | Split drive/dome control workflows |

Default behavioral intent:
- SBUS1 carries drive-centric controls.
- SBUS2 carries dome/trigger-centric controls in dual-SBUS mode.
- Digital trigger-capable mappings can use SBUS channels 17/18.

## Runtime Signal Ownership

Ownership by subsystem is explicit to reduce ambiguity:

| Signal domain | Owner | Notes |
|---|---|---|
| Drive command output | Body drive path | Safety-gated, speed-capped before transmit |
| Dome ESC output | Body PWM path | Receives mapped dome speed intent |
| Body audio playback | Body audio path | Body is authoritative sound source |
| Dome-local effects | Dome controller | Managed dome-side by dome firmware |
| RC decode and mapping | Body RC path | Runtime mode + mapping profile driven |
| Browser control and configuration | Body web/API path | Persists config and updates runtime state |

## Control-Path Topology

### RC-to-drive path

```
RC receiver input (PWM or SBUS)
	-> RC decode/mapping
	-> drive intent (speed/steer)
	-> safety and limit gating
	-> hoverboard UART frame output
```

### Browser-to-drive path

```
HTTP API request
	-> request validation
	-> drive intent update
	-> web-command timeout supervision
	-> safety and limit gating
	-> hoverboard UART frame output
```

### Body-dome coordination path

```
Body command/status routing
	<-> bidirectional serial link over slip ring
	<-> dome controller behavior/state
```

## Safety Topology

Drive safety is layered and converges on zero output behavior:
1. RC receiver hardware failsafe signaling.
2. SBUS software watchdog timeout.
3. Web-drive command timeout.
4. ESP32 task watchdog reset behavior.
5. Hoverboard-side UART timeout.

Estop topology:
- Estop is latching and requires explicit clear action.
- Loss/recovery of input links does not automatically clear estop.

## Configuration and State Topology

Configuration model:
- Runtime configuration is persisted and applied without requiring rebuilds for normal operation.
- RC mode, mapping, calibration, and major subsystem settings are operator-editable via web surfaces.

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
1. Verify serial-port wiring against docs/pin_map.md (S1/S2/S3, including dome TX-RX cross).
2. Verify selected RC mode wiring matches configured mode.
3. Confirm drive zero-output behavior under failsafe and estop states.
4. Confirm dome link bidirectional traffic and state transitions.
5. Confirm audio authority is body-side and commands route correctly.
6. Confirm status/diagnostic endpoints reflect expected live topology state.
