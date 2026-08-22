# Verification Playbook

Use this checklist when reporting completion.

## Status labels

- `software-verified`: Build/tests/checks passed; no upload implied.
- `controller-upload-verified`: Flashed to ESP32 controller; smoke checks passed.
- `full-hardware-verified`: Verified on integrated droid hardware.
- `partial`: Some checks passed, but key checks are deferred.
- `full-hardware-required`: Validation requires full droid hardware integration and remains pending.

## Risk-based backend checks

Automated tests are evidence, not the goal. Choose checks based on the risk touched:

- Firmware behavior change: start with `pio run -e artoo_esp32`.
- Safety invariants, protocol parsing, shared state transitions, config persistence,
  JSON/API contracts, or prior regression paths: add `pio test -e native`.
- Action registry, RC tokens, or `ACTION_REGISTRY[]`: add `make check-action-drift`.
- Static-analysis investigation: add `pio check`; do not run it by default.
- Docs, comments, copy, agent definitions, UI styling, or low-risk cleanup with no
  behavior change: inspection and targeted checks are acceptable.

If upload is requested and hardware is available:

- `pio run -e <env> -t upload --upload-port <port-or-host>`

## Frontend fallback checks (hardware unavailable)

1. Serve `data/` locally on port `4173`.
2. Run relevant scripts in `test/playwright/<page>/` against `http://127.0.0.1:4173`.
3. Capture screenshot-backed evidence.

If blocked:
- If server start is denied by permissions/harness: continue with reachable URL-first validation. If no URL exists, request either a user-provided URL or permission for one server-start command.
- If Playwright MCP fails with schema/toolchain errors (for example `Failed to compile JSON schema`): retry once with fresh navigation cycle, then continue with script-based fallback when permitted.
- Only classify as `partial` after recovery paths are exhausted; include exact failing step and error text.

Blocked-case evidence packet (required before `partial`):
1. Failed tool call (exact tool name)
2. Attempted input (exact command/URL/arguments)
3. Exact runtime error text
4. Permission source (`local`/`project`/`managed`/`UNKNOWN`)
5. Remediation attempted in-run
6. Retry result after remediation
7. One concrete operator action requested

Permission remediation:
- Preferred safe command: `python3 -m http.server 4173 --directory data`.
- If denied, add/confirm explicit allow rule for that exact command in `.claude/settings.local.json` (or `.claude/settings.json` for team-wide).
- Retry once after permission update before classifying as blocked.

## Reporting template

- Verification status: `<status-label>`
- Commands run: `<list>`
- Proven behavior: `<what was validated>`
- Deferred checks: `<hardware/integration checks still required>`
