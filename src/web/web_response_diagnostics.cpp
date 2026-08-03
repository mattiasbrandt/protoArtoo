// =============================================================================
// src/web/web_response_diagnostics.cpp
//
// Allocation-free diagnostics for declared-length static-response TCP enqueue
// recovery (GitHub issue #60).
// =============================================================================

#include "web_response_diagnostics.h"

#include <cstdio>

namespace {

uint32_t s_zeroProgressAttempts = 0;
uint32_t s_recoveries = 0;
uint32_t s_exhaustions = 0;

}  // namespace

void webResponseTcpRecordZeroProgress() {
    __atomic_fetch_add(&s_zeroProgressAttempts, 1U, __ATOMIC_RELAXED);
}

void webResponseTcpRecordRecovery() {
    __atomic_fetch_add(&s_recoveries, 1U, __ATOMIC_RELAXED);
}

void webResponseTcpRecordExhaustion() {
    __atomic_fetch_add(&s_exhaustions, 1U, __ATOMIC_RELAXED);
}

WebResponseTcpDiagnostics webResponseTcpDiagnosticsSnapshot() {
    return {
        __atomic_load_n(&s_zeroProgressAttempts, __ATOMIC_RELAXED),
        __atomic_load_n(&s_recoveries, __ATOMIC_RELAXED),
        __atomic_load_n(&s_exhaustions, __ATOMIC_RELAXED),
    };
}

bool formatWebResponseTcpDiagnosticsJson(char* buffer, size_t bufferSize,
                                         const WebResponseTcpDiagnostics& diagnostics) {
    if (buffer == nullptr || bufferSize == 0U) {
        return false;
    }

    const int written = snprintf(
        buffer, bufferSize,
        "{\"zeroProgress\":%lu,\"recoveries\":%lu,\"exhaustions\":%lu}",
        (unsigned long)diagnostics.zeroProgressAttempts,
        (unsigned long)diagnostics.recoveries,
        (unsigned long)diagnostics.exhaustions);
    return written > 0 && (size_t)written < bufferSize;
}
