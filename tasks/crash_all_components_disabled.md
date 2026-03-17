# Crash: All Components Disabled Boot (Xtensa alloca exception)

**Status:** Resolved (bench-tested) — no reboot loop after DomeTask LEDC guard fix
**Branch:** `phase/v0.4.0`
**Observed:** 2026-03-17

---

## Symptom

Device crashes on every boot when all hardware component toggles are OFF (the new
default-off behaviour introduced in this session). The device enters a reboot loop.

Serial output before crash:

```
[I][main] protoArtoo boot begin
[I][main] reset_reason=POWERON (1)
[I][main] config speed_limit_max=600 ...
[I][SERVO] all arm/aux outputs disabled (en_arm1/2/aux1-3=false) — servos idle
[I][DOME] dome disabled (en_dome=false) — skipping ESC arming
[I][DriveTask] hoverboard disabled (en_s1=false) — t   ← message truncated mid-send
Guru Meditation Error: Core 1 panic'ed (Double exception).
```

The DriveTask log message is cut off mid-word ("— t" instead of "— task idle"),
which may indicate the crash is in a different Core 1 task while DriveTask's UART
TX buffer was still draining (or it is truly in DriveTask's PA_LOG_INFO call).

---

## Crash Register Dump

```
PC      : 0x4008d2ae   (panic handler)
A1      : 0x3ffb56f0   (stack pointer — same every crash)
EXCVADDR: 0xffffffe0   (= -32; invalid access near address 0)
EXCCAUSE: 0x00000002   (instruction fetch / load-store fault)
```

**Backtrace decoded:**

| Address | Symbol |
|---------|--------|
| `0x4008d2ab` | `_xt_context_save` — Xtensa CPU context save (repeated = double exception) |
| `0x4008dcb4` | `xQueueCreateMutex` (FreeRTOS `queue.c:564`) |
| `0x400921a9` | `vPortEnterCritical` / `multi_heap_internal_lock` (heap.c:152) |
| `0x4008007d` | `_xt_alloca_exc` (Xtensa alloca exception handler, xtensa_vectors.S:1799) |

**Key indicator:** `_xt_alloca_exc` is the Xtensa hardware stack overflow handler.
It fires when a function prologue (ENTRY instruction) would push the stack pointer
below the stack bottom. This is a true stack overflow, not a null pointer dereference.

The subsequent `xQueueCreateMutex` → `multi_heap_internal_lock` frames are from
the `_xt_alloca_exc` handler itself trying to save context/allocate, which then
overflows again → "Double exception".

---

## What Changed (This Session)

Three independent changes were made before the crash appeared. Any one of them
(or a combination) is the likely cause.

### Change 1 — All component defaults set to OFF (`main.cpp`)

```cpp
// Before:
robotState.cfg_enable_arm1 = prefs.getBool("en_arm1", true);
robotState.cfg_enable_s1_hoverboard = prefs.getBool("en_s1", true);
// etc.

// After:
robotState.cfg_enable_arm1 = prefs.getBool("en_arm1", false);  // all false
robotState.cfg_enable_s1_hoverboard = prefs.getBool("en_s1", false);
```

This triggers all the "disabled" idle paths simultaneously on first boot.

### Change 2 — DriveTask: TWDT-safe idle path added

```cpp
void driveTask(void* pvParameters) {
    esp_task_wdt_add(NULL);          // was already first — unchanged
    {
        taskENTER_CRITICAL(&robotStateMux);
        bool enabled = robotState.cfg_enable_s1_hoverboard;
        taskEXIT_CRITICAL(&robotStateMux);
        if (!enabled) {
            PA_LOG_INFO(TAG, "hoverboard disabled (en_s1=false) — task idle");  // NEW
            for (;;) {
                esp_task_wdt_reset();
                vTaskDelay(pdMS_TO_TICKS(1000 / DRIVE_FREQ_HZ));
            }
        }
    }
    hoverSerial.begin(...);   // only reached when enabled
```

Previously DriveTask ALWAYS called `hoverSerial.begin()` + PA_LOG_INFO before
any enable check. The new idle path calls `PA_LOG_INFO` WITHOUT first calling
`hoverSerial.begin()`.

### Change 3 — SBUSInputTask renamed to RcInputTask + TWDT moved to first call

`esp_task_wdt_add(NULL)` was moved from AFTER the UART open to BEFORE it:

```cpp
void rcInputTask(void* pvParameters) {
    esp_task_wdt_add(NULL);       // MOVED HERE — was after sbus_drive.begin()

    taskENTER_CRITICAL(&robotStateMux);
    RcInputMode rcInputMode = robotState.cfg_rc_input_mode;
    bool enableRcCh1 = robotState.cfg_enable_rc_ch1;  // now false
    bool enableRcCh2 = robotState.cfg_enable_rc_ch2;  // now false
    taskEXIT_CRITICAL(&robotStateMux);

    bool driveSbusEnabled = is_drive_sbus_mode(rcInputMode) && enableRcCh1; // false
    bool domeSbusEnabled  = is_dome_sbus_mode(rcInputMode)  && enableRcCh2; // false
    // → neither UART opened
    // → PA_LOG_INFO "both receivers disabled — idle"
    // → falls into main task loop
```

### Change 4 — ServoTask: LEDC no longer initialized when all outputs disabled

`ledcPwmInit()` was moved from unconditional in `servoTaskInit()` to conditional
in `main.cpp`:

```cpp
bool anyLedc = cfg_enable_arm1 || cfg_enable_arm2 || cfg_enable_aux1 ||
               cfg_enable_aux2 || cfg_enable_aux3 || cfg_enable_dome;
if (anyLedc) {
    ledcPwmInit();
    ledcPwmInitNeutralPositions();
}
```

When ALL are false (new default), `ledcPwmInit()` is skipped entirely. The ESP-IDF
LEDC driver is never initialized. `ServoTask` was also given a new idle guard to
prevent calling `ledcPwmSetPulseWidth()` on an uninitialized driver.

---

## Hypotheses (In Priority Order)

### H1 — rcInputTask main loop runs without UART init, hits large stack frame ⚠️ LIKELY

When both RC channels are disabled (`driveSbusEnabled=false`, `domeSbusEnabled=false`)
the task still falls through to the `while(true)` loop. On the first iteration it
tries to call `PA_LOG_INFO(TAG, "stack HWM: %u words free", ...)`. The `%u` format
specifier in vsnprintf... but more importantly:

In `dual_sbus` mode with both channels disabled, the task loop reaches the SBUS
watchdog / Layer 2 safety code which has local variables including `RcRuntimeConfig cfg`
(estimated ~126 bytes). Combined with the full call stack depth of the loop entry,
this might exceed the 4096-byte stack.

**To test:** Add an idle guard in rcInputTask matching the DriveTask pattern —
if both SBUS channels are disabled AND mode is SBUS, enter a TWDT-feeding idle
loop before the `while(true)`:

```cpp
if (!driveSbusEnabled && !domeSbusEnabled && rcInputMode != RC_INPUT_STANDARD_PWM) {
    PA_LOG_INFO(TAG, "all SBUS channels disabled — task idle");
    for (;;) { esp_task_wdt_reset(); vTaskDelay(pdMS_TO_TICKS(5)); }
}
```

### H2 — DriveTask PA_LOG_INFO without prior hoverSerial.begin() causes crash ⚠️ POSSIBLE

Previously, `hoverSerial.begin()` (initialising UART1 / Serial1) was always called
before any `PA_LOG_INFO` in DriveTask. The HardwareSerial init creates internal
FreeRTOS queues/semaphores on the heap. When this is skipped (disabled path),
`Serial.printf` (UART0) may encounter an uninitialised stdio state that tries to
create a mutex internally (`xQueueCreateMutex` in the backtrace).

**To test:** Replace `PA_LOG_INFO` in the DriveTask idle path with `Serial.println`
directly (bypassing the macro) and see if the crash persists.

### H3 — LEDC driver uninitialized, ServoTask idle guard races before DomeTask ⚠️ LESS LIKELY

The ServoTask idle check was added but the DomeTask also uses LEDC. If there's a
race between task starts where ServoTask runs before the idle guard fires and
touches LEDC... but the ServoTask loop requires `xQueueReceive` which only processes
items if available, so it shouldn't touch LEDC when idle.

---

## Stack Layout Reference

All Core 1 real-time tasks at time of crash:

| Task | Stack | Status |
|------|-------|--------|
| DriveTask | 4096 | Idle path (new code path) |
| RCInputTask | 4096 | New name; TWDT moved to first call |
| ServoTask | 3072 | New idle guard added |
| DomeTask | 2048 | New disable-check in init |
| DomeLinkTask | 3072 | Existing disable path (working) |

---

## Files Changed (Unstaged / Uncommitted at Time of Writing)

```
src/tasks/drive.cpp          — DriveTask idle guard (Change 2)
src/tasks/rc_input.cpp       — renamed from sbus_input.cpp; TWDT moved (Change 3)
src/tasks/servo_task.cpp     — ServoTask idle guard + LEDC moved (Change 4)
src/tasks/dome_task.cpp      — DomeTaskInit disable check (working)
src/main.cpp                 — all defaults → false (Change 1); LEDC conditional
include/rc_input.h           — new header (renamed)
include/sbus_input.h         — deprecated stub forwarding to rc_input.h
data/setup.html              — all toggle labels updated to PCB silkscreen names
```

---

## Recommended Next Step

1. Flash this patch to ESP32 over USB (`/dev/ttyUSB0`) and cold boot with all toggles OFF.
2. Verify there is no reboot loop and no `Guru Meditation` during first 30s.
3. Capture serial evidence using `python3 tools/serial_monitor.py --duration 30`.
4. If stable, keep this mitigation and continue root-cause narrowing only if needed.

---

## Context: Why These Changes Were Made

These changes are part of a broader architectural fix to ensure that FreeRTOS tasks
respect component enable toggles from the Setup page. Previously, tasks initialised
hardware (UART, LEDC PWM) unconditionally regardless of whether that component was
enabled in config. The pattern now is:

- TWDT registration: always, first call in every task
- Hardware init (UART open, LEDC init): only when the relevant component is enabled
- If disabled: log once, enter `for(;;) { esp_task_wdt_reset(); vTaskDelay(); }`

DomeLinkTask and AudioTask already followed this pattern correctly.
DriveTask, ServoTask, DomeTask, and RCInputTask were fixed in this session,
but the fix triggered the crash described above.

---

## Work Log (This Document)

### 2026-03-17 — GPT-5.3-Codex session

#### Scope

- Diagnose severe boot crash with all components disabled.
- Apply smallest safe mitigation in task idle paths.
- Track all actions and verification in this same document.

#### Changes Applied

1. `src/tasks/rc_input.cpp`
- Added an early SBUS-mode idle guard in `rcInputTask()`:
    - Condition: `rcInputMode != RC_INPUT_STANDARD_PWM && !driveSbusEnabled && !domeSbusEnabled`
    - Behavior: feed TWDT and `vTaskDelay(5 ms)` forever.
- This prevents falling into the full SBUS loop when both receivers are disabled.

2. `src/tasks/drive.cpp`
- Removed `PA_LOG_INFO` in the disabled idle branch of `driveTask()`.
- Disabled branch now immediately enters TWDT-fed idle loop.
- Rationale: avoid stack-heavy formatted logging on early Core 1 disabled startup path.

#### Verification Run

- Build: `pio run -e protoArtoo` -> `SUCCESS`
- Native tests: `pio test -e native` -> `EXIT:0`
- Native summary: `431 test cases: 431 succeeded`

#### Current Assessment

- H1 mitigation is now implemented.
- H2 mitigation is partially implemented by removing the DriveTask disabled-path formatted log.
- Crash fix is **not yet hardware-confirmed** in this session.

#### Pending Hardware Validation

1. `pio run -e protoArtoo --target upload --upload-port /dev/ttyUSB0`
2. Boot with all toggles OFF (fresh power cycle).
3. `python3 tools/serial_monitor.py --duration 30`
4. Confirm no reboot loop and no Core 1 panic.

---

## Root Cause Confirmed

`domeTask()` called `setDomeNeutral()` unconditionally at task startup, even when
`cfg_enable_dome=false` and LEDC initialization was intentionally skipped
(all outputs disabled).

- Call path: `domeTask()` -> `setDomeNeutral()` -> `ledcPwmSetPulseWidth()`
- Failure mode: LEDC driver access in a disabled/no-LEDC-init boot configuration
  during early Core 1 task startup.

### Final Fix Applied

File: `src/tasks/dome_task.cpp`

- Added boot-time guard in `domeTask()`:
  - Read `cfg_enable_dome` under `robotStateMux`
  - Call `setDomeNeutral()` only when dome output is enabled

This preserves the disabled-state contract: no hardware access when component is
disabled.

### Hardware Validation Evidence (2026-03-17)

1. Uploaded via USB:
    - `pio run -e protoArtoo --target upload --upload-port /dev/ttyUSB0`
    - Result: success

2. Captured 30s boot log:
    - `/bin/python tools/serial_monitor.py --port /dev/ttyUSB0 --duration 30`

3. Observed stable boot (no panic loop):
    - `[I][main] reset_reason=POWERON (1)`
    - `[I][DOME] stack HWM: 1284 words free`
    - `[I][DomeLink] dome serial disabled (en_s3=false) — task idle`
    - `[I][main] init complete`
    - `[I][WebServer] ArduinoOTA ready on port 3232`

4. No `Guru Meditation`, no `Double exception`, no reboot loop within capture window.
