// =============================================================================
// test/stubs/include/freertos/task.h
// FreeRTOS task stub for native host builds.
// Provides stubs for functions used in console_task.cpp
// =============================================================================
#pragma once

#include <stdint.h>

// Task handle type stub
typedef void* TaskHandle_t;

// UBaseType_t is a fundamental type in FreeRTOS (usually size_t or uint32_t).
// Declared HERE, above its first use below, and not at the foot of the file:
// this header used UBaseType_t at line 16 and typedef'd it at line 27, which
// only compiled because every translation unit that reached it had already
// pulled the same typedef in through freertos/queue.h (via semphr.h). The
// first includer that did not got "'UBaseType_t' does not name a type" (#270).
// The duplicate in queue.h is harmless - identical typedefs may repeat.
typedef uint32_t UBaseType_t;

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

