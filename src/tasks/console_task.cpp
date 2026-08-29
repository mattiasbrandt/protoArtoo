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
//  - Emit Console Records to serial with PER-LINE atomicity: each sink callback
//    takes the serial mutex, writes its own one record line whole, and gives the
//    mutex back before returning (see console_task.cpp:135-246 for mutex discipline).
//    A multi-record response (begin -> field/item* -> end) is NOT held as one
//    locked block - #219 R1 measured operations' 190-entry catalog listing at
//    10985 B / ~0.95 s @115200 8N1, and the old per-group lock blocked every
//    PA_LOG_* caller for that whole window, including Core 1 prio-5 rcInputTask
//    and driveTask logging inside their loops (AGENTS.md Architecture Guardrails:
//    Core 1 is real-time, real-time paths must not block; and drive zero-frame
//    continuity at 50 Hz). Per-line locking is what docs/console-protocol.md
//    already specifies: section 3.1 says records of one request "may be
//    separated by other lines" (the Request ID reassembles them), and section
//    2.1 records the no-paging decision this line-level design makes
//    affordable. The only invariant that survives is section 6's "no line is
//    ever interleaved inside another".
//  - Log arriving mid-entry clears the input line, writes the log via consoleSerialEmitLine(),
//    then redraws the prompt and buffered command via embeddedCliPrint()
// =============================================================================

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <string.h>
#include <cstdio>

#include "logging.h"
#include "console_module.h"
#include "console_record.h"
#include "console_serial_output.h"
#include "console_cli_line.h"

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

