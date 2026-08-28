// =============================================================================
// src/tasks/console_task.cpp
//
// ConsoleTask - Serial console adapter using embedded-cli (ADR 0034)
// Core 0, non-real-time, created in setup() regardless of network state.
// No dynamic allocation in its loop; uses static buffers.
//
// Responsibilities:
//  - Initialize embedded-cli with static buffer (no dynamic allocation)
//  - Accept user input from UART0 (USB CDC on P4, serial bridge on artoo)
//  - Execute commands through the Console module
//  - Format and output Console Records to serial, atomized under serial mutex
//  - Maintain atomic output: log arriving mid-entry clears line, writes record, redraws prompt
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
extern "C" {
#include "embedded_cli.h"
}

static const char* TAG = "ConsoleTask";

// =============================================================================
// Forward declarations for record sink callbacks
// =============================================================================
static void onRecordBegin(uint32_t requestId, const char* operationType);
static void onRecordField(uint32_t requestId, const char* name, const char* value);
static void onRecordItem(uint32_t requestId, const char* value);
static void onRecordEnd(uint32_t requestId, ConsoleStatus status, ConsoleOutcome outcome,
                       ConsoleReason reason);

// =============================================================================
// Static Configuration and State
// =============================================================================

// Static buffer for embedded-cli instance (no dynamic allocation per criterion)
// Size from embedded_cli.h:206 - required buffer size is computed from config
static EmbeddedCliConfig embeddedCliConfig = {};
static EmbeddedCli* embeddedCli = nullptr;
static CLI_UINT embeddedCliBuffer[512];  // Sized for embeddedCliRequiredSize; CLI_UINT is size-aligned

// Output buffer for a single Console Record line
static char recordBuffer[CONSOLE_RECORD_LINE_MAX] = {};

// Current request ID for this command (for stack HWM measurement after first command)
static uint32_t currentRequestId = 0;

// =============================================================================
// Embedded-CLI Callbacks
// =============================================================================

// Called by embedded-cli when output is needed (echo, prompts, editing)
static void onCliWrite(EmbeddedCli* cli, char c) {
    (void)cli;  // Unused

    // ADR 0034: atomic serial output under serial mutex
    // Non-blocking take with short timeout to avoid starving embedded-cli
    SemaphoreHandle_t mutex = paGetSerialMutex();
    if (mutex != nullptr) {
        if (xSemaphoreTake(mutex, 0) == pdTRUE) {
            Serial.write((uint8_t)c);
            xSemaphoreGive(mutex);
        } else {
            // Mutex held by log writer; just write without coordination
            // (T2+: implement full coordination with line clearing/redraw)
            Serial.write((uint8_t)c);
        }
    } else {
        Serial.write((uint8_t)c);
    }
}

// Called by embedded-cli when a complete command line is ready
static void onCliCommand(EmbeddedCli* cli, CliCommand* cmd) {
    (void)cli;  // Unused

    if (cmd == nullptr || cmd->name == nullptr) {
        return;
    }

    // Parse command (T1: system.status.health, help, operations, unknown)
    const char* commandName = cmd->name;

    // Get request ID (global across both adapters)
    uint32_t requestId = consoleGetNextRequestId();
    currentRequestId = requestId;

    // Create console request
    ConsoleRequest request = {
        .requestId = requestId,
        .source = CONSOLE_SOURCE_SERIAL,
        .operationName = commandName,
    };

    // Create sink for record output (implemented inline below)
    ConsoleRecordSink sink = {
        .onRecordBegin = onRecordBegin,
        .onRecordField = onRecordField,
        .onRecordItem = onRecordItem,
        .onRecordEnd = onRecordEnd,
    };

    // Execute through Console module (ADR 0034)
    consoleExecuteCommand(&request, &sink);
}

// =============================================================================
// Console Record Sink Callbacks (output formatting)
// =============================================================================

static void onRecordBegin(uint32_t requestId, const char* operationType) {
    SemaphoreHandle_t mutex = paGetSerialMutex();
    if (mutex != nullptr) {
        xSemaphoreTake(mutex, portMAX_DELAY);
    }

    // Emit: < id=<n> type=begin operation=system.status.health
    size_t len = snprintf(recordBuffer, sizeof(recordBuffer),
                         "< id=%lu type=begin operation=%s",
                         (unsigned long)requestId, operationType);
    if (len < sizeof(recordBuffer)) {
        Serial.write((const uint8_t*)recordBuffer, len);
        Serial.write('\n');
    }
    // Mutex held for the entire record sequence (begin -> field... -> end)
}

