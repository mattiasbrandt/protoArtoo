// =============================================================================
// src/protocol_check.cpp
//
// Protocol Check implementation (issue #2 slice 3, ADR 0006). See header for
// the contract. Pure: depends only on the sequence model, the catalog lookup
// (for retrain rules), and the audio policy enums (for category bounds).
// =============================================================================

#include "protocol_check.h"

#include <string.h>

#include "audio_playback_policy.h"   // AUDIO_CATEGORY_COUNT, AUDIO_SLOT_COUNT
#include "sequence_dispatcher.h"     // sequenceCatalogFind()

// Result constructors (pcOk/pcFail/pcFailAt) are shared inlines in the header.

// -----------------------------------------------------------------------------
// Small parsers (no libc locale surprises; pure ASCII)
// -----------------------------------------------------------------------------
static bool isDigit(char c) { return c >= '0' && c <= '9'; }
static bool isUpper(char c) { return c >= 'A' && c <= 'Z'; }
static bool isAlnum(char c) {
    return isDigit(c) || isUpper(c) || (c >= 'a' && c <= 'z');
}
static bool isPrintable(char c) { return c >= 0x20 && c <= 0x7E; }

// Parse a run of decimal digits starting at *p into out; advance *p. Returns the
// digit count (0 => no number present, out untouched).
static int parseUint(const char** p, uint32_t& out) {
    const char* s = *p;
    uint32_t v = 0;
    int n = 0;
    while (isDigit(*s)) {
        v = v * 10 + (uint32_t)(*s - '0');
        ++s;
        ++n;
        if (n > 9) break;  // overflow guard; field bounds reject below anyway
    }
    if (n > 0) {
        out = v;
        *p = s;
    }
    return n;
}

// Bounded printable-charset check for a NUL-terminated command payload.
static bool charsetOk(const char* s) {
    size_t len = strnlen(s, PC_CMD_MAX + 1);
    if (len == 0 || len > PC_CMD_MAX) {
        return false;
    }
    for (size_t i = 0; i < len; ++i) {
        if (!isPrintable(s[i])) {
            return false;
        }
    }
    return true;
}

// -----------------------------------------------------------------------------
// Toggle group
// -----------------------------------------------------------------------------
bool protocolCheckToggleGroupValid(SeqToggleGroup g) {
    switch (g) {
        case TOGGLE_NONE:
        case TOGGLE_PIES:
        case TOGGLE_LOW:
        case TOGGLE_ALL:
        case TOGGLE_USER1:
        case TOGGLE_USER2:
        case TOGGLE_USER3:
        case TOGGLE_USER4:
            return true;
        default:
            return false;
    }
}

// -----------------------------------------------------------------------------
// Name: ^DM:[A-Z0-9_]{1,18}$
// -----------------------------------------------------------------------------
static bool nameValid(const char* name) {
    if (name == nullptr) return false;
    if (strncmp(name, "DM:", 3) != 0) return false;
    const char* body = name + 3;
    size_t len = strnlen(body, PC_NAME_BODY_MAX + 1);
    if (len == 0 || len > PC_NAME_BODY_MAX) return false;
    for (size_t i = 0; i < len; ++i) {
        char c = body[i];
        if (!(isUpper(c) || isDigit(c) || c == '_')) return false;
    }
    return true;
}