// Called by embedded-cli when a complete command line is ready
static void onCliCommand(EmbeddedCli* cli, CliCommand* cmd) {
    (void)cli;  // Unused

    if (cmd == nullptr || cmd->name == nullptr) {
        return;
    }

    // Parse command (T1: system.status.health, help, operations, unknown)
    const char* commandName = cmd->name;

    // Reconstruct meta-command arguments ("help <op>", "operations type=<t>")
    // into one command line - consoleExecuteCommand()'s own parsing expects
    // it (console_module.cpp:476-490's "operations " prefix check and the
    // "help " prefix check just above it), but embedded-cli's default
    // onCommand callback hands name and args separately. This used to
    // reconstruct only for "help", so "operations type=action" reached the
    // module as the bare string "operations" and its filter parse never
    // fired (#219 R2). See include/console_cli_line.h for the scope fence -
    // this covers ONLY "help" and "operations"; registry-entry operations
    // with arguments are #221's and #226's contract, not extended here.
    // Pulled out to a portable function (console_cli_line.cpp) specifically
    // so this reconstruction is unit-testable without the Arduino/FreeRTOS
    // dependencies the rest of this file carries - see
    // test/test_native/test_console_cli_line/.
    static char operationNameBuf[128];
    const char* operationName =
        consoleBuildCommandLine(commandName, cmd->args, operationNameBuf, sizeof(operationNameBuf));

    // Get request ID (global across both adapters)
    uint32_t requestId = consoleGetNextRequestId();
    currentRequestId = requestId;

    // Create console request
    ConsoleRequest request = {
        .requestId = requestId,
        .source = CONSOLE_SOURCE_SERIAL,
        .operationName = operationName,
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

// Emit one fully-formatted record line atomically: take the serial mutex,
// write the line + a newline, give the mutex back. This is the ONLY unit of
// atomicity the wire format needs (docs/console-protocol.md section 6: "no
// line is ever interleaved inside another"). A multi-record response
// (begin -> field/item* -> end) is deliberately NOT held as one locked block
// across this call boundary -- see the file header for why (#219 R1).
//
// CONSTRAINT: the mutex is NON-RECURSIVE (xSemaphoreCreateMutexStatic). Do
// not call PA_LOG_* (or anything else that takes paGetSerialMutex()) between
// the take and the give inside this function -- paLogLine routes through
// consoleSerialEmitLine(), which takes the same mutex with portMAX_DELAY, and
// a non-recursive mutex self-deadlocks the calling task. None of the sink
// callbacks below log while formatting a record, so this holds; it is the
// narrower, still-live form of the warning this file used to state at the
// whole-group level (begin..end) before #219 R1 moved locking to per-line.
static void emitRecordLine(const char* line, size_t len) {
    SemaphoreHandle_t mutex = paGetSerialMutex();
    if (mutex != nullptr) {
        xSemaphoreTake(mutex, portMAX_DELAY);
    }
    Serial.write((const uint8_t*)line, len);
    Serial.write('\n');
    if (mutex != nullptr) {
        xSemaphoreGive(mutex);
    }
}

static void onRecordBegin(uint32_t requestId, const char* operationType) {
    // Emit: < id=<n> type=begin operation=system.status.health
    size_t len = snprintf(recordBuffer, sizeof(recordBuffer),
                         "< id=%lu type=begin operation=%s",
                         (unsigned long)requestId, operationType);
    if (len < sizeof(recordBuffer)) {
        emitRecordLine(recordBuffer, len);
    }
}

static void onRecordField(uint32_t requestId, const char* name, const char* value) {
    // Emit: < id=<n> type=field name=<key> value=<value>
    size_t len = snprintf(recordBuffer, sizeof(recordBuffer),
                         "< id=%lu type=field name=%s value=%s",
                         (unsigned long)requestId, name, value);
    if (len < sizeof(recordBuffer)) {
        emitRecordLine(recordBuffer, len);
    }
}

static void onRecordItem(uint32_t requestId, const char* value) {
    // Emit: < id=<n> type=item value=<value>
    // Each item is its own locked line (emitRecordLine above); this is what
    // lets a 190-entry `operations` listing (#219 R1: 10985 B, ~0.95 s
    // @115200 8N1) share the wire with other tasks' log lines instead of
    // blocking them for the whole listing.
    size_t len = snprintf(recordBuffer, sizeof(recordBuffer),
                         "< id=%lu type=item value=%s",
                         (unsigned long)requestId, value);
    if (len < sizeof(recordBuffer)) {
        emitRecordLine(recordBuffer, len);
    }
}

static void onRecordResult(uint32_t requestId, ConsoleStatus status, ConsoleOutcome outcome,
                          ConsoleReason reason) {
    // Guard path: emit single result record for error/unknown/unsupported operations
    // Emit: < id=<n> type=result status=ok outcome=queued [reason=...]

    // Present exactly when there is a reason. This is the record an unavailable
    // operation answers with, so the reason must survive: the previous guard
    // excluded CONSOLE_REASON_NOT_IN_THIS_BUILD and would have dropped it.
    char reasonStr[64] = {};
    if (consoleReasonIsPresent(reason)) {
        snprintf(reasonStr, sizeof(reasonStr), " reason=%s", consoleReasonString(reason));
    }

    size_t len =
        snprintf(recordBuffer, sizeof(recordBuffer),
                 "< id=%lu type=result status=%s outcome=%s%s", (unsigned long)requestId,
                 consoleStatusString(status), consoleOutcomeString(outcome), reasonStr);
    if (len < sizeof(recordBuffer)) {
        emitRecordLine(recordBuffer, len);
    }
}

static void onRecordEnd(uint32_t requestId, ConsoleStatus status, ConsoleOutcome outcome,
                       ConsoleReason reason) {
    // Emit: < id=<n> type=end status=ok outcome=completed [reason=...]
    //
    // The reason field is present exactly when there is a reason. Testing
    // against NONE rather than the status keeps a genuine availability answer
    // intact: the previous guard also excluded CONSOLE_REASON_NOT_IN_THIS_BUILD
    // (then the enum's zero value, used as filler on success paths), which
    // would have silently dropped `reason=not-in-this-build` from a real
    // `unavailable` answer.
    char reasonStr[64] = {};
    if (consoleReasonIsPresent(reason)) {
        snprintf(reasonStr, sizeof(reasonStr), " reason=%s", consoleReasonString(reason));
    }

    size_t len =
        snprintf(recordBuffer, sizeof(recordBuffer),
                 "< id=%lu type=end status=%s outcome=%s%s", (unsigned long)requestId,
                 consoleStatusString(status), consoleOutcomeString(outcome), reasonStr);
    if (len < sizeof(recordBuffer)) {
        emitRecordLine(recordBuffer, len);
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

    // Disable live autocompletion (enableAutoComplete) for T1 scope.
    // Live autocompletion emits cursor save/restore escape sequences on every keystroke,
    // which is pure wire overhead when there are zero command bindings to suggest.
    // Tab completion itself is handled independently and still works (lib/embedded-cli.c:855).
    // This only disables the per-keystroke live display of suggestions.
    // T2+ will re-enable enableAutoComplete once #219 registers the command catalog.
    embeddedCliConfig->enableAutoComplete = false;

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
    embeddedCli->writeChar = consoleSerialWriteChar;
    embeddedCli->onCommand = onCliCommand;

    // Bind the CLI to the serial output coordinator (routes log/event/record lines)
    consoleSerialBindCli(embeddedCli);

    // Print the ready banner and initial prompt under serial mutex. The banner
    // names the detach key (#219 D4) - an operator attaching cold has no other
    // way to learn it, since the firmware never closes a terminal it does not
    // own. CONSOLE_DETACH_KEY_SERIAL is the same literal the bare `help`
    // meta-command's detach_key field uses (console_module.cpp), so the two
    // can't drift apart.
    SemaphoreHandle_t mutex = paGetSerialMutex();
    if (mutex != nullptr) {
        xSemaphoreTake(mutex, portMAX_DELAY);
    }
    Serial.print("Controller Console ready. Type 'help' for commands, "
                 CONSOLE_DETACH_KEY_SERIAL " to leave.\n");
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
        // ESP-IDF's uxTaskGetStackHighWaterMark() returns bytes, unlike vanilla FreeRTOS which returns words
        if (!hwmLogged && currentRequestId > 0) {
            UBaseType_t freeStack = uxTaskGetStackHighWaterMark(nullptr);
            PA_LOG_INFO(TAG, "stack HWM: %u bytes free after first command", (unsigned)freeStack);
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
