// =============================================================================
// include/console_serial_output.h
//
// Serial output coordination for Console task (ADR 0034, ADR 0036, ADR 0037)
//
// ONE OWNER, one seam. After consoleSerialBindCli() the Console task is the
// only task that writes this wire, and every byte it writes goes through
// consoleSerialWriteFrame() below, in exactly one Serial.write() call per
// unit. There are two kinds of unit and they differ only in their room policy
// -- never in how they are framed:
//
//  - a Console Record: the line and its CR LF, waiting for transmit room, and
//    dropped whole if the room never comes (consoleSerialEmitFramedLine);
//  - a log line the Console task has drained from the Log Ring: the WHOLE
//    redraw -- input line cleared, the line, the break, the prompt, the
//    buffered command -- rendered by embedded-cli into one buffer and written
//    as one frame, waiting for room under the same bound as a record
//    (consoleSerialEmitLine, reached through consoleSerialDrainLogs).
//
// WHAT ADR 0037 CHANGED, and what it removed. The wire used to be writable by
// any task that took a mutex: paLogLine() wrote from the calling task's
// context, on either core, so a Core 1 real-time task could block inside a
// PA_LOG_* for the transmit time of a frame plus the wait for the lock, and
// the line editor's state was mutated from two cores at once (the render under
// the mutex, embeddedCliProcess() without it). Loggers now write ONE place --
// the Log Ring -- and the Console task drains it. The serial mutex is gone: it
// coordinated writers that no longer exist, and with a single-threaded owner
// the render and the line editor can never run concurrently, which is also
// what lets the 448-byte redraw frame live on the Console task's stack instead
// of in .bss.
//
// The redraw is still composed and written in ONE call, for a reason that
// survives single ownership: it used to go out character by character through
// embeddedCliPrint(), ~70 transport calls, and on artoo-esp32, where
// Serial.write() blocks until the UART accepts the byte, those calls held the
// wire for ~6 ms at 115200 baud (#268). ADR 0036 had already decided "every
// line, record or log, is written with one call that includes the newline";
// the interactive log path is the place that decision could not reach until
// embedded-cli could render into a buffer (lib/embedded-cli/VENDORED.md
// Patch 8).
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>

// Forward declaration
typedef struct EmbeddedCli EmbeddedCli;

// How long a Console Record waits, before writing, for
// Serial.availableForWrite() to clear room for the whole line (including its
// CR LF terminator) before the record is dropped whole (ADR 0036). Reasoning for
// 100 ms, read from ~/.platformio-p4/.../cores/esp32/HWCDC.cpp, not guessed:
//  - HWCDC's TX ring is 256 bytes (HWCDC::begin()'s setTxBufferSize(256)) and
//    the host drains at most 64 B per 1 ms USB frame, so a fully-occupied
//    ring recovers room for the longest record (CONSOLE_RECORD_LINE_MAX + 2)
//    in ~4-5 ms once the host is actually reading. 100 ms leaves ~20x
//    headroom for scheduling jitter and Core 0 contention with the web
//    server/OTA.
//  - It sits ~30x below WATCHDOG_TIMEOUT_S's 3 s TWDT window
//    (include/config.h): the wait is only ever spent by the Console task,
//    which is not TWDT-subscribed, but keeping it an order of magnitude
//    smaller is the ADR's own bound and leaves no doubt it can never be
//    mistaken for a real wait against that window.
//  - It bounds the worst realistic case: `operations` emits up to ~190
//    records (docs/console-protocol.md 2.1). If a host is truly wedged but
//    HWCDC still reports connected -- its `connected` latch can outlive an
//    actual reader (HWCDC.cpp's isCDC_Connected()) -- the whole listing
//    stalls for at most 190 * 100 ms =~ 19 s of Core 0, non-real-time, wall
//    clock time. Long, but bounded, and this task is not TWDT-subscribed
//    (src/tasks/console_task.cpp's file header) and runs on Core 0 at a
//    10 ms cadence, which is what makes spending any of it here affordable.
static constexpr uint32_t CONSOLE_RECORD_ROOM_WAIT_BOUND_MS = 100;

