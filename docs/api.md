# REST API Reference

The current web stack exposes a safety-focused HTTP and SSE control surface over
the `protoArtoo` access point.

## Implementation Structure

API routes are organized into focused modules under `src/web/` with corresponding
headers in `include/`:

| File | Responsibility | Endpoints |
|------|---------------|-----------|
| `api_estop.cpp` | Emergency stop control | `POST /api/estop`, `POST /api/estop/clear` |
| `api_drive.cpp` | Drive commands, web control, and operation mode | `POST /api/drive`, `POST /api/web-control/enable`, `POST /api/web-control/disable`, `POST /api/mode` |
| `api_config.cpp` | Configuration management | `GET /api/config`, `POST /api/config` |
| `api_audio.cpp` | Audio control, mapping, and module status | `GET/POST /api/audio`, `POST /api/audio/query`, `GET/POST /api/audio/tracks`, `POST /api/audio/category-range`, `GET/POST /api/audio/mood-map`, CHIRP-only: `GET /api/audio/catalog`, `POST /api/audio/catalog/refresh`, `POST /api/audio/play-banked` |
| `api_status.cpp` | Status, health, and telemetry | `GET /api/status`, `GET /api/health`, `GET /api/logs`, `GET /api/wifi`, `GET /api/serial` |
| `api_validation.cpp` | Consolidated validation snapshot | `GET /api/validation` |
| `api_system.cpp` | System control and OTA | `POST /api/manual-command`, `POST /api/reboot`, `POST /upload/firmware` |
| `api_helpers.cpp` | Pure parsing/formatting helpers | Shared JSON formatting utilities |

Each module exposes a `register*Routes(AsyncWebServer&)` function called from
`web_server.cpp` during server initialization. This modular structure replaces
the previous monolithic `api_estop.cpp` that contained all 14 API routes.

## Base network

- AP SSID: `protoArtoo`
- Default AP IP: `192.168.4.1`
- Transport: HTTP on port 80

## Endpoints

### `POST /api/estop`

Latch the emergency stop.

- Request body: none
- Success response:

```json
{"ok":true}
```

Behavior:

- sets `robotState.estop = true`
- sets `robotState.failsafeSource = FS_ESTOP_CMD`
- increments `robotState.failsafeTriggerCount`

### `POST /api/estop/clear`

Clear the latched emergency stop.

- Request body: none
- Success response:

```json
{"ok":true}
```

Behavior:

- sets `robotState.estop = false`
- clears `failsafeSource` back to `FS_NONE` only if the active source was
  `FS_ESTOP_CMD`

### `POST /api/web-control/enable`

Enable explicit browser-control mode.

- Request body: none
- Success response:

```json
{"ok":true}
```

Behavior:

- sets `robotState.webControlEnabled = true`
- allows `/api/drive` to work even when SBUS is currently absent

### `POST /api/web-control/disable`

Disable explicit browser-control mode.

- Request body: none
- Success response:

```json
{"ok":true}
```

Behavior:

- sets `robotState.webControlEnabled = false`
- pushes a zero internal drive command when disabling browser-control mode

### `POST /api/drive`

Send a browser drive command.

- Request body: form-encoded `speed` and `steer`
- Success response:

```json
{"ok":true}
```

Behavior:

- routes the command through `SRC_WEB_API`
- applies the current `cfg_speedLimitMax` clamp
- clears `webDriveExpired` on fresh commands
- rejects commands while `estop` or `stationary` is active
- also rejects commands when SBUS is unhealthy unless explicit web-control mode
  has been enabled first
- rejects non-integer `speed` or `steer` values with `HTTP 400`

Example body:

```text
speed=260&steer=0
```

### `POST /api/mode`

Set the operation mode (Driving or Stationary).

- Request body: form-encoded `mode` parameter
  - `mode=stationary` — stationary performance mode (drive locked, dome/sounds active)
  - `mode=driving` — full movement control with tank-style steering
- Success response:

```json
{"ok":true}
```

- Error response (invalid mode):

```json
{"ok":false,"error":"invalid mode - use 'stationary' or 'driving'"}
```

Behavior:

- sets `robotState.stationary` to true or false
- logged with `[WebServer] [WEB] Mode set to stationary/driving`

Example body:

```text
mode=stationary
```

### Audio endpoints

