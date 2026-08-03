// =============================================================================
// src/web/web_response_diagnostics.cpp
//
// Allocation-free diagnostics for declared-length static-response TCP enqueue
// recovery (GitHub issue #60).
// =============================================================================

#include "web_response_diagnostics.h"

#include <cstdio>

#ifdef ARDUINO
#include <esp_heap_caps.h>
#endif

namespace {

uint32_t s_zeroProgressAttempts = 0;
uint32_t s_noSendSpace = 0;
uint32_t s_zeroWithSendSpace = 0;
uint32_t s_recoveries = 0;
uint32_t s_exhaustions = 0;
uint32_t s_writeErrMem = 0;
uint32_t s_writeErrNonMem = 0;
int32_t s_lastWriteErr = 0;
uint32_t s_lastWriteQueue = 0;
uint32_t s_maxWriteQueue = 0;
uint32_t s_writeQueueLimit = 0;
uint32_t s_lastWriteSize = 0;
uint32_t s_lastHeapFree8bit = 0;
uint32_t s_lastHeapLargest8bit = 0;
uint32_t s_minHeapLargest8bit = 0;

}  // namespace

void webResponseTcpRecordZeroProgress(bool hadSendSpace) {
    __atomic_fetch_add(&s_zeroProgressAttempts, 1U, __ATOMIC_RELAXED);
    __atomic_fetch_add(
        hadSendSpace ? &s_zeroWithSendSpace : &s_noSendSpace, 1U, __ATOMIC_RELAXED);
}

void webResponseTcpRecordRecovery() {
    __atomic_fetch_add(&s_recoveries, 1U, __ATOMIC_RELAXED);
}

void webResponseTcpRecordExhaustion() {
    __atomic_fetch_add(&s_exhaustions, 1U, __ATOMIC_RELAXED);
}

void webResponseTcpRecordAsyncClientAddFailure(int8_t error, bool memoryError,
                                               uint16_t queueLength, uint16_t queueLimit,
                                               uint16_t writeSize) {
    __atomic_fetch_add(
        memoryError ? &s_writeErrMem : &s_writeErrNonMem, 1U, __ATOMIC_RELAXED);
    __atomic_store_n(&s_lastWriteErr, (int32_t)error, __ATOMIC_RELAXED);
    __atomic_store_n(&s_lastWriteQueue, (uint32_t)queueLength, __ATOMIC_RELAXED);
    __atomic_store_n(&s_writeQueueLimit, (uint32_t)queueLimit, __ATOMIC_RELAXED);
    __atomic_store_n(&s_lastWriteSize, (uint32_t)writeSize, __ATOMIC_RELAXED);

#ifdef ARDUINO
    const uint32_t heapFree8bit =
        (uint32_t)heap_caps_get_free_size(MALLOC_CAP_8BIT);
    const uint32_t heapLargest8bit =
        (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    __atomic_store_n(&s_lastHeapFree8bit, heapFree8bit, __ATOMIC_RELAXED);
    __atomic_store_n(&s_lastHeapLargest8bit, heapLargest8bit, __ATOMIC_RELAXED);

    uint32_t smallest = __atomic_load_n(&s_minHeapLargest8bit, __ATOMIC_RELAXED);
    while ((smallest == 0U || heapLargest8bit < smallest) &&
           !__atomic_compare_exchange_n(&s_minHeapLargest8bit, &smallest, heapLargest8bit,
                                        false, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
    }
#endif

    uint32_t observed = __atomic_load_n(&s_maxWriteQueue, __ATOMIC_RELAXED);
    while ((uint32_t)queueLength > observed &&
           !__atomic_compare_exchange_n(&s_maxWriteQueue, &observed, (uint32_t)queueLength,
                                        false, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
    }
}

WebResponseTcpDiagnostics webResponseTcpDiagnosticsSnapshot() {
    return {
        __atomic_load_n(&s_zeroProgressAttempts, __ATOMIC_RELAXED),
        __atomic_load_n(&s_noSendSpace, __ATOMIC_RELAXED),
        __atomic_load_n(&s_zeroWithSendSpace, __ATOMIC_RELAXED),
        __atomic_load_n(&s_recoveries, __ATOMIC_RELAXED),
        __atomic_load_n(&s_exhaustions, __ATOMIC_RELAXED),
        __atomic_load_n(&s_writeErrMem, __ATOMIC_RELAXED),
        __atomic_load_n(&s_writeErrNonMem, __ATOMIC_RELAXED),
        __atomic_load_n(&s_lastWriteErr, __ATOMIC_RELAXED),
        __atomic_load_n(&s_lastWriteQueue, __ATOMIC_RELAXED),
        __atomic_load_n(&s_maxWriteQueue, __ATOMIC_RELAXED),
        __atomic_load_n(&s_writeQueueLimit, __ATOMIC_RELAXED),
        __atomic_load_n(&s_lastWriteSize, __ATOMIC_RELAXED),
        __atomic_load_n(&s_lastHeapFree8bit, __ATOMIC_RELAXED),
        __atomic_load_n(&s_lastHeapLargest8bit, __ATOMIC_RELAXED),
        __atomic_load_n(&s_minHeapLargest8bit, __ATOMIC_RELAXED),
    };
}

bool formatWebResponseTcpDiagnosticsJson(char* buffer, size_t bufferSize,
                                         const WebResponseTcpDiagnostics& diagnostics) {
    if (buffer == nullptr || bufferSize == 0U) {
        return false;
    }

    const int written = snprintf(
        buffer, bufferSize,
        "{\"zeroProgress\":%lu,\"noSendSpace\":%lu,\"zeroWithSendSpace\":%lu,"
        "\"recoveries\":%lu,\"exhaustions\":%lu,\"writeErrMem\":%lu,"
        "\"writeErrNonMem\":%lu,\"lastWriteErr\":%ld,\"lastWriteQueue\":%lu,"
        "\"maxWriteQueue\":%lu,\"writeQueueLimit\":%lu,\"lastWriteSize\":%lu,"
        "\"lastHeapFree8bit\":%lu,\"lastHeapLargest8bit\":%lu,"
        "\"minHeapLargest8bit\":%lu}",
        (unsigned long)diagnostics.zeroProgressAttempts,
        (unsigned long)diagnostics.noSendSpace,
        (unsigned long)diagnostics.zeroWithSendSpace,
        (unsigned long)diagnostics.recoveries,
        (unsigned long)diagnostics.exhaustions,
        (unsigned long)diagnostics.writeErrMem,
        (unsigned long)diagnostics.writeErrNonMem,
        (long)diagnostics.lastWriteErr,
        (unsigned long)diagnostics.lastWriteQueue,
        (unsigned long)diagnostics.maxWriteQueue,
        (unsigned long)diagnostics.writeQueueLimit,
        (unsigned long)diagnostics.lastWriteSize,
        (unsigned long)diagnostics.lastHeapFree8bit,
        (unsigned long)diagnostics.lastHeapLargest8bit,
        (unsigned long)diagnostics.minHeapLargest8bit);
    return written > 0 && (size_t)written < bufferSize;
}