// The most transmit room `Serial.availableForWrite()` can EVER report on this
// board, and therefore the largest reservation a room-wait may ask for. A
// waiter that asks for more than this waits out its whole bound and then drops
// a frame the transport would have taken -- room that can never exist is not a
// reason to throw a line away. Read from the two vendor cores in
// ~/.platformio*/packages/framework-arduinoespressif32, not guessed:
//
//  - CDC-on-boot build (FireBeetle 2): `Serial` is HWCDC.
//    HWCDC::availableForWrite() returns xRingbufferGetCurFreeSize(tx_ring_buf)
//    and HWCDC::begin() creates that ring with setTxBufferSize(256)
//    (HWCDC.cpp), so 256 is the ceiling. Above it, HWCDC::write() chunks the
//    payload by xRingbufferGetMaxItemSize() anyway, so a reservation of the
//    whole frame was never what made the write whole.
//  - artoo-esp32: `Serial` is HardwareSerial on UART0 with the TX ring
//    DISABLED (_txBufferSize 0, HardwareSerial.cpp:148). uartAvailableForWrite()
//    then falls back to uart_ll_get_txfifo_len() -- free space in the 128-byte
//    hardware FIFO -- because uart_get_tx_buffer_free_size() reports 0 with no
//    ring installed (esp32-hal-uart.c; esp_driver_uart/src/uart.c). 128 is the
//    ceiling there, which is why every record longer than ~126 bytes waited
//    the full 100 ms and was then dropped whole on that board. Nothing was
//    lost by writing it instead: with tx_buf_size 0, uart_tx_all() loops on
//    tx_fifo_sem with portMAX_DELAY until every byte is in the FIFO, so UART0
//    cannot short-write or drop a byte at all.
//
// The native test build is neither, and takes the smaller ceiling: a host test
// that pins the reservation must pin the tighter of the two transports.
#if ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE
static constexpr size_t CONSOLE_SERIAL_TX_ROOM_MAX = 256;
#else
static constexpr size_t CONSOLE_SERIAL_TX_ROOM_MAX = 128;
#endif

// Largest frame consoleSerialEmitLine can compose, from the pieces
// embedded-cli writes for one mid-entry redraw (lib/embedded-cli's
// clearCurrentLine/embeddedCliPrint):
//
//   clear      1 CR + (inputLineLength <= cmdBufferSize 64) + invitation 2 + 1 CR
//   the line   PA_LOG_SERIAL_LINE_MAX - 1 (the cap every serial-bound line has)
//   break      2 (CR LF)
//   prompt     2 (the "> " invitation)
//   command    cmdBufferSize - 1
//   cursor     8 ("\x1B[65535D" at its widest)
//
// = 68 + 255 + 2 + 2 + 63 + 8 = 398 at today's embeddedCliDefaultConfig()
// sizes. 448 leaves room for a longer invitation or command buffer without
// silently starting to refuse renders; a render that still does not fit is
// reported, not truncated (embeddedCliPrintToBuffer returns 0 and
// consoleSerialEmitLine falls back to sending the line on its own).
static constexpr size_t CONSOLE_SERIAL_FRAME_MAX = 448;

// Per-character writer for embedded-cli output.
// Exported for embedded-cli binding as cli->writeChar.
//
// This is the ECHO path and nothing else: embeddedCliProcess() calls it while
// the operator types, from the Console task. It needs no lock and takes none:
// the Console task is the only writer of this wire (ADR 0037), so an echoed
// character cannot land inside anything -- there is nothing else writing for
// it to land inside.
void consoleSerialWriteChar(EmbeddedCli* cli, char c);

// Bind the serial output coordinator to an embedded-cli instance, and with it
// hand the serial wire to the Console task (ADR 0037): from this call on,
// paLogLine() writes only to the Log Ring and consoleSerialDrainLogs() below
// is the only thing that puts a log line on the wire.
//
// Called once during console task initialization with the live CLI.
// Allows a host test to inject a test CLI for harness verification.
void consoleSerialBindCli(EmbeddedCli* cli);