Audio routes are implemented in `src/web/api_audio.cpp` and are backend-aware.
Non-CHIRP backends keep the flat numbered-track flow; CHIRP adds catalog and
banked playback routes when `AUDIO_CAP_CATALOG` is present.

#### `GET /api/audio`

Return live audio module status and capability flags.

Response includes driver identity and backend capability bits used by the Sound page
to gate status polling, current-track display, and CHIRP catalog UI.

#### `POST /api/audio`

Structured command endpoint.

- `action=play&track=N` (`1..65535`)
- `action=stop`
- `action=volume&level=N` (`0..30`)
- `action=dollar&cmd=$...` (raw MarcDuino `$` command, max 9 chars)

#### `POST /api/audio/query`

Queue an on-demand module status query in AudioTask.
Used by backends that do not support safe background polling.

#### `GET /api/audio/tracks`

Return persisted sound mappings and tuning values: named tracks, system-event tracks,
category ranges, random range, random intervals, and volume.

When CHIRP catalog capability is active, response also includes:

- `chirp_bindings` - per-slot `{bank,page,index}` mappings for Named/System slots
- `chirp_category_bindings` - per-category `{bank,page}` mappings keyed by category `lo` key (`snd_cat_*_lo`)

#### `POST /api/audio/tracks`

Update one persisted track/tuning key.

Form fields:
- `key` (track key)
- `track` (value; range depends on key class)
- optional CHIRP mapping fields: `bank` (`1..6`) + `page` (`A..Z`) for CHIRP-bindable named/system keys

Behavior:
- writes canonical numeric `snd_*` key/value for all backends
- for CHIRP-capable named/system keys, also writes/clears packed `chr_*` binding metadata
- if `bank`/`page` are omitted, any existing CHIRP slot binding for that key is cleared
- updates runtime state only after NVS persistence succeeds (failed CHIRP-binding write rolls back the track write)

CHIRP binding consumption:
- named/system slot playback (`$S/$F/.../$B`, system events, Sound-page slot tests)
  prefers valid `chr_*` mapping and falls back to numeric `snd_*` track
- random/category playback prefers category bank/page binding (`chr_cat_*`) when present and
  falls back to numeric playback (`snd_cat_*`, `snd_rand_*`) otherwise

#### `POST /api/audio/category-range`

Atomically update one category `lo/hi` pair to avoid partial two-request saves.
Validation requires either `0/0` (unset) or `1..999` with `lo <= hi`.

Form fields:
- required: `lo_key`, `hi_key`, `lo`, `hi`
- optional CHIRP binding fields: `bank` (`1..6`) + `page` (`A..Z`)

Behavior:
- writes the numeric category range (`snd_cat_*_lo`, `snd_cat_*_hi`)
- when `bank`+`page` are provided together, also writes category CHIRP binding (`chr_cat_*`)
- when `bank`/`page` are omitted, existing category CHIRP binding is preserved
- `bank` and `page` must be provided together; endpoint returns `404` on non-catalog backends
  if CHIRP binding fields are supplied
#### `GET /api/audio/mood-map` and `POST /api/audio/mood-map`

Get/set per-mood category-bitmask mapping (`quiet`, `mid`, `full`, `awakeplus`).

#### CHIRP-only catalog endpoints

These return `404` when the active backend does not expose catalog capability.

- `GET /api/audio/catalog`
  - Returns cached CHIRP manifest/list view: `ready`, `banks[]`, `entries[]`
  - Optional query param: `bank=1..6` for server-side filtering
- `POST /api/audio/catalog/refresh`
  - Queues asynchronous catalog rebuild (`GMAN` + `GNME`) in AudioTask
- `POST /api/audio/play-banked`
  - Plays explicit CHIRP tuple `{bank,page,index}` via `PLAY:index,bank,page`


### `GET /api/config`

Return the current persisted web-configurable settings loaded into runtime.

- Request body: none
- Success response shape:

