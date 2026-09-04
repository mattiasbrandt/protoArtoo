// =============================================================================
// include/console_task.h
//
// ConsoleTask - Serial console adapter using embedded-cli
// Core 0, non-real-time, created in setup() regardless of network state.
// =============================================================================

#pragma once

// Create and start the Console task.
//
// Call from setup() after paLogInit() has created the Log Ring: this task
// takes ownership of the serial wire as it starts (ADR 0037), and its first
// act on that wire is to drain the boot lines the ring already holds.
void consoleTask(void* pvParameters);