// Write a Console-owned string to the wire verbatim -- no terminator added, no
// redraw, no ring involvement. The ready banner and the "> " invitation, at
// boot and on host re-attach (src/tasks/console_task.cpp), are its only
// callers: they are the Console task announcing itself, not log lines, and
// routing them through the ring would hand them to a drain that has not
// started yet.
//
// Best-effort, NOT waiting for room, which is what the raw Serial.print() it
// replaces did. The room-wait also refuses to write at all while `Serial`
// reports no connected host, and on the FireBeetle 2 that is exactly the state
// a cold boot with no monitor attached is in -- so waiting here would throw
// the boot banner away on the one board where it is not otherwise repeated
// until #260's re-attach path fires. Same bytes, same policy, one seam.
void consoleSerialWriteText(const char* text);

// Emit one drained log line, with its mid-entry redraw, in ONE frame.
//
// Renders the whole redraw -- input line cleared, the line, the break, the
// prompt, the buffered command -- into a buffer with embeddedCliPrintToBuffer()
// and writes that buffer as one frame, waiting for transmit room under the
// same bound as a Console Record (ADR 0037 supersedes ADR 0036's "logs stay
// best-effort": the reason logs could not wait was a TWDT-subscribed logger
// blocking on the CDC, and the Console task is not TWDT-subscribed).
//
// If the redraw does not fit CONSOLE_SERIAL_FRAME_MAX the render is refused
// whole rather than truncated, and the line alone is sent through
// consoleSerialEmitFramedLine(). The line is still written whole; only the
// prompt redraw is lost, and the operator's next keystroke or log line draws
// it again.
//
// CONSTRAINT: the Console task only. It renders through the line editor, whose
// state that task also mutates in embeddedCliProcess(); single ownership is
// what makes both safe without a lock, so a second caller re-opens exactly the
// cross-core editor race ADR 0037 removed. It is also not re-entrant --
// embeddedCliPrintToBuffer() refuses a render started from inside a render.
void consoleSerialEmitLine(const char* line);

// Drain the Log Ring to the wire, and return how many lines were written.
//
// The Console task's own job (ADR 0037), called at three points: each poll of
// its 10 ms cadence, before it dispatches a command, and at every record
// boundary while a command runs -- so wire order is preserved to within one
// record or one poll.
//
// Ring eviction -- the writers overtaking the drain cursor -- is the only way
// a log line is lost from the wire now, and it is never silent: one counted
// marker line carrying the same ` dropped=<n>` token a record's closing line
// uses (consoleSerialFormatDroppedSuffix) is emitted before the drain
// continues. Keeping that true is why the drain does nothing at all while no
// host is attached: see the guard at the top of its implementation.
//
// Bounded per call at the ring's own depth: the ring cannot hold more than
// that, so one call always catches up with everything already written, and a
// writer appending faster than the wire drains cannot hold the Console task
// inside this function -- it resumes at the next poll.
//
// CONSTRAINT: the Console task only, and never from inside a render.
uint32_t consoleSerialDrainLogs(void);

// The ONE place a Console byte reaches the wire: optionally wait for transmit
// room, then write `len` bytes in a SINGLE Serial.write() call.
//
// `bytes` is written verbatim -- this function adds no terminator and applies
// no length cap, because its two callers have already composed exactly what
// belongs on the wire: a record line plus its CR LF
// (consoleSerialEmitFramedLine) or a whole rendered redraw
// (consoleSerialEmitLine). Callers must keep `len` within what they own.
//
// waitForRoom selects ADR 0036's two policies; see
// consoleSerialEmitFramedLine below for what each one means.
//
// Returns false only when a waiting caller gave up: nothing was written. A
// non-waiting caller always returns true.
bool consoleSerialWriteFrame(const char* bytes, size_t len, bool waitForRoom);