// -----------------------------------------------------------------------------
// Dome command whitelist + effect-class inference.
//   :SM<slot>,<pulse>,<move>   panel move    -> FX_PANEL when pulse>CLOSE
//   :CL00                       close+release -> FX_NONE (is itself a reset)
//   :SE<dd>                     dome sequence -> FX_NONE (dome-owned)
//   @0T1 / @0P1                 logic/PSI reset -> FX_NONE
//   @<d>{T,P,M}...              logic/PSI/text  -> FX_LOGIC_PSI
//   @HP...                      holo            -> FX_HOLO
//   *ST00                       holo reset      -> FX_NONE
//   *...                        holo            -> FX_HOLO
// Inference is conservative: an over-tag only causes an extra idempotent reset.
// -----------------------------------------------------------------------------
static ProtocolCheckResult classifyDome(const char* label, uint8_t idx,
                                        const char* cmd, uint8_t& fxOut) {
    fxOut = FX_NONE;

    if (!charsetOk(cmd)) {
        return pcFailAt(label, idx, "cmd", "empty, too long, or non-printable");
    }

    // :SM<slot>,<pulse>,<move>
    if (strncmp(cmd, ":SM", 3) == 0) {
        const char* p = cmd + 3;
        uint32_t slot = 0, pulse = 0, move = 0;
        if (parseUint(&p, slot) == 0 || *p != ',') {
            return pcFailAt(label, idx, "cmd", ":SM missing slot");
        }
        ++p;
        if (parseUint(&p, pulse) == 0 || *p != ',') {
            return pcFailAt(label, idx, "cmd", ":SM missing pulse");
        }
        ++p;
        if (parseUint(&p, move) == 0 || *p != '\0') {
            return pcFailAt(label, idx, "cmd", ":SM malformed (want slot,pulse,move)");
        }
        if (slot > PC_SM_SLOT_MAX) {
            return pcFailAt(label, idx, "cmd", ":SM slot out of range (0..12)");
        }
        if (pulse < PC_SM_PULSE_MIN || pulse > PC_SM_PULSE_MAX) {
            return pcFailAt(label, idx, "cmd", ":SM pulse out of range (800..2200)");
        }
        if (move < PC_SM_MOVE_MIN || move > PC_SM_MOVE_MAX) {
            return pcFailAt(label, idx, "cmd", ":SM move out of range (50..5000)");
        }
        fxOut = (pulse > PC_SM_PULSE_MIN) ? FX_PANEL : FX_NONE;
        return pcOk();
    }

    // :CL00 (the only accepted close-all form)
    if (strcmp(cmd, ":CL00") == 0) {
        fxOut = FX_NONE;
        return pcOk();
    }

    // :SE<dd> — exactly two digits (the Marcduino zero-padded form, e.g.
    // :SE09), so every dome sequence has a single canonical spelling.
    if (strncmp(cmd, ":SE", 3) == 0) {
        const char* p = cmd + 3;
        uint32_t n = 0;
        if (parseUint(&p, n) != 2 || *p != '\0') {
            return pcFailAt(label, idx, "cmd", ":SE want exactly 2 digits");
        }
        fxOut = FX_NONE;
        return pcOk();
    }

    // @... logic / PSI / text / holo
    if (cmd[0] == '@') {
        if (strcmp(cmd, "@0T1") == 0 || strcmp(cmd, "@0P1") == 0) {
            fxOut = FX_NONE;  // explicit reset
            return pcOk();
        }
        if (cmd[1] == 'H' && cmd[2] == 'P') {
            fxOut = FX_HOLO;  // @HP... holo
            return pcOk();
        }
        if (isDigit(cmd[1]) &&
            (cmd[2] == 'T' || cmd[2] == 'P' || cmd[2] == 'M')) {
            fxOut = FX_LOGIC_PSI;
            return pcOk();
        }
        return pcFailAt(label, idx, "cmd", "unrecognised @ command");
    }

    // *... holo (with *ST00 as the reset)
    if (cmd[0] == '*') {
        fxOut = (strcmp(cmd, "*ST00") == 0) ? FX_NONE : FX_HOLO;
        return pcOk();
    }

    return pcFailAt(label, idx, "cmd", "unknown command prefix");
}

// $<1..6 alnum>
static ProtocolCheckResult classifyAudio(const char* label, uint8_t idx,
                                         const char* cmd) {
    size_t len = strnlen(cmd, 8);
    if (cmd[0] != '$' || len < 2 || len > 7) {
        return pcFailAt(label, idx, "cmd", "audio want $ + 1-6 chars");
    }
    for (size_t i = 1; i < len; ++i) {
        if (!isAlnum(cmd[i])) {
            return pcFailAt(label, idx, "cmd", "audio chars must be alphanumeric");
        }
    }
    return pcOk();
}

