// =============================================================================
// include/console_serial_output.h
//
// Serial output coordination for Console task (ADR 0034, ADR 0036)
// Routes log/event/record output through embeddedCliPrint() under serial mutex
// so that lines arriving mid-entry clear the input line, print atomically,
// then redraw the prompt and buffered command. Console Records use the
// single-write framed emitter below instead: they never interact with the
// interactive input line (docs/console-protocol.md 3.1's per-line locking),
// and ADR 0036 gives them a room-wait policy logs must not inherit.
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Forward declaration
typedef struct EmbeddedCli EmbeddedCli;

// How long a Console Record waits, OUTSIDE the serial mutex, for
// Serial.availableForWrite() to clear room for the whole line (including its
// newline) before the record is dropped whole (ADR 0036). Reasoning for
// 100 ms, read from ~/.platformio-p4/.../cores/esp32/HWCDC.cpp, not guessed:
//  - HWCDC's TX ring is 256 bytes (HWCDC::begin()'s setTxBufferSize(256)) and
//    the host drains at most 64 B per 1 ms USB frame, so a fully-occupied
//    ring recovers room for the longest record (CONSOLE_RECORD_LINE_MAX + 1)
//    in ~4-5 ms once the host is actually reading. 100 ms leaves ~20x
//    headroom for scheduling jitter and Core 0 contention with the web
//    server/OTA.
//  - It sits ~30x below WATCHDOG_TIMEOUT_S's 3 s TWDT window
//    (include/config.h): the wait itself is harmless to that window because
//    it runs entirely outside the serial mutex (see
//    consoleSerialEmitFramedLine's contract below), but keeping it an order
//    of magnitude smaller is the ADR's own bound and leaves no doubt it can
//    never be mistaken for a real wait against that window.
//  - It bounds the worst realistic case: `operations` emits up to ~190
//    records (docs/console-protocol.md 2.1). If a host is truly wedged but
//    HWCDC still reports connected -- its `connected` latch can outlive an
//    actual reader (HWCDC.cpp's isCDC_Connected()) -- the whole listing
//    stalls for at most 190 * 100 ms =~ 19 s of Core 0, non-real-time, wall
//    clock time. Long, but bounded, and this task is not TWDT-subscribed
//    (src/tasks/console_task.cpp's file header) and runs on Core 0 at a
//    10 ms cadence, which is what makes spending any of it here affordable.
static constexpr uint32_t CONSOLE_RECORD_ROOM_WAIT_BOUND_MS = 100;

// Per-character writer for embedded-cli output.
// Exported for embedded-cli binding as cli->writeChar.
// Called by embeddedCliPrint() for every character.
// CONSTRAINT: The caller (consoleSerialEmitLine) holds the serial mutex.
// This writer must NOT attempt to take the mutex.
void consoleSerialWriteChar(EmbeddedCli* cli, char c);

// Bind the serial output coordinator to an embedded-cli instance.
// Called once during console task initialization with the live CLI.
// Allows a host test to inject a test CLI for harness verification.
void consoleSerialBindCli(EmbeddedCli* cli);

// Emit a complete line through the serial output coordinator.
// Takes the serial mutex, calls embeddedCliPrint() (which clears the current
// input line, prints the line, and redraws the prompt + buffered command),
// then gives the mutex. Safe for log/event/record emission from any Core 0 path.
//
// CONSTRAINT: The mutex is NON-RECURSIVE (xSemaphoreCreateMutexStatic creates
// non-recursive). Nothing called from inside this function may emit another log
// or call consoleSerialEmitLine recursively, or the console task will self-deadlock.
// paLogLine() holds the same mutex with portMAX_DELAY, so any Log emitted from
// inside consoleSerialEmitLine will block forever.
void consoleSerialEmitLine(const char* line);

// Emit exactly one line, plus its trailing newline, in a SINGLE Serial.write()
// call, under the serial mutex (ADR 0036). This is the one emit helper both
// the Console Record sink (src/tasks/console_task.cpp's emitRecordLine) and
// the pre-console-task log fallback (this file's consoleSerialEmitLine, the
// "no coordination available" branch) call, so the framing fix and the wait
// seam live in exactly one place. `line` is truncated to
// PA_LOG_SERIAL_LINE_MAX - 1 bytes first, matching every other serial-bound
// line in this file.
//
// This replaces the #245-era shape where a line and its newline were two
// independent Serial.write() calls that could succeed and fail independently
// -- a dropped line with its newline still delivered is what produced the
// blank-line drop signature ADR 0036 documents. One call means one outcome.
//
// waitForRoom selects which of ADR 0036's two policies applies:
//  - true  (Console Records): before taking the mutex, waits up to
//    CONSOLE_RECORD_ROOM_WAIT_BOUND_MS for Serial.availableForWrite() to
//    clear room for the whole line, and only while `Serial` (bool) reports a
//    connected host -- the same board-portable check console_task.cpp's
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
//    the driver's best-effort contract.
//
// CONSTRAINT: must be called WITHOUT the serial mutex already held. The wait
// (when requested) happens BEFORE the mutex take, specifically so it can
// never be inherited by a TWDT-subscribed task's portMAX_DELAY take in its
// own paLogLine() path -- waiting inside the mutex would re-open #245's
// starvation through a different door (ADR 0036, "Considered options").
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
// same as every other record field.
size_t consoleSerialFormatDroppedSuffix(char* buffer, size_t bufferSize, uint32_t droppedCount);

// Get the serial mutex used for atomic log and console output coordination
// Declared in src/main.cpp, accessed by console_task.cpp
SemaphoreHandle_t paGetSerialMutex(void);
