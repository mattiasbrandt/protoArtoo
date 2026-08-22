# Troubleshooting — crash/coredump + heap (agents + operators)

Procedures for diagnosing body-controller crashes (`resetReason=PANIC`), heap
exhaustion, and the flashing constraints that affect how you collect evidence.
Written for both the operator and a troubleshooting agent: each section is a
concrete, ordered procedure with exact commands and decision points.

Base URL: `http://artoo.local` (or the device IP — `GET /api/wifi` → `staIp`, or
`10.0.0.22` if mDNS is flaky). All HTTP probes work on the **seated** controller;
esptool flash/write operations do **not**. USB serial monitoring remains readable
with the reset caveat below.

---

## 1. A crash happened (`resetReason=PANIC`) — get the backtrace

The firmware saves an **ELF coredump to flash** on a PANIC (abort/exception/WDT),
retrievable over HTTP. This works seated (no USB needed).

```bash
# 1. Is there a coredump waiting?
curl -s http://artoo.local/api/coredump/status            # {"present":true,"size":...}

# 2. Fetch it (raw coredump-partition image).
curl -s http://artoo.local/api/coredump -o coredump.elf

# 3. Decode. Put a WORKING xtensa GDB on PATH first (see gotcha below), then:
GDB_DIR="$HOME/.platformio/packages/tool-xtensa-esp-elf-gdb/bin"
PATH="$GDB_DIR:$PATH" ~/.platformio/penv/bin/esp-coredump \
  --chip esp32 \
  info_corefile --core coredump.elf --core-format raw \
  .pio/build/artoo_esp32_chirp/firmware.elf

# 4. After analysing, clear it so the NEXT crash is captured.
curl -s -X POST http://artoo.local/api/coredump/erase
```

The decode prints the panic reason, the crashed task, registers, and per-task
backtraces. Endpoints: see [api.md](api.md) (System and OTA).

### Decode gotchas (tested 2026-06-19 — these cost real time)

- **`--chip esp32` is a GLOBAL option** — it goes BEFORE the `info_corefile`
  subcommand, not after. Wrong order: `esp-coredump: error: unrecognized
  arguments: --chip ...`.
- **`--core-format raw`** — the bytes from `/api/coredump` are the raw
  coredump-partition image (not a host ELF), so pass `raw`, not the default.
- **Use the modern GDB, not the old toolchain one.** esp-coredump shells out to
  `xtensa-esp32-elf-gdb`. The one in `toolchain-xtensa-esp32/` is python2.7-linked
  and dies on a modern host with `error while loading shared libraries:
  libpython2.7.so.1.0` → esp-coredump then reports a confusing `BrokenPipeError`
  in `pygdbmi` (NOT a Python-3 / pygdbmi bug — gdb just never started). The
  working one is `tool-xtensa-esp-elf-gdb/bin/xtensa-esp32-elf-gdb` (esp-gdb 16.3,
  no python2.7 dep) — put its dir first on `PATH` as shown above.
