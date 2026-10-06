# ESP32 Servo Communication Reference

## Purpose

This document captures factual electrical and signaling behavior for controlling hobby RC servos from ESP32-class controllers.

## 1. Servo Control Signal Basics

Standard positional hobby servos use pulse-width control:
- Update rate: typically 50 Hz (20 ms frame)
- Neutral pulse: typically around 1500 us
- Nominal travel pulses: commonly around 1000-2000 us

Notes:
- Real servos vary by model and manufacturer.
- Some units accept a wider pulse span (for example 500-2500 us), but this must be calibrated before use.

## 2. Electrical Interface

- Servo power rail is typically 4.8-6.0 V (model dependent).
- Logic control is single-ended PWM pulse input referenced to ground.
- ESP32 GPIO output is 3.3 V logic; this works for most servo signal inputs.
- Always share ground between controller and servo power source.

Power constraints:
- Servo current spikes can be high (hundreds of mA to >1 A depending on model/load).
- Do not power multiple servos from weak regulator rails intended for MCU logic only.

## 3. Typical MG996R and MG90S Ranges

MG996R (commonly observed values):
- Voltage: about 4.8-6.0 V
- Stall torque: about 9-11 kg*cm (voltage dependent)
- Speed: about 0.15-0.19 s/60 deg (voltage dependent)
- Typical stall current can approach or exceed about 1.0-1.4 A
- Common pulse range in practice: around 1000-2000 us (some variants accept wider)
- Control pulse: commonly centered around 1500 us

MG90S (commonly observed values):
- Voltage: about 4.8-6.0 V
- Stall torque: about 1.8-2.2 kg*cm (voltage dependent)
- Speed: about 0.08-0.10 s/60 deg (voltage dependent)
- Typical stall current can approach about 0.7-0.8 A
- Common pulse range in practice: often wider than 1000-2000 us depending on unit
- Control pulse: commonly centered around 1500 us

Because clone variants are common, treat these as starting references and verify with the actual installed servo.

## 4. ESP32 Generation Methods

Common peripheral choices:
- LEDC PWM: widely used for straightforward multi-servo control
- MCPWM: useful where tighter control timing is needed

Implementation expectations:
- Keep output rate stable.
- Clamp commanded pulse widths to calibrated min/max per servo channel.
- Avoid writing commands outside verified mechanical limits.

## 5. Calibration Workflow

1. Start at neutral (about 1500 us).
2. Sweep pulse width in small steps toward each end.
3. Stop before hard mechanical end-stops.
4. Record per-servo min/center/max values.
5. Apply per-channel clamp values in software.
6. Re-check after installation under real load.

## 6. Reliability and Safety Notes

- Brownouts and jitter are usually power-distribution problems first.
- Use short signal paths and adequate decoupling on the servo rail.
- Continuous stall conditions overheat servos; avoid holding against hard stops.
- Treat calibration as mechanical-system specific, not just electronics-specific.

Common failure modes and mitigation:
- Jitter/humming: usually power noise, poor grounding, or timing jitter. Improve supply and grounding first.
- MCU resets under movement: usually servo current surge collapsing logic rail; separate servo power from MCU regulator.
- Pulse mismatch overtravel: a library default pulse window can overdrive mechanical limits on some servos.
- Weak/unstable motion: verify GPIO is output-capable and avoid strap-sensitive pins for production wiring.
- Thermal stress: if a mechanism binds, reduce commanded endpoints and add timeout/idle strategies.

ESP32 pin caveats:
- GPIO 34-39 are input-only and cannot generate servo PWM output.
- Avoid boot strapping pins for critical servo channels unless boot-state impact is fully understood.

## 7. References

- Espressif MCPWM servo control example:
  https://github.com/espressif/esp-idf/blob/master/examples/peripherals/mcpwm/mcpwm_servo_control/README.md
- TowerPro MG996R datasheet (reference vendor sheet):
  https://www.electronicoscaldas.com/datasheet/MG996R_Tower-Pro.pdf
- TowerPro MG90S datasheet (reference vendor sheet):
  https://www.electronicoscaldas.com/datasheet/MG90S_Tower-Pro.pdf
