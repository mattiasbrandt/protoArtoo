# REST API Reference

Phase 1 exposes a minimal safety-focused HTTP API over the `protoArtoo` access
point.

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

### `GET /api/status`

Return a Phase 1 state snapshot.

- Request body: none
- Success response shape:

```json
{
  "estop": false,
  "sbusSignalLost": false,
  "sbusHwFailsafe": false,
  "webDriveExpired": false,
  "failsafeSource": 0,
  "driveSpeed": 0,
  "driveSteer": 0,
  "speedLimitScale": 1.0,
  "stationary": false,
  "failsafeCount": 0
}
```

Notes:

- `failsafeSource` is the numeric `FailsafeSource` enum value
- `speedLimitScale` reflects CH8 speed limiting from the drive SBUS receiver
- `stationary` indicates CH8 mode-lock is holding the drive at zero

## Error handling

Unknown routes return:

```json
{"ok":false,"error":"not found"}
```

## Phase boundary

Phase 1 does not yet expose drive, config, OTA, audio, or SSE endpoints. Those
arrive in later phases.
