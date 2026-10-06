# Failsafe System

protoArtoo tracks five Failsafe Layers for drive control. Each one can hold the
droid out of drive on its own, and the droid stays out of drive until every
active layer has cleared (`include/failsafe_gate.h`). The design goal is
simple: loss of control input, stalled firmware, or an operator stop must all
converge on zero drive output.

The two radio layers apply only while a Radio Controller is fitted. With none
fitted (`not_fitted`), they stand down and the feet are held by the web drive
timeout and the Latching Estop, so a web-only droid can drive.

## Table of Contents

- [Layer 1 - SBUS receiver hardware failsafe](#layer-1---sbus-receiver-hardware-failsafe)
- [Layer 2 - SBUS software watchdog](#layer-2---sbus-software-watchdog)
- [Layer 3 - Web drive command timeout](#layer-3---web-drive-command-timeout)
- [Layer 4 - Watchdog reset](#layer-4---watchdog-reset)
- [Layer 5 - Latching estop](#layer-5---latching-estop)
- [Foot Drive backstop - hoverboard UART timeout](#foot-drive-backstop---hoverboard-uart-timeout)
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
- Trigger: no valid drive-receiver frame for more than the `sbusTimeoutMs`
  Setting (`rc.sbusTimeoutMs`, 50-5000 ms, default `SBUS_TIMEOUT_MS = 200 ms`)
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

The drive watchdog follows the receiver that carries drive: SBUS #1, or SBUS #2
when the drive channels are routed to it (`include/rc_input_step.h`). A
separate dome-spin receiver has its own watchdog: losing it records
`FS_SBUS2_TIMEOUT` and stops the dome while drive continues
(`include/robot_state.h:35`, `src/tasks/rc_input.cpp`). That is not a drive
layer.

## Layer 3 - Web drive command timeout

- Source: body firmware timeout
- Implementation: `src/drive_arbiter.cpp` detects it; `src/tasks/drive.cpp`
  syncs it into the failsafe gate once per 50 Hz tick
- Trigger: last drive command came from `SRC_WEB_API` and is older than the
  `webDriveTimeoutMs` Setting (`drive.webDriveTimeoutMs`, 100-5000 ms, default
  `WEB_DRIVE_TIMEOUT_MS = 500`)
- Result: `webDriveExpired=true`, `driveSpeed=0`, `driveSteer=0`,
  `failsafeSource=FS_WEB_TIMEOUT`
- Cleared by: any newer drive command. A browser drive command renews it; an RC
  radio command ends it, so the radio drives again within one 50 Hz drive tick
  of sending a frame (the tick the frame arrives still reads the gate layer the
  previous tick set). On a tie the radio counts as newer.

Web control is intentionally dead-man style. A client must keep refreshing the
command; silence is treated as a stop condition. The hold belongs to the
browser's own command: once the RC radio has sent anything newer, a stale browser
command no longer stops the feet. Releasing the browser's drive button and
turning browser control off both send a zero browser command, so each starts the
timeout like any other.

## Layer 4 - Watchdog reset

- Source: the chip's watchdogs (task watchdog, interrupt watchdog, or RTC watchdog)
- Implementation: `src/tasks/drive.cpp` feeds the task watchdog; the boot
  decision is `src/failsafe_boot_twdt.cpp`, called from `src/main.cpp:516`
- Trigger: `DriveTask` stops reaching `esp_task_wdt_reset()` within
  `WATCHDOG_TIMEOUT_S` (3 s), or any other watchdog reset (interrupt WDT,
  RTC WDT, super WDT) that defeats the panic handler
- Result: ESP32 resets; next boot detects any watchdog reset reason
  (`ESP_RST_TASK_WDT`, `ESP_RST_INT_WDT`, or `ESP_RST_WDT`), sets
  `estop=true`, and records `failsafeSource=FS_WATCHDOG_RESET`. See ADR 0031.

This covers firmware hangs in the real-time drive loop and other watchdog
failures. The robot does not resume movement automatically after a watchdog
reboot. All watchdog reset types arm estop - not only the task watchdog -
because a watchdog firing indicates the firmware was in a crash state; the
distinction between which watchdog fired is less important than knowing that
something was wrong.

## Layer 5 - Latching estop

- Source: the operator
- Implementation: `src/failsafe_gate.cpp` (`failsafeClearEstop()` is the only
  path that clears it)
- Trigger: `POST /api/estop`, or the STOP button on the top bar
- Result: `estop=true`, `failsafeSource=FS_ESTOP_CMD`
- Cleared by: the top-bar estop button's release, `POST /api/estop/clear`,
  `POST /api/manual-command` with `command=clear_estop`, or the Console's
  `system.action.estop-clear`

Emergency stop is separate from the automatic timeouts above. `estop` does not
auto-clear when RC or web input returns. This prevents accidental restart after
a serious safety event.

## Foot Drive backstop - hoverboard UART timeout

This is not a Failsafe Layer the firmware tracks. It belongs to one Foot Drive,
the hoverboard with hacked firmware, and runs outside protoArtoo:

- Source: hoverboard motor controller firmware
- Trigger: hoverboard stops receiving valid UART frames for roughly 500 ms
- Result: hoverboard firmware stops the motors independently of the Body
  Controller

protoArtoo supports it by following the zero-frame rule: it never goes silent
intentionally. Even when stopped, it keeps transmitting zero commands.

## Boot safety defaults

The system boots with conservative defaults:

- when the receiver mode reads drive from SBUS, the SBUS watchdog layer is set
  at boot (`sbusSignalLost = true`) and clears after valid drive-receiver
  traffic is seen (`src/main.cpp:509-511`); with no Radio Controller fitted
  there is no radio layer to set
- watchdog-reset reboot sets `estop = true`
- the persisted Settings (`speedLimitMax`, `sbusTimeoutMs`,
  `webDriveTimeoutMs`) are loaded into the config cache before tasks start;
  tasks read them as a `ConfigSnapshot` through `configCacheRead()`
  (`src/main.cpp:89-91`, `src/drive_arbiter.cpp:147`)

## Implementation notes

Pins per Body Controller (`include/config.h`, full list in
[pin_map.md](pin_map.md)):

| Signal | Artoo PCB (artoo-esp32) | FireBeetle 2 (firebeetle2) |
|---|---|---|
| Drive SBUS (SBUS #1, RMT decoder) | GPIO 15 | GPIO 28 |
| Dome SBUS (SBUS #2, RMT decoder, `dual_sbus`) | GPIO 13 | GPIO 29 |
| Foot Drive UART1 TX / RX | GPIO 16 / 17 | GPIO 20 / 21 |

- Standard PWM: CH1-CH6 can be used directly when `standard_pwm` mode is selected
- SBUS digital channels CH17 and CH18 are captured for diagnostics/mapping and can
  be bound to trigger-style actions through the persisted RC mapping profile
- `SafetyMonitorTask` is observer-only; it logs failsafe transitions but does
  not command the motors directly

## Real-Time / Core Pinning Contract

protoArtoo runs on dual-core processors (ESP32 classic or ESP32-P4). Real-time
drive control and SBUS input processing are pinned to Core 1 to avoid
contention with WiFi, web API, and housekeeping tasks.

Stack sizes differ per chip. The tables below give the artoo-esp32 (ESP32)
value and the firebeetle2 (ESP32-P4) value. The measured call chain each one is
sized from lives in `tools/task_stack_recipes.json`, generated into
`include/task_stack_figures.h` (ADR 0040).

**Core 1 (Real-Time Control Loop - 50 Hz drive frame rate):**
- All tasks in this section must not allocate memory after startup.
- Priorities are relative within Core 1; lower priority tasks yield to higher.

| Task | Priority | Stack (ESP32 / ESP32-P4) | Rationale |
|------|----------|-------|---|
| **DriveTask** | 5 | 5632 / 6656 B | 50 Hz Foot Drive frame transmission + TWDT reset. Core-critical. Runs every 20 ms. Must complete within period or the hoverboard coasts. |
| **RCInputTask** | 5 | 6656 / 7168 B | ~200 Hz RC poll (SBUS or PWM). Decodes frames and routes to failsafe/arbiter. Core-critical. Not created when the receiver mode reads no input (`elrs`, `not_fitted`). |
| **ServoTask** | 4 | 4096 / 5120 B | 50 Hz servo/ESC PWM updates for arms and dome ESC. Processes queue without blocking. |
| **DomeTask** | 4 | 4096 / 5632 B | 50 Hz dome ESC command application. Processes queue, applies speed presets, respects estop. Not created when the Dome ESC is off at boot. |
| **DomeLinkTask** | 3 | 6144 / 10240 B | protoR2link to the Dome Controller (AstroPixelsPlus) over UART2. Runs the protoR2link Arbiter (UART slip ring vs WiFi fallback). Non-blocking I/O. |

**Core 0 (Housekeeping, Web, OTA):**
- Non-real-time tasks that handle WiFi, HTTP, SSE, OTA, audio, and logging.
- May allocate and free memory per-request.
- Do not block Core 1 RT loops.

| Task | Priority | Stack (ESP32 / ESP32-P4) | Rationale |
|------|----------|-------|---|
| **Pca9685Task** | 6 | 3584 / 5120 B | Sends PCA9685 servo frames over I2C, so a release is never queued behind a page load. Not on the task watchdog (`src/drivers/pca9685.cpp:287`). |
| **AudioTask** | 3 | 6144 / 9216 B | Commands to the sound module. On artoo-esp32 TX is a software bit-bang (blocking ~6 ms per command); on firebeetle2 the module has its own hardware UART. Kept off Core 1 to avoid timing interaction with DriveTask/ServoTask (`src/main.cpp:598-607`). Not created when audio is off at boot. |
| **SequenceDispatcherTask** | 3 | 5120 / 7680 B | 10 ms body-side DM:* coordinator. Routes to queues without holding Core 1 (ADR 0004). |
| **AuxLedTask** | 2 | 4096 / 7168 B | WS2812B effects. Independent of Core 1. Conditional on presence of LED channels. |
| **SafetyMonitorTask** | 2 | 4608 / 5120 B | 10 Hz audit loop. Logs failsafe transitions and heap diagnostics. Low priority observer. |
| **Console** | 2 | 11264 / 14848 B | Controller Console serial adapter (ADR 0036). Needs no network. |
| **ReactionTask** | 2 | 5632 / 6656 B | 20 Hz. Fires the Reactions bound to the droid's own conditions (ADR 0053). Reads the resolved drive output from `RobotState`; adds nothing to DriveTask and is not on the task watchdog. |
| **HostedRecovery** | 2 | - / 6144 B | ESP32-P4 only: recovers the link to the WiFi Module (`src/web/web_network_manager_hosted.cpp:965`). |
| **WebEvents** | 1 | 6144 / 9216 B | SSE event-stream manager. Broadcasts status to connected clients. Background task. |
| **ArduinoOTA** | 1 | 4096 / 8192 B | OTA firmware/filesystem updates. Started from WiFi event callback, runs in background. |

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
- Move **AudioTask** to Core 1: on artoo-esp32, 6 ms blocking bit-bang TX stalls drive frames and RC input at 50 Hz. A single audio command can miss an entire drive frame cycle. Safety invariant violated.
- Move **WebEvents** to Core 1: SSE broadcasts and JSON serialization consume Core 1 CPU, competing with real-time loops.

**Chip-Independence:**
All task placement is **chip-independent**. The Core 0/1 split and priority ordering
transfer unchanged from classic ESP32 to ESP32-P4 and other dual-core variants. The
mechanism (`xTaskCreatePinnedToCore`) is part of the FreeRTOS SMP API, not
board-specific hardware.
