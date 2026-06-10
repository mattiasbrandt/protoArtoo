// =============================================================================
// test/stubs/include/freertos/queue.h
// FreeRTOS queue type and function stubs for native host builds.
// =============================================================================
#pragma once
#include <stdint.h>
#include <string.h>

typedef void*    QueueHandle_t;
typedef long     BaseType_t;
typedef uint32_t UBaseType_t;

#define pdTRUE  ((BaseType_t)1)
#define pdFALSE ((BaseType_t)0)
#define portMAX_DELAY ((uint32_t)0xFFFFFFFFUL)

// Minimal inline stubs — sufficient for compile-time linkage.
// Tests that need to observe enqueue calls should provide their own
// non-inline definitions in a test-local translation unit.
inline QueueHandle_t xQueueCreate(UBaseType_t /*len*/, UBaseType_t /*itemSize*/) {
    return (void*)1;
}
inline BaseType_t xQueueSend(QueueHandle_t /*q*/, const void* /*item*/,
                              uint32_t /*ticksToWait*/) {
    return pdTRUE;
}
inline BaseType_t xQueueReceive(QueueHandle_t /*q*/, void* /*buf*/,
                                 uint32_t /*ticksToWait*/) {
    return pdFALSE;
}
inline UBaseType_t uxQueueMessagesWaiting(QueueHandle_t /*q*/) { return 0; }
