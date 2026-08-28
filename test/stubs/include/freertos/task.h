// =============================================================================
// test/stubs/include/freertos/task.h
// FreeRTOS task stub for native host builds.
// Provides stubs for functions used in console_task.cpp
// =============================================================================
#pragma once

#include <stdint.h>

// Task handle type stub
typedef void* TaskHandle_t;

// Stack high water mark measurement stub
// Returns the number of 32-bit words free in the stack
// For native tests, return a large fixed value indicating abundant stack
inline UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t xTask) {
    (void)xTask;  // Unused
    return 2048;  // Stub value: 8KB worth of 32-bit words
}

// Task delete stub
inline void vTaskDelete(TaskHandle_t xTaskToDelete) {
    (void)xTaskToDelete;  // Unused
}

// UBaseType_t is a fundamental type in FreeRTOS (usually size_t or uint32_t)
typedef uint32_t UBaseType_t;
