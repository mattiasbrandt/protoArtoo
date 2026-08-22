# REST API Reference

This document describes the currently exposed HTTP and SSE API in protoArtoo,
including request shape, accepted parameters, and observed response contracts.

**Route Coverage (for drift detection):** 64 routes total, derived from
`src/web/web_seam_routes.cpp`, `src/web/web_request_psychic.cpp` (SSE), and
upload handlers. Breakdown: 61 core API routes + 2 multipart upload routes +
1 SSE stream. All are documented here or listed under "Internal Routes".

## Table of Contents

- [Request/Response Conventions](#requestresponse-conventions)
- [Error Contract](#error-contract)
- [Identity](#identity)
- [Safety and Drive](#safety-and-drive)
- [Servo and AUX Outputs](#servo-and-aux-outputs)
- [Audio and Mood](#audio-and-mood)
- [Learned Sequences](#learned-sequences)
- [Configuration and RC](#configuration-and-rc)
- [Action Registry](#action-registry)
- [Status and Validation](#status-and-validation)
- [System and OTA](#system-and-ota)
- [SSE Events](#sse-events)
- [Profiling (Build-Conditional)](#profiling-build-conditional)
- [Internal Routes](#internal-routes)

## Request/Response Conventions

- Most POST endpoints read body fields as form parameters (`req->getParam(..., true)`).
- Endpoints that support JSON body read `plain` request payload and parse JSON.
- Success payload is usually `{"ok":true}` unless the endpoint returns a full JSON object.
- Errors are returned as JSON with `ok:false` + `error` in most routes.
- Base URL examples below use `http://artoo.local`.
- The controller hostname is lowercase `artoo`; use `artoo.local` on STA networks.
- If mDNS is unavailable on your host network, use the current device IP from `GET /api/wifi` (`staIp`) or your network lease table.

## Error Contract

All error responses use a unified JSON shape:

```json
{"ok":false,"error":"<error-token>"}
```

An error response may optionally include:
- `"hint"`: a string suggesting recovery action (e.g., `"POST /api/wake"`)
- `"field"`: the name of a request parameter that failed validation (e.g., `"speed"`)

The HTTP status code (4xx, 5xx) indicates the error class:
- `400` — invalid input (missing field, bad format, out of range)
- `409` — conflict with current state (e.g., drive blocked by estop)
- `423` — transient resource unavailable (e.g., sleeping, resource in use)
- `503` — service unavailable (queue full, hardware link down)
- `500` — server error

Example with hint and field:

```json
{"ok":false,"error":"missing password","hint":"POST /api/wifi to list networks","field":"password"}
```

### Unknown routes

A request that matches no endpoint and no static file gets the same shape, with
`content-type: application/json`:

```
GET /api/nonexistent-route
  ->  404   {"ok":false,"error":"not found"}
```

This holds for any method and any path, not only `/api/*`, so a client never has
to parse a non-JSON body to find out an endpoint is gone. Files that exist are
served normally; only requests that had no answer either way reach this reply.

## Identity

### GET /api/identity

Returns the droid's cosmetic name and mDNS hostname preference.

- Success: `200` JSON with `droidName` and `mdnsUseName`
- Errors: `500` on response build overflow

#### Example request

```bash
curl -s http://artoo.local/api/identity
```

#### Example response

```json
{"droidName":"artoo","mdnsUseName":true}
```

### POST /api/identity

Persists a new cosmetic droid name and/or mDNS hostname preference.

- Body fields:
  - `droidName`: required; must be 1–32 lowercase letters, numbers, or hyphens (no spaces)
  - `mdnsUseName`: optional; `true`, `false`, `0`, or `1` (defaults to existing value)
- Success: `200` JSON with updated `droidName` and `mdnsUseName`
- Errors:
  - `400` `{"ok":false,"error":"droidName is required"}`
  - `400` `{"ok":false,"error":"droidName must be 1..32 lowercase letters, numbers, or hyphens; spaces are not allowed"}`
  - `400` `{"ok":false,"error":"mdnsUseName must be true/false or 1/0"}`
  - `500` `{"ok":false,"error":"failed to persist identity"}`

#### Example request

```bash
curl -s -X POST http://artoo.local/api/identity \
  -d 'droidName=r2d2&mdnsUseName=true'
```

#### Example response

```json
{"droidName":"r2d2","mdnsUseName":true}
```

## Safety and Drive

### POST /api/estop

Latches emergency stop.

- Body: none
- Success: `200` `{"ok":true}`

#### Example request

```bash
curl -s -X POST http://artoo.local/api/estop
```

#### Example response

```json
{"ok":true}
```

### POST /api/estop/clear

Clears latched emergency stop.

- Body: none
- Success: `200` `{"ok":true}`

#### Example request

```bash
curl -s -X POST http://artoo.local/api/estop/clear
```

#### Example response

```json
{"ok":true}
```

### POST /api/web-control/enable

Enables explicit web control mode.

- Body: none
- Success: `200` `{"ok":true}`

#### Example request

```bash
curl -s -X POST http://artoo.local/api/web-control/enable
```

#### Example response

```json
{"ok":true}
```

### POST /api/web-control/disable

Disables explicit web control mode and pushes a zero internal drive command.

- Body: none
- Success: `200` `{"ok":true}`

#### Example request

```bash
curl -s -X POST http://artoo.local/api/web-control/disable
```

#### Example response

```json
{"ok":true}
```

### POST /api/mode

Sets operation mode.

- Body fields:
- `mode`: `stationary` or `driving`
- Success: `200` `{"ok":true}`
- Errors:
- `400` `{"ok":false,"error":"missing mode parameter"}`
- `400` `{"ok":false,"error":"invalid mode - use 'stationary' or 'driving'"}`

#### Example request

```bash
curl -s -X POST http://artoo.local/api/mode \
  -d 'mode=stationary'
```

#### Example response

```json
{"ok":true}
```

### POST /api/drive/speed-preset

Applies persisted speed preset.

- Body fields:
- `preset`: `slow`, `normal`, `turbo`
- Success: `200` JSON with active preset + `speedLimitMax`
- Errors:
- `400` `{"ok":false,"error":"missing preset"}`
- `400` `{"ok":false,"error":"invalid preset - use slow, normal, or turbo"}`
- `500` `{"ok":false,"error":"failed to persist speed preset"}`
- `500` `{"ok":false,"error":"speed preset response overflow"}`

#### Example request

```bash
curl -s -X POST http://artoo.local/api/drive/speed-preset \
  -d 'preset=turbo'
```

#### Example response

```json
{"ok":true,"preset":"turbo","speedLimitMax":600}
```

### POST /api/drive

Sends browser drive command.

- Body fields:
- `speed`: integer
- `steer`: integer
- Behavior:
- clamps both values to `[-cfg_speedLimitMax, +cfg_speedLimitMax]`
- blocked when `estop`, `stationary`, or SBUS unhealthy without web-control enable
- Success: `200` `{"ok":true}`
- Errors:
- `400` `{"ok":false,"error":"missing speed or steer"}`
- `400` `{"ok":false,"error":"speed and steer must be integers"}`
- `409` `{"ok":false,"error":"drive blocked by safety state"}`

#### Example request

```bash
curl -s -X POST http://artoo.local/api/drive \
  -d 'speed=260&steer=0'
```

#### Example response

```json
{"ok":true}
```

### POST /api/dome

Queues dome speed command.

- Body fields:
- `speed`: float in `-1.0..1.0`
- Success: `200` `{"ok":true}`
- Errors:
- `400` `{"ok":false,"error":"missing speed"}`
- `423` `{"error":"sleeping","hint":"POST /api/wake"}`
- `400` `{"ok":false,"error":"speed must be a float in range -1.0..1.0"}`
- `409` `{"ok":false,"error":"dome output is disabled"}`
- `503` `{"ok":false,"error":"dome command queue full"}`

#### Example request

```bash
curl -s -X POST http://artoo.local/api/dome \
  -d 'speed=0.5'
```

#### Example response

```json
{"ok":true}
```

### POST /api/dome/cmd

Sends a raw dome command or a factory sequence (DM:* name).

The command is routed based on its prefix:
- Starts with `DM:` — queued as a Learned Sequence or factory sequence (same as `POST /api/seq/test`)
- Otherwise — queued as a raw command to the dome link

- Body fields:
- `cmd`: required; raw string or `DM:*` factory sequence name; max 127 characters
- Success: `200` `{"ok":true}`
- Errors:
- `400` `{"ok":false,"error":"missing cmd parameter"}`
- `400` `{"ok":false,"error":"cmd too long (max 127)"}`
- `503` `{"ok":false,"error":"sequence queue full"}` (when `cmd` starts with `DM:`)
- `503` `{"ok":false,"error":"dome TX queue full or link not ready"}` (raw command)

#### Example request (factory sequence)

```bash
curl -s -X POST http://artoo.local/api/dome/cmd \
  -d 'cmd=DM:ROCKMARCH'
```

#### Example response

```json
{"ok":true}
```

#### Example request (raw dome command)

```bash
curl -s -X POST http://artoo.local/api/dome/cmd \
  -d 'cmd=$87'
```

#### Example response

```json
{"ok":true}
```

### GET /api/dome/layout

Fetches cached dome layout JSON from WiFi transport.

The layout is cached by the dome link task. This endpoint streams the cached bytes from a chunked response, so no per-request buffer allocation is needed.

If the cache is empty or transport is not WiFi, returns `503` and sets a flag for the background task to fetch on the next loop.

- Success: `200` chunked JSON with layout structure
- Errors:
- `503` `{"ok":false,"error":"dome layout unavailable (not WiFi)"}`
- `503` `{"ok":false,"error":"dome layout unavailable","retry":true}` (cache miss)

Response includes an `X-Dome-Layout-Age-Ms` header with the age of the cached layout in milliseconds.

#### Example request

```bash
curl -s http://artoo.local/api/dome/layout
```

#### Example response (abridged)

```json
{"servos":[{"channel":0,"id":"arm1","type":"gripper"}],"geometry":{}}
```

## Servo and AUX Outputs

### POST /api/servo

Queues servo command.

- Body fields:
- `arm`: `arm1|arm2|aux1|aux2|aux3|both`
- `action`: `open|close|stop|position`
- `positionUs`: required when `action=position`; range `500..2500`
- Success: `200` `{"ok":true}`
- Errors:
- `400` `{"ok":false,"error":"Missing arm or action parameter"}`
- `400` `{"ok":false,"error":"Invalid arm. Use: arm1, arm2, aux1, aux2, aux3, or both"}`
- `400` `{"ok":false,"error":"Invalid action. Use: open, close, stop, or position"}`
- `400` missing/invalid `positionUs`
- `503` `{"ok":false,"error":"Servo command queue full"}`

#### Example request (open)

```bash
curl -s -X POST http://artoo.local/api/servo \
  -d 'arm=arm1&action=open'
```

#### Example response

```json
{"ok":true}
```

#### Example request (position)

```bash
curl -s -X POST http://artoo.local/api/servo \
  -d 'arm=aux1&action=position&positionUs=1600'
```

#### Example response

```json
{"ok":true}
```

### POST /api/aux-led/color

Sets AUX LED color.

- Body formats:
- Form: `r`, `g`, `b` (0..255)
- JSON: `{"r":<0..255>,"g":<0..255>,"b":<0..255>}`
- Success: `200` AUX LED state JSON (`pin`, `r`, `g`, `b`, `effect`)
- Errors:
- `400` `{"ok":false,"error":"payload must contain r,g,b integers 0..255"}`
- `503` `{"ok":false,"error":"aux LED unavailable"}`
- `503` `{"ok":false,"error":"aux LED command queue full"}`

#### Example request (form)

```bash
curl -s -X POST http://artoo.local/api/aux-led/color \
  -d 'r=255&g=80&b=10'
```

#### Example response

```json
{"ok":true,"auxLed":{"pin":1,"r":255,"g":80,"b":10,"effect":"solid"}}
```

#### Example request (json)

```bash
curl -s -X POST http://artoo.local/api/aux-led/color \
  -H 'Content-Type: application/json' \
  -d '{"r":0,"g":0,"b":255}'
```

#### Example response

```json
{"ok":true,"auxLed":{"pin":1,"r":0,"g":0,"b":255,"effect":"solid"}}
```

### POST /api/aux-led/effect

Sets AUX LED effect.

- Body formats:
- Form: `effect`
- JSON: `{"effect":"off|solid|blink|pulse"}`
- Success: `200` AUX LED state JSON
- Errors:
- `400` `{"ok":false,"error":"effect must be one of off|solid|blink|pulse"}`
- `503` `{"ok":false,"error":"aux LED unavailable"}`
- `503` `{"ok":false,"error":"aux LED command queue full"}`

#### Example request

```bash
curl -s -X POST http://artoo.local/api/aux-led/effect \
  -d 'effect=blink'
```

#### Example response

```json
{"ok":true,"auxLed":{"pin":1,"r":0,"g":0,"b":255,"effect":"blink"}}
```

## Audio and Mood

### GET /api/audio

Returns live audio module status.

- Success: `200` JSON includes backend/driver and runtime status fields

#### Example request

```bash
curl -s http://artoo.local/api/audio
```

#### Example response

```json
{"driver":"dy-sv5w","capabilities":3,"link_ok":true,"active":false,"play_state":"stop","device":"FLASH","total_tracks":999,"current_track":0}
```

### POST /api/audio

Action endpoint.

- Body field: `action`
- `action=play`
- requires `track` in `1..65535`
- sleep mode blocked (`423`)
- `action=stop`
- no extra field
- `action=volume`
- requires `level` in `0..30`
- persists to NVS
- `action=dollar`
- requires `cmd` starting with `$`, max length 9 chars
- sleep mode blocked (`423`)
- Success: `200` `{"ok":true}`
- Errors:
- `400` missing/invalid action or missing required action field
- `400` unknown action error (`play|stop|volume|dollar` are accepted)
- `503` `{"ok":false,"error":"audio command queue full"}`
- `500` `{"ok":false,"error":"volume applied but NVS save failed"}`

#### Example request (play)

```bash
curl -s -X POST http://artoo.local/api/audio \
  -d 'action=play&track=42'
```

#### Example response

```json
{"ok":true}
```

#### Example request (stop)

```bash
curl -s -X POST http://artoo.local/api/audio \
  -d 'action=stop'
```

#### Example response

```json
{"ok":true}
```

#### Example request (volume)

```bash
curl -s -X POST http://artoo.local/api/audio \
  -d 'action=volume&level=18'
```

#### Example response

```json
{"ok":true}
```

#### Example request (dollar)

```bash
curl -s -X POST http://artoo.local/api/audio \
  -d 'action=dollar&cmd=$87'
```

#### Example response

```json
{"ok":true}
```

### POST /api/audio/query

Queues audio-module status query.

- Success: `200` `{"ok":true}`
- Error: `503` `{"ok":false,"error":"audio command queue full"}`

#### Example request

```bash
curl -s -X POST http://artoo.local/api/audio/query
```

#### Example response

```json
{"ok":true}
```

### GET /api/audio/tracks

Returns persisted audio mapping/tuning JSON.

- Success: `200` JSON map of named tracks, category bounds, intervals, volume, backend-specific fields
- Errors: `500` on response build overflow

#### Example request

```bash
curl -s http://artoo.local/api/audio/tracks
```

#### Example response (abridged)

```json
{"scream":12,"faint":15,"snd_cat_hum_lo":1,"snd_cat_hum_hi":120,"snd_int_mid":30,"audioVolume":18}
```

### POST /api/audio/tracks

Updates one persisted key.

- Body fields:
- `key`: track/tuning key
- `track`: non-negative integer
- optional CHIRP fields: `bank` (`1..6`) and `page` (`A..Z`) together
- Validation highlights:
- interval keys: `0..3600`
- normal non-banked track keys: `0..999` (some keys allow `0`, others require `1..999`)
- CHIRP banked index: `1..65535`
- Success: `200` `{"ok":true}`
- Errors include:
- missing key/track, unknown key
- invalid track range/type
- invalid CHIRP arguments
- `404` when CHIRP mapping requested on non-catalog backend
- `500` on NVS write failure

#### Example request (non-CHIRP)

```bash
curl -s -X POST http://artoo.local/api/audio/tracks \
  -d 'key=scream&track=23'
```

#### Example response

```json
{"ok":true}
```

#### Example request (CHIRP mapping)

```bash
curl -s -X POST http://artoo.local/api/audio/tracks \
  -d 'key=scream&track=42&bank=2&page=B'
```

#### Example response

```json
{"ok":true}
```

### POST /api/audio/category-range

Atomically updates one category low/high pair.

- Body fields (required):
- `lo_key`, `hi_key`, `lo`, `hi`
- Optional CHIRP binding fields:
- `bank` (`1..6`) + `page` (`A..Z`) together, or `clear_binding=true`
- Range validation:
- values must be integers in `0..999`
- allowed states: `0/0` or both in `1..999` with `lo <= hi`
- Success: `200` `{"ok":true}`
- Errors include:
- missing fields, invalid category key pair
- invalid range values
- invalid CHIRP binding arguments
- `404` when CHIRP binding operation requested on non-catalog backend
- `500` on NVS write failure

#### Example request

```bash
curl -s -X POST http://artoo.local/api/audio/category-range \
  -d 'lo_key=snd_cat_hum_lo&hi_key=snd_cat_hum_hi&lo=1&hi=120'
```

#### Example response

```json
{"ok":true}
```

### GET /api/audio/mood-map

Returns mood category masks.

- Success: `200` JSON with `quiet`, `mid`, `full`, `awakeplus`
- Error: `500` overflow while building response

#### Example request

```bash
curl -s http://artoo.local/api/audio/mood-map
```

#### Example response

```json
{"quiet":1,"mid":3,"full":15,"awakeplus":15}
```

### POST /api/audio/mood-map

Sets mood category masks.

- Supported input:
- Form fields: `quiet`, `mid`, `full`, `awakeplus`
- JSON body with same keys
- Value range: each mask `0..4095`
- Success: `200` `{"ok":true}`
- Errors include:
- invalid/missing fields
- JSON parse failure
- invalid range/type
- `500` on NVS write failure

#### Example request (form)

```bash
curl -s -X POST http://artoo.local/api/audio/mood-map \
  -d 'quiet=1&mid=3&full=15&awakeplus=15'
```

#### Example response

```json
{"ok":true}
```

#### Example request (json)

```bash
curl -s -X POST http://artoo.local/api/audio/mood-map \
  -H 'Content-Type: application/json' \
  -d '{"quiet":1,"mid":3,"full":15,"awakeplus":15}'
```

#### Example response

```json
{"ok":true}
```

### POST /api/mood

Applies runtime mood.

- Body field: `mood` in `{10, 11, 13, 14}`
- Success: `200` `{"ok":true}`
- Errors:
- `423` sleeping
- `400` missing/invalid mood

#### Example request

```bash
curl -s -X POST http://artoo.local/api/mood \
  -d 'mood=14'
```

#### Example response

```json
{"ok":true}
```

### GET /api/audio/catalog

Returns cached CHIRP catalog.

- Optional query param: `bank=1..6`
- Success: `200` JSON with `ready`, `banks[]`, `entries[]`
- Errors:
- `404` `{"ok":false,"error":"catalog unsupported by active backend"}`
- `400` invalid `bank`

#### Example request

```bash
curl -s 'http://artoo.local/api/audio/catalog?bank=2'
```

#### Example response (abridged)

```json
{"ready":true,"banks":[{"bank":2,"page":"A","dir":"BANK2_A","count":20}],"entries":[{"bank":2,"page":"A","index":1,"name":"TRACK_001"}]}
```

### POST /api/audio/catalog/refresh

Queues catalog refresh.

- Success: `200` `{"ok":true}`
- Errors:
- `404` unsupported backend
- `503` queue full

#### Example request

```bash
curl -s -X POST http://artoo.local/api/audio/catalog/refresh
```

#### Example response

```json
{"ok":true}
```

### POST /api/audio/play-banked

Plays CHIRP tuple.

- Body fields:
- `index` in `1..65535`
- `bank` in `1..6`
- `page` single letter `A..Z`
- Success: `200` `{"ok":true}`
- Errors:
- `423` sleeping
- `404` unsupported backend
- `400` missing/invalid fields
- `503` queue full

#### Example request

```bash
curl -s -X POST http://artoo.local/api/audio/play-banked \
  -d 'index=14&bank=2&page=A'
```

#### Example response

```json
{"ok":true}
```

## Learned Sequences

Learned Sequences are named command sequences stored in the controller's persistent storage. The `seq` endpoints manage playback, storage, and metadata. Each sequence is identified by a `DM:*` name (factory sequences and Learned Sequences use the same namespace).

### GET /api/seq/list

Returns an index of all Learned Sequences stored in the controller.

- Success: `200` JSON array of sequence metadata objects
  - `name`: sequence identifier (e.g., `"DM:ROCKMARCH"`)
  - `toggleGroup`: toggle group assignment
  - `suppressMs`: suppression interval in milliseconds
  - `source`: where the sequence came from (`"web"`, `"chirp"`, etc.)
  - `modified`: ISO 8601 timestamp of last modification
  - `valid`: boolean indicating if the sequence is valid and runnable
  - `retrained`: boolean; true if this sequence shadows a factory sequence
- Errors: `500` on response overflow

#### Example request

```bash
curl -s http://artoo.local/api/seq/list
```

#### Example response (abridged)

```json
[
  {"name":"DM:ROCKMARCH","toggleGroup":"movement","suppressMs":1000,"source":"web","modified":"2026-01-15T10:30:00Z","valid":true,"retrained":false},
  {"name":"DM:SPINNY","toggleGroup":"movement","suppressMs":500,"source":"web","modified":"2026-01-14T14:22:00Z","valid":true,"retrained":true}
]
```

### GET /api/seq/builtins

Returns factory sequence metadata and optionally a full factory sequence JSON.

- Query params:
  - `name=<sequence-name>`: optional; if provided, returns the full factory sequence JSON v1 for that name
- Success (list): `200` JSON array of factory sequence metadata (same shape as `/api/seq/list`)
- Success (single): `200` JSON v1 of a single factory sequence (full step data)
- Errors:
  - `404` `{"ok":false,"error":"factory sequence not found"}` (when fetching a single sequence by name)
  - `500` on response overflow

#### Example request (list all factory sequences)

```bash
curl -s http://artoo.local/api/seq/builtins
```

#### Example response (abridged)

```json
[
  {"name":"DM:ROCKMARCH","toggleGroup":"movement","suppressMs":1000,"source":"factory","modified":"","valid":true,"retrained":false},
  {"name":"DM:HELLO","toggleGroup":"greeting","suppressMs":2000,"source":"factory","modified":"","valid":true,"retrained":false}
]
```

#### Example request (fetch single factory sequence)

```bash
curl -s 'http://artoo.local/api/seq/builtins?name=DM:ROCKMARCH'
```

#### Example response (abridged)

```json
{"name":"DM:ROCKMARCH","version":1,"toggleGroup":"movement","suppressMs":1000,"steps":[{"type":"dome","action":"speed","payload":"0.5","durationMs":1000}]}
```

### POST /api/seq/test

Queues a sequence for playback by `DM:*` name (Learned or factory).

Accepts the sequence name via query parameter or JSON body. The body form is preferred for editor automation.

- Body (form):
  - `name`: `DM:*` sequence name
- Body (JSON):
  - `{"name":"DM:*"}`
- Success: `200` `{"ok":true}`
- Errors:
  - `400` `{"ok":false,"error":"missing or invalid DM:* name"}`
  - `423` `{"error":"sleeping","hint":"POST /api/wake"}` (sleep mode blocks)
  - `503` `{"ok":false,"error":"sequence queue full"}`

#### Example request (form)

```bash
curl -s -X POST http://artoo.local/api/seq/test \
  -d 'name=DM:ROCKMARCH'
```

#### Example response

```json
{"ok":true}
```

#### Example request (JSON body)

```bash
curl -s -X POST http://artoo.local/api/seq/test \
  -H 'Content-Type: application/json' \
  -d '{"name":"DM:ROCKMARCH"}'
```

#### Example response

```json
{"ok":true}
```

### POST /api/seq/stop

Aborts the currently running sequence (Learned or factory).

Stop is non-latching and idempotent. If no sequence is running, the request succeeds with no-op. Does not affect other subsystems (e.g., estop).

- Body: none
- Success: `200` `{"ok":true}`

#### Example request

```bash
curl -s -X POST http://artoo.local/api/seq/stop
```

#### Example response

```json
{"ok":true}
```

### GET /api/seq/last-run

Returns machine-readable evidence of the last sequence execution.

- Success: `200` JSON with execution details:
  - `name`: sequence name
  - `startMs`: timestamp when sequence started
  - `runDurationMs`: how long the sequence ran
  - `step`: current/final step index
  - `state`: execution state (`"idle"`, `"running"`, `"stopped"`, `"error"`, etc.)
  - Additional fields depend on sequence engine state
- Errors: `500` on response overflow

#### Example request

```bash
curl -s http://artoo.local/api/seq/last-run
```

#### Example response (abridged)

```json
{"name":"DM:ROCKMARCH","startMs":1234567890,"runDurationMs":5000,"step":3,"state":"running"}
```

### GET /api/seq

Fetches the raw JSON of a single Learned Sequence by name.

- Query params:
  - `name=<sequence-name>`: required; Learned Sequence name to fetch
- Success: `200` JSON v1 of the Learned Sequence
- Errors:
  - `400` `{"ok":false,"error":"name is required"}`
  - `404` `{"ok":false,"error":"sequence not found"}`
  - `500` on response overflow

#### Example request

```bash
curl -s 'http://artoo.local/api/seq?name=DM:CUSTOM'
```

#### Example response (abridged)

```json
{"name":"DM:CUSTOM","version":1,"steps":[{"type":"dome","action":"speed","payload":"0.5","durationMs":1000}]}
```

### POST /api/seq

Validates and persists a Learned Sequence.

Sends a full sequence JSON v1 in the body. The endpoint runs Protocol Check validation and persists to storage if valid.

- Body: JSON v1 sequence object with:
  - `name`: `DM:*` identifier
  - `version`: must be `1`
  - `steps`: array of valid step objects
  - `toggleGroup`: optional toggle group assignment
  - `suppressMs`: optional suppression interval (1000–120000 ms)
- Success: `200` `{"ok":true}`
- Errors:
  - `400` `{"ok":false,"error":"...","field":"<field>"}` (Protocol Check failure, see field)
  - `500` `{"ok":false,"error":"sequence save failed"}`

#### Example request

```bash
curl -s -X POST http://artoo.local/api/seq \
  -H 'Content-Type: application/json' \
  -d '{"name":"DM:CUSTOM","version":1,"steps":[{"type":"dome","action":"speed","payload":"0.5","durationMs":1000}]}'
```

#### Example response

```json
{"ok":true}
```

### DELETE /api/seq

Deletes a Learned Sequence from storage (Memory Wipe).

- Query params:
  - `name=<sequence-name>`: required; Learned Sequence name to delete
- Success: `200` `{"ok":true}`
- Errors:
  - `400` `{"ok":false,"error":"name is required"}`
  - `404` `{"ok":false,"error":"sequence not found"}`
  - `500` `{"ok":false,"error":"delete failed"}`

#### Example request

```bash
curl -s -X DELETE http://artoo.local/api/seq?name=DM:CUSTOM
```

#### Example response

```json
{"ok":true}
```

## Configuration and RC

### GET /api/config

Returns current config snapshot.

- Success: `200` JSON including:
- `drive`: speed limits, presets, web timeout, stationary
- `rc`: input mode, SBUS timeout, `sbus.recvCh2`
- `components`: enabled flags and servo type metadata
- `dome`: pulse calibration, speed limit, random movement config, wifi peer IP
- top-level servo calibration fields (`arm*OpenUs`, `aux*CloseUs`, etc.)
- `aux_led_pin`, `aux_led_count`
- `system.logLevel`
- `wifi`: Device WiFi Settings (ADR 0015) — `provisioned`, `mode` (`client`|`standalone_ap`), `staSsid`, `staPasswordSet`, `apSsid`, `apPasswordSet`, `pendingApply` (true when persisted settings differ from what is currently applied to WiFi hardware — a Staged Network Switch awaiting reboot/restart). Plaintext passwords are never returned.

#### Example request

```bash
curl -s http://artoo.local/api/config
```

#### Example response (abridged)

```json
{"drive":{"speedLimitMax":600,"speedPreset":"normal","webDriveTimeoutMs":500,"stationary":false},"rc":{"inputMode":"dual_sbus","sbusTimeoutMs":300,"sbus":{"recvCh2":false}},"components":{"arm1":{"enabled":true,"type":"mg996r"},"dome":{"enabled":true}},"dome":{"neutralUs":1500,"minPulseUs":1000,"maxPulseUs":2000,"speedLimitPct":100,"wifiPeerIp":""},"system":{"logLevel":2},"arm1OpenUs":1000,"arm1CloseUs":2000,"aux_led_pin":1,"aux_led_count":16}
```

### POST /api/config

Updates supported config fields and persists to NVS.

- Supported form fields include:
- drive: `speedLimitMax(0..600)`, `speedPresetSlow(0..600)`, `speedPresetNormal(0..600)`, `speedPresetTurbo(0..600)`, `webDriveTimeoutMs(100..5000)`, `stationary(bool)`
- system: `logLevel(1..4)` — 1 Error, 2 Warning, 3 Info, 4 Debug. Emission changes immediately; the log ring's depth follows the saved level at the next reboot.
- rc: `rcInputMode(standard_pwm|single_sbus|dual_sbus)`, `sbusTimeoutMs(50..5000)`, `sbusRecvCh2(bool)`
- components (bool): `enableArm1`, `enableArm2`, `enableAux1`, `enableAux2`, `enableAux3`, `enableDome`, `enableRcCh1..6`, `enableS1Hoverboard`, `enableS2Sound`, `enableS3DomeCtrl`
- dome calibration: `domeNeutralUs(1000..2000)`, `domeMinPulseUs(1000..2000)`, `domeMaxPulseUs(1000..2000)`, `domeSpeedLimitPct(0..100)`, `domeWifiPeerIp(valid IPv4 or empty)`
- dome random: `domeRndEnable(bool)`, `domeRndSpeedPct(5..100)`, `domeRndPauseMin(1..120)`, `domeRndPauseMax(1..120)`, `domeRndMoveMs(500..10000)`
- servo calibration: `arm1OpenUs..aux3CloseUs` each `500..2500`
- servo component types: `arm1Type|arm2Type|aux1Type|aux2Type|aux3Type` in `none|mg996r|mg90s|rgb`
- aux-led: `aux_led_pin(0..3)`, `aux_led_count(1..255)`

- Supported JSON body fields:
- `rc.sbusTimeoutMs` (50..5000)
- `rc.sbus.recvCh2` (boolean)
- `dome.wifiPeerIp` (string, empty or IPv4)
- `aux_led_pin` (0..3)
- `aux_led_count` (1..255)

- Success: `200` returns full updated config JSON (same shape as GET /api/config)
- Errors:
- `400` on invalid value/type or unsupported request with no accepted fields
- `500` failed persistence or response build/alloc failure

#### Example request (form)

```bash
curl -s -X POST http://artoo.local/api/config \
  -d 'speedLimitMax=400&webDriveTimeoutMs=750&enableArm1=true&enableDome=true&domeNeutralUs=1500'
```

#### Example response (abridged)

```json
{"drive":{"speedLimitMax":400,"webDriveTimeoutMs":750},"components":{"arm1":{"enabled":true},"dome":{"enabled":true}},"dome":{"neutralUs":1500}}
```

#### Example request (json)

```bash
curl -s -X POST http://artoo.local/api/config \
  -H 'Content-Type: application/json' \
  -d '{"rc":{"sbusTimeoutMs":300,"sbus":{"recvCh2":false}},"dome":{"wifiPeerIp":"10.0.0.50"},"aux_led_pin":1,"aux_led_count":32}'
```

#### Example response (abridged)

```json
{"rc":{"sbusTimeoutMs":300,"sbus":{"recvCh2":false}},"dome":{"wifiPeerIp":"10.0.0.50"},"aux_led_pin":1,"aux_led_count":32}
```

### GET /api/rc/map

Returns channel-centric map.

- Success: `200`
- Response shape:

```json
{
  "mode": "dual_sbus",
  "map": [
    { "source": "sbus1", "channel": 1, "action": "drive_speed" },
    { "source": "sbus1", "channel": 2, "action": "drive_steer" },
    { "source": "sbus2", "channel": 6, "action": "sound_rand_humming" },
    { "source": "sbus2", "channel": 7, "action": "seq", "payload": "31" }
  ],
  "capacity": { "total": 14, "used": 4 }
}
```

#### Example request

```bash
curl -s http://artoo.local/api/rc/map
```

#### Example response

```json
{"mode":"dual_sbus","map":[{"source":"sbus1","channel":1,"action":"drive_speed"},{"source":"sbus1","channel":2,"action":"drive_steer"}],"capacity":{"total":14,"used":2}}
```

### POST /api/rc/map

Replaces entire RC map.

- Body: JSON only via `plain` body param
- Required shape:

```json
{
  "map": [
    { "source": "sbus1", "channel": 1, "action": "drive_speed" },
    { "source": "sbus2", "channel": 6, "action": "sound_rand_humming" }
  ]
}
```

- Validation rules (enforced):
- `map` must be array of objects
- each entry needs valid `source`, `channel`, `action`
- no duplicate `source+channel`
- no duplicate backbone action: `drive_speed`, `drive_steer`, `dome_speed`
- source/channel must match allowed ranges by source
- `dome.action.sequence` payload must be valid `DM:NAME` format
- Success: `200` `{"ok":true}`
- Errors:
- `400` with `{"ok":false,"error":"..."}` and optional `entry` object
- `500` `{"ok":false,"error":"failed to persist config"}`

#### Example request

```bash
curl -s -X POST http://artoo.local/api/rc/map \
  -H 'Content-Type: application/json' \
  -d '{"map":[{"source":"sbus1","channel":1,"action":"drive_speed"},{"source":"sbus1","channel":2,"action":"drive_steer"}]}'
```

#### Example response

```json
{"ok":true}
```

### GET /api/rc

Returns live RC diagnostics snapshot.

- Success: `200`
- Response includes:
- `mode`, `updatedMs`
- `sources` map (`enabled`, `linked`, `ageMs`, `lostFrames`, `failsafe`)
- `channels` analog array (with normalized/mapped/deadband/reverse)
- `digital` action map (`activeSource`, `bindingChannel`, `pressed`)
- `mappingProfile.channels` calibration values (`min`, `center`, `max`, `deadband`, `reverse`)
- `raw` arrays (`sbus1`, `sbus2`, `pwm`) when available
- Errors: `500` json build/stream alloc failures

#### Example request

```bash
curl -s http://artoo.local/api/rc
```

#### Example response (abridged)

```json
{"mode":"dual_sbus","updatedMs":123456,"sources":{"sbus1":{"enabled":true,"linked":true,"ageMs":12,"lostFrames":0,"failsafe":false}},"channels":[{"id":1,"name":"driveSpeed","type":"analog","activeSource":"sbus1","bindingChannel":1,"raw":1010,"rawUs":1512,"normalized":0.021,"mapped":0.021,"inDeadband":false,"reverse":false}],"digital":{"arm1":{"activeSource":"sbus2","bindingChannel":17,"pressed":true}},"mappingProfile":{"version":1,"channels":{"driveSpeed":{"source":"sbus1","channel":1,"min":172,"center":992,"max":1811,"deadband":0,"reverse":false}}}}
```

### POST /api/rc/debug

Sets RC debug mode.

- Body: JSON `{ "enabled": true|false }`
- Max payload size: 128 bytes
- Success: `200` `{"ok":true}`
- Errors:
- `413` payload too large
- `400` invalid chunking or invalid JSON
- `500` request buffer allocation failed

#### Example request

```bash
curl -s -X POST http://artoo.local/api/rc/debug \
  -H 'Content-Type: application/json' \
  -d '{"enabled":true}'
```

#### Example response

```json
{"ok":true}
```

## Action Registry

### GET /api/actions

Returns all bindable actions.

- Success: `200` array of objects with:
- `id`, `name`, `display_name`, `domain`, `description`
- `safety_critical`, `testable`, `one_shot`, `token`
- Error: `500` response stream allocation failure

#### Example request

```bash
curl -s http://artoo.local/api/actions
```

#### Example response (abridged)

```json
[{"id":1,"name":"drive.action.speed","display_name":"Drive Speed","domain":"drive","description":"Primary drive speed control","safety_critical":true,"testable":false,"one_shot":false,"token":"drive_speed"}]
```

### POST /api/actions/test

Dispatches one test action through RC trigger path.

- Input options:
- Form field: `token`
- JSON body: `{ "token": "..." }`
- Success: `200` `{"ok":true,"token":"...","domain":"..."}`
- Errors:
- `400` invalid json/token
- `403` `safety_critical_blocked`
- `423` `web_control_disabled`
- `422` `action_not_testable`
- `500` response stream allocation failure

#### Example request (form)

```bash
curl -s -X POST http://artoo.local/api/actions/test \
  -d 'token=sound_rand_humming'
```

#### Example response

```json
{"ok":true,"token":"sound_rand_humming","domain":"sound"}
```

#### Example request (json)

```bash
curl -s -X POST http://artoo.local/api/actions/test \
  -H 'Content-Type: application/json' \
  -d '{"token":"sound_rand_humming"}'
```

#### Example response

```json
{"ok":true,"token":"sound_rand_humming","domain":"sound"}
```

## Status and Validation

### GET /api/status

Returns controller status snapshot.

- Success: `200`
- Top-level fixed fields include:
- `estop`, `webControlEnabled`, `sbusSignalLost`, `sbusHwFailsafe`, `webDriveExpired`
- `failsafeSource`, `failsafeCount`, `failsafeTriggerMs`, `failsafeZeroMs`, `failsafeTriggerToZeroMs`, `failsafeWatchdogMs`, `failsafeTriggerSource`
- `driveSpeed`, `driveSteer`, `domeTargetSpeed`, `domeEnabled`
- `speedLimitMax`, `speedPreset`, `stationary`
- `uptimeMs`, `firmwareVersion`, `webVersion`
- `heapFree`, `heapMin`, `heapLargestBlock`, `heapLargest8bit`
  - `heapLargest8bit` is the largest allocatable DRAM block (`MALLOC_CAP_8BIT`) —
    the pool `malloc` and the admission guards actually use. `heapLargestBlock`
    is legacy: it reads `MALLOC_CAP_INTERNAL`, which is dominated by a constant
    ~36 KB leftover-IRAM region that can never be allocated, so it stays near
    36 KB regardless of real heap pressure. Use `heapLargest8bit` for any
    heap-health judgement. (Note: `/api/health` has always reported the 8-bit
    value under the `heapLargestBlock` name.)
- `sseClients` — registered `/api/events` clients (admission cap is 3)
- `tcpAcceptRejectHeap`, `tcpAcceptRejectRate`, `tcpAcceptRejectAgeMs` —
  accept-guard rejection counters (heap floor / rate pacing) and milliseconds
  since the last rejection (`-1` if none since boot)
- `wifiRssi`, `wifiConnected`, `wifiClientConnected`, `littleFsReady`
- `sleepMode`, `sleepSinceMs`, `activeMood`
- `auxLed` object (`pin`, `r`, `g`, `b`, `effect`, `available`)
- Additional component objects are conditionally present when enabled (`arm1`, `arm2`, `aux1..aux3`, `dome`, `rcCh1..rcCh6`, `s1Hoverboard`, `s2Sound`, `s3DomeCtrl`)
- Includes top-level `dome_link` object (`state`, `transport`, counters, last_rx_ms)
- Includes `hoverboard` object when feedback is valid

#### Example request

```bash
curl -s http://artoo.local/api/status
```

#### Example response (abridged)

```json
{"estop":false,"webControlEnabled":false,"sbusSignalLost":false,"sbusHwFailsafe":false,"webDriveExpired":false,"failsafeSource":0,"driveSpeed":0,"driveSteer":0,"domeTargetSpeed":0.0,"domeEnabled":true,"speedLimitMax":600,"speedPreset":"normal","stationary":false,"uptimeMs":27790,"firmwareVersion":"v1.0.0","webVersion":"fs-v1.0.0","heapFree":173152,"heapMin":150932,"heapLargestBlock":132000,"wifiRssi":-70,"wifiConnected":true,"wifiClientConnected":true,"littleFsReady":true,"sleepMode":false,"sleepSinceMs":0,"activeMood":14,"auxLed":{"pin":1,"r":0,"g":0,"b":0,"effect":"off","available":true}}
```

### GET /api/health

Returns compact health JSON.

- Success: `200` JSON

#### Example request

```bash
curl -s http://artoo.local/api/health
```

#### Example response

```json
{"estop":false,"sbusSignalLost":false,"sbusHwFailsafe":false,"webControlEnabled":false,"wifiConnected":true,"wifiClientConnected":true,"littleFsReady":true,"heapFree":173152,"heapMin":150932,"heapLargestBlock":132000,"wifiRssi":-70}
```

### GET /api/wifi

Returns AP/STA connectivity JSON — the diagnostics/readiness surface for
active WiFi state. For saved Device WiFi Settings (mode, SSIDs,
password-set flags, staged-apply state), see the `wifi` block on
`GET /api/config` below. See also
[docs/wifi-provisioning.md](wifi-provisioning.md) for the operator-facing
provisioning/recovery flow (ADR 0015).

- Success: `200` JSON
- `apSsid`: SSID currently broadcast by the AP radio. During WiFi
  Provisioning or Network Recovery Mode this is the Default AP Credential's
  SSID (`protoArtoo`), not necessarily the operator's saved Standalone AP
  Mode SSID.
- `networkRecovery`: `true` only while Network Recovery Mode is active for
  this boot (entered via the local power-cycle gesture — see
  [docs/wifi-provisioning.md](wifi-provisioning.md)). It does not indicate
  ordinary WiFi Client Mode connection trouble.

#### Example request

```bash
curl -s http://artoo.local/api/wifi
```

#### Example response

```json
{"apSsid":"protoArtoo","apIp":"192.168.4.1","staEnabled":true,"staConnected":true,"staIp":"10.0.0.22","wifiRssi":-70,"networkRecovery":false}
```

### POST /api/wifi

Validates, stages, and persists Device WiFi Settings (ADR 0015). This is a Staged
Network Switch: settings are saved to NVS, but WiFi hardware is not touched here —
the new posture takes effect on the next reboot or WiFi restart. Only supplied
fields are changed. See [docs/wifi-provisioning.md](wifi-provisioning.md) for
the end-to-end operator flow this endpoint backs.

- Supported form fields:
- `wifiMode` (`client`|`standalone_ap`)
- `staSsid` (string, 0..32 chars)
- `staPassword` (string, 0..63 chars — write-only, omit to keep the existing saved password)
- `apSsid` (string, 0..32 chars)
- `apPassword` (string, empty for an open network or 8..63 chars — write-only, omit to keep the existing saved password)
- Validation:
- `wifiMode` must be `client` or `standalone_ap`
- `staSsid` is required (non-empty) once the resulting mode is `client`
- `apSsid` is required (non-empty) once the resulting mode is `standalone_ap`
- `apPassword` must be empty or 8..63 characters (ESP32 SoftAP/WPA2 requirement)
- Success: `200` with `{"ok":true,"wifi":{...}}` (same password-safe `wifi` shape as `GET /api/config`'s `wifi` block) and marks the settings provisioned
- Errors: `400` with `{"ok":false,"error":"..."}` on invalid/missing fields; `500` on persistence failure

#### Example request

```bash
curl -s -X POST http://artoo.local/api/wifi \
  -d 'wifiMode=client&staSsid=HomeNetwork&staPassword=supersecret'
```

#### Example response

```json
{"ok":true,"wifi":{"provisioned":true,"mode":"client","staSsid":"HomeNetwork","staPasswordSet":true,"apSsid":"protoArtoo","apPasswordSet":true,"pendingApply":true}}
```

### GET /api/serial

Returns serial/transport status JSON.

- Success: `200` JSON

#### Example request

```bash
curl -s http://artoo.local/api/serial
```

#### Example response (abridged)

```json
{"debug":{"label":"S0","name":"ESP debug","active":true},"hoverboard":{"label":"S1","name":"Hoverboard","active":true},"sound":{"label":"S2","name":"Sound","active":false},"dome":{"label":"S3","name":"protoR2link","active":true,"heartbeatRx":49,"heartbeatTx":52}}
```

### GET /api/logs

Returns recent log buffer.

- Success: `200`
- Content type: `text/plain`

#### Example request

```bash
curl -s http://artoo.local/api/logs
```

#### Example response

```text
[WebServer] HTTP server started on port 80
[RC] SBUS1 linked
[AUDIO] POST /api/audio play track=42
```

### GET /api/validation

Returns validation-focused consolidated snapshot.

- Success: `200`
- Response shape:
- `updatedMs`
- `drive` (`estop`, `webDriveExpired`, `sbusSignalLost`, `sbusHwFailsafe`, failsafe metrics)
- `domeLink` (`state`, `hbTx`, `hbRx`, `lastRxMs`)
- `audio` (`enabled`, `active`, `activeMood`, random and interval settings)
- `rc` (`mode`, `timeoutMs`, source map with `enabled|linked|signalLost|failsafe|ageMs`)
- Errors: `500` on json build or stream alloc failure

#### Example request

```bash
curl -s http://artoo.local/api/validation
```

#### Example response

```json
{"updatedMs":27790,"drive":{"estop":false,"webDriveExpired":false,"sbusSignalLost":false,"sbusHwFailsafe":false,"failsafeSource":0,"failsafeCount":2,"triggerMs":20123,"zeroMs":20138,"triggerToZeroMs":15,"watchdogMs":20123,"triggerSource":1},"domeLink":{"state":"connected","hbTx":52,"hbRx":49,"lastRxMs":110},"audio":{"enabled":true,"active":true,"activeMood":14,"randomMin":1,"randomMax":120,"intervalQuietS":0,"intervalMidS":30,"intervalFullS":20,"intervalAwakeS":10},"rc":{"mode":"dual_sbus","timeoutMs":200,"sources":{"sbus1":{"enabled":true,"linked":true,"signalLost":false,"failsafe":false,"ageMs":12},"sbus2":{"enabled":true,"linked":false,"signalLost":true,"failsafe":false,"ageMs":310},"pwm":{"enabled":false,"linked":false,"signalLost":false,"failsafe":false,"ageMs":0}}}}
```

## System and OTA

### POST /api/sleep

Enables sleep mode.

- Requires `webControlEnabled=true`
- Success: `200` JSON from sleep formatter (`ok`, `sleepMode`, `changed`)
- Errors:
- `409` `{"ok":false,"error":"web control is not enabled"}`
- `500` `{"ok":false,"error":"sleep response overflow"}`

#### Example request

```bash
curl -s -X POST http://artoo.local/api/sleep
```

#### Example response

```json
{"ok":true,"sleepMode":true,"changed":true}
```

### POST /api/wake

Disables sleep mode.

- Requires `webControlEnabled=true`
- Success: `200` JSON from wake formatter (`ok`, `sleepMode`, `changed`)
- Errors:
- `409` `{"ok":false,"error":"web control is not enabled"}`
- `500` `{"ok":false,"error":"wake response overflow"}`

#### Example request

```bash
curl -s -X POST http://artoo.local/api/wake
```

#### Example response

```json
{"ok":true,"sleepMode":false,"changed":true}
```

### POST /api/manual-command

Executes supported manual command.

- Body field: `command`
- Rate limit: minimum 100 ms between calls
- Sleep mode blocks prefixed control commands (`$ : # * @ % & !`)
- Success: `200` `{"ok":true}`
- Errors:
- `429` `{"ok":false,"error":"rate limit exceeded"}`
- `400` `{"ok":false,"error":"missing command"}`
- `423` `{"error":"sleeping","hint":"POST /api/wake"}`
- `400` `{"ok":false,"error":"unsupported command"}`

#### Example request

```bash
curl -s -X POST http://artoo.local/api/manual-command \
  -d 'command=estop'
```

#### Example response

```json
{"ok":true}
```

### POST /api/reboot

Requests reboot.

- Success: `200` `{"ok":true}` (reboot scheduled)

#### Example request

```bash
curl -s -X POST http://artoo.local/api/reboot
```

#### Example response

```json
{"ok":true}
```

### POST /upload/firmware

Streams OTA firmware update.

- Body: multipart upload
- Size limit: 4 MB
- Success: `200` `{"ok":true}` (reboot scheduled)
- Errors:
- `413` `{"ok":false,"error":"firmware image exceeds upload size limit"}`
- `500` `{"ok":false,"error":"update failed"}`

#### Example request

```bash
curl -s -X POST http://artoo.local/upload/firmware \
  -F 'firmware=@.pio/build/artoo_esp32/firmware.bin'
```

#### Example response

```json
{"ok":true}
```

### POST /upload/filesystem

Streams OTA filesystem update.

- Body: multipart upload
- Size limit: 1536 KB
- Success: `200` `{"ok":true}` (reboot scheduled)
- Errors:
- `413` `{"ok":false,"error":"filesystem image exceeds upload size limit"}`
- `500` `{"ok":false,"error":"filesystem update failed"}`

#### Example request

```bash
curl -s -X POST http://artoo.local/upload/filesystem \
  -F 'filesystem=@.pio/build/artoo_esp32/littlefs.bin'
```

#### Example response

```json
{"ok":true}
```

### GET /api/coredump/status

Reports whether a crash coredump is stored in the `coredump` flash partition. The
firmware saves an ELF coredump on a PANIC (`abort()`, watchdog, exception). Use
this to discover a crash to retrieve after a reboot. See
[troubleshooting.md](troubleshooting.md) for the full decode procedure.

- Success: `200` `{"present":<bool>,"size":<bytes>}`

#### Example request

```bash
curl -s http://artoo.local/api/coredump/status
```

#### Example response

```json
{"present":true,"size":18432}
```

### GET /api/coredump

Streams the raw ELF coredump from flash (chunked, no large heap buffer). Empty
when no coredump is present. Retrieval works on the seated controller over WiFi —
USB read is blocked when the ESP32 is in the PCB (GPIO15/SBUS strapping).

- Success: `200` `application/octet-stream` (ELF), `Content-Disposition: attachment; filename=coredump.elf`
- Errors:
- `404` `{"ok":false,"error":"no coredump"}`
- `500` `{"ok":false,"error":"no coredump partition"}`

#### Example request

```bash
curl -s http://artoo.local/api/coredump -o coredump.elf
# decode against the firmware.elf for the deployed version (keyed by fw-version.json):
esp-coredump info_corefile -c coredump.elf .pio/build/artoo_esp32_chirp/firmware.elf
```

### POST /api/coredump/erase

Erases the stored coredump so the next crash is captured. Do this after
retrieving a coredump.

- Success: `200` `{"ok":true}`
- Errors: `500` `{"ok":false,"error":"erase failed"}`

#### Example request

```bash
curl -s -X POST http://artoo.local/api/coredump/erase
```

#### Example response

```json
{"ok":true}
```

## SSE Events

### GET /api/events

Server-Sent Events stream.

- Event names currently emitted:
- `status` (same payload family as GET /api/status)
- `rc` (same payload family as GET /api/rc)
- `log` (plain log line text)
- Emission loop runs from `eventStreamTask` with 1000 ms delay.

#### Example request

```bash
curl -N -H 'Accept: text/event-stream' http://artoo.local/api/events
```

#### Example response (stream excerpt)

```text
event: status
data: {"estop":false,"webControlEnabled":false,...}

event: rc
data: {"mode":"dual_sbus","updatedMs":123456,...}

event: log
data: [WebServer] HTTP server started on port 80
```

## Profiling (Build-Conditional)

These routes exist only when `PA_HEAP_PROFILE` is enabled.

### GET /api/profiler

Returns heap/profile snapshot JSON including:

- `heapFree`, `heapMin`, `heapLargest`, `fragRatio`
- allocator block counters
- `failedAllocs`
- `taskStacks[]` high-water marks
- optional `taskHeap[]` when `CONFIG_HEAP_TASK_TRACKING`
- mode-window `current` and `snapshots[]`

#### Example request

```bash
curl -s http://artoo.local/api/profiler
```

#### Example response (abridged)

```json
{"heapFree":173152,"heapMin":150932,"heapLargest":132000,"fragRatio":0.238,"allocBlocks":412,"freeBlocks":128,"totalBlocks":540,"failedAllocs":0,"taskStacks":[{"name":"DriveTask","hwmBytes":2048,"status":"ok"}],"snapshots":[{"label":"boot","heapFree":150932,"largestBlock":120000,"ts":1234}]}
```

### POST /api/profiler/trace/start

Present only when `CONFIG_HEAP_TRACING` is enabled.

- Success/failure both return `200` with `ok:true|false`

#### Example request

```bash
curl -s -X POST http://artoo.local/api/profiler/trace/start
```

#### Example response

```json
{"ok":true,"mode":"LEAKS"}
```

### POST /api/profiler/trace/stop

Present only when `CONFIG_HEAP_TRACING` is enabled.

- Success/failure both return `200` with `ok:true|false`

#### Example request

```bash
curl -s -X POST http://artoo.local/api/profiler/trace/stop
```

#### Example response

```json
{"ok":true,"note":"dump written to serial log"}
```

## Internal Routes

The following routes are intentionally undocumented in the operator-facing API; they are internal diagnostic or measurement tools. They may change without warning and should not be used in operator automation.

### GET /api/admission/trace

**Build-gated: `PA_ADMISSION_TRACE` only.** Absent (404) if not enabled.

Returns a ring buffer of admission decisions — shed count, reasons, admission state snapshots.

Machine-readable evidence for heap pressure analysis and load tuning only. No stability guarantee.

- Query params:
  - `clear`: optional; if present, clears the ring buffer after sending
- Success: `200` JSON with trace data (format not documented)
- Errors: `503` on send failure

#### Example request

```bash
curl -s 'http://artoo.local/api/admission/trace?clear=1'
```
