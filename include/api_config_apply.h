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
#include <stdio.h>

#include "api_param_source.h"
#include "board_outputs.h"  // BOARD_OUTPUT_ID_MAX_LEN - the longest stored Output id
#include "config_cache.h"
#include "droid_build.h"
#include "guided_setup.h"
#include "servo_legacy_field_sets.h"  // SERVO_LEGACY_FIELD_SET_COUNT

// -----------------------------------------------------------------------------
// The fields that save an Output's Motion Profile (ADR 0052, #414)
//
// Time to full throw, time to get up to speed and the ease, per Output, named
// from its stored config id: arm1ThrowMs, arm1AccelMs, arm1Ease. The rule lives
// here and nowhere else, so the Apply Core that reads the three and
// GET /api/config, which hands each Output's names to the browser in
// components{} (throwField, accelField, easeField), cannot come to disagree -
// and a page never composes one (data/outputs.js saves by the name it read).
// -----------------------------------------------------------------------------
enum ConfigMotionField : uint8_t {
    CONFIG_MOTION_THROW = 0,
    CONFIG_MOTION_ACCEL,
    CONFIG_MOTION_EASE,
    CONFIG_MOTION_FIELD_COUNT,
};

// "ThrowMs" is the longest suffix; sizeof counts its terminator.
constexpr size_t CONFIG_MOTION_FIELD_NAME_MAX = BOARD_OUTPUT_ID_MAX_LEN + sizeof("ThrowMs");

inline bool configMotionFieldName(char* buf, size_t bufSize, const char* outputId,
                                  ConfigMotionField field) {
    static const char* const kSuffix[CONFIG_MOTION_FIELD_COUNT] = {"ThrowMs", "AccelMs", "Ease"};
    if (buf == nullptr || bufSize == 0 || outputId == nullptr ||
        field >= CONFIG_MOTION_FIELD_COUNT) {
        return false;
    }
    const int written = snprintf(buf, bufSize, "%s%s", outputId, kSuffix[field]);
    return written > 0 && (size_t)written < bufSize;
}

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
// The five legacy field sets can each produce one edit, and a request may carry
// a capture and a reverse besides (#364) -- each addressed at any Output,
// including a row the five names cannot reach. Hence the + 2.
struct ConfigServoOutputEdits {
    ServoOutputEdit edits[SERVO_LEGACY_FIELD_SET_COUNT + 2];
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

// What the request asked of guided Setup's record (#351).
//
// Two independent facts, so two flags. A step being marked visited is the
// browser saying "this question has now actually been on screen", and it happens
// many times during one run; the run ending happens once. A request that carries
// one must not be read as saying anything about the other.
//
// The record lives outside ConfigSnapshot on its own NVS keys, like the Droid
// Build above, so the core validates it here and the Commit Step merges it onto
// the live record through configCacheApplyGuidedSetup().
//
// The visited list arrives WHOLE rather than as an addition, for the reason the
// Fitted Parts do: the browser holds the run, an add-one wire would need a
// remove-one to match it, and replacing the list keeps the two ends from drifting
// into disagreement about what has been shown.
struct ConfigGuidedSetupEdit {
    bool runChanged = false;
    bool visitedChanged = false;
    bool summaryDoneChanged = false;
    bool summaryDone = false;
    GuidedSetupRun run = GUIDED_SETUP_NOT_RUN;
    GuidedSetupConfig visited = {};
};

struct ConfigApplyResult {
    bool changed = false;  // false -> shell sends the "no fields supplied" 400
    ConfigApplyError error;
    ConfigApplyActions actions;
    ConfigAppliedFields applied;
    ConfigServoOutputEdits servoOutputs;
    ConfigDroidBuildEdit droidBuild;
    ConfigGuidedSetupEdit guidedSetup;
    ConfigPartMove partMove;
};

// `working` must already hold the current cached snapshot (shell reads it
// via configCacheRead before calling). `domeEnabledBefore` is the live
// enable_dome_esc value snapshotted by the shell before the call, per ADR 0011's
// "snapshot live inputs before calling the core." `result` is fully
// reinitialized on entry (safe to reuse a static instance across calls).
void configApply(const ConfigParamSource& params, ConfigSnapshot* working, bool domeEnabledBefore,
                  ConfigApplyResult* result);
