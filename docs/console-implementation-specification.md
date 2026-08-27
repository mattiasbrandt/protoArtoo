# Controller Console Implementation Specification

This document maps every operation in the action registry to its implementation path. It serves as the reference for implementing the Console module and its two adapters (serial terminal and browser live-logs console).

**Scope:** 189 operations across four domains: drive, dome, sound, system (plus aux, rc, servo sub-domains).  
**ADR Reference:** ADR 0034 (one operation core below HTTP handlers, two adapters).  
**Inventory Base:** All 189 rows verified and cross-matched (dome/sound/system/drive-servo-aux-rc inventories).

---

## Operation Categories

Every operation falls into one of these categories, which determines its implementation path:

### 1. Action (127 entries)
**Definition:** Dispatch a command to a domain core and return an outcome.

**Signature:**  
```c
// Returns outcome: queued | queue-full | blocked | unavailable | invalid | internal-error
DispatchOutcome dispatchAction(const char* actionName, const void* args, CommandSource src);
```

**Executor Cores by Domain:**
- **drive** (17 actions): `driveArbiter`, `driveSpeedPresetCore`, `drivePanicStop`
- **dome** (11 actions): `sequenceStart`, `sequenceStop`, `seqStoreDelete`, `domeSpeedCore`, `domeSetSpeedOffset`
- **sound** (22 actions): `audioQueueCommand`, `audioStop`, `audioMoodStart`
- **servo** (10 actions): `servoArmCommand`, `servoParkCommand`
- **aux** (8 actions): `auxLedSetColor`, `auxLedSetEffect`
- **rc** (14 actions): `rcSetMode`, `rcInputMappingDynamic`, `rcTestAction`
- **system** (45 actions): `profilStart`, `systemReboot`, `commandedSetWebControl`, `set_log_level`, etc.

**Example mapping:**
- `drive.action.move` → executor: `driveArbiter` (in src/rc_dispatcher_helpers.cpp)
- `sound.action.random-humming` → executor: `audioQueueCommand` (in src/tasks/audio_driver.cpp)

### 2. Config (33 entries)
**Definition:** Read/apply/persist a configuration value, with Commit Step (ADR 0011).

**Signature (two-phase):**
```c
// Phase 1: Apply (in-memory, validate)
ConfigApplyResult configApply(const char* path, const void* value, CommandSource src);

// Phase 2: Commit (persist to NVS, emit effects)
bool configCommitStep(const char* path);
```

**Executor Pattern:**  
Each config row cites:
1. **Read path:** How the current value is fetched (e.g., `configCacheRead`, `robotState` field)
2. **Apply path:** The Apply Core (e.g., `configApplyDriveSpeedLimit` in src/config_apply.cpp)
3. **Commit path:** Where persistence happens (e.g., `PrefsWriter::writeDriveSpeedLimit` in src/config_nvsio.cpp)

**Example mapping:**
- `drive.config.speed-limit`
  - Executor: `configApplyDriveSpeedLimit`
  - Commit: `src/config_nvsio.cpp:PrefsWriter::writeDriveSpeedLimit`
- `wifi.config.ssid`
  - Executor: `configApplyWifiSsid`
  - Commit: `src/config_nvsio.cpp:PrefsWriter::writeWifiSsid`

### 3. Status (14 entries)
**Definition:** Query current state and return a structured snapshot.

**Classification:**
- **Standalone queries (4):** Own endpoint, return complete JSON
  - `dome.status.current` → `/api/status`
  - `sound.status.current` → `/api/audio`
  - `dome.status.serial-link` → `/api/serial`
  - `rc.status.snapshot` → `/api/rc`

- **Aggregate-field (9):** Describe one field within a larger response (metadata, never standalone commands)
  - `drive.status.current` (motor outputs in `/api/status`)
  - `servo.status.current` (servo states in `/api/status`)
  - `system.status.sleep-mode` (sleep state in `/api/status`)
  - etc.

**Executor Path (for standalone queries):**

Standalone queries return fields from JSON builders:

| Query | Builder | Fields (sample) |
|-------|---------|-----------------|
| drive.status.current | buildStatusJson | driveSpeed, driveSteer, speedLimitMax, speedPreset |
| sound.status.current | buildAudioJson | audioActive, activeMood, audioLinkOk |
| dome.status.serial-link | buildSerialJson | port, baud, available |
| rc.status.snapshot | buildRcJson | mode, enabledChannels, sbus{1,2}Status |
| system.api.event-stream | webEventStream | (SSE records, live event stream) |

