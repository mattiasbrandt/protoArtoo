# The Console task owns the serial wire; log lines reach it through the Log Ring

Status: proposed. Implemented by a #206 sub-issue; accepted when the mid-entry
log rows pass on both boards from the merged tip (artoo UART0 and P4 CDC).

#268 fixed a self-sustaining redraw loop on artoo-esp32 by rendering the
mid-entry redraw into one buffer and writing it with one call, and it named
the structural cause: two writers on one wire, and only one of them held the
lock. That fix is correct and does not settle who owns the wire. At
`0f1412fc`, after #268:

- The function seam has exceptions. `consoleSerialWriteFrame()` carries records
  and redraws, but the banner and prompt are raw `Serial.print` under a
  hand-taken mutex (`src/tasks/console_task.cpp`), three `Serial.flush()` sites
  take no lock (`console_task.cpp`, `src/main.cpp` x2), and `ESP_LOGx` in
  `src/drivers/sbus_decoder.cpp` and `ledc_pwm.cpp` reach UART0 through the IDF
  logger.
- The lock is held by whichever task logs, on either core. `paLogLine()` takes
  `logSerialMutex` and writes from the caller's context. RCInputTask, DriveTask
  and DomeLinkTask all log from Core 1; DriveTask logs in its failsafe path.
- The line editor's state is mutated from two cores. The render under the mutex
  (`embeddedCliPrintToBuffer`, any logging task) and `embeddedCliProcess` (the
  Console task, no lock - it cannot take one, because a command dispatched from
  inside it emits records through the same non-recursive mutex) both write
  `cmdBuffer`, `cmdSize`, `cursorPos`, `inputLineLength` and `flags`. The render
  saves and zeroes `cursorPos` and restores it afterwards; a Backspace processed
  on Core 0 inside that window with the cursor at line start leaves the restored
  `cursorPos` past the buffer, and the next insert computes its position by
  unsigned subtraction. Derived by reading, not reproduced; named because a
  safety gap is named rather than assumed away.
- On artoo-esp32 the UART0 transmit ring is zero-length and the FIFO is 128
  bytes, so a frame longer than the FIFO blocks the writing task for the drain
  time at 115200 8N1 - up to ~28 ms for the largest redraw frame - plus the wait
  for the mutex. DriveTask runs at 50 Hz. Nothing recorded that cost.

Every one of those is a consequence of the same premise: any task may write the
wire if it takes the lock. The premise was inherited from the pre-Console
logger and never decided.

**We decided the serial wire is owned by the Console task. After the serial
adapter binds, no other task writes it.**

