// =============================================================================
// include/api_config_apply.h
//
// Apply Core for POST /api/config (ADR 0011 config apply core).
//
// configApply(): pure function - no FreeRTOS, no request object, no
//   logging, no NVS. Reads parameters through a ConfigParamSource, validates
//   and mutates `working` in place, and writes a result carrying a
//   field-level error (byte-identical to the legacy 400 bodies), a bounded
//   applied-fields log record for the shell to replay, and plain-data
//   actions.
//
// ConfigApplyResult is ~2.5 KB (the applied-fields log record dominates) -
// too large to return by value on an 8 KB web server task stack (see
// api_seq.cpp's SeqRunEvidence for the same constraint). It is an
// out-parameter; callers keep their instance `static`, matching that
// precedent, rather than a stack local.
//
// ConfigApplyActions intentionally has no playDriveOnCue: ADR 0012 moves
// that rule to commandedSetStationary() (state-derived), once the later
// Z2 commanded_modes feature lands. Until then the shell keeps its existing
// inline stationary-release cue logic unchanged. playDomeOnCue stays a core
// action because it is config-derived (enable_dome_esc false->true).
//
// Defined in src/web/api_config_apply.cpp.
// =============================================================================
#pragma once

#include <stddef.h>

#include "api_param_source.h"
#include "config_cache.h"
#include "droid_build.h"
#include "servo_legacy_field_sets.h"  // SERVO_LEGACY_FIELD_SET_COUNT

struct ConfigApplyError {
    bool hasError = false;
    char message[192] = {0};  // byte-identical to the legacy error strings
};

struct ConfigApplyActions {
    bool playDomeOnCue = false;
};

// Bounded record of pre-formatted "[CFG] ..." log lines, in apply order.
// The core does not log (ADR 0002 purity discipline) - the shell replays
// these via PA_LOG_INFO. kMaxLines covers the largest single-request field
// count today (~29: 4 speed-group lines + 5 scalar lines + 15 boolFields +
// 5 dome-random lines) with headroom; kLineWidth covers the longest
// formatted line with margin.
struct ConfigAppliedFields {
    static constexpr size_t kMaxLines = 32;
    static constexpr size_t kLineWidth = 80;
    char lines[kMaxLines][kLineWidth];
    size_t count = 0;
};

// What the request asked of the addressed Servo Output rows (ADR 0041).
//
// Endpoints and component types still arrive in the five fixed field sets'
// parameter names -- arm1OpenUs, arm1Type and their siblings -- because the
// pages that send them are not rebuilt onto the rows until the C1 wave. The
// Apply Core is pure and cannot reach the live table, so it validates the
// numbers and records them here, addressed, and the Commit Step applies them
// through configCacheApplyServoOutputEdits(). Nothing stores an endpoint on the
// way: since #345 the row is the only place one lives.
//
// One entry per Output Address the request named, so a POST that carries one
// arm changes one row. `count` is zero on a request that named none.
struct ConfigServoOutputEdits {
    ServoOutputEdit edits[SERVO_LEGACY_FIELD_SET_COUNT];
    size_t count = 0;
};

// What the request asked of the Droid Build (ADR 0047).
//
// A Droid Build lives outside ConfigSnapshot on its own NVS keys, so it cannot
// be applied onto `working` the way a snapshot field is; the core validates it
// here and the Commit Step hands it to configCacheApplyDroidBuild().
//
// Each half is answered as a PAIR - a design and the variant of that design -
// because a variant only means anything against the design it belongs to, and a
// request carrying one without the other would have the core validate a pairing
// nobody stated. The Fitted Parts arrive whole for the same reason a set does:
// there is no merge to do and nothing here has to know what was fitted before.
//
// `changed` is false on a request that named none of it, which is every request
// the Droid Build is not about.
struct ConfigDroidBuildEdit {
    bool domeChanged = false;
    bool bodyChanged = false;
    bool fittedChanged = false;
    DroidDesignChoice dome = {};
    DroidDesignChoice body = {};
    DroidFittedParts fitted = {};
};

struct ConfigApplyResult {
    bool changed = false;  // false -> shell sends the "no fields supplied" 400
    ConfigApplyError error;
    ConfigApplyActions actions;
    ConfigAppliedFields applied;
    ConfigServoOutputEdits servoOutputs;
    ConfigDroidBuildEdit droidBuild;
};

// `working` must already hold the current cached snapshot (shell reads it
// via configCacheRead before calling). `domeEnabledBefore` is the live
// enable_dome_esc value snapshotted by the shell before the call, per ADR 0011's
// "snapshot live inputs before calling the core." `result` is fully
// reinitialized on entry (safe to reuse a static instance across calls).
void configApply(const ConfigParamSource& params, ConfigSnapshot* working, bool domeEnabledBefore,
                  ConfigApplyResult* result);