// -----------------------------------------------------------------------------
// Metadata + retrain rules
// -----------------------------------------------------------------------------
ProtocolCheckResult protocolCheckMeta(const char* name, uint32_t suppressMs,
                                      SeqToggleGroup toggleGroup,
                                      uint32_t endTimeMs) {
    if (!nameValid(name)) {
        return pcFail("name", "must match DM:[A-Z0-9_]{1,18}");
    }
    if (!protocolCheckToggleGroupValid(toggleGroup)) {
        return pcFail("toggleGroup", "unknown toggle group");
    }
    if (toggleGroup >= TOGGLE_USER1 && toggleGroup <= TOGGLE_USER4) {
        // The engine's branch-pick/latch switches are not wired for the user
        // latches yet — such a toggle would run open-branch-only and never
        // latch. Reject on save so a Learned toggle cannot execute silently
        // wrong; lift this when the engine gains user-latch state.
        return pcFail("toggleGroup", "user toggle groups are not supported yet");
    }
    if (suppressMs < PC_SUPPRESS_MIN_MS || suppressMs > PC_SUPPRESS_MAX_MS) {
        return pcFail("suppressMs", "out of range (1000..120000)");
    }
    if (suppressMs < endTimeMs) {
        return pcFail("suppressMs", "must be >= sequence end time");
    }

    // Retrain (shadowing) rules: a Learned Sequence bearing a Factory name must
    // keep the factory's toggle semantics coherent (ADR 0006 / issue #2 grill 4).
    const SequenceEntry* factory = sequenceCatalogFind(name);
    if (factory != nullptr) {
        if (factory->toggleGroup != TOGGLE_NONE) {
            if (toggleGroup != factory->toggleGroup) {
                return pcFail("toggleGroup",
                            "retraining a factory toggle requires the same group");
            }
        } else if (toggleGroup != TOGGLE_NONE) {
            return pcFail("toggleGroup",
                        "retraining a factory non-toggle requires group none");
        }
    }
    return pcOk();
}

