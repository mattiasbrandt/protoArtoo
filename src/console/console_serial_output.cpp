// =============================================================================
// src/console/console_serial_output.cpp
//
// Serial output coordinator for Console task (ADR 0034)
// Routes log/event/record output through embeddedCliPrint() under serial mutex.
// Proves the serial output coordinator criterion from #217.
// =============================================================================

#include "console_serial_output.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

extern "C" {
#include "embedded_cli.h"
}

// Static reference to the bound CLI instance, set during console task init
static EmbeddedCli* g_boundCli = nullptr;

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
        Serial.print(line);
        Serial.print("\n");
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
    embeddedCliPrint(g_boundCli, line);

    // Give the mutex back
    xSemaphoreGive(mutex);
}