// Emit exactly one line, plus its trailing CR LF, in a SINGLE Serial.write()
// call (ADR 0036). This is the one emit helper the Console Record sink
// (src/tasks/console_task.cpp's emitRecordLine), the pre-bind log path
// (src/main.cpp's paLogLine, before the Console task owns the wire) and
// consoleSerialEmitLine's render-did-not-fit fallback all call, so the framing
// and the wait policy live in exactly one place. `line` is truncated to
// PA_LOG_SERIAL_LINE_MAX - 1 bytes first, matching every other serial-bound
// line in this file. The write itself is consoleSerialWriteFrame's, so a
// record and a redraw reach the wire through the same seam.
//
// The terminator is CR LF, not a bare LF (#267). A Console session is
// attached in raw mode so Tab and cursor bytes reach the firmware unedited
// (docs/console-protocol.md 8), and raw mode disables the kernel's ONLCR
// NL->CR-NL translation: a bare LF leaves the carriage where it was, so
// records staircase down-right across the terminal while embedded-cli's own
// log lines -- already "\r\n", lib/embedded-cli/src/embedded_cli.c:235 --
// sit at column 0 on the same wire. Both halves of the wire now agree. Every
// supported client (this repo's console_client.py, `pio device monitor`,
// picocom) reads the extra CR as line-terminator whitespace.
//
// This replaces the #245-era shape where a line and its newline were two
// independent Serial.write() calls that could succeed and fail independently
// -- a dropped line with its newline still delivered is what produced the
// blank-line drop signature ADR 0036 documents. One call means one outcome.
//
// waitForRoom selects which of ADR 0036's two policies applies:
//  - true  (Console Records and drained log lines): waits up to
//    CONSOLE_RECORD_ROOM_WAIT_BOUND_MS for Serial.availableForWrite() to
//    clear room for the whole line INCLUDING both terminator bytes (the
//    reservation is lineLen + 2, never lineLen + 1: a record written short
//    by its CR is the drop this reservation exists to prevent), capped at
//    CONSOLE_SERIAL_TX_ROOM_MAX because a reservation larger than the
//    transport's whole buffer can never be satisfied, and only
//    while `Serial` (bool) reports a connected host -- the same
//    board-portable check console_task.cpp's
//    #260 host-attach debounce already uses (HWCDC::isCDC_Connected() on the
//    P4, "UART driver installed" -- effectively always true post-boot -- on
//    artoo-esp32, so this adds nothing measurable there). If room never
//    clears within the bound, NOTHING is written: the record is dropped
//    whole, and this function returns false so the caller can count the
//    drop toward that request's `dropped=<n>`.
//  - false (log lines, #245's best-effort contract): no wait, no connected
//    check; the write is attempted immediately and this always returns
//    true. HWCDC's own zero-timeout retry (HWCDC::write(), 20 attempts
//    within microseconds) may still short-write it under host backpressure,
//    exactly as before -- only the two-call framing bug is fixed here, not
//    the driver's best-effort contract. This is the PRE-BIND path only
//    (src/main.cpp's paLogLine): once the Console task owns the wire, a
//    drained log line waits like a record (ADR 0037).
bool consoleSerialEmitFramedLine(const char* line, size_t len, bool waitForRoom);

// Formats the `dropped=<n>` suffix ADR 0036 stamps on a request's closing
// record (src/tasks/console_task.cpp's onRecordResult/onRecordEnd) when the
// sink could not send `droppedCount` earlier records within
// CONSOLE_RECORD_ROOM_WAIT_BOUND_MS. Writes " dropped=<n>" (with the leading
// space, ready to concatenate onto an existing record line) and returns its
// length when droppedCount is nonzero; writes an empty string and returns 0
// when it is zero, so the field is absent exactly when there is nothing to
// report, matching the `reason=` field's own presence rule
// (docs/console-protocol.md 3.3). Pure formatting, no I/O -- kept here
// rather than inline in console_task.cpp (which src/tasks/console_task.cpp's
// own file header notes is Arduino/FreeRTOS-only and not part of the native
// build) specifically so this wire-format rule is provable on the host,
// same as every other record field. consoleSerialDrainLogs() formats the log
// ring's eviction marker through this same helper, so the one idiom cannot
// drift between the two things that can be dropped.
size_t consoleSerialFormatDroppedSuffix(char* buffer, size_t bufferSize, uint32_t droppedCount);
