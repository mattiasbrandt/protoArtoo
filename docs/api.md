# REST API Reference

The current web stack exposes a safety-focused HTTP and SSE control surface over
the `protoArtoo` access point.

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

### `GET /api/config`

Return the current persisted web-configurable settings loaded into runtime.

- Request body: none
- Success response shape:

```json
{
  "speedLimitMax": 600,
  "webDriveTimeoutMs": 500,
  "ch8ModeLock": false
}
```

### `POST /api/config`

Update the current web-configurable settings and persist them to NVS.

- Request body: form-encoded
- Supported fields:
  - `speedLimitMax` (`0..600`)
  - `webDriveTimeoutMs` (`100..5000`)
  - `ch8ModeLock` (`true`/`false` or `1`/`0`)
- Success response: same shape as `GET /api/config`

Example body:

```text
speedLimitMax=400&webDriveTimeoutMs=750&ch8ModeLock=true
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
  - `estop`
  - `clear_estop`
  - `enable_web_control`
  - `disable_web_control`
  - `reboot`

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
- Success response shape:

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
  "uptimeMs": 27790,
  "firmwareVersion": "v0.1.0-phase2-dev",
  "heapFree": 173152,
  "heapMin": 150932,
  "wifiRssi": -70,
  "wifiClientConnected": true,
  "littleFsReady": true
}
```

Notes:

- `failsafeSource` is the numeric `FailsafeSource` enum value
- `speedLimitScale` reflects CH8 speed limiting from the drive SBUS receiver
- `stationary` indicates CH8 mode-lock is holding the drive at zero
- `uptimeMs` and `firmwareVersion` support the shared device-info status block in the UI
- `heapFree`, `heapMin`, `wifiRssi`, `wifiClientConnected`, and `littleFsReady` support dashboard health/status surfaces

### `GET /api/events`

Open a server-sent events stream for live status updates.

- Request header: `Accept: text/event-stream`
- Event name: `status`
- Payload: same JSON shape as `GET /api/status`

## Error handling

Unknown routes return:

```json
{"ok":false,"error":"not found"}
```

## Phase boundary

The web stack now includes Phase 2 status streaming, browser drive, persisted config updates via NVS,
dedicated WiFi/Firmware/Serial pages, dashboard health/log/manual-command surfaces, and OTA/reboot support.
