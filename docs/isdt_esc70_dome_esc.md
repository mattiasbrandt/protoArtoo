# ISDT ESC70 Dome ESC Baseline (protoArtoo)

This document defines the **project-recommended** ESC profile for dome rotation on protoArtoo.
Use it as a setup baseline before build-specific tuning.

## Scope and hardware context

- ESC: ISDT ESC70 (brushed ESC)
- Typical dome motor pairing: JGB37-520 24V 600 RPM brushed gearmotor
- Runtime command path in protoArtoo: `POST /api/dome`
  (standard RC PWM 1000-2000 us, neutral 1500 us)
- App/BLE is setup-time only; runtime control still comes from PWM input

The ESC70 is the recommended ESC for this project. Actual motor + drivetrain load
(gear mesh, ring friction, wiring quality, supply sag, motor voltage rating at actual bus voltage)
is build-specific, so final behavior must always be validated on each droid.

## Locked baseline settings (ISD Go app)

Use this profile as the default starting point:

| Setting | Recommended value | Why |
|---|---|---|
| Running mode | Forward and reverse | Required for bidirectional dome rotation |
| Battery Type | Match actual pack (commonly LiPo) | Correct protection behavior |
| Cells | Match actual pack (for example 3S) | Correct cutoff scaling |
| Cutoff voltage | Auto (or safe equivalent manual value) | Battery protection |
| BEC voltage | 5.0V | Conservative, stable baseline |
| Motor rotation | Forward (swap only if direction is inverted) | Mechanical direction match |
| PWM frequency | **1 kHz** | Strong low-end torque baseline in this project setup |
| Start force | **MAX** | Improves breakaway under dome load |
| Brake force | **1** (minimum available) | Reduces resistance during reversals |
| Active drag brake level | Disabled | Avoids neutral drag torque |
| Active brake enable | Disabled | Avoids aggressive braking on direction changes |

## Curve recommendations

Curve tuning does not increase available torque beyond 100% command, but it can improve
mid-range authority and reduce reversal losses.

Recommended curve targets:

- **Throttle curve (aggressive midrange):**
  - +50 throttle -> +80 to +85 power
  - -50 throttle -> -80 to -85 power
  - Keep endpoints +/-100 -> +/-100
- **Brake curve (soft):**
  - 50 throttle -> ~10 to 20 brake power
  - 100 throttle -> ~25 to 35 brake power

## Calibration and verification checklist

1. Complete remote throttle calibration (max, min, neutral) in ISD Go.
2. Confirm app throttle percentage mirrors protoArtoo command percentage.
3. Verify unloaded motor spin at +50 / +70 / +90 command.
4. Verify loaded dome behavior with sustained one-direction holds before testing reversals.
5. Test both directions and note any localized high-friction ring sectors.

## Integration notes

- Curve tuning improves partial-throttle response shape, but cannot exceed available 100% output torque.
- Under high drivetrain load, avoid instant hard direction flips; prefer neutral dwell + ramped reversal.
- Treat RC mapping and API/web command paths as separate verification items during bring-up.
## Sources

1. ISDT ESC70 product page: https://www.isdt.co/esc70.html?lang=en
2. ISDT ESC70 app menu guide: https://www.isdt.co/english-esc70-app-menu-guide.html?lang=en
3. ISDT ESC70 manual PDF: https://www.isdt.co/down/pdf/ESC70.pdf