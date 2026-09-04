// =============================================================================
// test/stubs/include/freertos/semphr.h
// FreeRTOS semaphore stub for native host builds.
//
// Models a NON-RECURSIVE mutex with real ownership semantics, because that is
// what src/main.cpp creates (xSemaphoreCreateMutexStatic, main.cpp:84) and
// what the Console serial output coordinator has to be correct against.
//
// A stub that always fails to take, or always returns a null handle, silently
// forces every caller down its "no coordination" branch and makes the
// coordinator untestable. Do not reintroduce one.
// =============================================================================
#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>  // BaseType_t, pdTRUE, pdFALSE

// Real FreeRTOS declares xSemaphoreCreateMutexStatic() to take a
// StaticSemaphore_t* (a fixed-size opaque buffer the kernel places the real
// queue/semaphore control block into) - this stub ignores the buffer's
// contents entirely (see below), so the exact size does not matter for
// correctness here; sized generously (96 B, comfortably above the real
// struct's ~80 B on a 32-bit target) so callers that declare static storage
// for one (src/web/api_config.cpp's config write lock) compile natively.
typedef struct { unsigned char reserved[96]; } StaticSemaphore_t;

// Opaque-enough mutex model for host tests: a holder token plus a depth count.
// Depth never exceeds 1 - a second take by the same notional owner fails, which
// is exactly how a non-recursive FreeRTOS mutex behaves and is the deadlock the
// console must not walk into.
struct PaStubMutex {
    int held;       // 0 = free, 1 = held
    int takeCount;  // cumulative successful takes, for assertions
    int giveCount;  // cumulative gives, including unmatched ones
    int unmatchedGives;  // gives while not held - a real defect if non-zero
    int failedTakes;     // takes refused because the mutex was already held
};

typedef struct PaStubMutex* SemaphoreHandle_t_stub;

// The production type is declared in freertos/FreeRTOS.h as void*; these helpers
// cast through it so production code compiles unchanged.
inline struct PaStubMutex* paStubMutexStorage(void) {
    static struct PaStubMutex storage = {0, 0, 0, 0, 0};
    return &storage;
}

inline void paStubMutexReset(void) {
    struct PaStubMutex* m = paStubMutexStorage();
    m->held = 0;
    m->takeCount = 0;
    m->giveCount = 0;
    m->unmatchedGives = 0;
    m->failedTakes = 0;
}

inline SemaphoreHandle_t xSemaphoreCreateMutexStatic(void* buffer) {
    (void)buffer;
    paStubMutexReset();
    return (SemaphoreHandle_t)paStubMutexStorage();
}

inline BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, unsigned long timeout) {
    (void)timeout;
    if (sem == nullptr) {
        return pdFALSE;
    }
    struct PaStubMutex* m = (struct PaStubMutex*)sem;
    if (m->held) {
        m->failedTakes++;
        // Non-recursive: a take while held fails. With portMAX_DELAY on a real
        // target this would block forever - the host surfaces it as a failure so
        // a test can assert the deadlock does not exist.
        return pdFALSE;
    }
    m->held = 1;
    m->takeCount++;
    return pdTRUE;
}

inline void xSemaphoreGive(SemaphoreHandle_t sem) {
    if (sem == nullptr) {
        return;
    }
    struct PaStubMutex* m = (struct PaStubMutex*)sem;
    m->giveCount++;
    if (!m->held) {
        m->unmatchedGives++;  // giving a mutex this task never took
        return;
    }
    m->held = 0;
}
