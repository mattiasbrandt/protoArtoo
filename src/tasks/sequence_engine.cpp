// =============================================================================
// src/tasks/sequence_engine.cpp
//
// Pure DM:* sequence cursor engine (ADR 0004, issue #2). No Arduino, FreeRTOS,
// logging, or RNG dependencies — fully natively testable. See header for the
// peek/commit execution model.
//
// Slice 2 commit 1: flat steps (STEP_DOME_CMD / STEP_AUDIO / STEP_END) plus
// terminal auto-reset. STEP_LOOP, STEP_RANDOM, and STEP_AUDIO_CATEGORY are
// resolved in later slice-2 commits; until then they are skipped.
// =============================================================================

#include <string.h>

#include "sequence_engine.h"

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

// Rollover-safe "now has reached at".
static bool timeReached(uint32_t now, uint32_t at) {
    return (int32_t)(now - at) >= 0;
}

static void setPayload(SeqAction& a, const char* payload) {
    a.payload[0] = '\0';
    if (payload != nullptr) {
        strncpy(a.payload, payload, sizeof(a.payload) - 1);
        a.payload[sizeof(a.payload) - 1] = '\0';
    }
}

static void addFinal(SeqEngineState& st, SeqActionKind kind, const char* payload) {
    if (st.finalCount >= (uint8_t)(sizeof(st.finalQ) / sizeof(st.finalQ[0]))) {
        return;
    }
    SeqAction& a = st.finalQ[st.finalCount++];
    a.kind = kind;
    a.audioCategory = 0;
    a.audioFallbackSlot = 0;
    setPayload(a, payload);
}

// Flip the latched group state for the branch that just completed normally
// (ADR 0004 decision 8). OPENALL acts on every panel, so its latch carries
// the pie and ring groups with it in both directions.
static void applyToggleLatch(SeqEngineState& st) {
    const bool open = st.openBranch;
    switch (st.entry->toggleGroup) {
        case TOGGLE_PIES:
            st.latches.piesOpen = open;
            break;
        case TOGGLE_LOW:
            st.latches.lowOpen = open;
            break;
        case TOGGLE_ALL:
            st.latches.allOpen = open;
            st.latches.piesOpen = open;
            st.latches.lowOpen = open;
            break;
        default:
            break;
    }
}

// Build the terminal auto-reset queue from activeFx (ADR 0004 decision 7).
// FX_PANEL close-all is suppressed on the normal end of a toggle open branch
// (panels are meant to stay open); FX_AUDIO stops playback only on abnormal
// termination so tracks that outlive their sequence end naturally.
static void beginFinish(SeqEngineState& st, bool abnormal) {
    st.finishing = true;
    st.finishAbnormal = abnormal;
    st.finalCount = 0;
    st.finalCursor = 0;
    st.pendingComputed = false;

    if (st.activeFx & FX_LOGIC_PSI) {
        addFinal(st, SEQ_ACT_DOME_CMD, "@0T1");
        addFinal(st, SEQ_ACT_DOME_CMD, "@0P1");
    }
    if (st.activeFx & FX_HOLO) {
        addFinal(st, SEQ_ACT_DOME_CMD, "*ST00");
    }
    if (st.activeFx & FX_PANEL) {
        if (abnormal || !st.openBranch) {
            addFinal(st, SEQ_ACT_DOME_CMD, ":CL00");
        }
    }
    if ((st.activeFx & FX_AUDIO) && abnormal) {
        addFinal(st, SEQ_ACT_AUDIO_STOP, nullptr);
    }

    // Toggle bookkeeping on normal completion. Close branches finish their
    // per-slot :SM closes without a release (issue #2 gap #1: only :CL00
    // group-releases), so emit :CL00 once no group remains latched open —
    // releasing earlier would slam panels another sequence left open.
    if (!abnormal && st.entry->toggleGroup != TOGGLE_NONE) {
        applyToggleLatch(st);
        if (!st.openBranch &&
            !st.latches.piesOpen && !st.latches.lowOpen && !st.latches.allOpen) {
            addFinal(st, SEQ_ACT_DOME_CMD, ":CL00");
        }
    }
}

static void finishIdle(SeqEngineState& st) {
    st.entry = nullptr;
    st.steps = nullptr;
    st.stepCount = 0;
    st.activeFx = 0;
    st.finishing = false;
    st.pendingComputed = false;
}

// Resolve the step under the cursor into a pending action with an absolute
// fire time. Returns false for step types that emit nothing (skipped).
static bool resolveStep(SeqEngineState& st, const SeqStep& step, SeqRandFn rnd) {
    (void)rnd;  // used by STEP_RANDOM (later slice-2 commit)

    SeqAction a;
    a.kind = SEQ_ACT_NONE;
    a.audioCategory = 0;
    a.audioFallbackSlot = 0;
    a.payload[0] = '\0';

    switch (step.type) {
        case STEP_DOME_CMD:
            a.kind = SEQ_ACT_DOME_CMD;
            setPayload(a, step.payload);
            break;
        case STEP_AUDIO:
            a.kind = SEQ_ACT_AUDIO_DOLLAR;
            setPayload(a, step.payload);
            break;
        case STEP_AUDIO_CATEGORY:
            a.kind = SEQ_ACT_AUDIO_CATEGORY;
            a.audioCategory = step.params.audioCategory;
            a.audioFallbackSlot = step.params.audioFallbackSlot;
            break;
        default:
            return false;  // STEP_LOOP / STEP_RANDOM: later slice-2 commits
    }

    st.pending = a;
    st.pendingFireAt = st.startMs + step.tMs;
    st.pendingComputed = true;
    return true;
}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

