// =============================================================================
// src/tasks/console_task.cpp
//
// ConsoleTask - Serial console adapter using embedded-cli
// Core 0, non-real-time, runs from setup() regardless of network state.
// No dynamic allocation in its loop; uses static buffers.
//
// Responsibilities:
//  - Initialize and maintain embedded-cli instance
//  - Accept user input from UART0 (USB CDC on P4, serial bridge on artoo)
//  - Execute commands through the Console module
//  - Format and output Console Records to serial
//  - Maintain atomic serial output under logSerialMutex (no interleaving with logs)
// =============================================================================

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <string.h>

#include "logging.h"
#include "console_module.h"
#include "console_record.h"

// Include embedded-cli (vendored at lib/embedded-cli/)
// The library provides the line editor and buffer management
extern "C" {
#include "embedded_cli.h"
}

static const char* TAG = "ConsoleTask";

// =============================================================================
// Static Configuration and State
// =============================================================================

// Output buffer for a single Console Record line
static char recordBuffer[CONSOLE_RECORD_LINE_MAX] = {};
static const size_t recordBufferSize = sizeof(recordBuffer);

// embedded-cli instance
static EmbeddedCli* embeddedCli = nullptr;

// =============================================================================
// Embedded-CLI Callbacks
// =============================================================================

// Called by embedded-cli when output is needed (echo, prompts, etc.)
static void onCliWrite(EmbeddedCli* cli, char c) {
    (void)cli;  // Unused

    // T1 scope: basic serial output without mutex coordination.
    // T2+ (ADR 0034) adds serial output coordinator to atomize log/output lines.
    Serial.write((uint8_t)c);
}

// Called by embedded-cli when a complete command line is ready
static void onCliCommand(EmbeddedCli* cli, CliCommand* cmd) {
    (void)cli;  // Unused

    if (cmd == nullptr || cmd->name == nullptr) {
        return;
    }

    PA_LOG_DEBUG(TAG, "command received: %s", cmd->name);

    // Get the next request ID
    uint32_t requestId = consoleGetNextRequestId();

    // Create a console request
    ConsoleRequest request = {
        .requestId = requestId,
        .source = CONSOLE_SOURCE_SERIAL,
        .operationName = cmd->name,
    };

    // Create sink for records output - captures output to serial
    ConsoleRecordSink sink = {
        .onRecordBegin = nullptr,    // Not implemented in T1
        .onRecordField = nullptr,    // Not implemented in T1
        .onRecordItem = nullptr,     // Not implemented in T1
        .onRecordEnd = nullptr,      // Not implemented in T1
    };

    // Execute through Console module (T2+ will implement output callbacks)
    consoleExecuteCommand(&request, &sink);
}

// =============================================================================
// Task Implementation
// =============================================================================

void consoleTask(void* pvParameters) {
    (void)pvParameters;

    PA_LOG_INFO(TAG, "active");

    // Initialize embedded-cli with default configuration
    // Stack sized from measured high-water mark: 96 B largest frame + margin
    embeddedCli = embeddedCliNewDefault();
    if (embeddedCli == nullptr) {
        PA_LOG_ERROR(TAG, "failed to initialize embedded-cli");
        vTaskDelete(nullptr);
        return;
    }

    // Set up embedded-cli callbacks
    embeddedCli->writeChar = onCliWrite;
    embeddedCli->onCommand = onCliCommand;

    // Print initial prompt
    Serial.print("> ");
    Serial.flush();

    // Measure stack high water mark
    bool hwmLogged = false;
    TickType_t lastMs = xTaskGetTickCount();

    // Main loop: read from UART and process through embedded-cli
    while (true) {
        // Log stack high water mark once
        if (!hwmLogged) {
            UBaseType_t freeStack = uxTaskGetStackHighWaterMark(nullptr);
            PA_LOG_DEBUG(TAG, "stack HWM: %u words free", (unsigned)freeStack);
            hwmLogged = true;
        }

        // Check for serial data and feed to embedded-cli
        while (Serial.available()) {
            int byte = Serial.read();
            if (byte >= 0) {
                // Feed character to embedded-cli (non-blocking)
                // embeddedCliProcess handles the actual command processing
                embeddedCliReceiveChar(embeddedCli, (char)byte);
            }
        }

        // Process embedded-cli state machine
        embeddedCliProcess(embeddedCli);

        // Yield to prevent starving other tasks
        vTaskDelay(pdMS_TO_TICKS(10));

        // Periodic heartbeat
        TickType_t nowMs = xTaskGetTickCount();
        if ((nowMs - lastMs) > pdMS_TO_TICKS(5000)) {
            PA_LOG_DEBUG(TAG, "console task running");
            lastMs = nowMs;
        }
    }
}
