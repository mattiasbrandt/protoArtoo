# protoR2 Commands Reference

Implementation-focused command reference for supported command inputs.

This file is not a protocol spec and not a protoR2link contract. It documents
what command inputs are accepted by the current firmware and where to verify
full behavior.

## Source of Truth

Use these as authoritative:

- `docs/action-registry.yaml` for canonical action/event names and metadata
- `docs/api.md` for HTTP request/response contracts
- `GET /api/actions` for runtime bindable action tokens
- `src/web/api_drive.cpp` and `src/web/api_system.cpp` for manual command routing
- `src/tasks/dome_link.cpp` and `src/drivers/dome_rx_parser.cpp` for dome RX parsing

## Command Surfaces

| Surface | Input form | Notes |
|---|---|---|
| HTTP API commands | `POST /api/...` | Full schemas in `docs/api.md` |
| Manual commands | `POST /api/manual-command` with `command=<value>` | Fixed keyword set + Command Ownership routing |
| Dome RX commands | UART/WiFi line input from dome | Body handles a bounded subset |
| RC bindable actions | token-based bindings via `/api/rc/map` | Discover valid tokens via `GET /api/actions` |

## HTTP API Commands

Command behavior is defined in `docs/api.md`.

To enumerate command-style surfaces without duplicating endpoint docs:

1. Use `docs/action-registry.yaml` entries with:
   - `type: action`
   - `api_path` not null
2. Verify request/response details in `docs/api.md`.

Examples of high-use command endpoints:

- `/api/estop`, `/api/estop/clear`
- `/api/drive`, `/api/drive/speed-preset`
- `/api/audio`, `/api/mood`
- `/api/servo`, `/api/dome`
- `/api/manual-command`, `/api/reboot`, `/api/sleep`, `/api/wake`

## Manual Commands (`POST /api/manual-command`)

Exact supported keyword commands (case-insensitive):

- `estop`
- `clear_estop`
- `enable_web_control`
- `disable_web_control`
- `reboot`

Refused (case-insensitive):

- `#st`, `#sm` -- these resolve to stationary/driving mode in the keyword
  table, but the routing below claims every `#` line first, so they never
  reach it and no mode ever changes. The route answers `400` and points at
  `POST /api/mode` (`docs/api.md`). Set the mode there, or with
  `system.action.set-mode` over the Console.

Routing (case-sensitive):

- `$...` -> body audio queue. `$8nn` is bank 8, sound nn, refused where the
  sound module has no bank 8; every other `$nnn` is a raw track
- `:...` and `#...` -> Command Ownership: the body runs the lines naming
  things it models and forwards every other one to the dome verbatim. A
  full-droid sequence (`:SE01`-`:SE09`, `:SE15`, `:SE16`) runs its body half
  and is forwarded too. The list of what the body answers, and what it
  refuses, is `docs/marcduino_commands.md`
- `*...`, `@...`, `%...`, `&...`, `!...` -> forwarded to dome TX (ADR 0045)

A forward is answered as a forward (`{"ok":true,"forwarded":true}`), never as
done, and a forward that could not be queued is answered `503`.

Sleep guard:

- In sleep mode, prefixed control commands are blocked for prefixes
  `$ : # * @ % & !` until wake.

## Dome RX Commands (Body-Side Handling)

Recognized line families from dome ingress:

- Heartbeat (`MD_DOME_HB`)
- Mood aliases: `:SE10`, `:SE11`, `:SE13`, `:SE14`
- Sequence control lines:
  - `dome=seqon,<seconds>`
  - `dome=seqoff`
  - `dome=rot,<speedPct>,<durationMs>` (timed dome rotation; clamped to +/-100,
    ignored when the Dome ESC is not staged active)
- Cue lines:
  - `BD:<cue>`
- Marcduino subset the body parser runs:
  - `:OPxx`, `:CLxx`, `:OFxx` (01-05 and 00/99), `:MVxxdddd` (01-05)
  - `:SE30-:SE36` (body routines: each starts the Factory Sequence `DM:SE30`..`DM:SE36`
    through the Sequence Coordinator, so a Retrained Sequence of that name replaces it)
  - `:SE01-:SE09`, `:SE15`, `:SE16` (the body half only)
  - `$...`
  - `#APSL`, `#APWU`, `#PAHB`

A line the dome sends is never forwarded back to it, including a full-droid
`:SE` line: the dome already has it.

The body does not run `@...`, `*...`, `%...`, `&...` or `!...` lines; those
belong to the dome. Every line the body does not run, these included, is
counted as an unknown dome RX line.

## RC Bindable Command Actions

RC mapping and action testing use runtime tokens.

- List valid bindable tokens: `GET /api/actions`
- Bind/update mapping: `POST /api/rc/map`
- Trigger testable tokens: `POST /api/actions/test`

Do not hardcode token lists in docs. Use runtime discovery and
`docs/action-registry.yaml` as canonical inventory.
