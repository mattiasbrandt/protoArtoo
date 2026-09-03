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
- artoo-esp32 keeps its blocking UART write. The wait condition is
  transport-neutral, so the sink ticket measures that it adds nothing on UART0
  and does not move #219 R1's shared-wire behaviour or the ADR 0017 budgets.
