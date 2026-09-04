// =============================================================================
// src/console/console_serial_output.cpp
//
// Serial output coordinator for Console task (ADR 0034, ADR 0036)
// Every log/event/record byte reaches the wire through consoleSerialWriteFrame
// below: one Serial.write() per unit, under the serial mutex. Proves the serial
// output coordinator criterion from #217, the single-write-framing / room-wait
// seam from #265, and #268's extension of that framing to the interactive
// redraw itself (see the header for the interleaving it removes).
// =============================================================================

#include "console_serial_output.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "logging.h"

extern "C" {
#include "embedded_cli.h"
}

// Static reference to the bound CLI instance, set during console task init
static EmbeddedCli* g_boundCli = nullptr;

// The single Serial.write() call every Console byte reaches the wire through.
// One call per unit is the whole point (ADR 0036, #268): a unit delivered in
// one call cannot be interleaved by another writer on this wire, and cannot be
// half-delivered by a transport that short-writes.
//
// CONSTRAINT: the serial mutex must already be held. consoleSerialWriteFrame()
// is the entry point that takes it; consoleSerialEmitLine() calls this
// directly because it holds the mutex across its render as well as its write,
// and the mutex is non-recursive.
static void consoleSerialWriteFrameLocked(const char* bytes, size_t len) {
    Serial.write((const uint8_t*)bytes, len);
}

// =============================================================================
// Per-Character Writer
// =============================================================================

// Exported for seam testing and embedded-cli binding.
// The ECHO path: embeddedCliProcess() calls this for every character the line
// editor puts on screen while the operator types, from the Console task, which
// holds no lock there (src/tasks/console_task.cpp's main loop). So the byte
// takes the serial mutex itself -- one character is its own frame -- and lands
// either before or after a log line's frame, never inside it.
// CONSTRAINT: must be called WITHOUT the serial mutex held; see the header.
void consoleSerialWriteChar(EmbeddedCli* cli, char c) {
    (void)cli;  // Unused
    consoleSerialWriteFrame(&c, 1, /*waitForRoom=*/false);
}

// =============================================================================
// Seam Implementation
// =============================================================================

void consoleSerialBindCli(EmbeddedCli* cli) {
    g_boundCli = cli;
}

void consoleSerialEmitLine(const char* line) {
    if (line == nullptr) {
        return;
    }

    SemaphoreHandle_t mutex = paGetSerialMutex();
    if (mutex == nullptr || g_boundCli == nullptr) {
        // No coordination available (boot, before console task binds the
        // CLI - consoleSerialBindCli() runs from consoleTask(), so every
        // paLogLine() call made during setup() lands here). Best-effort:
        // routes through the single shared framed writer with
        // waitForRoom=false, same #245 contract as always. This used to
        // write the line and its newline as two independent Serial.write()
        // calls -- the exact shape ADR 0036 measured dropping whole lines
        // while keeping their newline (a blank-line drop signature) -- so it
        // now goes through the one place that fix lives, same as the record
        // sink.
        consoleSerialEmitFramedLine(line, strlen(line), /*waitForRoom=*/false);
        return;
    }

    // Take the serial mutex for the entire emission (atomic under the lock).
    // It covers the render as well as the write: the frame buffer below is
    // static (see its comment) and embeddedCliPrintToBuffer mutates the line
    // editor's own state, so two logging tasks must not be inside it at once.
    if (xSemaphoreTake(mutex, portMAX_DELAY) != pdTRUE) {
        // Should not happen with portMAX_DELAY, but fail gracefully
        return;
    }

    // For the bound path, truncate if needed inside the lock to avoid data
    // races. Create a local buffer only if truncation is required.
    size_t lineLen = strlen(line);
    const char* lineToEmit = line;
    char lineBuf[PA_LOG_SERIAL_LINE_MAX];

    if (lineLen >= PA_LOG_SERIAL_LINE_MAX) {
        // Truncate to max length (excluding null terminator)
        strncpy(lineBuf, line, PA_LOG_SERIAL_LINE_MAX - 1);
        lineBuf[PA_LOG_SERIAL_LINE_MAX - 1] = '\0';
        lineToEmit = lineBuf;
        lineLen = PA_LOG_SERIAL_LINE_MAX - 1;
    }

    // Render what embeddedCliPrint() would have written -- clear the input
    // line, the log line, the break, the prompt, the buffered command, the
    // cursor move -- into one buffer (lib/embedded-cli/VENDORED.md Patch 8)
    // and put it on the wire in ONE write. Character-at-a-time is what let
    // another writer on this wire land a byte inside the line (#268).
    //
    // The buffer is static, not a local: any task can log, and 448 bytes on
    // every logging task's stack is a cost none of them budgeted for. The
    // serial mutex held here is what makes one shared buffer safe.
    static char frame[CONSOLE_SERIAL_FRAME_MAX];
    const size_t frameLen =
        embeddedCliPrintToBuffer(g_boundCli, lineToEmit, frame, sizeof(frame));

    if (frameLen > 0) {
        consoleSerialWriteFrameLocked(frame, frameLen);
    }

    // Give the mutex back
    xSemaphoreGive(mutex);

    if (frameLen == 0) {
        // The redraw did not fit, so nothing was rendered and the line editor
        // was left untouched. Send the line on its own, whole, through the
        // same single-write seam: the operator still sees the line, and the
        // prompt comes back with their next keystroke or the next log line.
        // Outside the mutex on purpose -- consoleSerialEmitFramedLine takes
        // it, and it is non-recursive.
        consoleSerialEmitFramedLine(lineToEmit, lineLen, /*waitForRoom=*/false);
    }
}