static void onRecordField(uint32_t requestId, const char* name, const char* value) {
    // Mutex held from onRecordBegin
    // Emit: < id=<n> type=field name=<key> value=<value>
    size_t len = snprintf(recordBuffer, sizeof(recordBuffer),
                         "< id=%lu type=field name=%s value=%s",
                         (unsigned long)requestId, name, value);
    if (len < sizeof(recordBuffer)) {
        Serial.write((const uint8_t*)recordBuffer, len);
        Serial.write('\n');
    }
}

static void onRecordItem(uint32_t requestId, const char* value) {
    (void)requestId;
    (void)value;
    // T2+ scope: list items
}

static void onRecordEnd(uint32_t requestId, ConsoleStatus status, ConsoleOutcome outcome,
                       ConsoleReason reason) {
    // Emit: < id=<n> type=end status=ok outcome=queued [reason=...]
    char reasonStr[64] = {};
    if (status == CONSOLE_STATUS_ERR && reason != CONSOLE_REASON_NOT_IN_THIS_BUILD) {
        snprintf(reasonStr, sizeof(reasonStr), " reason=%s", consoleReasonString(reason));
    }

    size_t len =
        snprintf(recordBuffer, sizeof(recordBuffer),
                 "< id=%lu type=end status=%s outcome=%s%s", (unsigned long)requestId,
                 consoleStatusString(status), consoleOutcomeString(outcome), reasonStr);
    if (len < sizeof(recordBuffer)) {
        Serial.write((const uint8_t*)recordBuffer, len);
        Serial.write('\n');
    }

    // Release serial mutex after complete response
    SemaphoreHandle_t mutex = paGetSerialMutex();
    if (mutex != nullptr) {
        xSemaphoreGive(mutex);
    }
}

// =============================================================================
// Task Implementation
// =============================================================================

void consoleTask(void* pvParameters) {
    (void)pvParameters;

    PA_LOG_INFO(TAG, "active");

    // Initialize embedded-cli with static buffer configuration (no dynamic allocation)
    // Per embedded_cli.h:160-217, use embeddedCliRequiredSize + static buffer
    embeddedCliConfig.cliBuffer = embeddedCliBuffer;
    embeddedCliConfig.cliBufferSize = sizeof(embeddedCliBuffer);
    embeddedCliConfig.rxBufferSize = 256;  // Input line buffer

    embeddedCli = embeddedCliNew(&embeddedCliConfig);
    if (embeddedCli == nullptr) {
        PA_LOG_ERROR(TAG, "failed to initialize embedded-cli with static buffer");
        vTaskDelete(nullptr);
        return;
    }

    // Set up embedded-cli callbacks
    embeddedCli->writeChar = onCliWrite;
    embeddedCli->onCommand = onCliCommand;

    // Print initial prompt under serial mutex
    SemaphoreHandle_t mutex = paGetSerialMutex();
    if (mutex != nullptr) {
        xSemaphoreTake(mutex, portMAX_DELAY);
    }
    Serial.print("> ");
    if (mutex != nullptr) {
        xSemaphoreGive(mutex);
    }
    Serial.flush();

    // Measure stack high water mark
    bool hwmLogged = false;

    // Main loop: read from UART and process through embedded-cli
    while (true) {
        // Log stack high water mark once after first command is processed
        // (ADR 0034: stack sized from measured high-water mark with margin)
        if (!hwmLogged && currentRequestId > 0) {
            UBaseType_t freeStack = uxTaskGetStackHighWaterMark(nullptr);
            PA_LOG_DEBUG(TAG, "stack HWM: %u words free (96 B frame + margin)", (unsigned)freeStack);
            hwmLogged = true;
        }

        // Process any available serial data
        while (Serial.available()) {
            int byte = Serial.read();
            if (byte >= 0) {
                // Feed character to embedded-cli (non-blocking)
                // Calls onCommand when a complete line is ready
                embeddedCliReceiveChar(embeddedCli, (char)byte);
            }
        }

        // Process embedded-cli state machine (handles buffering, history, etc.)
        embeddedCliProcess(embeddedCli);

        // Yield to prevent starving other tasks
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
