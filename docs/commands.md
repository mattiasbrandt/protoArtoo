# protoArtoo Commands Reference

Implementation-focused command reference for supported command inputs.

This file is not a protocol spec and not a dome-link contract. It documents
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
| Manual commands | `POST /api/manual-command` with `command=<value>` | Fixed keyword set + prefix routing |
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
- `#st` (stationary mode)
- `#sm` (driving mode)

Prefix routing (case-sensitive):

- `$...` -> body audio queue
- `:...` and `#...` -> body Marcduino parser
- `*...`, `@...`, `%...`, `&...`, `!...` -> forwarded to dome TX

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
- Cue lines:
  - `BD:<cue>`
- Marcduino subset routed to body parser:
  - `:OPxx`, `:CLxx`, `:MVxxdddd`
  - `:SE30-:SE36`
  - `:SE01-:SE09`, `:SE15`, `:SE16` (decomposed to body-side actions)
  - `$...`
  - `#APSL`, `#APWU`

Intentionally not body-handled by parser path (ignored/deferred by topology):

- `@...`, `*...`, `%...`, `&...`, `!...`

## RC Bindable Command Actions

RC mapping and action testing use runtime tokens.

- List valid bindable tokens: `GET /api/actions`
- Bind/update mapping: `POST /api/rc/map`
- Trigger testable tokens: `POST /api/actions/test`

Do not hardcode token lists in docs. Use runtime discovery and
`docs/action-registry.yaml` as canonical inventory.