```json
{
  "speedLimitMax": 600,
  "webDriveTimeoutMs": 500,
  "ch8ModeLock": false,
  "rcInputMode": "dual_sbus",
  "enableArm1": true,
  "enableArm2": true,
  "enableAux1": false,
  "enableAux2": false,
  "enableAux3": false,
  "enableDome": true,
  "enableRcCh1": true,
  "enableRcCh2": true,
  "enableRcCh3": false,
  "enableRcCh4": false,
  "enableRcCh5": false,
  "enableRcCh6": false,
  "enableS1Hoverboard": true,
  "enableS2Sound": true,
  "enableS3DomeCtrl": true,
  "domeNeutralUs": 1500,
  "domeMinPulseUs": 1000,
  "domeMaxPulseUs": 2000,
  "domeSpeedLimitPct": 100,
  "rcPwmDriveSpeed": "pwm:1:1000:1500:2000:0:0",
  "rcPwmDriveSteer": "pwm:2:1000:1500:2000:0:0",
  "rcPwmDriveLimit": "none:0:1000:1500:2000:0:0",
  "rcPwmDomeSpeed": "pwm:3:1000:1500:2000:0:0",
  "rcPwmArm1": "pwm:4:1000:1500:2000:0:0",
  "rcPwmArm2": "pwm:5:1000:1500:2000:0:0",
  "rcPwmSound": "pwm:6:1000:1500:2000:0:0",
  "rcSbusDriveSpeed": "sbus1:1:172:992:1811:0:0",
  "rcSbusDriveSteer": "sbus1:2:172:992:1811:0:0",
  "rcSbusDriveLimit": "sbus1:8:172:992:1811:0:0",
  "rcSbusDomeSpeed": "sbus2:1:172:992:1811:0:0",
  "rcSbusArm1": "sbus2:2:172:992:1811:0:0",
  "rcSbusArm2": "sbus2:3:172:992:1811:0:0",
  "rcSbusSound": "none:0:1000:1500:2000:0:0"
}
```

### `POST /api/config`

Update the current web-configurable settings and persist them to NVS.

