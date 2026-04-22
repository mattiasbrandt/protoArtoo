# Verification Playbook

Use this checklist when reporting completion.

## Status labels

- `usb-standalone-verified`: Verified on an ESP32 connected over USB only, with no additional droid hardware/serial peripherals attached.
- `partial`: Some checks passed, but key checks are deferred.
- `full-hardware-required`: Validation requires full droid hardware integration and remains pending.

## Backend baseline checks

1. `pio run -e protoArtoo`
2. `pio test -e native`
3. `pio check`

If upload is requested and hardware is available:

4. `pio run -e <env> -t upload --upload-port <port-or-host>`

## Frontend fallback checks (hardware unavailable)

1. Serve `data/` locally on port `4173`.
2. Run relevant scripts in `test/playwright/<page>/` against `http://127.0.0.1:4173`.
3. Capture screenshot-backed evidence.

## Reporting template

- Verification status: `<status-label>`
- Commands run: `<list>`
- Proven behavior: `<what was validated>`
- Deferred checks: `<hardware/integration checks still required>`
