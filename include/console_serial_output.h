// =============================================================================
// include/console_serial_output.h
//
// Serial output coordination for Console task (ADR 0034)
// Provides access to the serial mutex for atomic output under console_task.cpp
// =============================================================================
#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Get the serial mutex used for atomic log and console output coordination
// Takes the mutex with appropriate timeout to ensure log/console atomicity
// Declared in src/main.cpp, accessed by console_task.cpp
SemaphoreHandle_t paGetSerialMutex(void);
