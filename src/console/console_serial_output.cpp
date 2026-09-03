// =============================================================================
// src/console/console_serial_output.cpp
//
// Serial output coordinator for Console task (ADR 0034, ADR 0036)
// Routes log/event/record output through embeddedCliPrint() under serial mutex.
// Proves the serial output coordinator criterion from #217, and the
// single-write-framing / room-wait seam from #265.
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

// =============================================================================
// Per-Character Writer
// =============================================================================

// Exported for seam testing and embedded-cli binding.
// Called by embeddedCliPrint() for every character of output.
// CONSTRAINT: The mutex is held by the caller (consoleSerialEmitLine).
// This writer must NOT attempt to take the mutex or any other blocking operation.
void consoleSerialWriteChar(EmbeddedCli* cli, char c) {
    (void)cli;  // Unused
    Serial.write((uint8_t)c);
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

    // Take the serial mutex for the entire emission (atomic under the lock)
    if (xSemaphoreTake(mutex, portMAX_DELAY) != pdTRUE) {
        // Should not happen with portMAX_DELAY, but fail gracefully
        return;
    }

    // For the bound path with embeddedCliPrint, truncate if needed inside the lock
    // to avoid data races. Create a local buffer only if truncation is required.
    size_t lineLen = strlen(line);
    const char* lineToEmit = line;
    char lineBuf[PA_LOG_SERIAL_LINE_MAX];

    if (lineLen >= PA_LOG_SERIAL_LINE_MAX) {
        // Truncate to max length (excluding null terminator)
        strncpy(lineBuf, line, PA_LOG_SERIAL_LINE_MAX - 1);
        lineBuf[PA_LOG_SERIAL_LINE_MAX - 1] = '\0';
        lineToEmit = lineBuf;
    }

    // Use embeddedCliPrint() which implements the coordinator:
    // - clears the current input line (carriage return + spaces + carriage return)
    // - writes the line plus a line break
    // - redraws the prompt
    // - redraws the buffered command
    // - restores cursor position
    embeddedCliPrint(g_boundCli, lineToEmit);

    // Give the mutex back
    xSemaphoreGive(mutex);
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

    if (waitForRoom) {
        // Outside the mutex, on purpose (see the CONSTRAINT in the header):
        // a TWDT-subscribed task's paLogLine() takes the same mutex with
        // portMAX_DELAY, and this wait must never be something it inherits.
        uint32_t waitedMs = 0;
        while (Serial && Serial.availableForWrite() < (int)total &&
               waitedMs < CONSOLE_RECORD_ROOM_WAIT_BOUND_MS) {
            vTaskDelay(pdMS_TO_TICKS(1));
            waitedMs++;
        }
        if (!Serial || Serial.availableForWrite() < (int)total) {
            // Room never cleared (or the host was never connected to begin
            // with): drop the record whole. Nothing is written, and the
            // mutex is never taken -- a half-sent line is exactly the
            // #245-era failure mode this function exists to remove.
            return false;
        }
    }

    SemaphoreHandle_t mutex = paGetSerialMutex();
    if (mutex != nullptr) {
        xSemaphoreTake(mutex, portMAX_DELAY);
    }
    Serial.write((const uint8_t*)buf, total);
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
