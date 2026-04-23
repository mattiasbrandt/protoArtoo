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

If blocked:
- If server start is denied by permissions/harness: continue with reachable URL-first validation. If no URL exists, request either a user-provided URL or permission for one server-start command.
- If Playwright MCP fails with schema/toolchain errors (for example `Failed to compile JSON schema`): retry once with fresh navigation cycle, then continue with script-based fallback when permitted.
- Only classify as `partial` after recovery paths are exhausted; include exact failing step and error text.

Permission remediation:
- Preferred safe command: `python3 -m http.server 4173 --directory data`.
- If denied, add/confirm explicit allow rule for that exact command in `.claude/settings.local.json` (or `.claude/settings.json` for team-wide).
- Retry once after permission update before classifying as blocked.

## Reporting template

- Verification status: `<status-label>`
- Commands run: `<list>`
- Proven behavior: `<what was validated>`
- Deferred checks: `<hardware/integration checks still required>`