For aggregate-fields: List the parent query name and the field name within it (see inventory `notes` column for aggregation details).

### 4. Event (15 entries)
**Definition:** Observable lifecycle events emitted by the firmware.

**Emitter Sites:**
- `system.event.boot-complete` - Emitted in setup() after all tasks running
- `system.event.dome-enabled` - Emitted when dome link successfully establishes
- `system.event.drives-engaged` - Emitted when drive output first becomes active
- System lifecycle events (reboot, reset reason, OTA start/complete)
- Domain-specific events (audio started, servo parked, etc.)

**Event Stream Path:**
All events flow through `webEventStreamBroadcast()` (src/web/web_server.cpp:816).  
Events are published to live SSE clients and archived in the event ring buffer.

---

## Universal Metadata Now in Registry

As of #212, every operation carries:

### 1. **executor field (new)**
The name of the function/core that implements this operation.  
Examples: `driveArbiter`, `audioQueueCommand`, `configApplyWifiSsid`

**Validation:** Inventory cross-check confirms all 189 values are domain cores, never HTTP adapters.

### 2. **board_capability field (existing)**
Which board(s) can run this operation (if it's optional).  
Examples: `PA_CAP_HOSTED_WIFI`, `PA_CAP_SECONDARY_DRIVE`

Absent = universal (all boards).

### 3. **build_flag field (existing)**
Which developer build flags gate this feature.  
Examples: `PA_HEAP_PROFILE`, `PA_ADMISSION_TRACE`

Absent = always compiled in.

### 4. **fields list (planned for deliverable 2)**
For status queries: the JSON field names returned verbatim.  
Example (drive.status.current):
```yaml
fields:
  - driveSpeed
  - driveSteer
  - speedLimitMax
  - speedPreset
  - stationary
```

---

## Dispatch and Safety Invariants

### Command Source Tracking
Every command entering the Console module carries a `CommandSource`:
```c
enum CommandSource {
    SRC_SBUS = 0,
    SRC_WEB_API = 1,
    SRC_SERIAL_CONSOLE = 2,  // New (ADR 0034)
    SRC_WEB_CONSOLE = 3,      // New (ADR 0034)
};
```

### Safety Gates (Unchanged)
These gates apply to all command sources equally:
1. **Estop state** - Drives, weapons locked when active
2. **Stationary/sleep mode** - Motion gated while parked
3. **Component toggle** - Disabled subsystems refuse commands
4. **Queue capacity** - Refuses when queue is full (outcome: `queue-full`)
5. **Speed caps** - Motor outputs clamped pre-transmission

### Non-RC Control Gate
The "web control" Commanded Mode (ADR 0027) gates non-RC sources from commanding drive motion when the RC link is unhealthy.

- **SERIAL_CONSOLE** and **WEB_CONSOLE** sources:
  - Gated by `Non-RC Control` Commanded Mode
  - Same gate as HTTP API (requires `webControlEnabled`)
  - Physical serial terminal is a trusted local source (no unlock ceremony)

---

## Outcome Enumeration

Actions return an outcome instead of `void` (ADR 0034 change):

| Outcome | Meaning |
|---------|---------|
| `queued` | Command accepted and queued for execution |
| `queue-full` | Command queue at capacity; operation did not proceed |
| `blocked` | Guard prevented execution (e.g., estop, stationary, component disabled) |
| `unavailable` | Operation not in this build or not on this board |
| `invalid` | Command arguments failed validation |
| `internal-error` | Unexpected firmware state or error |

**Console rendering:** Each outcome is rendered as a Console Record with stable reason codes:
- `not-in-this-build` (build flag gates it)
- `not-on-this-board` (board capability gates it)
- `queue-full`
- `estop-engaged`
- `stationary-required`
- `component-disabled`
- `rc-link-unhealthy` (when non-RC control gate applies)

---

## Implementation Checklist for Tickets #219-#227

### Per-Domain Operations
Each ticket implements one domain's operations in the Console module:

- **#220 (RC commands):** 14 actions + 1 status (rc.status.snapshot) + 1 event (rc-mode-change)
- **#221 (Audio/servo configs):** 10 config entries + command safety
- **#222 (Drive safety):** 17 actions + 1 config + safety invariant tests
- **#223 (Dome sequences):** 11 actions + 3 configs + 2 aggregate-fields
- **#224 (Availability reasons):** Unavailable operation listings per board/build
- **#225 (Status queries):** 5 standalone status queries + aggregate-field metadata
- **#226 (Config persistence):** 33 configs, Apply Core + Commit Step pairs
- **#227 (System operations & secrets):** 45 system actions, password exclusion

### Integration Points
- **ADR 0011 (Commit Step):** Apply Core + Commit Step pair for every config operation
- **ADR 0021 (Web request seam):** Console adapter = ordinary admitted route handler
- **ADR 0034 (Console seam):** Operation core sits below HTTP handlers; both adapters call the same one
- **RobotState zones (ADR 0012):** Snapshot reads via `taskENTER_CRITICAL(&robotStateMux)`
- **Commanded Modes (ADR 0027):** Runtime toggles + NVS persistence for component enables

---

## Reference: All 189 Operations by Category

### Actions (127)
```
drive:       17 actions (move, speed, steer, speed presets, panic-stop, turbo-on/off)
dome:        11 actions (delete-sequence, dome-sequence, droid-sequences, speed, speed-offset)
sound:       22 actions (random-humming, cantina, march, etc. - built-in sequences)
servo:       10 actions (arm commands, park)
aux:          8 actions (led-color, led-effect, servo commands)
rc:          14 actions (set-mode, dynamic-mapping, test, calibrate, etc.)
system:      45 actions (estop, reboot, profiler-start/stop, set-identity, etc.)
```

### Configs (33)
```
drive:        3 configs (speed-limit, speed-preset-active, hoverboard-motor-dir)
audio:        4 configs (volume, default-mood, etc.)
servo:        3 configs (arm-invert, park-position, etc.)
wifi:         6 configs (ssid, password, mode, baud, etc.)
system:      17 configs (enable_* toggles, identity, log-level, mood-timeout, etc.)
```

### Status (14)
```
Standalone:      5 queries (drive, sound, dome-link, rc, event-stream)
Aggregate-field: 9 metadata entries (within /api/status response)
```

### Events (15)
```
System:   4 events (boot-complete, dome-enabled, drives-engaged, ...)
Dome:     2 events (link-established, link-lost)
Audio:    3 events (started, stopped, mood-changed)
Servo:    3 events (parked, released, error)
RC:       3 events (mode-changed, calibration-complete, ...)
```

---

## Registry Authority

The action registry (`docs/action-registry.yaml`) is the single source of truth for:
- Operation names and metadata (display name, description, domain, type)
- Parameter schemas (type, range, enum values, required)
- Safety properties (safety_critical, requires_web_control)
- Availability (board_capability, build_flag)
- Executor reference (newly added in #212)
- API paths (REST endpoints)
- Marcduino command mappings (BD:*, $*)

Inventory files (`tools/console_inventory/*.yaml`) provide evidence that every registry entry has:
- A real executor core (not an HTTP adapter)
- Required citations (file:line)
- Configuration rows cite their Commit Step (ADR 0011)

The drift checker (`tools/check_action_registry_drift.py`) enforces:
- Registry ↔ Action C++ enum consistency
- Registry ↔ RC parser/tokenizer consistency
- Registry ↔ API routes consistency
- Registry ↔ Dome cues consistency
- Registry ↔ Audio dollar-commands consistency

---

## Files Touched by This Specification

- `src/console/console_core.cpp` - Transport-independent operation dispatcher
- `src/console/console_record.cpp` - Record serialization (key=value, outcome codes)
- `src/tasks/console_task.cpp` - Serial adapter task (persistent Core 0)
- `src/web/api_console.cpp` - Browser Live Logs endpoint
- `include/console_*.h` - Console module headers (API, record types)
- `test/test_console/` - Host-native tests for core logic
- `docs/action-registry.yaml` - Operation metadata (189 entries, newly annotated with executor)
- `docs/console-protocol.md` - Grammar, quoting, line endings, meta-commands (transport spec)

---

## Next Steps for Implementation Tickets

1. **#219-#220:** Verify operation naming, parameter schemas, and availability rules match registry
2. **#217, #228:** Measure task stack/buffer requirements; hook Console task into task management
3. **#229:** Integration testing - concurrent web + serial clients, config persistence atomicity
4. **#231:** Acceptance audit - compare browser and serial catalogs, verify parity, sign off
