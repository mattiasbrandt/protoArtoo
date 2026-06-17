// =============================================================================
// src/tasks/sequence_engine.cpp
//
// Pure DM:* sequence cursor engine (ADR 0004, issue #2). No Arduino, FreeRTOS,
// logging, or RNG dependencies — fully natively testable. See header for the
// peek/commit execution model.
//
// Panel movement is authored as logical dome panel intent; raw servo-pulse
// commands are reserved for manual diagnostics outside body-owned sequences.
// =============================================================================

#include <stdio.h>
#include <string.h>

#include "sequence_engine.h"

static const char* const kRingTargets[] = { "01", "02", "03", "04", "07", "11", "13" };
static const char* const kPieTargets[]  = { "P1", "P2", "P3", "P4", "P5", "P6" };
static const char* const kAllTargets[]  = {
    "01", "02", "03", "04", "07", "11", "13",
    "P1", "P2", "P3", "P4", "P5", "P6",
};

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
            st.latches.ringOpen = open;
            break;
        case TOGGLE_ALL:
            st.latches.piesOpen = open;
            st.latches.ringOpen = open;
            break;
        default:
            break;
    }
}

// Panel-intent command scope, for scoped terminal cleanup (issue #2). On this
// hardware :CL00 is a real "close every physical panel group" action, not a
// harmless safe-reset, so terminal cleanup must close only the groups the run
// touched. :CL and :OF count as touched scope too: a scoped repeat close is
// safe, whereas closing an untouched group is exactly the hazard we avoid.
static const uint8_t PANEL_RING = 1 << 0;
static const uint8_t PANEL_PIE  = 1 << 1;

// Map a panel-intent command (:OP/:CL/:OF<target>) to the group(s) it touches.
// Returns 0 for non-panel commands (@..., *..., :SE##, ...).
static uint8_t panelTouchBits(const char* cmd) {
    if (cmd == nullptr || cmd[0] != ':') {
        return 0;
    }
    const bool isPanel = (cmd[1] == 'O' && cmd[2] == 'P') ||   // open
                         (cmd[1] == 'C' && cmd[2] == 'L') ||   // close
                         (cmd[1] == 'O' && cmd[2] == 'F');     // flutter
    if (!isPanel) {
        return 0;
    }
    const char* t = cmd + 3;
    if (t[0] == 'P')                  return PANEL_PIE;              // P1..P6 pie aliases
    if (t[0] == '1' && t[1] == '4')  return PANEL_PIE;              // 14 = pie/top group
    if (t[0] == '1' && t[1] == '5')  return PANEL_RING;             // 15 = ring/bottom group
    if (t[0] == '0' && t[1] == '0')  return PANEL_RING | PANEL_PIE; // 00 = all panels
    return PANEL_RING;  // 01,02,03,04,07,11,13 = ring panels
}

// Record the touched scope of a dispatched dome command (no-op for non-panel).
static void recordPanelTouch(SeqEngineState& st, const char* cmd) {
    const uint8_t bits = panelTouchBits(cmd);
    if (bits & PANEL_RING) st.panelTouchedRing = true;
    if (bits & PANEL_PIE)  st.panelTouchedPie  = true;
}

// Body-authoritative latch update: an explicit group/all close on the wire means
// that group is now closed, regardless of which sequence sent it.
static void updateLatchesForClose(SeqEngineState& st, const char* cmd) {
    if (cmd == nullptr || cmd[0] != ':' || cmd[1] != 'C' || cmd[2] != 'L') {
        return;
    }
    const char* t = cmd + 3;
    if (t[0] == '0' && t[1] == '0') {            // :CL00 close-all
        st.latches.piesOpen = false;
        st.latches.ringOpen = false;
    } else if (t[0] == '1' && t[1] == '4') {     // :CL14 pie/top group
        st.latches.piesOpen = false;
    } else if (t[0] == '1' && t[1] == '5') {     // :CL15 ring/bottom group
        st.latches.ringOpen = false;
    }
}

