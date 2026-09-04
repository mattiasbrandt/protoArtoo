// =============================================================================
// src/console/console_serial_output.cpp
//
// Serial output coordinator for Console task (ADR 0034, ADR 0036, ADR 0037)
// This file is the ONLY writer of the serial wire in the firmware: every
// record, log line, echoed character and banner reaches it through
// consoleSerialWriteFrame below, one Serial.write() per unit, and after the
// bind only the Console task calls any of it. Proves the serial output
// coordinator criterion from #217, the single-write-framing / room-wait seam
// from #265, #268's extension of that framing to the interactive redraw, and
// #270's single ownership (test/test_tools/test_one_serial_seam.py enforces
// the "only writer" half against the whole tree).
// =============================================================================

#include "console_serial_output.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "logging.h"

extern "C" {
#include "embedded_cli.h"
}

// Static reference to the bound CLI instance, set during console task init
static EmbeddedCli* g_boundCli = nullptr;

// =============================================================================
// Per-Character Writer
// =============================================================================

// Exported for seam testing and embedded-cli binding.
// The ECHO path: embeddedCliProcess() calls this for every character the line
// editor puts on screen while the operator types, from the Console task
// (src/tasks/console_task.cpp's main loop). No lock: the Console task is the
// only writer of this wire (ADR 0037), so an echoed byte has nothing to be
// interleaved with. Best-effort like every other keystroke echo -- a
// character the transport refuses is one the operator retypes, and waiting
// per byte would put the room-wait bound on every keystroke.
void consoleSerialWriteChar(EmbeddedCli* cli, char c) {
    (void)cli;  // Unused
    consoleSerialWriteFrame(&c, 1, /*waitForRoom=*/false);
}

// =============================================================================
// Seam Implementation
// =============================================================================

void consoleSerialBindCli(EmbeddedCli* cli) {
    g_boundCli = cli;
    // The bind IS the switch to ring-only logging (ADR 0037): from here on
    // paLogLine() only appends, and consoleSerialDrainLogs() below is what
    // puts a log line on the wire. Placing the drain cursor and raising the
    // ownership flag happen together, inside the ring's own critical section,
    // so the line-in-flight at this instant is written exactly once
    // (src/main.cpp's paLogWireBindToConsole).
    paLogWireBindToConsole();
}

void consoleSerialWriteText(const char* text) {
    if (text == nullptr) {
        return;
    }
    // Best-effort: see the header. A wait would refuse to write while the CDC
    // reports no host, which is the state a cold-booted FireBeetle 2 is in.
    consoleSerialWriteFrame(text, strlen(text), /*waitForRoom=*/false);
}

void consoleSerialEmitLine(const char* line) {
    if (line == nullptr) {
        return;
    }

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

    if (g_boundCli == nullptr) {
        // Nothing to render a redraw against (no CLI bound). Send the line on
        // its own through the same single-write seam. Reachable only before
        // the bind, and paLogLine() takes the same path there for the same
        // reason; kept as a guard rather than an assumption.
        consoleSerialEmitFramedLine(lineToEmit, lineLen, /*waitForRoom=*/false);
        return;
    }

    // Render what embeddedCliPrint() would have written -- clear the input
    // line, the log line, the break, the prompt, the buffered command, the
    // cursor move -- into one buffer (lib/embedded-cli/VENDORED.md Patch 8)
    // and put it on the wire in ONE write. Character-at-a-time is what let
    // another writer on this wire land a byte inside the line (#268).
    //
    // The buffer is a LOCAL, and that is the point of ADR 0037's ownership
    // change. It used to be static, because any task could be inside this
    // function and 448 bytes on every logging task's stack is a cost none of
    // them budgeted for. Only the Console task reaches it now, so the frame
    // lives on that one task's stack and artoo-esp32 gets the .bss back.
    char frame[CONSOLE_SERIAL_FRAME_MAX];
    const size_t frameLen =
        embeddedCliPrintToBuffer(g_boundCli, lineToEmit, frame, sizeof(frame));

    if (frameLen > 0) {
        // Waits for transmit room under the record bound (ADR 0037). If the
        // room never comes the frame is dropped whole and the render has
        // already advanced the editor's screen bookkeeping -- the operator
        // sees a prompt redrawn one line later than the editor believes,
        // which their next keystroke or the next drained line repairs. The
        // line itself is never lost: /api/logs still has it.
        consoleSerialWriteFrame(frame, frameLen, /*waitForRoom=*/true);
        return;
    }

    // The redraw did not fit, so nothing was rendered and the line editor
    // was left untouched. Send the line on its own, whole, through the
    // same single-write seam: the operator still sees the line, and the
    // prompt comes back with their next keystroke or the next log line.
    consoleSerialEmitFramedLine(lineToEmit, lineLen, /*waitForRoom=*/true);
}

