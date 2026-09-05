# Console records wait for USB CDC transmit room; log lines stay best-effort

Status: proposed. Implemented by the P4 sink ticket under #206; accepted when the
#215 row passes (`system.status.health` 13/13 on repeated runs, `operations`
terminated).

On the FireBeetle 2, `Serial` is the USB-Serial-JTAG CDC (`HWCDC`). `d252db7`
(#245 defect 1) sets `Serial.setTxTimeoutMs(0)` in `setup()` so that a host
which stops draining while USB stays enumerated - a detached monitor is enough,
SOF keepalives keep `isPlugged()` true - cannot hold a writer for 20 x 100 ms
and starve a TWDT-subscribed task. That made the transport best-effort by
design: `HWCDC` keeps a 256-byte TX ring (`setTxBufferSize(256)` at `begin()`);
with a zero timeout `HWCDC::write()` fails its ring send immediately, retries
twenty times within microseconds, then returns a short write, and the bytes
that did not fit are gone. The host drains at most 64 bytes per 1 ms USB frame.

The serial sink writes every line as two calls under the serial mutex - the
line, then the newline (`emitRecordLine` in `src/tasks/console_task.cpp`; the
log path in `src/console/console_serial_output.cpp` has the same shape). A
one-byte newline fits when a 45-byte line does not. Measured 2026-09-03 on the
P4 at `e2fd2365`: two identical `system.status.health` queries returned
different subsets of the 13-field group, blank lines where fields should be,
and no `type=end`. That is not host-side loss. It is the sink dropping whole
lines and keeping their newlines, and no Console Client can repair it.

A Console Record that can vanish silently cannot be bench evidence: a dropped
`field` is indistinguishable from a firmware that never emitted it. Log lines
are different. The log ring behind `/api/logs` is their authoritative record,
and their serial copy was declared best-effort in #245.

**We decided the serial Console sink waits for transmit room before writing a
record, and log lines do not.**

- Before taking the serial mutex, the record path waits - bounded well below
  the 3 s task-watchdog window, and only while the CDC reports a connected
  host - until `Serial.availableForWrite()` can take the whole line including
  its newline. The wait sits outside the mutex, so the unbounded mutex take in
  the log path of TWDT-subscribed tasks cannot inherit it.
- Every line, record or log, is written with one call that includes the
  newline. A line that still cannot be sent is dropped whole; a blank line is
  no longer a drop signature.
- The sink counts the records it still could not send within one request and
  stamps `dropped=<n>` on that request's closing record when the count is
  nonzero. A lost closing record leaves the group unterminated, which a
  Console Client already treats as loss; the two together make every drop
  visible on the wire. Serial only: the browser adapter builds its response
  whole and cannot drop.
- `Serial.setTxTimeoutMs(0)` stays. The driver remains best-effort; the
  Console adds patience above it, never blocking.
- The console task is not TWDT-subscribed and runs on Core 0 at a 10 ms
  cadence. That is what makes a bounded wait affordable there and nowhere
  else.

## Considered options

- **Restore a non-zero HWCDC timeout.** Rejected: it reintroduces the #245
  wedge for every TWDT-subscribed task that logs.
- **Enlarge the TX ring** (`setTxBufferSize` before `begin()`). Rejected as the
  fix: it raises the burst that fits and does not remove the drop; an
  `operations` listing is ~11 KB.
- **Wait inside the serial mutex.** Rejected: loggers block for the wait, which
  is the #245 starvation through a different door.
- **Have the Console Client tolerate loss.** Rejected: a client cannot tell a
  dropped field from an unemitted one. It does flag the signature (a blank
  line, an unterminated group) as a verdict; that is detection, not a fix.

## Consequences

- `HWCDC::flush()` with a zero timeout takes the `tries == 0` path: it marks
  the CDC disconnected and discards the ring. Call sites today:
  `src/tasks/console_task.cpp:390` after the boot banner, `src/main.cpp:540`
  and `:554`. The sink ticket decides whether those calls survive on
  CDC-on-boot builds.
- The framing lives in `console_serial_output.cpp`, which the native
  environment compiles and which already has a suite; `console_task.cpp` only
  calls it. The single-write framing and the wait seam are proven natively;
  the burst evidence is the P4 row on #215.
- "Every line, record or log, is written with one call" reached the record sink
  and the pre-console-task log fallback in #265, and the **interactive** log
  path - a line arriving mid-entry, which is a whole redraw and not just a line
  - in #268. That path went through `embeddedCliPrint()`, one write per
  character, which is why it could not simply inherit the rule: embedded-cli
  had to be able to render a redraw into a buffer first
  (`lib/embedded-cli/VENDORED.md` Patch 8).
- artoo-esp32 keeps its blocking UART write. The wait condition is
  transport-neutral, so the sink ticket measures that it adds nothing on UART0
  and does not move #219 R1's shared-wire behaviour or the ADR 0017 budgets.

## Amended 2026-09-04 (ADR 0037)

"Log lines stay best-effort" no longer holds. ADR 0037 makes the Console task
the only writer of the serial wire: loggers append to the Log Ring and the
Console task drains it, so the reason logs could not wait - a TWDT-subscribed
task blocked on the CDC - is gone. Log lines now wait for room under the same
bound as records, and the only loss is ring eviction, marked on the wire. The
record rule stands unchanged: one write per line, `dropped=<n>` on the closing
record. "Outside any lock" is no longer a rule that needs stating: there is no
lock, and only the Console task - which is not TWDT-subscribed - ever spends
the wait.

Two of this ADR's own Consequences are settled by the same slice (#270):

- The `HWCDC::flush()` question. `src/main.cpp`'s two calls are **removed**;
  the Arduino loop task does not own this wire, and on a CDC-on-boot build the
  call discarded the TX ring rather than draining it. `console_task.cpp`'s call
  survives, guarded to artoo-esp32, because there it is the wire's owner
  waiting for its own banner on a transport where `flush()` genuinely waits.
- *"The wait condition is transport-neutral, so the sink ticket measures that
  it adds nothing on UART0."* Read rather than measured, and it was not
  neutral: `Serial.availableForWrite()` cannot exceed the 128-byte hardware
  FIFO on artoo-esp32 (the TX ring is disabled there), so a reservation for
  any line longer than that could never be satisfied and every such record was
  dropped whole after the full 100 ms wait - on a transport whose `write()`
  cannot lose a byte in the first place. The reservation is now capped at what
  the transport can report (`CONSOLE_SERIAL_TX_ROOM_MAX`).

## Amended 2026-09-05 (#275): "connected host" is not "live TX"

The room-wait writes "only while the CDC reports a connected host" - the
`Serial` (bool) check in `writeFrameCounted()`. That guard is weaker than it
reads. On the P4 `Serial` is `HWCDC::isCDC_Connected()`, and the driver sets
its `connected` latch from a RECEIVED packet too, in the RX branch of
`hw_cdc_isr_handler()` (`HWCDC.cpp`). So the first byte a re-attaching host
sends makes `connected` true - and therefore this room-wait proceed - while
the transmit path is dead. `connected` is a host-present signal, not a
TX-liveness one, and nothing in this ADR's record policy can tell the two
apart: on a wedged endpoint every record still waits out
`CONSOLE_RECORD_ROOM_WAIT_BOUND_MS` and is dropped whole, exactly as it would
for an attached-but-not-reading host. That is why the #275 fix is upstream of
this policy - a settle hold-off that stops the wedge being created (ADR 0037's
amendment) - rather than a change to the wait here. Measured and diagnosed on
the P4 (USJ hw_ver3) at `017b168d`; register evidence on #275.
