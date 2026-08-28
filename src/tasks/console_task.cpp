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
//  - Emit Console Records to serial through consoleSerialEmitLine() for atomic output
//  - Atomic output: log arriving mid-entry clears the input line, writes the record,
//    then redraws the prompt and buffered command via embeddedCliPrint()
// =============================================================================

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <string.h>

#include "logging.h"
#include "console_module.h"
#include "console_record.h"
#include "console_serial_output.h"

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
static void onRecordResult(uint32_t requestId, ConsoleStatus status, ConsoleOutcome outcome,
                          ConsoleReason reason);
static void onRecordEnd(uint32_t requestId, ConsoleStatus status, ConsoleOutcome outcome,
                       ConsoleReason reason);

// =============================================================================
// Static Configuration and State
// =============================================================================

// Static buffer for embedded-cli instance (no dynamic allocation per criterion)
// Per embedded_cli.h:202, embeddedCliRequiredSize() computes the required size.
// This buffer must be large enough for the config (verified at init time).
static CLI_UINT embeddedCliBuffer[512];  // CLI_UINT is size-aligned per embedded_cli.h
static EmbeddedCliConfig* embeddedCliConfig = nullptr;
static EmbeddedCli* embeddedCli = nullptr;

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
        .onRecordResult = onRecordResult,
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
    // CONSTRAINT: Mutex held for the entire record sequence (begin -> field... -> end)
    // The mutex is NON-RECURSIVE (xSemaphoreCreateMutexStatic creates non-recursive).
    // Any log emitted between onRecordBegin and onRecordEnd will self-deadlock the console task,
    // since paLogLine also takes the same mutex with portMAX_DELAY.
    // Current usage: system.status.health does not log during execution, so this is safe.
    // Future operations (#219+) must ensure they do not call PA_LOG_* between begin and end.
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

static void onRecordResult(uint32_t requestId, ConsoleStatus status, ConsoleOutcome outcome,
                          ConsoleReason reason) {
    // Guard path: emit single result record for error/unknown/unsupported operations
    // Emit: < id=<n> type=result status=ok outcome=queued [reason=...]
    // Take serial mutex for atomic emission (log lines will wait while this emits)
    SemaphoreHandle_t mutex = paGetSerialMutex();
    if (mutex != nullptr) {
        xSemaphoreTake(mutex, portMAX_DELAY);
    }

    char reasonStr[64] = {};
    if (status == CONSOLE_STATUS_ERR && reason != CONSOLE_REASON_NOT_IN_THIS_BUILD) {
        snprintf(reasonStr, sizeof(reasonStr), " reason=%s", consoleReasonString(reason));
    }

    size_t len =
        snprintf(recordBuffer, sizeof(recordBuffer),
                 "< id=%lu type=result status=%s outcome=%s%s", (unsigned long)requestId,
                 consoleStatusString(status), consoleOutcomeString(outcome), reasonStr);
    if (len < sizeof(recordBuffer)) {
        Serial.write((const uint8_t*)recordBuffer, len);
        Serial.write('\n');
    }

    // Give the mutex
    if (mutex != nullptr) {
        xSemaphoreGive(mutex);
    }
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
    // Per embedded_cli.h documentation, derive config from embeddedCliDefaultConfig()
    // and check the required size against our static buffer
    embeddedCliConfig = embeddedCliDefaultConfig();
    embeddedCliConfig->cliBuffer = embeddedCliBuffer;
    embeddedCliConfig->cliBufferSize = sizeof(embeddedCliBuffer);
    // Use default rxBufferSize, cmdBufferSize, historyBufferSize
    // Use default enableAutoComplete (true) - needed for #219

    // Verify buffer is large enough for the configuration
    uint16_t requiredSize = embeddedCliRequiredSize(embeddedCliConfig);
    if (requiredSize > sizeof(embeddedCliBuffer)) {
        PA_LOG_ERROR(TAG, "embedded-cli buffer too small: need %u bytes, have %zu",
                     requiredSize, sizeof(embeddedCliBuffer));
        vTaskDelete(nullptr);
        return;
    }

    embeddedCli = embeddedCliNew(embeddedCliConfig);
    if (embeddedCli == nullptr) {
        PA_LOG_ERROR(TAG, "failed to initialize embedded-cli with static buffer");
        vTaskDelete(nullptr);
        return;
    }

    // Set up embedded-cli callbacks
    embeddedCli->writeChar = onCliWrite;
    embeddedCli->onCommand = onCliCommand;

    // Bind the CLI to the serial output coordinator (routes log/event/record lines)
    consoleSerialBindCli(embeddedCli);

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

    // Measure stack high water mark after first command is processed
    // This allows us to measure the stack usage for command parsing + execution + record emission
    bool hwmLogged = false;

    // Main loop: read from UART and process through embedded-cli
    while (true) {
        // Log stack high water mark once after first command is processed
        // Measured value guides stack depth sizing for future runs (ADR 0034)
        if (!hwmLogged && currentRequestId > 0) {
            UBaseType_t freeStack = uxTaskGetStackHighWaterMark(nullptr);
            PA_LOG_INFO(TAG, "stack HWM: %u words free after first command", (unsigned)freeStack);
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
