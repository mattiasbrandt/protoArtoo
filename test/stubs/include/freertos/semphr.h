// =============================================================================
// test/stubs/include/freertos/semphr.h
// FreeRTOS semaphore stub for native host builds.
//
// Models a NON-RECURSIVE mutex with real ownership semantics, because that is
// what the project's remaining static mutex is: src/console/console_module.cpp's
// config-write window (xSemaphoreCreateMutexStatic), which both adapters must
// be correct against - test_console_module.cpp and test_console_concurrency.cpp
// drive it through this stub's exposed singleton.
//
// This used to name src/main.cpp's serial mutex instead. That mutex is gone
// (#270, ADR 0037): the Console task is the only writer of the serial wire, so
// there is nothing left for a lock to coordinate there, and the serial output
// coordinator's own suite now asserts that it takes NO mutex at all.
//
// A stub that always fails to take, or always returns a null handle, silently
// forces every caller down its "no coordination" branch and makes the config
// window untestable. Do not reintroduce one.
// =============================================================================
#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>  // BaseType_t, pdTRUE, pdFALSE

// Real FreeRTOS declares xSemaphoreCreateMutexStatic() to take a
// StaticSemaphore_t* (a fixed-size opaque buffer the kernel places the real
// queue/semaphore control block into) - this stub ignores the buffer's
// contents entirely (see below), so the exact size does not matter for
// correctness here. 96 B, which is above the real struct: measured at 92 B on
// artoo-esp32 at this IDF/sdkconfig (`xtensa-esp32-elf-nm --print-size` on a
// firmware.elf, symbol size 0x5c), not the "~80 B on a 32-bit target" this
// comment used to estimate. The margin is 4 bytes, not 16; a future
// configUSE_* that grows StaticQueue_t would need this raised, and the stub
// ignoring the contents is what stops that from being a correctness problem
// here.
//
// The one caller that declares static storage for a mutex is
// src/console/console_module.cpp's config-write window. It used to be cited as
// "matching src/main.cpp's own logSerialMutexStorage precedent"; that
// precedent was deleted with the serial mutex (#270), so this is now the only
// one.
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
