// =============================================================================
// include/web_response_diagnostics.h
//
// Allocation-free diagnostics for declared-length static-response TCP enqueue
// recovery (GitHub issue #60).
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>

struct WebResponseTcpDiagnostics {
    uint32_t zeroProgressAttempts;
    uint32_t recoveries;
    uint32_t exhaustions;
};

// Called from the patched ESPAsyncWebServer response path. These functions are
// fixed-cost atomic increments and must remain allocation-free.
void webResponseTcpRecordZeroProgress();
void webResponseTcpRecordRecovery();
void webResponseTcpRecordExhaustion();

// Return one coherent-enough telemetry snapshot. Counters are monotonic
// evidence only; independent atomic loads do not provide transactional state.
WebResponseTcpDiagnostics webResponseTcpDiagnosticsSnapshot();

// Format the diagnostics as one JSON object for the existing status payload.
// Returns false when the caller-supplied buffer is invalid or too small.
bool formatWebResponseTcpDiagnosticsJson(char* buffer, size_t bufferSize,
                                         const WebResponseTcpDiagnostics& diagnostics);
