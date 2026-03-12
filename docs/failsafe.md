# Failsafe System

protoArtoo Phase 1 implements five independent safety layers for drive control.
The design goal is simple: loss of control input, stalled firmware, or hoverboard
link failure must all converge on zero drive output.

## Layer 1 - SBUS receiver hardware failsafe

- Source: RC receiver firmware
- Implementation: `src/tasks/sbus_input.cpp`
- Trigger: decoded SBUS frame reports `failsafe=true`
- Result: `driveSpeed=0`, `driveSteer=0`, `sbusHwFailsafe=true`,
  `failsafeSource=FS_SBUS_HW`

This is the fastest RC-side safety path. If the receiver itself detects radio
loss, protoArtoo immediately zeros drive output on the next decoded frame.

## Layer 2 - SBUS software watchdog

- Source: body firmware timeout
- Implementation: `src/tasks/sbus_input.cpp`
- Trigger: no valid drive-receiver frame for more than `SBUS_TIMEOUT_MS` (200 ms)
- Result: `sbusSignalLost=true`, `driveSpeed=0`, `driveSteer=0`,
  `failsafeSource=FS_SBUS_TIMEOUT`

This protects against missing frames, unplugged receivers, and decode failures
even if the RC receiver does not assert its own failsafe flag.

## Layer 3 - Web drive command timeout

- Source: body firmware timeout
- Implementation: `src/tasks/drive.cpp`
- Trigger: last drive command came from `SRC_WEB_API` and is older than
  `cfg_webDriveTimeoutMs` (default `WEB_DRIVE_TIMEOUT_MS = 500`)
- Result: `webDriveExpired=true`, `driveSpeed=0`, `driveSteer=0`,
  `failsafeSource=FS_WEB_TIMEOUT`

Web control is intentionally dead-man style. A client must keep refreshing the
command; silence is treated as a stop condition.

## Layer 4 - ESP32 Task Watchdog Timer

- Source: ESP32 hardware watchdog
- Implementation: `src/main.cpp`, `src/tasks/drive.cpp`
- Trigger: `DriveTask` stops reaching `esp_task_wdt_reset()` within
  `WATCHDOG_TIMEOUT_S` (3 s)
- Result: ESP32 resets; next boot detects `ESP_RST_TASK_WDT`, sets
  `estop=true`, and records `failsafeSource=FS_WATCHDOG_RESET`

This covers firmware hangs in the real-time drive loop. The robot does not
resume movement automatically after a watchdog reboot.

## Layer 5 - Hoverboard UART timeout

- Source: hoverboard motor controller firmware
- Implementation: external to protoArtoo
- Trigger: hoverboard stops receiving valid UART frames for roughly 500 ms
- Result: hoverboard firmware stops the motors independently of the ESP32

protoArtoo supports this layer by following the zero-frame rule: it never goes
silent intentionally. Even when stopped, it keeps transmitting zero commands.

## Latching estop behavior

Emergency stop is separate from the automatic timeouts above.

- `POST /api/estop` sets `estop=true`
- `POST /api/estop/clear` is the only way to clear it
- `estop` does not auto-clear when RC or web input returns

This prevents accidental restart after a serious safety event.

## Boot safety defaults

Phase 1 boots with conservative defaults:

- `sbusSignalLost = true` until valid drive SBUS traffic is seen
- watchdog-reset reboot sets `estop = true`
- `cfg_speedLimitMax`, `cfg_sbusTimeoutMs`, and `cfg_webDriveTimeoutMs` are
  loaded into `RobotState` before tasks start

## Phase 1 notes

- Drive SBUS: GPIO 15 via the custom RMT decoder
- Dome SBUS: GPIO 13 RMT initialized, dome processing deferred to a later phase
- Hoverboard: UART1 on GPIO 16/17
- `SafetyMonitorTask` is observer-only; it logs failsafe transitions but does
  not command the motors directly
