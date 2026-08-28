// =============================================================================
// test/stubs/include/freertos/semphr.h
// FreeRTOS semaphore stub for native host builds.
// =============================================================================
#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>  // For pdTRUE, pdFALSE, BaseType_t

// Semaphore creation and management stubs for native tests
inline SemaphoreHandle_t xSemaphoreCreateMutexStatic(void* buffer) {
    (void)buffer;  // Unused
    return nullptr;  // Stub: return null handle (tests do not take/give)
}

inline BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, unsigned long timeout) {
    (void)sem;
    (void)timeout;
    return pdFALSE;  // Stub: always fail (tests do not block on mutexes)
}

inline void xSemaphoreGive(SemaphoreHandle_t sem) {
    (void)sem;  // Unused
}
