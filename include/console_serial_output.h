// =============================================================================
// include/console_serial_output.h
//
// Serial output coordination for Console task (ADR 0034)
// Routes log/event/record output through embeddedCliPrint() under serial mutex
// so that lines arriving mid-entry clear the input line, print atomically,
// then redraw the prompt and buffered command.
// =============================================================================
#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Forward declaration
typedef struct EmbeddedCli EmbeddedCli;

// Bind the serial output coordinator to an embedded-cli instance.
// Called once during console task initialization with the live CLI.
// Allows a host test to inject a test CLI for harness verification.
void consoleSerialBindCli(EmbeddedCli* cli);

// Emit a complete line through the serial output coordinator.
// Takes the serial mutex, calls embeddedCliPrint() (which clears the current
// input line, prints the line, and redraws the prompt + buffered command),
// then gives the mutex. Safe for log/event/record emission from any Core 0 path.
//
// CONSTRAINT: The mutex is NON-RECURSIVE (xSemaphoreCreateMutexStatic creates
// non-recursive). Nothing called from inside this function may emit another log
// or call consoleSerialEmitLine recursively, or the console task will self-deadlock.
// paLogLine() holds the same mutex with portMAX_DELAY, so any Log emitted from
// inside consoleSerialEmitLine will block forever.
void consoleSerialEmitLine(const char* line);

// Get the serial mutex used for atomic log and console output coordination
// Declared in src/main.cpp, accessed by console_task.cpp
SemaphoreHandle_t paGetSerialMutex(void);