void seqEngineInit(SeqEngineState& st) {
    memset(&st, 0, sizeof(st));
}

bool seqEngineActive(const SeqEngineState& st) {
    return st.entry != nullptr;
}

const char* seqEngineName(const SeqEngineState& st) {
    return st.entry != nullptr ? st.entry->name : nullptr;
}

void seqEngineClearLatches(SeqEngineState& st) {
    st.latches.piesOpen = false;
    st.latches.lowOpen = false;
    st.latches.allOpen = false;
}

void seqEngineStart(SeqEngineState& st, const SequenceEntry* entry, uint32_t nowMs) {
    if (entry == nullptr) {
        return;
    }

    const SeqStep* steps = entry->steps;
    uint8_t count = entry->stepCount;
    bool openBranch = false;

    if (entry->toggleGroup != TOGGLE_NONE && entry->closeSteps != nullptr) {
        bool groupOpen = false;
        switch (entry->toggleGroup) {
            case TOGGLE_PIES: groupOpen = st.latches.piesOpen; break;
            case TOGGLE_LOW:  groupOpen = st.latches.lowOpen;  break;
            case TOGGLE_ALL:  groupOpen = st.latches.allOpen;  break;
            default: break;
        }
        if (groupOpen) {
            steps = entry->closeSteps;
            count = entry->closeStepCount;
        } else {
            openBranch = true;
        }
    }

    st.entry = entry;
    st.steps = steps;
    st.stepCount = count;
    st.openBranch = openBranch;
    st.cursor = 0;
    st.startMs = nowMs;
    st.activeFx = 0;
    st.inLoop = false;
    st.loopHeader = 0;
    st.iterStartRel = 0;
    st.heldSlot = 0;
    st.pickedMask = 0;
    st.pendingComputed = false;
    st.pendingFireAt = 0;
    st.finishing = false;
    st.finishAbnormal = false;
    st.finalCount = 0;
    st.finalCursor = 0;
}

void seqEngineAbort(SeqEngineState& st) {
    if (st.entry == nullptr || st.finishing) {
        return;
    }
    beginFinish(st, true);
}

bool seqEnginePeek(SeqEngineState& st, uint32_t nowMs, SeqRandFn rnd, SeqAction& out) {
    if (st.entry == nullptr) {
        return false;
    }

    if (!st.finishing) {
        while (true) {
            if (st.cursor >= st.stepCount) {
                // Malformed table (no STEP_END sentinel) — finish defensively.
                beginFinish(st, false);
                break;
            }
            const SeqStep& step = st.steps[st.cursor];

            if (step.type == STEP_END) {
                if (!timeReached(nowMs, st.startMs + step.tMs)) {
                    return false;
                }
                beginFinish(st, false);
                break;
            }

            if (!st.pendingComputed) {
                if (!resolveStep(st, step, rnd)) {
                    st.cursor++;
                    continue;
                }
            }
            if (!timeReached(nowMs, st.pendingFireAt)) {
                return false;
            }
            out = st.pending;
            return true;
        }
    }

    // Finishing: drain terminal auto-reset actions, then go idle.
    if (st.finalCursor < st.finalCount) {
        out = st.finalQ[st.finalCursor];
        return true;
    }
    finishIdle(st);
    return false;
}

void seqEngineCommit(SeqEngineState& st) {
    if (st.entry == nullptr) {
        return;
    }

    if (st.finishing) {
        if (st.finalCursor < st.finalCount) {
            const SeqAction& a = st.finalQ[st.finalCursor];
            if (a.kind == SEQ_ACT_DOME_CMD && strcmp(a.payload, ":CL00") == 0) {
                seqEngineClearLatches(st);
            }
            st.finalCursor++;
        }
        if (st.finalCursor >= st.finalCount) {
            finishIdle(st);
        }
        return;
    }

    if (st.cursor >= st.stepCount || !st.pendingComputed) {
        return;  // nothing peeked — defensive
    }

    const SeqStep& step = st.steps[st.cursor];
    st.activeFx |= step.effectClass;

    // Body-authoritative latched panel state: any close-all on the wire means
    // every panel group is closed, regardless of which sequence sent it.
    if (st.pending.kind == SEQ_ACT_DOME_CMD && strcmp(st.pending.payload, ":CL00") == 0) {
        seqEngineClearLatches(st);
    }

    st.pendingComputed = false;
    st.cursor++;
}
