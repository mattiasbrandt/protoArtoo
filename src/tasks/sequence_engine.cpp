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

// dueRel is the fire offset (ms) relative to finishStartMs. Instant resets pass
// 0; staggered individual ring closes pass increasing offsets so only one ring
// servo actuates at a time (group closes brown out the dome — see addRingClose).
static void addFinal(SeqEngineState& st, SeqActionKind kind, const char* payload,
                     uint16_t dueRel = 0) {
    const uint8_t cap = (uint8_t)(sizeof(st.finalQ) / sizeof(st.finalQ[0]));
    if (st.finalCount >= cap) {
        return;
    }
    const uint8_t idx = st.finalCount++;
    SeqAction& a = st.finalQ[idx];
    a.kind = kind;
    a.audioCategory = 0;
    a.audioFallbackSlot = 0;
    a.domeSpeedPct = 0;
    a.domeDurationMs = 0;
    setPayload(a, payload);
    st.finalDueRel[idx] = dueRel;
}

static void addFinalDomeRotateStop(SeqEngineState& st) {
    const uint8_t cap = (uint8_t)(sizeof(st.finalQ) / sizeof(st.finalQ[0]));
    if (st.finalCount >= cap) {
        return;
    }
    const uint8_t idx = st.finalCount++;
    SeqAction& a = st.finalQ[idx];
    a.kind = SEQ_ACT_DOME_ROTATE;
    a.audioCategory = 0;
    a.audioFallbackSlot = 0;
    a.domeSpeedPct = 0;
    a.domeDurationMs = 0;
    a.payload[0] = '\0';
    st.finalDueRel[idx] = 0;
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

// Physical ring (bottom) panels in bit order. Pie panels are tracked separately
// and never auto-closed by engine cleanup (pie-close mechanical safety is a
// separate open item). kRingPanels[i] maps to ringOpenMask bit i.
static const uint8_t  kRingPanels[]     = {1, 2, 3, 4, 7, 11, 13};
static const uint8_t  kRingPanelCount   = (uint8_t)(sizeof(kRingPanels) / sizeof(kRingPanels[0]));
static const uint16_t kRingAllMask      = (uint16_t)((1u << kRingPanelCount) - 1u);
// Per-panel cadence for staggered cleanup closes: only one ring servo actuates
// at a time, so single-servo inrush never overlaps (a simultaneous group close
// browns out the dome from a loaded ring — 2026-06-17 hardware repro).
static const uint16_t kRingCloseSpacingMs = 500;

// ringOpenMask bit index for ring panel number n, or -1 if not a ring panel.
static int ringPanelBit(int n) {
    for (uint8_t i = 0; i < kRingPanelCount; i++) {
        if ((int)kRingPanels[i] == n) return (int)i;
    }
    return -1;
}

static void setAllRingOpen(SeqEngineState& st, bool open) {
    if (open) st.ringOpenMask |= kRingAllMask;
    else      st.ringOpenMask  = (uint16_t)(st.ringOpenMask & ~kRingAllMask);
}

// Update the per-run net-open RING mask from a dispatched dome command. Only
// :OP/:CL change logical open state; :OF leaves it uncertain (no mark — the
// authored branch must clean up its own flutters). Pie targets (14 group, P*
// individual) do not affect the ring mask.
static void recordRingOpenState(SeqEngineState& st, const char* cmd) {
    if (cmd == nullptr || cmd[0] != ':') {
        return;
    }
    bool open;
    if (cmd[1] == 'O' && cmd[2] == 'P')      open = true;   // :OP — open
    else if (cmd[1] == 'C' && cmd[2] == 'L') open = false;  // :CL — close
    else return;                                            // :OF / non-panel — no change
    const char* t = cmd + 3;
    if (t[0] == '0' && t[1] == '0') { setAllRingOpen(st, open); return; } // 00 = all
    if (t[0] == '1' && t[1] == '5') { setAllRingOpen(st, open); return; } // 15 = ring group
    if (t[0] == '1' && t[1] == '4') return;                              // 14 = pie group
    if (t[0] == 'P') return;                                             // P* = pie individual
    if (t[0] < '0' || t[0] > '9' || t[1] < '0' || t[1] > '9') return;
    const int bit = ringPanelBit((t[0] - '0') * 10 + (t[1] - '0'));
    if (bit < 0) return;
    if (open) st.ringOpenMask |= (uint16_t)(1u << bit);
    else      st.ringOpenMask  = (uint16_t)(st.ringOpenMask & ~(1u << bit));
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

// Queue load-shaped terminal ring cleanup: NEVER a group close (:CL15/:CL14/
// :CL00) and NEVER an automatic pie close. A group close drives every servo in
// the group simultaneously, which browns out the dome from a loaded ring
// (2026-06-17 hardware repro: reset_reason=BROWNOUT at terminal :CL15). So close
// only the ring panels this run left logically OPEN, one at a time at
// kRingCloseSpacingMs cadence. No-op when nothing is left open. Pie panels left
// open are NOT auto-closed here (pie-close safety is a separate open item) — an
// authored sequence that opens pies must close them in its own branch.
static void addRingClose(SeqEngineState& st) {
    uint16_t emitted = 0;
    for (uint8_t i = 0; i < kRingPanelCount; i++) {
        if (!(st.ringOpenMask & (uint16_t)(1u << i))) {
            continue;
        }
        const uint8_t n = kRingPanels[i];
        char cmd[6] = { ':', 'C', 'L', (char)('0' + n / 10), (char)('0' + n % 10), '\0' };
        emitted++;
        addFinal(st, SEQ_ACT_DOME_CMD, cmd, (uint16_t)(kRingCloseSpacingMs * emitted));
    }
}

uint8_t seqEngineRingPanelCount(void) {
    return kRingPanelCount;
}

bool seqEngineRingCloseCmd(uint8_t i, char* buf, uint8_t bufLen) {
    if (i >= kRingPanelCount || buf == nullptr || bufLen < 6) {
        return false;
    }
    const uint8_t n = kRingPanels[i];
    buf[0] = ':';
    buf[1] = 'C';
    buf[2] = 'L';
    buf[3] = (char)('0' + n / 10);
    buf[4] = (char)('0' + n % 10);
    buf[5] = '\0';
    return true;
}

int seqEngineRingPanelNumber(uint8_t i) {
    if (i >= kRingPanelCount) {
        return -1;
    }
    return (int)kRingPanels[i];
}

bool seqEngineFinishing(const SeqEngineState& st) {
    return st.finishing;
}

// Build the terminal auto-reset queue from activeFx (ADR 0004 decision 7).
// FX_PANEL close-all is suppressed on the normal end of a toggle open branch
// (panels are meant to stay open); FX_AUDIO stops playback only on abnormal
// termination so tracks that outlive their sequence end naturally.
static void beginFinish(SeqEngineState& st, bool abnormal) {
    st.finishing = true;
    st.finishAbnormal = abnormal;
    st.finishStartSet = false;
    st.finalCount = 0;
    st.finalCursor = 0;
    st.pendingComputed = false;

    bool wantRingClose = false;

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
        // A :SE## dome-native sequence manages its own panels; ring cleanup
        // closes only the ring panels the body itself left open (none for a pure
        // :SE## step). Never a group close.
        wantRingClose = true;
    } else if (st.activeFx & FX_PANEL) {
        if (abnormal || !st.openBranch) {
            wantRingClose = true;
        }
    }
    if ((st.activeFx & FX_AUDIO) && abnormal) {
        addFinal(st, SEQ_ACT_AUDIO_STOP, nullptr);
    }
    if (st.domeRotateActive) {
        addFinalDomeRotateStop(st);
    }

    // Toggle bookkeeping on normal completion. A close branch runs its own
    // per-target closes; once no group remains latched open we still request ring
    // cleanup, which is a no-op when the branch already closed every ring panel
    // (e.g. DM:LOW) — that is exactly why the brownout-prone terminal group close
    // is gone: addRingClose closes only ring panels actually left open, staggered.
    if (!abnormal && st.entry->toggleGroup != TOGGLE_NONE) {
        applyToggleLatch(st);
        if (!st.openBranch &&
            !st.latches.piesOpen && !st.latches.ringOpen) {
            wantRingClose = true;
        }
    }

    // Enqueue staggered individual ring closes LAST so the instant effect resets
    // and audio stop above are not delayed behind the spaced closes.
    if (wantRingClose) {
        addRingClose(st);
    }
}

static void finishIdle(SeqEngineState& st) {
    st.entry = nullptr;
    st.steps = nullptr;
    st.stepCount = 0;
    st.activeFx = 0;
    st.domeRotateActive = false;
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
    a.domeSpeedPct = 0;
    a.domeDurationMs = 0;
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
        case STEP_DOME_ROTATE:
            a.kind = SEQ_ACT_DOME_ROTATE;
            a.domeSpeedPct = step.params.speedPct;
            a.domeDurationMs = step.params.durationMs;
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
    st.ringOpenMask = 0;
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
    st.domeRotateActive = false;
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

            if (step.type == STEP_CLEAR_LATCHES) {
                // Body-internal latch reset (no dome/audio output). Honor the
                // authored fire time so it lands after the staggered closes that
                // precede it, then advance without emitting an action.
                if (!timeReached(nowMs, st.startMs + step.tMs)) {
                    return false;
                }
                seqEngineClearLatches(st);
                st.cursor++;
                continue;
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

    // Finishing: drain terminal auto-reset actions at their scheduled offsets,
    // then go idle. finishStartMs is anchored lazily on the first finishing peek
    // (seqEngineAbort carries no nowMs). Staggered ring closes carry increasing
    // finalDueRel offsets so only one ring servo actuates at a time; instant
    // resets (offset 0) fire immediately. Returning false when the next action is
    // not yet due keeps the engine active so the dispatcher ticks back.
    if (st.finalCursor < st.finalCount) {
        if (!st.finishStartSet) {
            st.finishStartMs = nowMs;
            st.finishStartSet = true;
        }
        if (!timeReached(nowMs, st.finishStartMs + st.finalDueRel[st.finalCursor])) {
            return false;
        }
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
        recordRingOpenState(st, st.pending.payload);
        updateLatchesForClose(st, st.pending.payload);
    } else if (st.pending.kind == SEQ_ACT_DOME_ROTATE &&
               st.pending.domeSpeedPct != 0) {
        st.domeRotateActive = true;
    }

    st.pendingComputed = false;
    st.cursor++;
}
