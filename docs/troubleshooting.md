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

# 2. Fetch it (raw ELF).
curl -s http://artoo.local/api/coredump -o coredump.elf

# 3. Decode against the firmware.elf for the DEPLOYED version.
#    The deployed version is in GET /api/status -> firmwareVersion, which matches
#    the committed data/fw-version.json git hash; build/checkout that commit's elf.
esp-coredump info_corefile -c coredump.elf .pio/build/protoArtoo_chirp/firmware.elf
#    (esp-coredump ships with esp-idf / `pip install esp-coredump`.)

# 4. After analysing, clear it so the NEXT crash is captured.
curl -s -X POST http://artoo.local/api/coredump/erase
```

The decode prints the panic reason, the crashed task, and per-task backtraces.
Decode each backtrace address against the same `firmware.elf`. The
`firmware.elf` MUST match the running firmware (same git hash) or addresses
mislead. Endpoints: see [api.md](api.md) (System and OTA).

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
high-water marks, a failed-allocation **counter + backtrace** (logged to
`/api/logs`, decode addresses with `xtensa-esp32-elf-addr2line`), mode-scoped
low-water snapshots (`boot`, `rc_linked`, `audio_play`), and largest-block/frag.
Watch over **minutes**, not one snapshot — `heapMin`/`failedAllocs` evolve.

```bash
curl -s http://artoo.local/api/profiler | grep -oE '"(heapFree|heapMin|heapLargest|fragRatio|failedAllocs)":[0-9.]*'
curl -s "http://artoo.local/api/logs" | grep alloc_failed     # size + caller backtrace
```

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

---

## References

- API: [api.md](api.md) — `/api/coredump*`, `/api/profiler`, `/api/status`, `/api/logs`, `/api/seq/last-run`.
- Heap root-cause + fixes: GitHub issue #8 and `tasks/heap-exhaustion-and-flash-findings-2026-06-19.md`.
- In-PCB USB flash limitation: `tasks/lessons.md` (2026-03-15 entry).
- ESP-IDF coredump guide: <https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/core_dump.html>
