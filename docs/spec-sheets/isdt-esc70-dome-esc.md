# ISDT ESC70 Brushed ESC Reference (Dome Rotation Use)

## Purpose

This document defines a practical baseline profile for ISDT ESC70 in bidirectional dome-rotation style applications.

## 1. Scope

- ESC model: ISDT ESC70 (brushed)
- Control input model: standard RC servo-style PWM control
- Typical command convention: 1000-2000 us pulse width, neutral at 1500 us
- ISDT mobile app is a setup/configuration path; runtime control remains PWM-driven
- Typical dome-use pairing example: brushed gearmotor such as JGB37-520 class

Final tuning is always mechanical-build dependent (ring friction, gear mesh, mass/inertia, wiring, supply sag, and motor characteristics).

## 2. Baseline Settings (ISD Go)

| Setting | Baseline | Rationale |
|---|---|---|
| Running mode | Forward and reverse | Required for bidirectional dome movement |
| Battery type | Match actual pack chemistry | Correct cutoff/protection behavior |
| Cell count | Match actual pack | Correct voltage scaling |
| Cutoff voltage | Auto (or equivalent safe manual value) | Battery protection |
| BEC voltage | 5.0 V | Conservative baseline for accessory power |
| Motor rotation | Forward (swap if mechanically reversed) | Direction alignment |
| PWM frequency | 1 kHz | Common low-end torque baseline for heavier loads |
| Start force | High or max | Improves breakaway torque |
| Brake force | Minimum practical value | Reduces abrupt reversal loading |
| Active drag brake | Disabled | Avoids neutral drag torque |
| Active brake | Disabled | Avoids aggressive braking on direction flips |

## 3. Curve Guidance

Curve shaping changes response feel, not absolute maximum output.

Suggested starting curve:
- Throttle curve: stronger midrange response while keeping endpoints linear
- Brake curve: soft low-mid brake values to avoid shock loading

Concrete baseline values used by many dome builds:
- Throttle curve target: around +50 input -> +80 to +85 output, and -50 input -> -80 to -85 output
- Keep endpoints close to linear saturation: +/-100 input -> +/-100 output
- Brake curve target: around 50 input -> 10 to 20 brake, 100 input -> 25 to 35 brake

## 4. Calibration and Verification Checklist

1. Complete throttle calibration (max, min, neutral) in ISD Go.
2. Verify command polarity and neutral hold behavior.
3. Test unloaded movement at multiple command levels.
4. Test loaded dome movement with sustained one-direction runs.
5. Test direction reversals with neutral dwell to check mechanical stress.
6. Re-tune start force and curve if breakaway or oscillation issues appear.

## 5. Operational Cautions

- Avoid immediate full-power direction reversals under heavy load.
- If cogging/stall appears at low command, increase start force first.
- If harsh reversals occur, reduce brake aggressiveness and add neutral dwell in controller logic.

## 6. References

- ISDT ESC70 product page: https://www.isdt.co/esc70.html?lang=en
- ISDT ESC70 app menu guide: https://www.isdt.co/english-esc70-app-menu-guide.html?lang=en
- ISDT ESC70 manual PDF: https://www.isdt.co/down/pdf/ESC70.pdf