- A log line is written once, to the Log Ring, under the ring's own critical
  section. That was already its authoritative record (#245); the serial copy
  is now produced from it rather than beside it.
- The Console task keeps a drain cursor into the ring and writes lines from it:
  at each poll of its 10 ms cadence, before dispatching a command, and at every
  record boundary while a command runs. Wire order is therefore preserved to
  within one record or one poll.
- Log lines wait for transmit room under the same bound as records (ADR 0036's
  reason for not waiting - a TWDT-subscribed logger blocked on the CDC - no
  longer applies; the Console task is not TWDT-subscribed). The only loss is
  ring eviction: when the writer has overtaken the cursor, the drain emits one
  counted marker line before continuing, the `dropped=<n>` idiom applied to
  logs.
- The log path holds no serial mutex, and the mutex is removed: the Console task
  is single-threaded over the wire and over the line editor, so the render and
  the editor never run concurrently and the redraw frame can live on the
  Console task's stack.
- Before the adapter binds, `paLogLine()` writes directly as it does today; the
  switch to ring-only is the bind itself. Setup-phase writers (`Serial.begin`,
  the flush sites) run before the task exists, and the slice moves the one that
  does not. The IDF logger is pointed at the ring through its vprintf hook so
  the two driver files stop bypassing it. The panic handler keeps its own
  direct path.
- The invariant is enforced twice: a source scan in the
  `test_queue_send_timeout_zero.py` mould, with an allowlist naming the sink
  file and the pre-bind setup sites with a written reason each; and a native
  test in which a line logged from a non-Console task never reaches the serial
  stub except through the drain.

## Considered options

- **Enforce the function seam and stop** - route banner and prompt through
  `consoleSerialWriteFrame()`, allowlist the flush sites, add the scan test.
  Rejected as the whole answer: it leaves the cross-core editor state, the
  Core 1 blocking and the lock-held-by-anyone model exactly where they are.
- **Function seam plus an editor-state lock** - a short critical section around
  the render and around per-character processing, excluding command dispatch
  (a Patch 9 to embedded-cli). Rejected: it adds a second lock and a second
  patch to protect a shared-state model that single ownership removes outright.
- **Document the gaps and leave them** - rejected: the editor race is the last
  implicit guarantee in a path whose other guarantee #268 just made explicit.
- **Drain the ring tail from a shutdown handler during panic** - rejected: it
  puts code in the panic path, which ADR 0031 treats as safety-relevant, to
  recover lines the panic handler's own fault report mostly makes redundant.
- **The Console task's own log lines bypass the ring** - rejected: two write
  paths and a cursor that must skip what was already written, to preserve wire
  order that the record-boundary drain already keeps to within one record.

## Consequences

- Serial log latency is bounded by the poll cadence and by record boundaries.
  A line logged by the Console task itself inside a command that panics before
  its next record stays in the ring and is lost with the reboot; the command
  echo and the ring content up to the command's start are already on the wire,
  and the panic handler prints the fault, task and backtrace. This is the one
  thing serial no longer shows, accepted 2026-09-04.
- ADR 0036's record rule is unchanged; its "logs stay best-effort" clause is
  superseded (amendment note there). #219 R1's per-line locking is superseded
  for the log path.
- The 448-byte static redraw frame from #268 moves to the Console task's stack,
  and `logSerialMutex` and its accessor are deleted; artoo static RAM recovers
  what #268 spent.
- A Core 1 task's log call now costs one bounded critical section and a copy
  into the ring, on every transport. The worst case a Core 1 task could spend
  inside `PA_LOG` on artoo is no longer a property of the wire.
- The magnitude figures in #268's body (2.88 MB in 5 s) exceed what a 115200
  link can carry by ~50x; the shape is evidence, the rate is a measurement
  artefact. They are re-measured with a byte count and a hexdump from the
  merged tip before the #217/#233 identity is asserted again.
- The vendored `embeddedCliResetInput()` clears `cmdSize` without clearing
  `cmdBuffer` (P4 host-attach path only); the slice that takes ownership of the
  editor fixes it under the same P4 attach rows.
- The slice is a #206 sub-issue at `controller-upload-verified` on both boards,
  file-disjoint from the decision-1 slice (ADR 0011, 2026-09-04) except where
  the Console module's own mutex is narrowed.

## Amended 2026-09-05 (#275): a settle window before the task speaks to the CDC

The Console task owns the wire, and on the P4 that ownership now includes a
hold-off after a USB CDC re-attach. Reading `Serial` (bool) is
`HWCDC::isCDC_Connected()`, which - while `isPlugged() && !connected`, the
state right after a host's bus reset - arms the IN-empty interrupt and calls
`usb_serial_jtag_ll_txfifo_flush()`, committing a zero-length IN packet. On USJ
hw_ver3 that packet, committed mid-enumeration before the host has configured
the device, survives the reset: `serial_in_ep_data_free` stays 0, IN-empty
never fires again, and every record is dropped whole thereafter - the permanent
TX wedge #274 found and #275 diagnosed (register evidence on #275, H1).

So the task no longer calls any HWCDC method blindly each poll. It polls
`HWCDC::isPlugged()` - the side-effect-free SOF watchdog, not `Serial` - and on
a genuine plugged edge (debounced against the watchdog's few-ms flap) it makes
NO HWCDC call at all for `CONSOLE_CDC_SETTLE_MS` (`include/console_cdc_settle.h`):
no drain, no read, no echo, no `isCDC_Connected()`. No speculative flush is
committed while enumeration is in flight. After the window the two-poll #260
attach debounce runs unchanged, and the first `isCDC_Connected()` lands on a
configured endpoint - the state the proven cold-boot attach has always been in.
The window and the debounce live in one host-testable inline unit; the #260
debounce moved there unchanged. artoo-esp32 has no `isPlugged()`, is fed
`plugged=true`, and never opens a window, so its behaviour is untouched.
Verified across three replug cycles and a no-host boot on the P4; evidence on
#275.
