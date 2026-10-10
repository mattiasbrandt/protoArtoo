// =============================================================================
// include/api_rc_map_apply.h
//
// Apply Core for POST /api/rc/map (ADR 0011 RC mapping apply core), plus its
// ADR 0036 Commit Step and its Write Window.
//
// rcMapApply(): pure function - no FreeRTOS, no request object, no
//   NVS. Reads the JSON map body through a ConfigParamSource, validates
//   each entry, and applies them onto `working` in place (clearing existing
//   slots first, exactly as the legacy handler did). On the first invalid
//   entry, returns first-error-wins with a byte-identical error message and
//   an echo of the offending entry (matching the legacy `sendValidationError`
//   JSON shape) so the shell can rebuild the same error body. Where each
//   binding is stored is the RC Map Store's (rcMapStorePlace(),
//   include/rc_map_store.h).
//
// rcMapCommitApplied() and rcMapWriteWindow() are kept beside it, as
// api_wifi_apply.h keeps its own: every adapter calls the Write Window, and
// none holds the lock or carries the Commit Step's effects itself.
//
// Defined in src/web/api_rc_map_apply.cpp.
// =============================================================================
#pragma once

#include <stdint.h>

#include "api_config_snapshot.h"
#include "api_apply_refusal.h"
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
    // The refusal as data (include/api_apply_refusal.h, docs/api.md "Refusals
    // from a settings write"): the request field it is about ("map.channel",
    // "calibration.min"), why, and what the field takes.
    ApplyRefusal refusal;
};

// `working` must already hold the current cached snapshot (shell reads it
// via configCacheRead before calling); it is mutated in place on success.
// `result` is fully reinitialized on entry (safe to reuse a static
// instance across calls, per the ADR 0011 apply-core stack-size lesson).
void rcMapApply(const ConfigParamSource& params, ConfigSnapshot* working, RcMapApplyResult* result);

// Commit Step for the POST /api/rc/map Apply Core (ADR 0036): the map into the
// config cache, the Working Snapshot refreshed from what the cache then holds,
// and the system config saved to NVS from it - one snapshot on the caller's
// stack, not two. `working` must hold rcMapApply()'s output (result.ok).
// WebRequest-free, as ADR 0036's Consequences asked: the caller renders its
// own failure.
struct RcMapCommitOutcome {
    bool persisted = false;  // false -> the caller reports "failed to persist config"
};
RcMapCommitOutcome rcMapCommitApplied(ConfigSnapshot* working);

// Write Window for an RC Map write (ADR 0011, amended 2026-09-24; GLOSSARY.md
// "Write Window"): take the config write lock, read the cache into
// `*working`, run rcMapApply(), and when it is ok run rcMapCommitApplied(),
// then release. The route read-modify-writes the same config cache and the
// same NVS namespace the config write does, so it is guarded the same way.
//
// False -> busy: nothing read or written; `*result` and `*commit` untouched.
// True -> `*result` holds rcMapApply()'s answer, and when it is ok `*commit`
// holds the Commit Step's outcome.
bool rcMapWriteWindow(const ConfigParamSource& params, ConfigSnapshot* working,
                      RcMapApplyResult* result, RcMapCommitOutcome* commit);