- Request body: form-encoded
- Supported fields:
  - `speedLimitMax` (`0..600`)
  - `webDriveTimeoutMs` (`100..5000`)
  - `ch8ModeLock` (`true`/`false` or `1`/`0`)
  - `rcInputMode` (`standard_pwm`, `single_sbus`, `dual_sbus`)
    - `dual_sbus` requires `enableS3DomeCtrl=false`
    - in `single_sbus`, setting `rc.sbus.recvCh2=true` selects SBUS2 on GPIO 13 (the same receiver input used as receiver #2 in `dual_sbus`)
  - `enableArm1`, `enableArm2`, `enableAux1`, `enableAux2`, `enableAux3`, `enableDome` (`true`/`false` or `1`/`0`)
  - `enableRcCh1`, `enableRcCh2`, `enableRcCh3`, `enableRcCh4`, `enableRcCh5`, `enableRcCh6` (`true`/`false` or `1`/`0`)
  - `enableS1Hoverboard`, `enableS2Sound`, `enableS3DomeCtrl` (`true`/`false` or `1`/`0`)
  - `domeNeutralUs`, `domeMinPulseUs`, `domeMaxPulseUs` (`1000..2000`)
  - `domeSpeedLimitPct` (`0..100`)
  - `rcPwmDriveSpeed`, `rcPwmDriveSteer`, `rcPwmDriveLimit`, `rcPwmDomeSpeed`, `rcPwmArm1`, `rcPwmArm2`, `rcPwmSound`
  - `rcSbusDriveSpeed`, `rcSbusDriveSteer`, `rcSbusDriveLimit`, `rcSbusDomeSpeed`, `rcSbusArm1`, `rcSbusArm2`, `rcSbusSound`
  - `rc.sbus.recvCh2` (`true`/`false` or `1`/`0`)

- Success response: same shape as `GET /api/config`

RC binding fields use the persisted format
`source:channel:min:center:max:deadband:reverse`, for example
`sbus1:1:172:992:1811:0:0` or `pwm:3:1000:1500:2000:0:0`.

- `source` is one of `none`, `pwm`, `sbus1`, or `sbus2`
- SBUS channels `17` and `18` are valid digital trigger channels when a binding targets
  SBUS input
- `rcPwm*` bindings are the persisted profile used by `standard_pwm`
- `rcSbus*` bindings are the persisted profile used by both `single_sbus` and `dual_sbus`

`rc.sbus.recvCh2` (boolean) — When `rc.inputMode` is `single_sbus`, selects which
physical receiver is used: `false` = SBUS1 (GPIO 15), `true` = SBUS2 (GPIO 13).
Changing this value does not modify channel mapping assignments. Default: `false`.


### `GET /api/rc`

Return the live RC diagnostics snapshot used by the Setup-page RC Mapping surface.

- Request body: none
- Success response shape:

```json
{
  "mode": "dual_sbus",
  "updatedMs": 123456,
  "sources": {
    "sbus1": {"enabled": true, "linked": true, "ageMs": 12, "lostFrames": 0, "failsafe": false},
    "sbus2": {"enabled": true, "linked": true, "ageMs": 18, "lostFrames": 1, "failsafe": false},
    "pwm": {"enabled": false, "linked": false, "ageMs": 0, "lostFrames": 0, "failsafe": false}
  },
  "channels": [
    {
      "id": 1,
      "name": "driveSpeed",
      "type": "analog",
      "activeSource": "sbus1",
      "bindingChannel": 1,
      "raw": 1010,
      "rawUs": 1512,
      "normalized": 0.021,
      "mapped": 0.021,
      "inDeadband": false,
      "reverse": false
    },
    {
      "id": 2,
      "name": "driveSteer",
      "type": "analog",
      "activeSource": "sbus1",
      "bindingChannel": 2,
      "raw": 992,
      "rawUs": 1500,
      "normalized": 0.000,
      "mapped": 0.000,
      "inDeadband": true,
      "reverse": false
    }
  ],
  "digital": {
    "arm1": {"activeSource": "sbus2", "bindingChannel": 17, "pressed": true},
    "arm2": {"activeSource": "sbus2", "bindingChannel": 18, "pressed": false}
  },
  "mappingProfile": {
    "version": 1,
    "channels": {
      "driveSpeed": {"source": "sbus1", "channel": 1, "min": 172, "center": 992, "max": 1811, "deadband": 0, "reverse": false},
      "driveSteer": {"source": "sbus1", "channel": 2, "min": 172, "center": 992, "max": 1811, "deadband": 0, "reverse": false},
      "driveLimit": {"source": "sbus1", "channel": 8, "min": 172, "center": 992, "max": 1811, "deadband": 0, "reverse": false},
      "domeSpeed": {"source": "sbus2", "channel": 1, "min": 172, "center": 992, "max": 1811, "deadband": 0, "reverse": false},
      "arm1": {"source": "sbus2", "channel": 2, "min": 172, "center": 992, "max": 1811, "deadband": 0, "reverse": false},
      "arm2": {"source": "sbus2", "channel": 3, "min": 172, "center": 992, "max": 1811, "deadband": 0, "reverse": false},
      "sound": {"source": "none", "channel": 0, "min": 1000, "center": 1500, "max": 2000, "deadband": 0, "reverse": false}
    }
  }
}
```

Behavior:

- `mode` reflects the active runtime `rcInputMode`
- `sources` reports link health for `pwm`, `sbus1`, and `sbus2`
- `channels` are action-oriented analog rows (`driveSpeed`, `driveSteer`, `driveLimit`,
  `domeSpeed`, `arm1`, `arm2`, `sound`) for bindings that currently resolve to analog
  input channels
- `digital` contains action-oriented trigger rows when a binding resolves to SBUS
  channel `17` or `18`
- `mappingProfile` mirrors the persisted binding/calibration profile currently active at
  runtime

Example body:

```text
speedLimitMax=400&webDriveTimeoutMs=750&ch8ModeLock=true&enableArm1=true&enableDome=true&domeNeutralUs=1500
```

### `POST /api/servo`

Control servo arms and auxiliary outputs (ARM1, ARM2, AUX1–AUX3).

- Request body: form-encoded `arm` and `action`
- Supported arms: `arm1`, `arm2`, `aux1`, `aux2`, `aux3`
- Supported actions: `open`, `close`, `stop`
- Success response:

```json
{"ok":true}
```

Behavior:

- routes the command through `SRC_WEB_API`
- rejects commands if the target subsystem is disabled via config
- rejects commands if the servo command queue is full (HTTP 503)
- `open` and `close` trigger configured sequences; `stop` sends neutral pulse

Example body:

```text
arm=arm1&action=open
```

### `POST /api/dome`

Control dome motor rotation.

- Request body: form-encoded `speed`
- Speed range: `-1.0` (full reverse) to `+1.0` (full forward); `0` = stop
- Success response:

```json
{"ok":true}
```

Behavior:

- routes the command through `SRC_WEB_API`
- rejects commands if dome subsystem is disabled via config
- rejects commands if the dome command queue is full (HTTP 503)
- speed is clamped to the configured dome speed limit

Example body:

```text
speed=0.5
```

### `GET /api/wifi`

Return the current access point and WiFi client network status.

- Request body: none
- Success response shape:

```json
{
  "apSsid": "protoArtoo",
  "apIp": "192.168.4.1",
  "staEnabled": true,
  "staConnected": true,
  "staIp": "10.0.0.22"
}
```

### `GET /api/serial`

Return the current controller transport map and live serial/transport status.

- Request body: none

### `GET /api/health`

Return a compact controller health snapshot for the dashboard.

- Request body: none

### `GET /api/logs`

Return the recent in-memory controller log buffer as plain text.

- Request body: none
- Response content type: `text/plain`

### `POST /api/manual-command`

Execute a supported manual controller command.

- Request body: form-encoded `command`
- Supported commands:
  - `estop` — latch emergency stop
  - `clear_estop` — clear latched emergency stop
  - `enable_web_control` — enable explicit browser-control mode
  - `disable_web_control` — disable explicit browser-control mode
  - `reboot` — request controller reboot
  - Marcduino commands (prefixed with `:`, `$`, `#`, `@`, `*`, `%`, `&`, `!`) — routed through
    the shared body-side parser used by the manual command box
    - `:`, `$`, and `#` are body-local in the current Phase 3 slice
    - `@`, `*`, `%`, `&`, and `!` are recognized but not handled on the body controller
    - `:OP01` — open panel 1
    - `:CL01` — close panel 1
    - `:SE30` — play sequence 30
    - `$87` — accepted as a body-local audio stub and logged for traceability
    - `#SQ01` — accepted as a reserved body-local config command and logged for traceability
    - (see Marcduino protocol documentation for full command set)

Example body:

```text
command=estop
```

Or for Marcduino commands:

```text
command=:OP01
```

### `POST /api/reboot`

Request a controller reboot.

- Request body: none
- Success response:

```json
{"ok":true}
```

### `POST /upload/firmware`

Upload a replacement firmware binary over HTTP.

- Request body: multipart form data
- Expected field: `firmware` (`.bin` application image)
- Success response:

```json
{"ok":true}
```

Notes:

- this updates the firmware image only
- web asset changes still require a LittleFS upload
- a successful upload schedules a reboot

### `GET /api/status`

Return the current web-control state snapshot.

- Request body: none
- Success response shape (fixed fields):

```json
{
  "estop": false,
  "webControlEnabled": false,
  "sbusSignalLost": false,
  "sbusHwFailsafe": false,
  "webDriveExpired": false,
  "failsafeSource": 0,
  "driveSpeed": 0,
  "driveSteer": 0,
  "speedLimitScale": 1.0,
  "stationary": false,
  "failsafeCount": 0,
  "failsafeTriggerMs": 0,
  "failsafeZeroMs": 0,
  "failsafeTriggerToZeroMs": 0,
  "failsafeWatchdogMs": 0,
  "failsafeTriggerSource": 0,
  "uptimeMs": 27790,
  "firmwareVersion": "v0.1.0-phase3-dev",
  "fsVersion": "fs-v1.0.0-alpha.1",
  "heapFree": 173152,
  "heapMin": 150932,
  "wifiRssi": -70,
  "wifiConnected": true,
  "wifiClientConnected": true,
  "littleFsReady": true
}
```

Conditional component keys (present only if enabled):

- `arm1`, `arm2`, `aux1`, `aux2`, `aux3`, `dome`
- `rcCh1`-`rcCh6`
- `s1Hoverboard`, `s2Sound`, `s3DomeCtrl`

Each enabled component key returns an object with `state` and `detail`, not a boolean.

Example response with arm1, RC routing, and dome enabled:

```json
{
  "estop": false,
  "webControlEnabled": false,
  "sbusSignalLost": false,
  "sbusHwFailsafe": false,
  "webDriveExpired": false,
  "failsafeSource": 0,
  "driveSpeed": 0,
  "driveSteer": 0,
  "speedLimitScale": 1.0,
  "stationary": false,
  "failsafeCount": 0,
  "failsafeTriggerMs": 0,
  "failsafeZeroMs": 0,
  "failsafeTriggerToZeroMs": 0,
  "failsafeWatchdogMs": 0,
  "failsafeTriggerSource": 0,
  "uptimeMs": 27790,
  "firmwareVersion": "v0.1.0-phase3-dev",
  "fsVersion": "fs-v1.0.0-alpha.1",
  "heapFree": 173152,
  "heapMin": 150932,
  "wifiRssi": -70,
  "wifiConnected": true,
  "wifiClientConnected": true,
  "littleFsReady": true,
  "arm1": {"state": "ready", "detail": "Target 1500 us"},
  "rcCh1": {"state": "active", "detail": "Drive SBUS active, last 14 ms ago, lost frames 0"},
  "rcCh3": {"state": "standby", "detail": "PWM-capable input available when standard_pwm mode is selected"},
  "dome": {"state": "idle", "detail": "Target 0%"}
}
```

Notes:

- `wifiConnected` is true when WiFi control surface is available (AP active or STA connected)
- `wifiClientConnected` is true when at least one station is attached to the device soft AP
- `failsafeSource` is the numeric `FailsafeSource` enum value
- `failsafeTriggerMs`, `failsafeZeroMs`, `failsafeTriggerToZeroMs`, `failsafeWatchdogMs`, and `failsafeTriggerSource` provide timing evidence for failsafe trigger-to-zero behavior in hardware validation
- `speedLimitScale` reflects CH8 speed limiting from the drive SBUS receiver
- `stationary` indicates CH8 mode-lock is holding the drive at zero
- `uptimeMs`, `firmwareVersion`, and `fsVersion` support the shared device-info status block in the UI
- `heapFree`, `heapMin`, `wifiRssi`, `wifiConnected`, `wifiClientConnected`, and `littleFsReady` support dashboard health/status surfaces
- Disabled components are absent from the response, not emitted as false placeholders

### `GET /api/validation`

Return a compact validation-focused snapshot that consolidates drive/failsafe, dome-link, audio, and RC source health for hardware-closure checks.

- Request body: none
- Success response shape:

```json
{
  "updatedMs": 27790,
  "drive": {
    "estop": false,
    "webDriveExpired": false,
    "sbusSignalLost": false,
    "sbusHwFailsafe": false,
    "failsafeSource": 0,
    "failsafeCount": 2,
    "triggerMs": 20123,
    "zeroMs": 20138,
    "triggerToZeroMs": 15,
    "watchdogMs": 20123,
    "triggerSource": 1
  },
  "domeLink": {
    "state": "connected",
    "hbTx": 52,
    "hbRx": 49,
    "lastRxMs": 110
  },
  "audio": {
    "enabled": true,
    "active": true,
    "activeMood": 14,
    "randomMin": 1,
    "randomMax": 120,
    "intervalQuietS": 0,
    "intervalMidS": 30,
    "intervalFullS": 20,
    "intervalAwakeS": 10
  },
  "rc": {
    "mode": "dual_sbus",
    "timeoutMs": 200,
    "sources": {
      "sbus1": {"enabled": true, "linked": true, "signalLost": false, "failsafe": false, "ageMs": 12},
      "sbus2": {"enabled": true, "linked": false, "signalLost": true, "failsafe": false, "ageMs": 310},
      "pwm": {"enabled": false, "linked": false, "signalLost": false, "failsafe": false, "ageMs": 0}
    }
  }
}
```

Notes:
- `drive.failsafeSource` and `drive.triggerSource` are numeric `FailsafeSource` enum values.
- `domeLink.state` is one of `disabled`, `not_seen`, `connected`, or `lost`.
- `rc.sources.*.linked` is the source-ready indicator for control eligibility; `signalLost` and `failsafe` provide source-specific fault detail.

### `GET /api/events`

Open a server-sent events stream for live status updates.

- Request header: `Accept: text/event-stream`
- Event names:
  - `status` — same JSON shape as `GET /api/status`, emitted at 1 Hz
  - `rc` — same JSON shape as `GET /api/rc`, emitted from the existing event task at
    approximately 10 Hz
- On initial connect, the stream sends one `status` event and one `rc` event immediately

## Error handling

Unknown routes return:

```json
{"ok":false,"error":"not found"}
```

## Phase boundary

The web stack now includes Phase 2 status streaming, browser drive, persisted config updates via NVS,
Phase 3 RC diagnostics/mapping surfaces, dedicated WiFi/Firmware/Serial pages, dashboard health/log/manual-command surfaces, and OTA/reboot support.