// -----------------------------------------------------------------------------
// Branch validation + effect-class stamping
// -----------------------------------------------------------------------------
ProtocolCheckResult protocolCheckBranch(const char* label, SeqStep* steps,
                                        uint8_t count) {
    if (steps == nullptr || count == 0) {
        return pcFail(label, "branch is empty");
    }
    if (count > PC_MAX_STEPS) {
        return pcFail(label, "too many steps (max 96)");
    }
    // Exactly one terminal STEP_END, and it must be last.
    for (uint8_t i = 0; i < count; ++i) {
        if (steps[i].type == STEP_END && i != (uint8_t)(count - 1)) {
            return pcFailAt(label, i, "type", "STEP_END only allowed as last step");
        }
    }
    if (steps[count - 1].type != STEP_END) {
        return pcFailAt(label, (uint8_t)(count - 1), "type",
                      "branch must end with STEP_END");
    }

    // Mark loop-body members and validate loop structure (no nesting).
    bool inBody[PC_MAX_STEPS] = { false };
    for (uint8_t i = 0; i < count; ++i) {
        if (steps[i].type != STEP_LOOP) continue;
        const SeqStepParams& lp = steps[i].params;
        if (lp.bodyCount == 0) {
            return pcFailAt(label, i, "bodyCount", "loop body is empty");
        }
        uint16_t last = (uint16_t)i + lp.bodyCount;  // last body index
        if (last >= count || (uint16_t)(last) >= PC_MAX_STEPS) {
            return pcFailAt(label, i, "bodyCount", "loop body overruns the branch");
        }
        if (lp.periodMs < PC_LOOP_PERIOD_MIN || lp.periodMs > PC_LOOP_PERIOD_MAX) {
            return pcFailAt(label, i, "periodMs", "out of range (100..60000)");
        }
        if (lp.durationMs == 0 || lp.durationMs > PC_LOOP_DUR_MAX) {
            return pcFailAt(label, i, "durationMs", "out of range (1..120000)");
        }
        for (uint8_t j = (uint8_t)(i + 1); j <= (uint8_t)last; ++j) {
            if (steps[j].type == STEP_LOOP) {
                return pcFailAt(label, j, "type", "nested loops are not allowed");
            }
            inBody[j] = true;
        }
    }

    // Per-step validation + effect-class stamping + monotonic t (top level only).
    uint32_t prevT = 0;
    for (uint8_t i = 0; i < count; ++i) {
        SeqStep& s = steps[i];

        if (!inBody[i]) {
            if (s.tMs < prevT) {
                return pcFailAt(label, i, "t", "t must be non-decreasing");
            }
            prevT = s.tMs;
        }

        switch (s.type) {
            case STEP_DOME_CMD: {
                uint8_t fx = FX_NONE;
                ProtocolCheckResult r = classifyDome(label, i, s.payload, fx);
                if (!r.ok) return r;
                s.effectClass = fx;
                break;
            }
            case STEP_AUDIO: {
                ProtocolCheckResult r = classifyAudio(label, i, s.payload);
                if (!r.ok) return r;
                s.effectClass = FX_AUDIO;  // stop on abnormal termination
                break;
            }
            case STEP_AUDIO_CATEGORY: {
                if (s.params.audioCategory >= AUDIO_CATEGORY_COUNT) {
                    return pcFailAt(label, i, "category", "unknown audio category");
                }
                if (s.params.audioFallbackSlot >= AUDIO_SLOT_COUNT) {
                    return pcFailAt(label, i, "fallback", "unknown fallback slot");
                }
                s.effectClass = FX_AUDIO;
                break;
            }
            case STEP_RANDOM: {
                const SeqStepParams& p = s.params;
                if (p.slotSet > SLOTSET_HOLD) {
                    return pcFailAt(label, i, "set", "unknown slot set");
                }
                if (p.pulseMin < PC_SM_PULSE_MIN || p.pulseMax > PC_SM_PULSE_MAX ||
                    p.pulseMin > p.pulseMax) {
                    return pcFailAt(label, i, "pulse", "random pulse out of range");
                }
                if (p.moveMs < PC_SM_MOVE_MIN || p.moveMs > PC_SM_MOVE_MAX) {
                    return pcFailAt(label, i, "moveMs", "random move out of range");
                }
                if (p.jitterMs > PC_RAND_JITTER_MAX) {
                    return pcFailAt(label, i, "jitterMs", "jitter too large (max 2000)");
                }
                s.effectClass = FX_PANEL;
                break;
            }
            case STEP_LOOP:
                s.effectClass = FX_NONE;  // body steps carry their own classes
                break;
            case STEP_END:
                s.effectClass = FX_NONE;
                break;
            default:
                return pcFailAt(label, i, "type", "unknown step type");
        }
    }
    return pcOk();
}

// -----------------------------------------------------------------------------
// Full draft check
// -----------------------------------------------------------------------------
ProtocolCheckResult protocolCheck(SeqDraft& draft) {
    if (draft.steps == nullptr || draft.stepCount == 0) {
        return pcFail("steps", "main branch is empty");
    }
    // End time = terminal STEP_END of the main branch.
    uint32_t endTimeMs = draft.steps[draft.stepCount - 1].tMs;

    ProtocolCheckResult r =
        protocolCheckMeta(draft.name, draft.suppressMs, draft.toggleGroup, endTimeMs);
    if (!r.ok) return r;

    r = protocolCheckBranch("steps", draft.steps, draft.stepCount);
    if (!r.ok) return r;

    // A toggle group requires a close branch; a non-toggle must not carry one.
    const bool isToggle = (draft.toggleGroup != TOGGLE_NONE);
    const bool hasClose = (draft.closeSteps != nullptr && draft.closeStepCount > 0);
    if (isToggle && !hasClose) {
        return pcFail("closeSteps", "toggle sequence needs a close branch");
    }
    if (!isToggle && hasClose) {
        return pcFail("closeSteps", "non-toggle sequence must not have a close branch");
    }
    if (hasClose) {
        r = protocolCheckBranch("closeSteps", draft.closeSteps, draft.closeStepCount);
        if (!r.ok) return r;
    }
    return pcOk();
}
