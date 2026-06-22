# Troubleshooting — crash/coredump + heap (agents + operators)

Procedures for diagnosing body-controller crashes (`resetReason=PANIC`), heap
exhaustion, and the flashing constraints that affect how you collect evidence.
Written for both the operator and a troubleshooting agent: each section is a
concrete, ordered procedure with exact commands and decision points.

Base URL: `http://artoo.local` (or the device IP — `GET /api/wifi` → `staIp`, or
`10.0.0.22` if mDNS is flaky). All HTTP probes work on the **seated** controller;
USB does **not** (see "Flashing constraint" below).

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
  .pio/build/protoArtoo_chirp/firmware.elf

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
curl -s http://artoo.local/api/status | grep -oE '"(heapFree|heapMin|heapLargestBlock|resetReason)":[^,}]*'
```
- `heapMin` is the all-time low-water. A floor below ~10-20 KB is unsafe.
- `heapLargestBlock` much smaller than `heapFree` = fragmentation.

### Deep read (profiler build)

Flash `protoArtoo_profiler` (CHIRP + `PA_HEAP_PROFILE`; same code as
`protoArtoo_chirp` plus instrumentation). `GET /api/profiler` adds: per-task stack
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
outcome+reason, and dome-link drop/retry counts — diff it against
[sequence-parity.md](sequence-parity.md) instead of eyeballing.

---

## 3. Flashing constraint (READ before collecting USB evidence)

**The seated controller cannot be USB-flashed or USB-read.** GPIO15 is
`PIN_SBUS1_RX`, a strapping pin; the SBUS receiver fights download-mode strapping
and the PCB loads the EN/GPIO0 auto-reset circuit, so esptool reports *"Download
mode detected, but no sync reply / TX path seems down"* (`tasks/lessons.md:549`).

- **Seated → use OTA** (`make ota-chirp OTA_IP=...`) and **HTTP** for all evidence
  (`/api/coredump`, `/api/profiler`, `/api/logs`, `/api/status`). This is the
  normal path and why the coredump/profiler evidence is exposed over HTTP.
- **USB flash → unseat the ESP32**, then `make flash BUILD_ENV=protoArtoo_chirp
  UPLOAD_PORT=/dev/ttyUSB0`, then reseat. A partition-table change (e.g. the
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

---

## References

- API: [api.md](api.md) — `/api/coredump*`, `/api/profiler`, `/api/status`, `/api/logs`, `/api/seq/last-run`.
- Heap root-cause + fixes: GitHub issue #8 and `tasks/heap-exhaustion-and-flash-findings-2026-06-19.md`.
- In-PCB USB flash limitation: `tasks/lessons.md` (2026-03-15 entry).
- ESP-IDF coredump guide: <https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/core_dump.html>