// =============================================================================
// Single-write framed emitter (ADR 0036)
// =============================================================================

bool consoleSerialEmitFramedLine(const char* line, size_t len, bool waitForRoom) {
    if (line == nullptr) {
        return true;  // nothing to drop
    }

    // One buffer holding the line AND its CR LF terminator, so the wire only
    // ever sees them delivered together by a single Serial.write() call --
    // see this function's header comment for the two-call defect this
    // replaces.
    //
    // CR LF, not a bare LF (#267): a Console session must be attached in raw
    // mode so Tab and cursor bytes reach the firmware unedited
    // (docs/console-protocol.md 8), and raw mode disables the kernel's ONLCR
    // NL->CR-NL translation. A bare LF then feeds the line down without
    // returning the carriage, so every line starts one column further right
    // than the last. embedded-cli already terminates the interactive log path
    // with "\r\n" (lib/embedded-cli/src/embedded_cli.c:235's lineBreak), so
    // this is what makes records and logs agree on the one wire they share.
    // The buffer is one byte longer than the content cap for the same reason:
    // the "truncate to PA_LOG_SERIAL_LINE_MAX - 1" content rule is unchanged,
    // only the terminator grew.
    size_t lineLen = len;
    if (lineLen > PA_LOG_SERIAL_LINE_MAX - 1) {
        lineLen = PA_LOG_SERIAL_LINE_MAX - 1;
    }
    char buf[PA_LOG_SERIAL_LINE_MAX + 1];
    memcpy(buf, line, lineLen);
    buf[lineLen] = '\r';
    buf[lineLen + 1] = '\n';
    // ADR 0036 reserves room for the WHOLE line including its terminator, so
    // the reservation follows the extra byte: a record must never be written
    // short for want of the CR.
    size_t total = lineLen + 2;

    return consoleSerialWriteFrame(buf, total, waitForRoom);
}

bool consoleSerialWriteFrame(const char* bytes, size_t len, bool waitForRoom) {
    if (bytes == nullptr || len == 0) {
        return true;  // nothing to send, so nothing to drop
    }

    if (waitForRoom) {
        // Never reserve more room than this transport can ever report having
        // (CONSOLE_SERIAL_TX_ROOM_MAX, console_serial_output.h). Asking for
        // more is a wait that cannot succeed: it burns the whole bound and
        // then drops a frame the transport would have accepted. Above the
        // ceiling the reservation means "as empty as this transport gets",
        // and the transport's own write path carries the remainder -- blocking
        // on UART0, chunked through the TX ring on the CDC.
        const size_t reserve = len < CONSOLE_SERIAL_TX_ROOM_MAX ? len : CONSOLE_SERIAL_TX_ROOM_MAX;

        // Outside the mutex, on purpose (see the CONSTRAINT in the header):
        // a TWDT-subscribed task's paLogLine() takes the same mutex with
        // portMAX_DELAY, and this wait must never be something it inherits.
        uint32_t waitedMs = 0;
        while (Serial && Serial.availableForWrite() < (int)reserve &&
               waitedMs < CONSOLE_RECORD_ROOM_WAIT_BOUND_MS) {
            vTaskDelay(pdMS_TO_TICKS(1));
            waitedMs++;
        }
        if (!Serial || Serial.availableForWrite() < (int)reserve) {
            // Room never cleared (or the host was never connected to begin
            // with): drop the frame whole. Nothing is written, and the
            // mutex is never taken -- a half-sent line is exactly the
            // #245-era failure mode this function exists to remove.
            return false;
        }
    }

    SemaphoreHandle_t mutex = paGetSerialMutex();
    if (mutex != nullptr) {
        xSemaphoreTake(mutex, portMAX_DELAY);
    }
    consoleSerialWriteFrameLocked(bytes, len);
    if (mutex != nullptr) {
        xSemaphoreGive(mutex);
    }
    return true;
}

size_t consoleSerialFormatDroppedSuffix(char* buffer, size_t bufferSize, uint32_t droppedCount) {
    if (buffer == nullptr || bufferSize == 0) {
        return 0;
    }
    if (droppedCount == 0) {
        buffer[0] = '\0';
        return 0;
    }
    int written = snprintf(buffer, bufferSize, " dropped=%lu", (unsigned long)droppedCount);
    if (written < 0 || (size_t)written >= bufferSize) {
        // Truncation would corrupt the record envelope worse than omitting
        // the field; drop it rather than emit a torn `dropped=` token.
        buffer[0] = '\0';
        return 0;
    }
    return (size_t)written;
}
