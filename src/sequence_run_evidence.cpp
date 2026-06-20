// =============================================================================
// sequence_run_evidence.cpp — see sequence_run_evidence.h.
//
// One static record, written on the dispatcher task and read (snapshot-copied)
// by the Core 0 web handler. Guarded by a portMUX (no-op in native tests). Net-
// open / touched ring masks and effect scopes are derived from the recorded
// command stream, so the record reflects what was actually sent on the wire.
// =============================================================================
#include "sequence_run_evidence.h"

#include <Arduino.h>      // portMUX_TYPE, taskENTER_CRITICAL (native: stubbed)
#include <string.h>

#include "robot_state.h"  // portMUX_TYPE

static portMUX_TYPE seqEvidenceMux = portMUX_INITIALIZER_UNLOCKED;
static SeqRunEvidence g;                 // the live record (zero-initialized)
static uint32_t       gDropBaseline = 0; // queueOverflowCount at run start

// ---- ring panel set helpers (single source = sequence_engine) ----------------
static uint16_t ringAllMask() {
    const uint8_t c = seqEngineRingPanelCount();
    return (uint16_t)((1u << c) - 1u);
}

static int ringBitForNumber(int n) {
    const uint8_t c = seqEngineRingPanelCount();
    for (uint8_t i = 0; i < c; i++) {
        if (seqEngineRingPanelNumber(i) == n) return (int)i;
    }
    return -1;
}

// ---- derivation from a recorded dome command --------------------------------
static void applyScope(SeqRunEvidence& r, const char* cmd) {
    if (cmd[0] == ':') {
        if ((cmd[1] == 'O' && (cmd[2] == 'P' || cmd[2] == 'F')) ||
            (cmd[1] == 'C' && cmd[2] == 'L')) {
            r.fxScopes |= SEQ_EVID_FX_PANEL;
        } else if (cmd[1] == 'S' && cmd[2] == 'E') {
            r.fxScopes |= SEQ_EVID_FX_DOME_SEQ;
        }
        return;
    }
    if (cmd[0] == '@') {
        if (strcmp(cmd, "@0T1") == 0 || strcmp(cmd, "@0P1") == 0) return;  // reset
        if (cmd[1] == 'H' && cmd[2] == 'P') r.fxScopes |= SEQ_EVID_FX_HOLO;
        else                                r.fxScopes |= SEQ_EVID_FX_LOGIC_PSI;
        return;
    }
    if (cmd[0] == '*') {
        if (strcmp(cmd, "*ST00") != 0) r.fxScopes |= SEQ_EVID_FX_HOLO;  // *ST00 = reset
        return;
    }
    if (cmd[0] == 'D' && cmd[1] == 'V' && cmd[2] == ':') {  // future DV: visual preset
        r.fxScopes |= (uint8_t)(SEQ_EVID_FX_LOGIC_PSI | SEQ_EVID_FX_HOLO);
    }
}

static void applyRing(SeqRunEvidence& r, const char* cmd) {
    if (cmd[0] != ':') return;
    bool isOpen = false, isClose = false;
    if (cmd[1] == 'O' && cmd[2] == 'P')      isOpen = true;
    else if (cmd[1] == 'C' && cmd[2] == 'L') isClose = true;
    else if (cmd[1] == 'O' && cmd[2] == 'F') { /* flutter: touched only */ }
    else return;  // :SE / other — not a panel command

    const char* t = cmd + 3;
    uint16_t bits = 0;
    if (t[0] == '0' && t[1] == '0')      bits = ringAllMask();  // 00 = all (ring part)
    else if (t[0] == '1' && t[1] == '5') bits = ringAllMask();  // 15 = ring group
    else if (t[0] == '1' && t[1] == '4') return;                // 14 = pie group
    else if (t[0] == 'P')                return;                // individual pie
    else {
        if (t[0] < '0' || t[0] > '9' || t[1] < '0' || t[1] > '9') return;
        const int b = ringBitForNumber((t[0] - '0') * 10 + (t[1] - '0'));
        if (b < 0) return;
        bits = (uint16_t)(1u << b);
    }
    r.touchedRingMask |= bits;
    if (isOpen)       r.netOpenRingMask |= bits;
    else if (isClose) r.netOpenRingMask = (uint16_t)(r.netOpenRingMask & ~bits);
}

// Human-readable form of an emitted action for the TX/cleanup buffers.
static void actionToString(const SeqAction& act, char* out, size_t cap) {
    switch (act.kind) {
        case SEQ_ACT_DOME_CMD:
        case SEQ_ACT_AUDIO_DOLLAR:
            strncpy(out, act.payload, cap - 1);
            out[cap - 1] = '\0';
            break;
        case SEQ_ACT_AUDIO_CATEGORY:
            snprintf(out, cap, "<audioCat:%u>", (unsigned)act.audioCategory);
            break;
        case SEQ_ACT_AUDIO_STOP:
            strncpy(out, "<stop>", cap - 1);
            out[cap - 1] = '\0';
            break;
        case SEQ_ACT_DOME_ROTATE:
            snprintf(out, cap, "<domeRotate:%d:%u>",
                     (int)act.domeSpeedPct, (unsigned)act.domeDurationMs);
            break;
        default:
            strncpy(out, "<none>", cap - 1);
            out[cap - 1] = '\0';
            break;
    }
}

