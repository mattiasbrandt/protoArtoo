# Failsafe System

protoArtoo implements five independent safety layers for drive control.
The design goal is simple: loss of control input, stalled firmware, or hoverboard
link failure must all converge on zero drive output.

## Table of Contents

- [Layer 1 - SBUS receiver hardware failsafe](#layer-1---sbus-receiver-hardware-failsafe)
- [Layer 2 - SBUS software watchdog](#layer-2---sbus-software-watchdog)
- [Layer 3 - Web drive command timeout](#layer-3---web-drive-command-timeout)
- [Layer 4 - ESP32 Task Watchdog Timer](#layer-4---esp32-task-watchdog-timer)
- [Layer 5 - Hoverboard UART timeout](#layer-5---hoverboard-uart-timeout)
- [Latching estop behavior](#latching-estop-behavior)
- [Boot safety defaults](#boot-safety-defaults)
- [Implementation notes](#implementation-notes)

## Layer 1 - SBUS receiver hardware failsafe

- Source: RC receiver firmware
- Implementation: `src/tasks/rc_input.cpp`
- Trigger: decoded SBUS frame reports `failsafe=true`
- Result: `driveSpeed=0`, `driveSteer=0`, `sbusHwFailsafe=true`,
  `failsafeSource=FS_SBUS_HW`

This is the fastest RC-side safety path. If the receiver itself detects radio
loss, protoArtoo immediately zeros drive output on the next decoded frame.

Diagnostics surfaces also report the current SBUS hardware-failsafe
bit per source in `GET /api/rc` and `event: rc`.

## Layer 2 - SBUS software watchdog

- Source: body firmware timeout
- Implementation: `src/tasks/rc_input.cpp`
- Trigger: no valid drive-receiver frame for more than `cfg_sbusTimeoutMs`
  (default `SBUS_TIMEOUT_MS = 200 ms`)
- Result: `sbusSignalLost=true`, `driveSpeed=0`, `driveSteer=0`,
  `failsafeSource=FS_SBUS_TIMEOUT`

This protects against missing frames, unplugged receivers, and decode failures
even if the RC receiver does not assert its own failsafe flag.

`lost_frame` is tracked separately from the software watchdog:

- consecutive or intermittent lost-frame events increment the per-source
  diagnostics counters (`lostFrames` in `GET /api/rc`)
- a single `lost_frame` bit does not, by itself, trigger a drive failsafe
- the actual failsafe transition still depends on the watchdog timeout or the
  receiver-reported hardware failsafe bit

## Layer 3 - Web drive command timeout

- Source: body firmware timeout
- Implementation: `src/tasks/drive.cpp`
- Trigger: last drive command came from `SRC_WEB_API` and is older than
  `cfg_webDriveTimeoutMs` (default `WEB_DRIVE_TIMEOUT_MS = 500`)
- Result: `webDriveExpired=true`, `driveSpeed=0`, `driveSteer=0`,
  `failsafeSource=FS_WEB_TIMEOUT`

Web control is intentionally dead-man style. A client must keep refreshing the
command; silence is treated as a stop condition.

## Layer 4 - ESP32 Watchdog Timer

- Source: ESP32 hardware watchdog (task watchdog, interrupt watchdog, or RTC watchdog)
- Implementation: `src/main.cpp`, `src/tasks/drive.cpp`
- Trigger: `DriveTask` stops reaching `esp_task_wdt_reset()` within
  `WATCHDOG_TIMEOUT_S` (3 s), or any other watchdog reset (interrupt WDT,
  RTC WDT, super WDT) that defeats the panic handler
- Result: ESP32 resets; next boot detects any watchdog reset reason
  (`ESP_RST_TASK_WDT`, `ESP_RST_INT_WDT`, or `ESP_RST_WDT`), sets
  `estop=true`, and records `failsafeSource=FS_WATCHDOG_RESET`. See ADR 0031.

This covers firmware hangs in the real-time drive loop and other watchdog
failures. The robot does not resume movement automatically after a watchdog
reboot. All watchdog reset types arm estop — not only the task watchdog —
because a watchdog firing indicates the firmware was in a crash state; the
distinction between which watchdog fired is less important than knowing that
something was wrong.

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
- clear paths: `POST /api/estop/clear` and `POST /api/manual-command` with
  `command=clear_estop`
- `estop` does not auto-clear when RC or web input returns

This prevents accidental restart after a serious safety event.

## Boot safety defaults

The system boots with conservative defaults:

- `sbusSignalLost = true` on boot; SBUS modes clear it after valid drive-receiver traffic is seen
- watchdog-reset reboot sets `estop = true`
- `cfg_speedLimitMax`, `cfg_sbusTimeoutMs`, and `cfg_webDriveTimeoutMs` are
  loaded into `RobotState` before tasks start

## Implementation notes

- Drive SBUS: GPIO 15 via the custom RMT decoder
- Dome SBUS: GPIO 13 via the custom RMT decoder when `dual_sbus` mode is selected
- Standard PWM: CH1-CH6 can be used directly when `standard_pwm` mode is selected
- SBUS digital channels CH17 and CH18 are captured for diagnostics/mapping and can
  be bound to trigger-style actions through the persisted RC mapping profile
- Hoverboard: UART1 on GPIO 16/17
- `SafetyMonitorTask` is observer-only; it logs failsafe transitions but does
  not command the motors directly

## Real-Time / Core Pinning Contract

protoArtoo runs on dual-core processors (ESP32 classic or ESP32-P4). Real-time
drive control and SBUS input processing are pinned to Core 1 to avoid
contention with WiFi, web API, and housekeeping tasks.

**Core 1 (Real-Time Control Loop - 50 Hz drive frame rate):**
- All tasks in this section must not allocate memory after startup.
- Priorities are relative within Core 1; lower priority tasks yield to higher.

| Task | Priority | Stack | Chip-Specific? | Rationale |
|------|----------|-------|---|---|
| **DriveTask** | 5 | 4096 B | No | 50 Hz hoverboard frame transmission + TWDT reset. Core-critical. Runs every 20 ms. Must complete within period or hoverboard coasts. |
| **RCInputTask** | 5 | 7168 B | No | ~200 Hz RC poll (SBUS or PWM). Decodes frames and routes to failsafe/arbiter. Core-critical. |
| **ServoTask** | 4 | 4096 B | No | 50 Hz servo/ESC PWM updates for arms and dome ESC. Processes queue without blocking. |
| **DomeTask** | 4 | 3072 B | No | 50 Hz dome ESC command application. Processes queue, applies speed presets, respects estop. |
| **DomeLinkTask** | 3 | 6144 B | No | Bidirectional UART2 to dome controller (AstroPixelsPlus). Coordinates transport arbiter (UART vs WiFi fallback). Non-blocking I/O. |

**Core 0 (Housekeeping, Web, OTA):**
- Non-real-time tasks that handle WiFi, HTTP, SSE, OTA, audio, and logging.
- May allocate and free memory per-request.
- Do not block Core 1 RT loops.

| Task | Priority | Stack | Chip-Specific? | Rationale |
|------|----------|-------|---|---|
| **AudioTask** | 3 | 6144 B | No | Software bit-bang TX to audio module (blocking ~6 ms per command). Kept off Core 1 to isolate timing jitter (ADR 0004). Conditional on enable_s2_sound. |
| **SequenceDispatcherTask** | 3 | 4096 B | No | 10 ms body-side DM:* coordinator. Routes to queues without holding Core 1. |
| **AuxLedTask** | 2 | 4096 B | No | WS2812B effects. Independent of Core 1. Conditional on presence of LED channels. |
| **SafetyMonitorTask** | 2 | 3072 B | No | 10 Hz audit loop. Logs failsafe transitions and heap diagnostics. Low priority observer. |
| **WebEvents** | 1 | 6144 B | No | SSE event-stream manager. Broadcasts status to connected clients. Background task. |
| **ArduinoOTA** | 1 | 4096 B | No | OTA firmware/filesystem updates. Started from WiFi event callback, runs in background. |

**Pinning Mechanism Validity on ESP32-P4:**
- Dual-core verified: `SOC_CPU_CORES_NUM = 2U` (components/soc/esp32p4/include/soc/soc_caps.h:179)
- `xTaskCreatePinnedToCore()` signature and semantics identical on P4 RISC-V
  (components/freertos/esp_additions/include/freertos/idf_additions.h)
- `CONFIG_FREERTOS_UNICORE` not set (dual-core SMP enabled by default)
- Core IDs (0, 1) are valid on both classic ESP32 and ESP32-P4 RISC-V
- TWDT configuration (`esp_task_wdt_config_t`) unchanged on P4 (components/esp_system/include/esp_task_wdt.h:22-25)

**What Breaks If A Task Moves:**
- Move **DriveTask** off Core 1: WiFi ISRs on Core 0 may preempt the 50 Hz loop, causing frame continuity loss. Safety invariant violated.
- Move **RCInputTask** off Core 1: RC input processing and failsafe response add unpredictable latency; SBUS watchdog may fire spuriously. RC control becomes unreliable.
- Move **DomeLinkTask** off Core 1 and into Core 0: UART2 bidirectional traffic competes with SSE broadcasts and web handlers; transport arbiter decisions may stall. Dome synchronization degrades.
- Move **AudioTask** to Core 1: 6 ms blocking bit-bang TX stalls drive frames and RC input at 50 Hz. A single audio command can miss an entire drive frame cycle. Safety invariant violated.
- Move **WebEvents** to Core 1: SSE broadcasts and JSON serialization consume Core 1 CPU, competing with real-time loops.

**Chip-Independence:**
All task placement is **chip-independent**. The Core 0/1 split and priority ordering
transfer unchanged from classic ESP32 to ESP32-P4 and other dual-core variants. The
mechanism (`xTaskCreatePinnedToCore`) is part of the FreeRTOS SMP API, not
board-specific hardware.
