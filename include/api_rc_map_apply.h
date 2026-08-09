// =============================================================================
// include/api_rc_map_apply.h
//
// Apply Core for POST /api/rc/map (ADR 0011 RC mapping apply core).
//
// rcMapApply(): pure function — no FreeRTOS, no request object, no
//   NVS. Reads the JSON map body through a ConfigParamSource, validates
//   each entry, and applies them onto `working` in place (clearing existing
//   slots first, exactly as the legacy handler did). On the first invalid
//   entry, returns first-error-wins with a byte-identical error message and
//   an echo of the offending entry (matching the legacy `sendValidationError`
//   JSON shape) so the shell can rebuild the same error body.
//
// Defined in src/web/api_rc_map_apply.cpp.
// =============================================================================
#pragma once

#include <stdint.h>

#include "api_config_snapshot.h"
#include "api_param_source.h"
#include "config_cache.h"

struct RcMapApplyErrorEntry {
    bool present = false;
    char source[8] = {0};   // "pwm"/"sbus1"/"sbus2"/"none"
    uint8_t channel = 0;
    char action[40] = {0};  // robotActionIdToString() result
    char payload[16] = {0};
};

struct RcMapApplyResult {
    bool ok = false;
    char errorMessage[96] = {0};
    RcMapApplyErrorEntry errorEntry;
};

// `working` must already hold the current cached snapshot (shell reads it
// via configCacheRead before calling); it is mutated in place on success.
// `result` is fully reinitialized on entry (safe to reuse a static
// instance across calls, per the ADR 0011 apply-core stack-size lesson).
void rcMapApply(const ConfigParamSource& params, ConfigSnapshot* working, RcMapApplyResult* result);
