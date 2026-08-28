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

    SemaphoreHandle_t mutex = paGetSerialMutex();
    if (mutex == nullptr || g_boundCli == nullptr) {
        // No coordination available (boot, before console task). Direct write.
        // Cap line length before write to prevent serial buffer overflow.
        // PA_LOG_* macro lines are already bounded (logging.h:33-42), so this
        // only truncates direct paLogLine() calls with over-length strings.
        size_t lineLen = strlen(line);
        if (lineLen > PA_LOG_SERIAL_LINE_MAX - 1) {
            lineLen = PA_LOG_SERIAL_LINE_MAX - 1;
        }

        // Take mutex if available to serialize with other writers
        if (mutex != nullptr) {
            xSemaphoreTake(mutex, portMAX_DELAY);
        }
        Serial.write((const uint8_t*)line, lineLen);
        Serial.write('\n');
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