- **The `firmware.elf` MUST be the exact crash-time build** (same git hash as
  `GET /api/status` firmwareVersion / the committed `data/fw-version.json`).
  A mismatch shows as a GDB/TCB evaluation error in `print_crashed_task_info`
  (symbols/addresses don't resolve). Keep the elf for each deployed version, or
  `git checkout` that commit and rebuild.

If you cannot run GDB at all, `/api/profiler` (profiler build) reports
`lastFail.bt` (raw PCs) which you can decode statically with
`xtensa-esp32-elf-addr2line -e <firmware.elf> <pc...>`.

If `/api/coredump/status` returns `{"present":false}` after a crash: either the
crash predates the coredump partition (added 2026-06-19, issue #8), or the reset
was not a PANIC (`GET /api/status` → `resetReason`: `POWERON`/`SW`/`EXT` = clean
reset, not a crash). A clean reset has no coredump.

---

## 2. Suspected heap exhaustion / OOM

Symptom: PANIC under load, or OTA failing mid-transfer, or sluggish HTTP. Root
class on this board is **internal-heap exhaustion** → failed allocation →
(exceptions-disabled) `abort()` → PANIC. See issue #8 and
[tasks/heap-exhaustion-and-flash-findings-2026-06-19.md].

### Quick read (any build, over HTTP)

```bash
curl -s http://artoo.local/api/status | grep -oE '"(heapFree|heapMin|heapLargest8bit|sseClients|tcpAcceptRejectHeap|tcpAcceptRejectRate|resetReason)":[^,}]*'
```
- `heapMin` is the all-time low-water. A floor below ~10-20 KB is unsafe.
- `heapLargest8bit` is the number that matters: the largest allocatable DRAM
  block. Healthy rest is ~20 KB on a fresh boot and ~12-14 KB after any heavy
  connection churn (a bounded one-time warm-up, not a leak). The admission
  floors sit at 7.5-9 KB; sustained readings near them mean requests are
  being shed.
- Do NOT judge heap by `heapLargestBlock` in `/api/status`: it reads a pool
  dominated by unusable leftover IRAM and sits near 36 KB no matter what
  (kept only for backward compatibility).
- `tcpAcceptRejectHeap`/`tcpAcceptRejectRate` climbing during normal use =
  the accept guards are shedding; check what is generating connection churn.

### Deep read (profiler build)

Flash `artoo_esp32_profiler` (CHIRP + `PA_HEAP_PROFILE`; same code as
`artoo_esp32_chirp` plus instrumentation). `GET /api/profiler` adds: per-task stack
high-water marks, a failed-allocation **counter + `lastFail`** (size, caps, and a
backtrace of raw PCs — decode with `xtensa-esp32-elf-addr2line -e <firmware.elf>
<pc...>`), mode-scoped low-water snapshots (`boot`, `rc_linked`, `audio_play`),
and largest-block/frag. Watch over **minutes**, not one snapshot — `heapMin`/
`failedAllocs` evolve.

```bash
curl -s http://artoo.local/api/profiler | grep -oE '"(heapFree|heapMin|heapLargest|fragRatio|failedAllocs)":[0-9.]*'
curl -s http://artoo.local/api/profiler | grep -oE '"lastFail":\{[^]]*\]\}'   # size/caps + bt PCs
```

> **Heap-hook safety (learned the hard way, #8):** the alloc-failed hook runs IN
> the failing allocation's context, on that task's stack. It must be
> **allocation-free** — an earlier version logged via `Print::printf`, which
> mallocs; under heap exhaustion that malloc also failed and re-entered the hook,
> recursing until a task stack overflowed (decoded from a coredump: a 64-byte
> mDNS alloc on the lwIP `tiT` task — `tiT` was the victim, not the cause). The
> hook now only counts + captures raw PCs behind a reentrancy guard; the handler
> formats them. Same rule for any future heap/ISR hook: no malloc, no
> `Print::printf`, no `String`.

Method (from issue #8 / Codex review): **remove project-owned heap pressure and
attribute before tuning system/WiFi buffers.** Use the failed-alloc backtrace to
identify the *failing* caller, and the per-task/mode evidence to find the
*consumer*. `CONFIG_HEAP_TASK_TRACKING` is an attribution tool (has overhead),
not a final-margin measurement.

### Per-run sequence evidence

For a misbehaving body-owned `DM:*` run, `GET /api/seq/last-run` gives the TX
stream, cleanup emitted, net-open/touched ring masks, inferred effect scopes,
outcome+reason, retained-TX truncation, and body-local queue-full/retry counts.
It does not sample the dome controller's `/api/health.cmd_queue.queue_full_count`;
compare that value separately when diagnosing remote dome ingress drops.

---

## 3. Flashing constraint (READ before collecting USB evidence)

**The seated controller cannot be USB-flashed or have flash memory read by
esptool.** GPIO15 is `PIN_SBUS1_RX`, a strapping pin; the SBUS receiver fights
download-mode strapping and the PCB loads the EN/GPIO0 auto-reset circuit, so
esptool reports *"Download mode detected, but no sync reply / TX path seems
down"* (`tasks/lessons.md:549`). USB serial monitoring is a separate read path
and remains available as described below.

- **Seated → use OTA** (`make ota-chirp OTA_IP=...`) and **HTTP** for all evidence
  (`/api/coredump`, `/api/profiler`, `/api/logs`, `/api/status`). This is the
  normal path and why the coredump/profiler evidence is exposed over HTTP.
- **USB flash → unseat the ESP32**, then
  `make flash BUILD_ENV=artoo_esp32_chirp UPLOAD_PORT=/dev/ttyUSB0`, then reseat.
  A partition-table change (e.g. the
  coredump partition) needs this full USB flash + `uploadfs`; OTA does not rewrite
  the partition table.

### Serial monitor caveat

Opening the USB serial port toggles DTR/RTS, which **resets the ESP32** (so a
"reboot" right when you connect the monitor is self-inflicted, not a crash). USB
serial *read* works seated (RX only); only flashing needs the blocked TX/bootloader
path. Use `tools/serial_monitor.py` (holds DTR/RTS low — though on this board the
connect can still reset). `resetReason` distinguishes: `PANIC` = real crash;
`POWERON`/`EXT` = external/serial reset.

2026-06-22 regression note: this reset is no longer limited to the seated Artoo
PCB. With the ESP32 unseated and only USB connected, a second
`tools/serial_monitor.py --port /dev/ttyUSB0 --duration 10` attach still printed
the ROM boot banner (`rst:0x1 (POWERON_RESET)`). A month-plus earlier, USB serial
monitor attach worked without rebooting. Treat current USB-open resets as a
regression in the host/USB-UART/reset-line path until the change is explained.
The project monitor now defaults to a POSIX `O_NOCTTY`/`termios` backend that
does not touch DTR/RTS; use `--pyserial` only when intentionally comparing the
older pyserial path, which can toggle modem-control lines on open.

### Serial log integrity caveat

Treat USB serial as a convenience stream, not the only diagnostic record. The
project-owned `PA_LOG_*` path writes each line through one serialized sink and
retains the same line in the `/api/logs` ring/SSE console. Prefer `/api/logs`,
`/api/status`, `/api/profiler`, and `/api/coredump` for evidence that must
survive USB monitor resets or ambiguous serial captures.

Normal repo builds leave `CORE_DEBUG_LEVEL` unset and call
`Serial.setDebugOutput(false)` during setup, so Arduino core `log_*` output is
compiled out or detached from UART0. If a focused debug build enables
`CORE_DEBUG_LEVEL` or re-enables Arduino/IDF serial debug output, treat framework
serial logs as best-effort unless that build also deliberately funnels the
framework log path through the project sink. Boot ROM and panic output can still
write below the application logging layer.

This caveat was checked against the pinned pioarduino platform `55.03.37`
(arduino-esp32 `3.3.7`). Re-check Arduino core `log_printf` locking and
`HardwareSerial` debug-output behavior when changing the platform/framework pin.

---

## 4. Estop-clear dome resync (expected ring "park" — not a crash)

Clearing estop (`POST /api/estop/clear`) makes the body **resync the dome to a
known safe state**. You will hear the dome **ring panels "park" (drive closed)**,
even if they were already closed. This is by design, not a reboot or crash.

On the estop-clear edge, `src/tasks/sequence_dispatcher.cpp` emits, over the body
link:

```
#PAWU                              # wake state re-sent (sleep-sync arbiter)
@0T1  @0P1                         # logic + PSI reset (immediate, non-servo)
:CL01 :CL02 :CL03 :CL04 :CL07 :CL11 :CL13   # staggered ring close, 500 ms apart
```

Why: after an estop the dome's panel state is **unknown**, so the body assumes
closed and resyncs to a safe state (same pattern as the dome-reconnect resync,
ADR 0004 dec. 8). It is **brownout-safe by design** — individual staggered
closes only, **never a group `:CL00`/`:CL15`** (a group close drives every ring
servo at once and browns out a loaded ring — 2026-06-17 hardware finding). Pies
are never auto-closed on resync.

Verify it was the resync and not a fault: dome `/api/health` `reset_reason` stays
`POWERON`, `coredump_present=false`, and the dome RX log shows the inbound
`#PAWU`/`@0T1`/`@0P1`/`:CLnn` above with **no** group close. The dome has no
internal panel-home/park handler — panel servos move only on actual inbound
`:OP`/`:CL`/`:OF`/`:SM`/DM commands (confirmed body + dome 2026-06-29).

---

## 5. Can't reach the controller over WiFi at all

Not a crash — see [wifi-provisioning.md](wifi-provisioning.md) instead. Saved
WiFi Client Mode settings that no longer work (wrong password, network
unreachable) do **not** auto-fall-back to an AP; that would hide the real
problem. Use the documented **Network Recovery Mode** local gesture (3 rapid
power cycles) to temporarily re-open WiFi Provisioning and fix the saved
settings without erasing them.

---

## References

- API: [api.md](api.md) — `/api/coredump*`, `/api/profiler`, `/api/status`, `/api/logs`, `/api/seq/last-run`.
- WiFi setup, mode switching, recovery: [wifi-provisioning.md](wifi-provisioning.md) (ADR 0015).
- Heap root-cause + fixes: GitHub issue #8 and `tasks/heap-exhaustion-and-flash-findings-2026-06-19.md`.
- In-PCB USB flash limitation: `tasks/lessons.md` (2026-03-15 entry).
- ESP-IDF coredump guide: <https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/core_dump.html>
