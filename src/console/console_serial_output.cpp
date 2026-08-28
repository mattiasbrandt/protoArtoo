// =============================================================================
// src/console/console_serial_output.cpp
//
// Serial output coordinator for Console task (ADR 0034)
// Routes log/event/record output through embeddedCliPrint() under serial mutex.
// Proves the serial output coordinator criterion from #217.
// =============================================================================

#include "console_serial_output.h"

#include <Arduino.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

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

    // Cap line length to prevent serial buffer overflow
    static char lineBuf[PA_LOG_SERIAL_LINE_MAX];
    size_t lineLen = strlen(line);
    const char* lineToEmit = line;

    if (lineLen >= PA_LOG_SERIAL_LINE_MAX) {
        // Truncate to max length (excluding null terminator)
        strncpy(lineBuf, line, PA_LOG_SERIAL_LINE_MAX - 1);
        lineBuf[PA_LOG_SERIAL_LINE_MAX - 1] = '\0';
        lineToEmit = lineBuf;
    }

    SemaphoreHandle_t mutex = paGetSerialMutex();
    if (mutex == nullptr || g_boundCli == nullptr) {
        // No coordination available (boot, before console task). Direct write.
        // Still take the mutex if available, to serialize with paLogLine writes.
        if (mutex != nullptr) {
            xSemaphoreTake(mutex, portMAX_DELAY);
        }
        Serial.print(lineToEmit);
        Serial.print("\n");
        if (mutex != nullptr) {
            xSemaphoreGive(mutex);
        }
        return;
    }

    // Take the serial mutex for the entire emission (atomic under the lock)
    if (xSemaphoreTake(mutex, portMAX_DELAY) != pdTRUE) {
        // Should not happen with portMAX_DELAY, but fail gracefully
        return;
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