uint32_t consoleSerialDrainLogs(void) {
    // No host on the other end: leave the lines in the ring rather than
    // writing them into a transport that will refuse them.
    //
    // This is what keeps ADR 0037's "the only loss is ring eviction" true on
    // the FireBeetle 2. A drained line waits for room like a record, and the
    // room-wait refuses outright while `Serial` reports no connected host -
    // so draining into a detached CDC would advance the cursor over lines
    // that never reached anyone, silently. Holding the cursor instead means
    // the ring fills, and the operator who attaches gets the marker plus
    // whatever the ring still holds. On artoo-esp32 `Serial` reports only
    // that the UART driver is installed (HardwareSerial::operator bool()),
    // which is true from setup() onwards, so this guard is inert there.
    //
    // The residual, stated rather than hidden: a host that is attached but has
    // stopped reading can still make a single frame miss its room wait, and
    // that one line is then lost from the wire without a marker. That is ADR
    // 0036's record policy applied to logs, which is what ADR 0037 asked for;
    // /api/logs still has the line either way.
    if (!Serial) {
        return 0;
    }

    uint32_t written = 0;
    uint32_t reads = 0;
    char line[LOG_LINE_MAX];
    uint32_t evicted = 0;

    // Bounded at the ring's own depth. The ring cannot hold more lines than
    // that, so one call always catches up with everything already written;
    // what the bound refuses is being held here indefinitely by writers
    // appending as fast as the wire drains, which at a record boundary would
    // stall the command that is mid-answer. Whatever is left waits for the
    // next of this function's three call sites.
    while (reads < LOG_RING_MAX_LINES && paLogDrainNextLine(line, sizeof(line), &evicted)) {
        reads++;
        if (evicted > 0) {
            // Ring eviction is the only remaining way a log line is lost from
            // the wire, and it is marked rather than silent (ADR 0037). The
            // count is formatted through the same helper a record's closing
            // line uses, so the `dropped=<n>` idiom cannot drift between the
            // two things that can be dropped.
            char marker[48];
            char suffix[24];
            consoleSerialFormatDroppedSuffix(suffix, sizeof(suffix), evicted);
            snprintf(marker, sizeof(marker), "[log]%s", suffix);
            consoleSerialEmitLine(marker);
            written++;
        }
        consoleSerialEmitLine(line);
        written++;
    }
    return written;
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

        // Only the Console task ever spends this wait, and it is not
        // TWDT-subscribed (src/tasks/console_task.cpp's file header). That is
        // what ADR 0037 changed: the wait used to be reachable from any
        // logging task's context, which is why ADR 0036 had to keep it
        // strictly outside the lock.
        uint32_t waitedMs = 0;
        while (Serial && Serial.availableForWrite() < (int)reserve &&
               waitedMs < CONSOLE_RECORD_ROOM_WAIT_BOUND_MS) {
            vTaskDelay(pdMS_TO_TICKS(1));
            waitedMs++;
        }
        if (!Serial || Serial.availableForWrite() < (int)reserve) {
            // Room never cleared (or the host was never connected to begin
            // with): drop the frame whole. Nothing is written -- a half-sent
            // line is exactly the #245-era failure mode this function exists
            // to remove.
            return false;
        }
    }

    // The one Serial.write() in the firmware. One call per unit is the whole
    // point (ADR 0036, #268): a unit delivered in one call cannot be
    // half-delivered by a transport that short-writes, and with a single
    // owner (ADR 0037) there is nothing else on this wire to interleave it.
    Serial.write((const uint8_t*)bytes, len);
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
