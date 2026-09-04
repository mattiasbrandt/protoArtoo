# Troubleshooting — crash/coredump + heap (agents + operators)

Procedures for diagnosing body-controller crashes (`resetReason=PANIC`), heap
exhaustion, and the flashing constraints that affect how you collect evidence.
Written for both the operator and a troubleshooting agent: each section is a
concrete, ordered procedure with exact commands and decision points.

Base URL: `http://artoo.local` — the artoo-esp32 controller's default mDNS name
(or the device IP — `GET /api/wifi` → `staIp`, or `10.0.0.22` if mDNS is
flaky). A FireBeetle 2 controller answers at `http://firebeetle2.local`
instead; the two boards default to different names so they never contest each
other on the same LAN (#242). All HTTP probes work on the **seated**
controller; esptool flash/write operations do **not**. USB serial monitoring
remains readable with the reset caveat below.

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

### OTA fails with `[ERROR]: No response from device` (host firewall)

Applies to **both boards** — nothing in this path is board-dependent.

The board is not the problem. `espota.py` prints `Waiting for device...` and then
blocks on `sock.accept()` on a **TCP** listener it opens on the *host*, with a
hardcoded 10 s timeout (`--timeout`/`OTA_TIMEOUT` do not extend this specific
wait — that flag covers the invitation/result timeout, and
`tools/ota_upload.py --transfer-timeout` patches a *different* hardcoded 10 s,
the per-chunk transfer one). The device receives the OTA invitation, accepts
it, and tries to connect back to that host port — `ArduinoOTA error: 2
update=0 No Error` followed by `error: 4 update=12 Aborted` in the board's own
log means the device could not open the TCP connection *back*, i.e.
`OTA_CONNECT_ERROR`. On a host with a default-deny inbound firewall (e.g.
`ufw` with `DEFAULT_INPUT_POLICY="DROP"`), that inbound connection is silently
dropped, and by default espota chooses a **random** host port each run, so
there is no single rule to add ahead of time.

Fix: `make ota` (and every other `make *-ota` target, plus the `make`
interactive wizard) pins that host port to a fixed value —
`OTA_HOST_PORT`, default **32320** — via `tools/ota_upload.py --host-port`.
Allow it once on your host:

```bash
sudo ufw allow from 10.0.0.0/24 to any port 32320 proto tcp   # match your LAN CIDR
```

Override the port with `OTA_HOST_PORT=<port>` (CLI or `user.mk`, see
`user.mk.example`) only if 32320 is already taken on your machine — otherwise
firewall the default instead of moving it, so the rule above keeps working.

### Serial monitor caveat

> [!IMPORTANT]
> **Measured 2026-08-28 (32 unseated open/close trials, artoo-esp32 on a CP2102
> bridge).** Attaching a host terminal does **not** reset this board -- with one
> exception, which resets every single time. The blanket "opening the port resets
> the ESP32" advice that stood here before that session was wrong for five of the
> six methods tested, and it is replaced by the matrix below.

**Safe: use any of these.** Five trials each, zero resets.

| Attach method | resets |
|---|---|
| `tools/console_client.py` (default POSIX `O_NOCTTY`/termios backend) | 0/5 |
| `pio device monitor` | 0/5 |
| `picocom` | 0/5 |
| `cat` after `stty -F <port> -hupcl` | 0/5 |
| `cat` after `stty -F <port> hupcl` | 0/5 |

**Unsafe: `tools/console_client.py --pyserial`.** 7/7 resets, and 2 of those 7 also
left the board **stranded off the network** -- silent on serial and absent from
WiFi, because it came up in the ROM download stub instead of the application.

Why, from the source rather than from inference: `open_pyserial_port()` sets
`dtr = False` and `rts = False` *before* `Serial.open()`, and pyserial's
`serialposix.Serial.open()` then calls `_update_dtr_state()` and
`_update_rts_state()` as two **separate** ioctls after the open. DTR and RTS are
therefore driven low one after the other rather than together, and DTR is left
low across the transition. Every other method above raises and lowers both lines
together, and `TIOCMGET` shows both asserted after open and still asserted after
close.

> [!NOTE]
> That paragraph describes what the **host driver** does to DTR/RTS. The EN and
> GPIO0 transitions at the chip were **not** captured: there is no logic analyser
> or scope on this bench, so the board-side auto-reset behaviour remains
> `UNKNOWN`. Nothing here should be read as a waveform measurement. Verification
> step if an instrument is acquired: a 4-channel capture at >= 1 MS/s on DTR, RTS,
> EN and GPIO0 across a port open with each client above.

**Recovering a stranded board.** Deassert DTR (so GPIO0 is high and the chip boots
the application, not the download stub), then pulse RTS to cycle EN:

```python
import serial, time
s = serial.Serial('/dev/ttyUSB0', 115200)
s.dtr = False        # GPIO0 high -> boot the app image
s.rts = True         # EN low  -> hold in reset
time.sleep(0.2)
s.rts = False        # EN high -> boot
s.close()
```

A successful recovery prints `rst:0x1 (POWERON_RESET),boot:0x13 (SPI_FAST_FLASH_BOOT)`.
If it prints a `DOWNLOAD_BOOT` mode instead, DTR was still asserted.

**The rule:** pick any safe method above, and **never change DTR or RTS after the
port is open**. A deliberate post-open toggle resets the board every time -- that
is how the matrix above was proven able to detect a reset at all, rather than
merely never firing.

**Anchoring a reset.** Two independent anchors, because a missed serial capture
looks identical to "no reset": the ROM boot banner (`rst:0x...`) in the serial
stream, **and** `resetReason` + `uptimeMs` from `/api/status` over HTTP either
side of the attach. The HTTP anchor does not travel over the serial path.
`resetReason` distinguishes causes: `PANIC` = real crash; `POWERON`/`EXT` =
external/serial reset.

USB serial *read* works seated (RX only); only flashing needs the blocked
TX/bootloader path (GPIO15/SBUS strapping). **The seated arm of this matrix was
not run** -- seated measurement is not available on this bench -- so every row
above is an unseated result.

The 2026-06-22 regression note that stood here (a second POSIX-backend attach
printing the ROM banner while the board was unseated) is **not reproducible**: the
POSIX backend measured 0/5 across this session. What that earlier observation
actually captured is not established, and is not re-asserted here.

### Console interactive session

The Controller Console ([console-protocol.md](console-protocol.md)) is
bidirectional and needs a raw-mode terminal (section 8): Tab and cursor bytes
must reach the firmware unedited, and local echo must be off (the firmware
echoes). Three clients are supported below, all built on attach methods
measured 0/5 resets in the matrix above; use whichever is already on your
machine.

> [!IMPORTANT]
> This section proves only what the host side of each client does --
> `open()` ordering, DTR/RTS flags, default local-echo state -- read from each
> tool's own source (pyserial's `miniterm.py`, PlatformIO's
> `device/monitor/command.py`, picocom 3.1's `picocom.c`). Whether the
> underlying open resets the artoo-esp32 is the attach matrix's measured
> result above, not re-measured per client here; nothing below is a new
> board-reset trial.

**`python3 tools/console_client.py --interactive`** -- this repo's own tool,
nothing extra to install. Extends the exact open the read-only default uses
(still no DTR/RTS touch, still `O_NOCTTY`) with a write path and a raw local
terminal. Its other two modes, the scripted directives and the exit codes are
in [console-client.md](console-client.md); `make console` is the same thing
with the port resolved for you.

```
python3 tools/console_client.py --interactive
```

- **Ctrl-C exits the session locally**, matching the convention already
  documented for every supported Console terminal below and in
  [console.md](console.md#attach-a-serial-terminal): "that is your terminal
  program's own default key, not something the firmware does". The firmware
  never acts on an inbound Ctrl-C byte (console-protocol.md section 8 says it
  "never attempts to close a terminal it does not own"), so this tool
  consumes Ctrl-C itself rather than sending it -- it is never forwarded to
  the port.
- `--interactive --pyserial` together is refused with an error: `--pyserial`
  is the unsafe backend measured 7/7 above, and is never valid for a client an
  operator is told is safe.

**`pio device monitor -e artoo_esp32`** -- already configured for this
project: the `artoo_esp32` env in `platformio.ini` ships `monitor_raw = yes`
(its comment names the same reason -- the Console's VT100 backspace sequences
need raw passthrough), so no `--raw` flag is needed here. Always pass
`-e artoo_esp32` explicitly rather than relying on environment autodetection.
Ctrl-C exits by default (`device/monitor/command.py`'s `--exit-char` default
is `3`, i.e. Ctrl-C) -- this is the tool [console.md](console.md) already
describes.
**Never pass `--dtr` or `--rts`.** Read from `device_monitor.py`/
`miniterm.py`: those flags are forwarded straight to pyserial's
`serial_instance.dtr`/`.rts` *before* `.open()` -- the identical ordering that
makes `console_client.py --pyserial` unsafe above. Left unset (the default),
neither line is touched at open, matching the matrix's measured `pio device
monitor` row.
- **Do not press Ctrl-T Ctrl-R or Ctrl-T Ctrl-D while attached.** Those are
  miniterm's own live RTS-toggle and DTR-toggle keys (Ctrl-T is its menu key,
  read from `miniterm.py`'s `handle_menu_key()`).

**`picocom -b 115200 -q --imap "" --omap "" <port>`** -- the exact invocation
measured 0/5 in the attach matrix. Read from picocom 3.1 source (the version
in Arch's `extra` repo at time of writing; the exact version on the #214
bench is not recorded, so re-check this against `picocom --help`/`man
picocom` if the installed version differs): local echo defaults off (matches
the Console's requirement -- do not add `--echo`), and DTR/RTS are only
actively driven at open when `--lower-rts`/`--raise-rts`/`--lower-dtr`/
`--raise-dtr` are given, none of which appear above, so the port opens with
both lines untouched exactly as measured.
- **Ctrl-C does *not* exit picocom** -- unlike the two clients above, picocom
  only binds its escape-prefixed key combinations (below); a bare Ctrl-C is
  forwarded to the port as ordinary data. **Ctrl-A Ctrl-X exits cleanly.**
- **Do not press Ctrl-A Ctrl-T, Ctrl-A Ctrl-G, or Ctrl-A Ctrl-P while
  attached.** Picocom's own live DTR-toggle, RTS-toggle and DTR-pulse keys
  (Ctrl-A is its escape key, read from `picocom.c`'s `KEY_TOG_DTR`/
  `KEY_TOG_RTS`/`KEY_PULSE`).

Whichever client is used, the shared rule from the matrix above still holds:
**never change DTR or RTS after the port is open.**

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

Check the network itself as well:

- **WPA3-only WiFi networks are not supported; use WPA2 or WPA2/WPA3 mixed
  mode.** A WPA3-only access point refuses the controller, and the symptom
  looks the same as a wrong password: the join never completes. Mixed mode
  (the common home-router default) works.

---

## 6. Controller Console: command typed but nothing (useful) happened

Full guide: [console.md](console.md). Two specific symptoms:

### A long command on serial comes back as `line-too-long`

The serial Console's input line holds **62 bytes** — far shorter than the
dashboard's 255-byte limit. Past that point extra keystrokes stop appearing,
and pressing Enter throws the whole line away with
`invalid reason=line-too-long` rather than running the part that fit. That is
deliberate: a command shortened halfway through a value is not the command you
typed, so it never runs. Backspacing back under the limit does not help — the
characters that were dropped were never stored, so the line is still refused.

Retype the command in the dashboard's Live Logs command box, where the limit is
255 bytes, or keep serial commands short.

### An action answers `blocked reason=blocked-by-state` or `unavailable reason=temporarily-unavailable`

- `blocked reason=blocked-by-state` on an action almost always means **Web
  control** is off — turn it on with the **✓ Enable Web Control** button
  under Safety Controls on the Drive page, `POST /api/web-control/enable`,
  or the Console command `system.action.enable-web-control` (works from
  serial, needs no network, and needs no Web control of its own), then
  retry. `system.action.estop` always answers this way, on purpose; use the
  dashboard's E-Stop control or `POST /api/estop` instead.
- `outcome=queue-full` means the part of the firmware that would run the
  command is busy right now (its queue is momentarily full) — the command
  was not accepted; wait a moment and try again.
- `unavailable reason=temporarily-unavailable` on an action, despite the
  name, is not the same as busy: today it means the action has nothing to
  do (a sound action drawing from a category with no tracks configured is
  the current example) — retrying won't help until the underlying
  configuration is fixed.

## References

- API: [api.md](api.md) — `/api/coredump*`, `/api/profiler`, `/api/status`, `/api/logs`, `/api/seq/last-run`.
- Controller Console: [console.md](console.md), [console-protocol.md](console-protocol.md),
  [console-client.md](console-client.md) (`tools/console_client.py`).
- WiFi setup, mode switching, recovery: [wifi-provisioning.md](wifi-provisioning.md) (ADR 0015).
- Heap root-cause + fixes: GitHub issue #8 and `tasks/heap-exhaustion-and-flash-findings-2026-06-19.md`.
- In-PCB USB flash limitation: `tasks/lessons.md` (2026-03-15 entry).
- ESP-IDF coredump guide: <https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/core_dump.html>
