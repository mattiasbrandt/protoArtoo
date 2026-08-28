// =============================================================================
// include/console_task.h
//
// ConsoleTask - Serial console adapter using embedded-cli
// Core 0, non-real-time, created in setup() regardless of network state.
// =============================================================================

#pragma once

// Create and start the Console task
// Call from setup() after logSerialMutex is initialized
void consoleTask(void* pvParameters);
