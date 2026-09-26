// =============================================================================
// include/api_config_apply.h
//
// Apply Core for POST /api/config (ADR 0011 config apply core).
//
// configApply(): pure function - no FreeRTOS, no request object, no
//   logging, no NVS. Reads parameters through a ConfigParamSource, validates
//   and mutates `working` in place, and writes a result carrying a
//   field-level error (the legacy 400 sentence word for word, plus the
//   field, reason and accepts as data - ADR 0011 amended 2026-09-25), a
//   bounded applied-fields log record for the shell to replay, and
//   plain-data actions.
//
// Two doors, one check per field (ADR 0068): a field arrives under its form
// name (`rcMember`), which the pages and the Controller Console send, or in a
// JSON body in the shape GET /api/config reads it (`rc.member`), which is what
// a restore posts back. The body is read once, at the top, and every check
// reads its field by form name whichever door it came in by.
//
// ConfigApplyResult is 2,060 B on artoo-esp32 (the applied-fields log record
// dominates, and that chip keeps fewer lines and rows) and larger where it
// keeps 32 lines and 24 rows - too large to return by value on an 8 KB web
// server task stack (see
// api_seq.cpp's SeqRunEvidence for the same constraint). It is an
// out-parameter, never a stack local: POST /api/config keeps its instance in
// the web request scratch (include/web_request_scratch.h) and the Console
// module a static of its own.
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
#include <stdio.h>

#include "api_apply_refusal.h"
#include "api_param_source.h"
#include "config.h"  // PA_CHIP_TARGET_ESP32 - a presence macro, so it must be in scope
#include "config_cache.h"
#include "config_records.h"

struct ConfigApplyError {
    bool hasError = false;
    char message[192] = {0};  // word for word the legacy error strings
    // What the sentence says, as data: set on every error, never read out of
    // `message` (include/api_apply_refusal.h).
    ApplyRefusal refusal;
};

struct ConfigApplyActions {
    bool playDomeOnCue = false;
};

// Bounded record of pre-formatted "[CFG] ..." log lines, in apply order.
// The core does not log (ADR 0002 purity discipline) - the shell replays
// these via PA_LOG_INFO. kMaxLines covers an ordinary form save many times
// over; kLineWidth covers the longest formatted line with margin.
//
// A whole Configuration posted back in the GET shape (ADR 0068) - a restore -
// logs more lines than that. The record is not grown for it: the result is a
// static on two adapters, and BSS is the scarcest budget on this target. The
// lines past the bound are counted in `dropped` instead, and the shell says how
// many it could not show rather than letting the log look complete.
struct ConfigAppliedFields {
    // Sixteen on artoo-esp32, where BSS is the heap, so a restore there counts
    // more of its lines as `dropped` while its fields still apply in full. That
    // is that board's size for good: the dropped count already says more
    // changed (operator decision 2026-09-25, #428).
#if defined(PA_CHIP_TARGET_ESP32)
    static constexpr size_t kMaxLines = 16;
#else
    static constexpr size_t kMaxLines = 32;
#endif
    static constexpr size_t kLineWidth = 80;
    char lines[kMaxLines][kLineWidth];
    size_t count = 0;
    size_t dropped = 0;
};

// What the request asked of the addressed Servo Output rows (ADR 0041).
//
// An Output's settings arrive as rows, in the shape GET /api/servo/outputs
// reads them (ADR 0068), and its capture and reverse as acts. The Apply Core
// is pure and cannot reach the live table, so it validates the numbers and
// records them here, addressed, and the Commit Step applies them through
// configCacheApplyServoOutputEdits(). Nothing stores an endpoint on the way:
// since #345 the row is the only place one lives.
//
// One typed entry per Output Address the request named, so a POST that
// carries one row changes one row. `count` is zero on a request that named
// none. The row door can name every row a table holds, and a request may carry
// a capture and a reverse besides (#364) -- each addressed at any Output.
struct ConfigServoOutputEdits {
    ServoOutputEdit edits[SERVO_OUTPUT_ROW_MAX + 2];
    size_t count = 0;
};

// What the request asked of a Part's place on the Outputs (ADR 0050, #347).
//
// A move arrives as three fields that mean something only together: the Part,
// the Output it is on now, and the Output it is going to, each end an Output
// Address or `none`. This core checks their shape - a Part this build models, an
// address a driver actually has. Whether the Part really is where the request
// says is a question about the live table, which this core cannot reach, so the
// Commit Step asks it and refuses the whole request, before anything else in it
// lands, when the answer is no.
//
// `requested` is false on a request that named none of the three, which is
// every request that is not a move.
struct ConfigPartMove {
    bool requested = false;
    ServoOutputPartMove move = {};
};

struct ConfigApplyResult {
    bool changed = false;  // false -> shell sends the "no fields supplied" 400
    // Whether the request stated the fields RC input also writes at runtime:
    // the speed group (speedLimitMax, or a preset value the active limit is
    // derived from) and stationary. The Commit Step keeps the live value of
    // whichever it did not, so an RC change that landed after the request's
    // read is not reverted by a request that said nothing about it (#417).
    bool speedLimitStated = false;
    bool stationaryStated = false;
    ConfigApplyError error;
    ConfigApplyActions actions;
    ConfigAppliedFields applied;
    ConfigServoOutputEdits servoOutputs;
    // What the request stated of each Record (include/config_records.h): the
    // Records' own checks stage it here and the Commit Step merges it.
    ConfigRecordEdits records;
    ConfigPartMove partMove;
};

// `working` must already hold the current cached snapshot (shell reads it
// via configCacheRead before calling). `domeEnabledBefore` is the live
// enable_dome_esc value snapshotted by the shell before the call, per ADR 0011's
// "snapshot live inputs before calling the core." `result` is fully
// reinitialized on entry (safe to reuse a static instance across calls).
void configApply(const ConfigParamSource& params, ConfigSnapshot* working, bool domeEnabledBefore,
                  ConfigApplyResult* result);