// Queue the terminal panel close scoped to what the run touched: :CL00 only when
// both groups were touched, ring-only -> :CL15, pie-only -> :CL14, none -> skip.
static void addScopedPanelClose(SeqEngineState& st) {
    if (st.panelTouchedRing && st.panelTouchedPie) {
        addFinal(st, SEQ_ACT_DOME_CMD, ":CL00");
    } else if (st.panelTouchedRing) {
        addFinal(st, SEQ_ACT_DOME_CMD, ":CL15");
    } else if (st.panelTouchedPie) {
        addFinal(st, SEQ_ACT_DOME_CMD, ":CL14");
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
    if (st.activeFx & FX_DOME_SEQUENCE) {
        addFinal(st, SEQ_ACT_DOME_CMD, "@0T1");
        addFinal(st, SEQ_ACT_DOME_CMD, "@0P1");
        addFinal(st, SEQ_ACT_DOME_CMD, "*ST00");
        // Scoped, not blanket :CL00: a :SE## dome-native sequence manages its own
        // panels, so close only groups the body itself touched (none for a pure
        // :SE## step). :CL00 would stall the pies on this droid.
        addScopedPanelClose(st);
    } else if (st.activeFx & FX_PANEL) {
        if (abnormal || !st.openBranch) {
            addScopedPanelClose(st);
        }
    }
    if ((st.activeFx & FX_AUDIO) && abnormal) {
        addFinal(st, SEQ_ACT_AUDIO_STOP, nullptr);
    }

    // Toggle bookkeeping on normal completion. Close branches finish their
    // per-target closes without a release, so emit :CL00 once no group remains
    // latched open; releasing earlier would close panels another sequence left open.
    if (!abnormal && st.entry->toggleGroup != TOGGLE_NONE) {
        applyToggleLatch(st);
        if (!st.openBranch &&
            !st.latches.piesOpen && !st.latches.ringOpen) {
            addScopedPanelClose(st);
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

// Absolute fire time of the step under the cursor. Steps inside a STEP_LOOP
// body are scheduled relative to the current iteration start.
static uint32_t stepFireAt(const SeqEngineState& st, const SeqStep& step) {
    if (st.inLoop) {
        return st.startMs + st.steps[st.loopHeader].tMs + st.iterStartRel + step.tMs;
    }
    return st.startMs + step.tMs;
}

// Pick a panel slot for a STEP_RANDOM step. SLOTSET_HOLD reuses the previous
// pick (SCREAM flutters one panel through several moves). pickDistinct
// re-rolls a bounded number of times to avoid slots already picked this run
// (OVERLOAD drifts a distinct panel set), then accepts a repeat rather than
// looping forever on an exhausted set.
static uint8_t pickTarget(SeqEngineState& st, const SeqStep& step, SeqRandFn rnd) {
    if (step.params.slotSet == SLOTSET_HOLD) {
        return st.heldTarget;
    }

    uint8_t base = 0;
    uint8_t n = (uint8_t)(sizeof(kAllTargets) / sizeof(kAllTargets[0]));
    switch (step.params.slotSet) {
        case SLOTSET_RING:
            n = (uint8_t)(sizeof(kRingTargets) / sizeof(kRingTargets[0]));
            break;
        case SLOTSET_PIE:
            base = (uint8_t)(sizeof(kRingTargets) / sizeof(kRingTargets[0]));
            n = (uint8_t)(sizeof(kPieTargets) / sizeof(kPieTargets[0]));
            break;
        default:
            break;
    }

    uint8_t target = (uint8_t)(base + (rnd() % n));
    if (step.params.pickDistinct) {
        for (uint8_t attempt = 0;
             attempt < 8 && (st.pickedMask & (uint16_t)(1u << target)) != 0;
             ++attempt) {
            target = (uint8_t)(base + (rnd() % n));
        }
    }
    st.pickedMask |= (uint16_t)(1u << target);
    st.heldTarget = target;
    return target;
}

static const char* targetName(uint8_t target) {
    const uint8_t ringCount = (uint8_t)(sizeof(kRingTargets) / sizeof(kRingTargets[0]));
    if (target < ringCount) {
        return kRingTargets[target];
    }
    const uint8_t pieIdx = (uint8_t)(target - ringCount);
    if (pieIdx < (uint8_t)(sizeof(kPieTargets) / sizeof(kPieTargets[0]))) {
        return kPieTargets[pieIdx];
    }
    return "00";
}

// Resolve the step under the cursor into a pending action with an absolute
// fire time. Returns false for step types that emit nothing (skipped).
static bool resolveStep(SeqEngineState& st, const SeqStep& step, SeqRandFn rnd) {
    SeqAction a;
    a.kind = SEQ_ACT_NONE;
    a.audioCategory = 0;
    a.audioFallbackSlot = 0;
    a.payload[0] = '\0';
    uint32_t jitter = 0;

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
        case STEP_RANDOM: {
            const uint8_t target = pickTarget(st, step, rnd);
            const char* prefix = ":OF";
            if (step.params.pulseMin == RAND_OPEN) {
                prefix = ":OP";
            } else if (step.params.pulseMin == RAND_CLOSE) {
                prefix = ":CL";
            }
            a.kind = SEQ_ACT_DOME_CMD;
            snprintf(a.payload, sizeof(a.payload), "%s%s", prefix, targetName(target));
            if (step.params.jitterMs > 0) {
                jitter = rnd() % (uint32_t)(step.params.jitterMs + 1);
            }
            break;
        }
        default:
            return false;
    }

    st.pending = a;
    st.pendingFireAt = stepFireAt(st, step) + jitter;
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
    st.latches.ringOpen = false;
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
            case TOGGLE_LOW:  groupOpen = st.latches.ringOpen; break;
            case TOGGLE_ALL:  groupOpen = st.latches.piesOpen && st.latches.ringOpen; break;
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
    st.panelTouchedRing = false;
    st.panelTouchedPie = false;
    st.inLoop = false;
    st.loopHeader = 0;
    st.iterStartRel = 0;
    st.heldTarget = 0;
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

            // Loop body exhausted: start the next iteration if its start time
            // is still inside the loop duration, else fall through to the
            // post-loop steps (the final iteration may overhang durationMs;
            // post-loop tMs values are authored past the worst-case end).
            if (st.inLoop) {
                const SeqStepParams& lp = st.steps[st.loopHeader].params;
                const uint8_t bodyEnd = (uint8_t)(st.loopHeader + 1 + lp.bodyCount);
                if (st.cursor >= bodyEnd) {
                    const uint32_t nextIter = st.iterStartRel + lp.periodMs;
                    if (nextIter < lp.durationMs) {
                        st.iterStartRel = nextIter;
                        st.cursor = (uint8_t)(st.loopHeader + 1);
                    } else {
                        st.inLoop = false;
                    }
                    continue;
                }
            }

            const SeqStep& step = st.steps[st.cursor];

            if (step.type == STEP_END) {
                if (!timeReached(nowMs, st.startMs + step.tMs)) {
                    return false;
                }
                beginFinish(st, false);
                break;
            }

            if (step.type == STEP_LOOP) {
                if (!timeReached(nowMs, st.startMs + step.tMs)) {
                    return false;
                }
                if (step.params.bodyCount == 0) {
                    st.cursor++;  // degenerate loop — skip
                    continue;
                }
                st.inLoop = true;
                st.loopHeader = st.cursor;
                st.iterStartRel = 0;
                st.cursor++;
                continue;
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
            if (a.kind == SEQ_ACT_DOME_CMD) {
                updateLatchesForClose(st, a.payload);
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

    // Record touched panel scope for scoped terminal cleanup, and keep the
    // body-authoritative latches in step with explicit group/all closes on the
    // wire, regardless of which sequence sent them (issue #2).
    if (st.pending.kind == SEQ_ACT_DOME_CMD) {
        recordPanelTouch(st, st.pending.payload);
        updateLatchesForClose(st, st.pending.payload);
    }

    st.pendingComputed = false;
    st.cursor++;
}
