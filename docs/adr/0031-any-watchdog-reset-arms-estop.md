# Any watchdog reset arms estop at boot, not only the task watchdog

A droid that has just been reset by a watchdog was, moments earlier, not
servicing something it promised to service. Until an operator has looked at it,
it must not drive. That rule is a safety guardrail in `AGENTS.md`, it is
`FailsafeLayer::WATCHDOG_RESET` in the failsafe gate, and its whole value is
that it holds for the crash you did not anticipate.

The implementation held for one crash shape only:

```cpp
return resetReason == ESP_RST_TASK_WDT;
```

That matches the Task Watchdog and nothing else. `ESP_RST_INT_WDT` — an
interrupt service routine that blocked long enough to trip the interrupt
watchdog — has never armed estop on any board, and an ISR that stops returning
is not a gentler failure than a task that stops feeding its watchdog.

Porting to the ESP32-P4 turned that narrowness from a gap into a hole. The two
chips do not describe watchdog resets the same way. On the classic ESP32,
`esp_system/port/soc/esp32/reset_reason.c` gives the task watchdog its own
reason code:

```c
case RESET_REASON_CORE_MWDT0:  return ESP_RST_TASK_WDT;
case RESET_REASON_CORE_MWDT1:  return ESP_RST_INT_WDT;
```

The ESP32-P4 cannot, and the IDF says so in its own comment:

```c
case RESET_REASON_CPU_MWDT:  case RESET_REASON_CPU_RWDT:
case RESET_REASON_SYS_SUPER_WDT:  case RESET_REASON_SYS_RWDT:
case RESET_REASON_CORE_MWDT:  case RESET_REASON_CORE_RWDT:
    /* Code is the same for INT vs Task WDT */
    return ESP_RST_WDT;
```

No `RESET_REASON_*` on the P4 maps to `ESP_RST_TASK_WDT`. The only path that
produces that value is the panic handler storing a hint in RTC memory before it
resets. Because `esp_task_wdt_init()` is configured with `.trigger_panic = true`,
the ordinary timeout does run the panic handler, so the common case reports
`ESP_RST_TASK_WDT` and estop arms. The failures that do not are precisely the
ones a watchdog exists to catch: the panic handler itself faulting or hanging,
the RTC or super watchdog firing as the backstop, an MWDT expiring on a path that
never reaches the panic handler. Those arrive as `ESP_RST_WDT`, the comparison
returns false, and the droid boots ready to drive.

**We decided that any watchdog reset arms estop at boot — `ESP_RST_TASK_WDT`,
`ESP_RST_INT_WDT`, and `ESP_RST_WDT` alike — on every board.**

This deliberately changes shipped artoo-esp32 behaviour: an RTC-watchdog reset
now arms estop where previously it did not. We are treating that as a defect
being fixed rather than a regression being introduced. The asymmetric
alternative — broadening only on the P4 — was rejected outright. A safety rule
that means different things on different boards is a rule nobody can reason
about, and this one would have differed in the direction of the newer, less
proven target.

The trade is not close. Arming when we did not strictly need to costs one
`POST /api/estop/clear` by an operator who is already standing at a droid that
just rebooted. Failing to arm costs an unattended droid moving after a crash
severe enough to defeat its own panic handler. Where the failure mode is
physical, we take the conservative reading — the same call this project made
when a vendor diagram invited five volts onto a pad marked `NC`.

`FailsafeLayer::TWDT_RESET` is renamed to `FailsafeLayer::WATCHDOG_RESET`, and
`bootTwdtResetDecision()` to `bootWatchdogResetDecision()`. The old names are
how this happened: the enum's own comment already read *"watchdog-reset boot
recovery"* while the code beneath it matched a single task-watchdog constant, and
nobody reading `TWDT_RESET` had reason to suspect the mismatch. Neither name is
exposed in the HTTP API or the web UI — `/api/status` reports `estop`,
`sbusSignalLost` and `sbusHwFailsafe`, never the layer enum — so the rename costs
nothing observable and the compiler finds every site.

The decision function stays pure and host-testable per ADR 0005: it takes an
`esp_reset_reason_t` and returns a bool, so the reset classes that must arm estop
are pinned by native tests rather than by a device that has to be crashed on
purpose.

## Consequences

- On both targets, an interrupt-watchdog reset now arms estop. It never did
  before, on any board.
- On artoo-esp32, an RTC-watchdog reset now arms estop. Operators may see estop
  latched after crash classes that previously booted clean, and the recovery is
  unchanged: `POST /api/estop/clear`.
- `docs/failsafe.md` must stop naming `ESP_RST_TASK_WDT` as the trigger.
- Reset classes that are *not* watchdogs — brownout, external reset, software
  reset, deep-sleep wake — are unchanged and still do not arm estop. This
  decision is about watchdogs, not about every abnormal boot.