// -----------------------------------------------------------------------------
const char* seqRunOutcomeName(SeqRunOutcome o) {
    switch (o) {
        case SEQ_RUN_RUNNING:   return "running";
        case SEQ_RUN_COMPLETED: return "completed";
        case SEQ_RUN_ABORTED:   return "aborted";
        case SEQ_RUN_PREEMPTED: return "preempted";
        case SEQ_RUN_ESTOP:     return "estop";
        case SEQ_RUN_RECONNECT: return "reconnect";
        case SEQ_RUN_NONE:
        default:                return "none";
    }
}

void seqEvidenceBegin(const char* name, uint8_t source, uint32_t startMs,
                      uint32_t domeQueueDropBaseline) {
    taskENTER_CRITICAL(&seqEvidenceMux);
    memset(&g, 0, sizeof(g));
    g.outcome = SEQ_RUN_RUNNING;
    g.valid = true;
    strncpy(g.name, name != nullptr ? name : "", SEQ_EVID_NAME_LEN - 1);
    g.name[SEQ_EVID_NAME_LEN - 1] = '\0';
    g.source = source;
    g.startMs = startMs;
    gDropBaseline = domeQueueDropBaseline;
    taskEXIT_CRITICAL(&seqEvidenceMux);
}

void seqEvidenceRecordTx(const SeqAction& act, bool cleanup) {
    char rep[SEQ_EVID_CMD_LEN];
    actionToString(act, rep, sizeof(rep));
    const bool isDome = (act.kind == SEQ_ACT_DOME_CMD ||
                         act.kind == SEQ_ACT_DOME_ROTATE);

    taskENTER_CRITICAL(&seqEvidenceMux);
    if (g.outcome == SEQ_RUN_RUNNING) {
        // General TX ring (oldest entry overwritten once full).
        if (g.txTotalCount >= SEQ_EVID_TX_CAP) g.txOverflowCount++;
        strncpy(g.tx[g.txHead], rep, SEQ_EVID_CMD_LEN - 1);
        g.tx[g.txHead][SEQ_EVID_CMD_LEN - 1] = '\0';
        g.txHead = (uint8_t)((g.txHead + 1) % SEQ_EVID_TX_CAP);
        g.txTotalCount++;

        if (cleanup) {
            g.cleanupTotalCount++;
            if (g.cleanupCount < SEQ_EVID_CLEANUP_CAP) {
                strncpy(g.cleanup[g.cleanupCount], rep, SEQ_EVID_CMD_LEN - 1);
                g.cleanup[g.cleanupCount][SEQ_EVID_CMD_LEN - 1] = '\0';
                g.cleanupCount++;
            } else {
                g.cleanupTruncated = true;
            }
        }

        if (act.kind == SEQ_ACT_DOME_CMD) {
            applyScope(g, rep);
            applyRing(g, rep);
        } else if (!isDome) {
            // SEQ_ACT_DOME_ROTATE has no Marcduino payload, no scope/ring tracking.
            g.fxScopes |= SEQ_EVID_FX_AUDIO;
        }
    }
    taskEXIT_CRITICAL(&seqEvidenceMux);
}

void seqEvidenceNoteRetry(void) {
    taskENTER_CRITICAL(&seqEvidenceMux);
    if (g.outcome == SEQ_RUN_RUNNING) g.dispatchRetryCount++;
    taskEXIT_CRITICAL(&seqEvidenceMux);
}

// Finalize the run. Only the first call after a Begin takes effect (guard on
// RUNNING), so an abort path that ends the run wins over the COMPLETED fallback
// the dispatcher emits at its later idle transition.
void seqEvidenceEnd(SeqRunOutcome outcome, const char* reason, uint32_t endMs,
                    uint32_t domeQueueDropNow) {
    taskENTER_CRITICAL(&seqEvidenceMux);
    if (g.outcome == SEQ_RUN_RUNNING) {
        g.outcome = outcome;
        strncpy(g.reason, reason != nullptr ? reason : "", SEQ_EVID_REASON_LEN - 1);
        g.reason[SEQ_EVID_REASON_LEN - 1] = '\0';
        g.endMs = endMs;
        g.domeQueueDropDelta =
            (domeQueueDropNow >= gDropBaseline) ? (domeQueueDropNow - gDropBaseline) : 0;
    }
    taskEXIT_CRITICAL(&seqEvidenceMux);
}

bool seqEvidenceSnapshot(SeqRunEvidence& out) {
    taskENTER_CRITICAL(&seqEvidenceMux);
    memcpy(&out, &g, sizeof(g));
    taskEXIT_CRITICAL(&seqEvidenceMux);
    return out.valid;
}
