// =============================================================================
// test/test_native/test_sequence_engine/test_sequence_engine.cpp
//
// Native tests for the pure DM:* sequence cursor engine (sequence_engine.cpp).
// Synthetic clock, deterministic RNG, local step-table fixtures — no FreeRTOS.
//
// Covers ADR 0004 decisions 4 (preempt cleanup), 7 (auto-reset), and the
// peek/commit retry contract.
// =============================================================================

#include <stdio.h>
#include <string.h>

#include <unity.h>

#include "audio_playback_policy.h"
#include "droid_parts.h"  // droidPartIdIsKnown() - a routine names Parts the catalog has
#include "sequence_dispatcher.h"
#include "sequence_engine.h"

void setUp()    {}
void tearDown() {}

// Deterministic RNG stub — value irrelevant for flat steps.
static uint32_t stubRand() { return 0; }

// -----------------------------------------------------------------------------
// Fixtures
// -----------------------------------------------------------------------------

// Mirrors DM:NOD: audio + dome at t=0, panel close at 150, release at 300.
static const SeqStep kFlatSteps[] = {
    SEQ_AUDIO(0, "$H"),
    SEQ_DOME(0, FX_NONE, "@1MYes"),
    SEQ_DOME(0, FX_PANEL, ":OP01"),
    SEQ_DOME(150, FX_NONE, ":CL01"),
    SEQ_DOME(300, FX_NONE, ":CL00"),
    SEQ_TERM(300),
};
static const SequenceEntry kFlatEntry = {
    "TEST:FLAT", kFlatSteps,
    (uint8_t)(sizeof(kFlatSteps) / sizeof(kFlatSteps[0])),
    3000, TOGGLE_NONE, nullptr, 0, nullptr,
};

// Mirrors DM:VADER's shape: persistent logic/PSI effects, long tail.
static const SeqStep kFxSteps[] = {
    SEQ_AUDIO(0, "$M"),
    SEQ_DOME(0, FX_LOGIC_PSI, "@0T11"),
    SEQ_DOME(0, FX_LOGIC_PSI, "@0P11"),
    SEQ_DOME(47000, FX_NONE, "@0T1"),
    SEQ_DOME(47000, FX_NONE, "@0P1"),
    SEQ_TERM(47000),
};
static const SequenceEntry kFxEntry = {
    "TEST:FX", kFxSteps,
    (uint8_t)(sizeof(kFxSteps) / sizeof(kFxSteps[0])),
    47000, TOGGLE_NONE, nullptr, 0, nullptr,
};

// Drain every action due at `now`, appending payloads to `log` (pipe-separated).
static int drainAt(SeqEngineState& st, uint32_t now, char* log, size_t logSize) {
    SeqAction act;
    int n = 0;
    while (seqEnginePeek(st, now, stubRand, act)) {
        if (log != nullptr) {
            if (log[0] != '\0') {
                strncat(log, "|", logSize - strlen(log) - 1);
            }
            const char* tag = act.payload;
            if (act.kind == SEQ_ACT_AUDIO_STOP) {
                tag = "<stop>";
            } else if (act.kind == SEQ_ACT_DOME_ROTATE) {
                char rot[32];
                snprintf(rot, sizeof(rot), "<rot:%d:%u>",
                         (int)act.domeSpeedPct, (unsigned)act.domeDurationMs);
                strncat(log, rot, logSize - strlen(log) - 1);
                seqEngineCommit(st);
                ++n;
                continue;
            }
            strncat(log, tag, logSize - strlen(log) - 1);
        }
        seqEngineCommit(st);
        ++n;
    }
    return n;
}

// -----------------------------------------------------------------------------
// Lifecycle
// -----------------------------------------------------------------------------

void test_engine_idle_after_init() {
    SeqEngineState st;
    seqEngineInit(st);
    TEST_ASSERT_FALSE(seqEngineActive(st));
    TEST_ASSERT_NULL(seqEngineName(st));

    SeqAction act;
    TEST_ASSERT_FALSE(seqEnginePeek(st, 0, stubRand, act));
}

void test_engine_active_and_named_after_start() {
    SeqEngineState st;
    seqEngineInit(st);
    seqEngineStart(st, &kFlatEntry, 1000);
    TEST_ASSERT_TRUE(seqEngineActive(st));
    TEST_ASSERT_EQUAL_STRING("TEST:FLAT", seqEngineName(st));
}

// -----------------------------------------------------------------------------
// Flat scheduling
// -----------------------------------------------------------------------------

void test_flat_steps_fire_in_order_at_their_times() {
    SeqEngineState st;
    seqEngineInit(st);
    seqEngineStart(st, &kFlatEntry, 1000);

    char log[256] = "";
    TEST_ASSERT_EQUAL_INT(3, drainAt(st, 1000, log, sizeof(log)));
    TEST_ASSERT_EQUAL_STRING("$H|@1MYes|:OP01", log);

    // Nothing due between scheduled times.
    TEST_ASSERT_EQUAL_INT(0, drainAt(st, 1100, nullptr, 0));

    log[0] = '\0';
    TEST_ASSERT_EQUAL_INT(1, drainAt(st, 1150, log, sizeof(log)));
    TEST_ASSERT_EQUAL_STRING(":CL01", log);

    // t=300: authored :CL00 step fires, then STEP_END runs terminal cleanup.
    // The ring is already closed (:CL01 then the :CL00 step cleared the net-open
    // mask), so terminal cleanup emits nothing — never a group close.
    log[0] = '\0';
    TEST_ASSERT_EQUAL_INT(1, drainAt(st, 1300, log, sizeof(log)));
    TEST_ASSERT_EQUAL_STRING(":CL00", log);
    TEST_ASSERT_FALSE(seqEngineActive(st));
}

void test_late_tick_catches_up_all_overdue_steps() {
    SeqEngineState st;
    seqEngineInit(st);
    seqEngineStart(st, &kFlatEntry, 1000);

    // One late tick past the END time drains the whole sequence. The ring ends
    // closed (steps :CL01 + :CL00), so terminal cleanup adds no panel close.
    char log[256] = "";
    TEST_ASSERT_EQUAL_INT(5, drainAt(st, 9000, log, sizeof(log)));
    TEST_ASSERT_EQUAL_STRING("$H|@1MYes|:OP01|:CL01|:CL00", log);
    TEST_ASSERT_FALSE(seqEngineActive(st));
}

void test_end_not_due_keeps_sequence_active() {
    SeqEngineState st;
    seqEngineInit(st);
    seqEngineStart(st, &kFxEntry, 1000);

    drainAt(st, 1000, nullptr, 0);
    TEST_ASSERT_TRUE(seqEngineActive(st));
    TEST_ASSERT_EQUAL_INT(0, drainAt(st, 20000, nullptr, 0));
    TEST_ASSERT_TRUE(seqEngineActive(st));
}

// -----------------------------------------------------------------------------
// Peek/commit retry contract
// -----------------------------------------------------------------------------

void test_peek_without_commit_returns_same_action() {
    SeqEngineState st;
    seqEngineInit(st);
    seqEngineStart(st, &kFlatEntry, 1000);

    SeqAction a = {};
    SeqAction b = {};
    TEST_ASSERT_TRUE(seqEnginePeek(st, 1000, stubRand, a));
    TEST_ASSERT_TRUE(seqEnginePeek(st, 1010, stubRand, b));
    TEST_ASSERT_EQUAL_INT((int)a.kind, (int)b.kind);
    TEST_ASSERT_EQUAL_STRING(a.payload, b.payload);

    seqEngineCommit(st);
    TEST_ASSERT_TRUE(seqEnginePeek(st, 1010, stubRand, b));
    TEST_ASSERT_EQUAL_STRING("@1MYes", b.payload);
}

void test_retry_does_not_drift_later_steps() {
    SeqEngineState st;
    seqEngineInit(st);
    seqEngineStart(st, &kFlatEntry, 1000);

    // Stall the very first action (queue full) until t=1150, then drain:
    // the t=150 step must still fire at 1150, not 1150+150.
    SeqAction act;
    TEST_ASSERT_TRUE(seqEnginePeek(st, 1000, stubRand, act));  // not committed

    char log[256] = "";
    TEST_ASSERT_EQUAL_INT(4, drainAt(st, 1150, log, sizeof(log)));
    TEST_ASSERT_EQUAL_STRING("$H|@1MYes|:OP01|:CL01", log);
}

// -----------------------------------------------------------------------------
// Terminal auto-reset (ADR 0004 decision 7)
// -----------------------------------------------------------------------------

void test_abort_emits_logic_psi_reset() {
    SeqEngineState st;
    seqEngineInit(st);
    seqEngineStart(st, &kFxEntry, 1000);
    drainAt(st, 1000, nullptr, 0);  // fire FX_LOGIC_PSI steps

    seqEngineAbort(st);
    char log[256] = "";
    TEST_ASSERT_EQUAL_INT(2, drainAt(st, 1500, log, sizeof(log)));
    TEST_ASSERT_EQUAL_STRING("@0T1|@0P1", log);
    TEST_ASSERT_FALSE(seqEngineActive(st));
}

void test_abort_before_any_fx_step_emits_nothing() {
    SeqEngineState st;
    seqEngineInit(st);
    seqEngineStart(st, &kFxEntry, 1000);
    // No steps fired yet — nothing was activated, nothing to reset.
    seqEngineAbort(st);
    TEST_ASSERT_EQUAL_INT(0, drainAt(st, 1000, nullptr, 0));
    TEST_ASSERT_FALSE(seqEngineActive(st));
}

void test_abort_when_idle_is_noop() {
    SeqEngineState st;
    seqEngineInit(st);
    seqEngineAbort(st);
    TEST_ASSERT_FALSE(seqEngineActive(st));
}

void test_restart_after_abort_runs_fresh() {
    SeqEngineState st;
    seqEngineInit(st);
    seqEngineStart(st, &kFxEntry, 1000);
    drainAt(st, 1000, nullptr, 0);
    seqEngineAbort(st);
    drainAt(st, 1000, nullptr, 0);

    seqEngineStart(st, &kFlatEntry, 5000);
    char log[256] = "";
    TEST_ASSERT_EQUAL_INT(3, drainAt(st, 5000, log, sizeof(log)));
    TEST_ASSERT_EQUAL_STRING("$H|@1MYes|:OP01", log);
}

// -----------------------------------------------------------------------------
// Catalog access
// -----------------------------------------------------------------------------

void test_catalog_find_returns_entry_for_vader() {
    const SequenceEntry* e = sequenceCatalogFind("DM:VADER");
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_STRING("DM:VADER", e->name);
    TEST_ASSERT_EQUAL_UINT32(47000, e->suppressMs);
    TEST_ASSERT_EQUAL_INT(TOGGLE_NONE, (int)e->toggleGroup);
}

void test_catalog_find_returns_null_for_unknown() {
    TEST_ASSERT_NULL(sequenceCatalogFind("DM:NOPE"));
    TEST_ASSERT_NULL(sequenceCatalogFind(""));
    TEST_ASSERT_NULL(sequenceCatalogFind(nullptr));
}

// Real catalog entry through the engine: DM:VADER end-to-end at its true times.
void test_real_vader_entry_runs_through_engine() {
    const SequenceEntry* e = sequenceCatalogFind("DM:VADER");
    TEST_ASSERT_NOT_NULL(e);

    SeqEngineState st;
    seqEngineInit(st);
    seqEngineStart(st, e, 0);

    char log[256] = "";
    // VADER's visual identity is now the dome-native DV:VADER preset (task #5),
    // replacing the raw @HPA0021|47/@0T11/@0P11 lines.
    TEST_ASSERT_EQUAL_INT(2, drainAt(st, 0, log, sizeof(log)));
    TEST_ASSERT_EQUAL_STRING("$M|DV:VADER", log);

    log[0] = '\0';
    // Normal END at 47000: logic/PSI + holo auto-reset, then DV:RESET_VISUALS
    // closes the DV lifecycle so dome visual_preset telemetry settles to the
    // RESET_VISUALS idle marker (issue #9 §2). VADER uses FX_AUDIO_BOUNDED (ADR 0010),
    // so the audio stop is emitted on normal termination (hard cut, not ring-out).
    TEST_ASSERT_EQUAL_INT(5, drainAt(st, 47000, log, sizeof(log)));
    TEST_ASSERT_EQUAL_STRING("@0T1|@0P1|*ST00|DV:RESET_VISUALS|<stop>", log);
    TEST_ASSERT_FALSE(seqEngineActive(st));
}

// Abnormal termination of DM:VADER additionally stops the long audio track.
void test_real_vader_abort_stops_audio_and_resets_holos() {
    const SequenceEntry* e = sequenceCatalogFind("DM:VADER");
    TEST_ASSERT_NOT_NULL(e);

    SeqEngineState st;
    seqEngineInit(st);
    seqEngineStart(st, e, 0);
    drainAt(st, 0, nullptr, 0);

    seqEngineAbort(st);
    char log[256] = "";
    // DV:RESET_VISUALS closes the DV lifecycle after the raw holo/logic resets
    // and before the abnormal audio stop (issue #9 §2).
    TEST_ASSERT_EQUAL_INT(5, drainAt(st, 5000, log, sizeof(log)));
    TEST_ASSERT_EQUAL_STRING("@0T1|@0P1|*ST00|DV:RESET_VISUALS|<stop>", log);
    TEST_ASSERT_FALSE(seqEngineActive(st));
}

// FX_AUDIO_BOUNDED stops the audio on normal termination (ADR 0010).
void test_bounded_audio_emits_stop_on_normal_termination() {
    static const SeqStep steps[] = {
        SEQ_AUDIO_FX(0, FX_AUDIO_BOUNDED, "$X"),  // synthetic bounded audio track
        SEQ_DOME(0, FX_NONE, "@0T11"),             // just set logic flag, not FX_LOGIC_PSI
        SEQ_TERM(5000),
    };
    static const SequenceEntry entry = {
        "TEST:BOUNDED", steps, (uint8_t)(sizeof(steps) / sizeof(steps[0])),
        5000, TOGGLE_NONE, nullptr, 0, nullptr,
    };

    SeqEngineState st;
    seqEngineInit(st);
    seqEngineStart(st, &entry, 1000);

    char log[256] = "";
    TEST_ASSERT_EQUAL_INT(2, drainAt(st, 1000, log, sizeof(log)));
    TEST_ASSERT_EQUAL_STRING("$X|@0T11", log);

    // Normal end at 6000: audio stop is emitted due to FX_AUDIO_BOUNDED, no other FX.
    log[0] = '\0';
    TEST_ASSERT_EQUAL_INT(1, drainAt(st, 6000, log, sizeof(log)));
    TEST_ASSERT_EQUAL_STRING("<stop>", log);
    TEST_ASSERT_FALSE(seqEngineActive(st));
}

// Plain FX_AUDIO does NOT stop on normal termination (ring-out preserved).
void test_plain_audio_no_stop_on_normal_termination() {
    static const SeqStep steps[] = {
        SEQ_AUDIO_FX(0, FX_AUDIO, "$X"),  // plain long audio track
        SEQ_DOME(0, FX_NONE, "@0T11"),     // just set logic flag, not FX_LOGIC_PSI
        SEQ_TERM(5000),
    };
    static const SequenceEntry entry = {
        "TEST:PLAIN", steps, (uint8_t)(sizeof(steps) / sizeof(steps[0])),
        5000, TOGGLE_NONE, nullptr, 0, nullptr,
    };

    SeqEngineState st;
    seqEngineInit(st);
    seqEngineStart(st, &entry, 1000);

    char log[256] = "";
    TEST_ASSERT_EQUAL_INT(2, drainAt(st, 1000, log, sizeof(log)));
    TEST_ASSERT_EQUAL_STRING("$X|@0T11", log);

    // Normal end at 6000: no audio stop emitted for plain FX_AUDIO (ring-out).
    log[0] = '\0';
    TEST_ASSERT_EQUAL_INT(0, drainAt(st, 6000, log, sizeof(log)));
    TEST_ASSERT_FALSE(seqEngineActive(st));
}

// STEP_AUDIO_STOP (ADR 0010) emits a Track Stop at its own authored mid-sequence
// time, independent of terminal cleanup — lets an author bound a long track
// earlier than SEQ_TERM (e.g. ROCKMARCH's 47000 ms cutoff before its 48250 ms
// TERM) without the mood-disabling dollar "$s" stop.
void test_audio_stop_step_emits_stop_at_authored_time() {
    static const SeqStep steps[] = {
        SEQ_AUDIO_FX(0, FX_AUDIO_BOUNDED, "$X"),  // synthetic bounded audio track
        SEQ_AUDIO_STOP(2000),                      // early Track Stop, well before TERM
        SEQ_DOME(2500, FX_NONE, "@0T11"),
        SEQ_TERM(5000),
    };
    static const SequenceEntry entry = {
        "TEST:AUDIOSTOP", steps, (uint8_t)(sizeof(steps) / sizeof(steps[0])),
        5000, TOGGLE_NONE, nullptr, 0, nullptr,
    };

    SeqEngineState st;
    seqEngineInit(st);
    seqEngineStart(st, &entry, 1000);

    char log[256] = "";
    TEST_ASSERT_EQUAL_INT(1, drainAt(st, 1000, log, sizeof(log)));
    TEST_ASSERT_EQUAL_STRING("$X", log);

    // Not yet due just before the authored stop time.
    log[0] = '\0';
    TEST_ASSERT_EQUAL_INT(0, drainAt(st, 2999, log, sizeof(log)));

    // The authored Track Stop fires at 3000 (1000 start + 2000 tMs).
    log[0] = '\0';
    TEST_ASSERT_EQUAL_INT(1, drainAt(st, 3000, log, sizeof(log)));
    TEST_ASSERT_EQUAL_STRING("<stop>", log);

    // The rest of the choreography still runs normally afterward.
    log[0] = '\0';
    TEST_ASSERT_EQUAL_INT(1, drainAt(st, 3500, log, sizeof(log)));
    TEST_ASSERT_EQUAL_STRING("@0T11", log);

    // Normal termination still fires its own (redundant, harmless) Track Stop
    // via FX_AUDIO_BOUNDED.
    log[0] = '\0';
    TEST_ASSERT_EQUAL_INT(1, drainAt(st, 6000, log, sizeof(log)));
    TEST_ASSERT_EQUAL_STRING("<stop>", log);
    TEST_ASSERT_FALSE(seqEngineActive(st));
}

// STEP_AUDIO_CATEGORY resolves into a category action with the fallback slot.
void test_audio_category_step_emits_category_action() {
    const SequenceEntry* e = sequenceCatalogFind("DM:ALARM");
    TEST_ASSERT_NOT_NULL(e);

    SeqEngineState st;
    seqEngineInit(st);
    seqEngineStart(st, e, 0);

    SeqAction act = {};
    TEST_ASSERT_TRUE(seqEnginePeek(st, 0, stubRand, act));
    TEST_ASSERT_EQUAL_INT(SEQ_ACT_AUDIO_CATEGORY, (int)act.kind);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)AUDIO_CATEGORY_ALERT, act.audioCategory);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)AUDIO_SLOT_NAMED_SCREAM, act.audioFallbackSlot);
}

void test_dome_rotate_step_emits_typed_rotation_action() {
    static const SeqStep steps[] = {
        SEQ_DOME_ROTATE(1200, -35, 900),
        SEQ_TERM(2200),
    };
    static const SequenceEntry entry = {
        "TEST:ROTATE", steps, (uint8_t)(sizeof(steps) / sizeof(steps[0])),
        3000, TOGGLE_NONE, nullptr, 0, nullptr,
    };

    SeqEngineState st;
    seqEngineInit(st);
    seqEngineStart(st, &entry, 1000);

    SeqAction act = {};
    TEST_ASSERT_FALSE(seqEnginePeek(st, 2199, stubRand, act));
    TEST_ASSERT_TRUE(seqEnginePeek(st, 2200, stubRand, act));
    TEST_ASSERT_EQUAL_INT(SEQ_ACT_DOME_ROTATE, (int)act.kind);
    TEST_ASSERT_EQUAL_INT8(-35, act.domeSpeedPct);
    TEST_ASSERT_EQUAL_UINT32(900, act.domeDurationMs);
}

void test_abort_after_dome_rotate_emits_neutral_rotation_stop() {
    static const SeqStep steps[] = {
        SEQ_DOME_ROTATE(0, 35, 900),
        SEQ_TERM(2000),
    };
    static const SequenceEntry entry = {
        "TEST:ROT_ABORT", steps, (uint8_t)(sizeof(steps) / sizeof(steps[0])),
        3000, TOGGLE_NONE, nullptr, 0,
    };

    SeqEngineState st;
    seqEngineInit(st);
    seqEngineStart(st, &entry, 0);

    char log[128] = "";
    TEST_ASSERT_EQUAL_INT(1, drainAt(st, 0, log, sizeof(log)));
    TEST_ASSERT_EQUAL_STRING("<rot:35:900>", log);

    seqEngineAbort(st);
    log[0] = '\0';
    TEST_ASSERT_EQUAL_INT(1, drainAt(st, 100, log, sizeof(log)));
    TEST_ASSERT_EQUAL_STRING("<rot:0:0>", log);
    TEST_ASSERT_FALSE(seqEngineActive(st));
}

void test_terminal_cleanup_after_dome_rotate_emits_neutral_rotation_stop() {
    static const SeqStep steps[] = {
        SEQ_DOME_ROTATE(0, -20, 5000),
        SEQ_TERM(100),
    };
    static const SequenceEntry entry = {
        "TEST:ROT_TERM", steps, (uint8_t)(sizeof(steps) / sizeof(steps[0])),
        3000, TOGGLE_NONE, nullptr, 0,
    };

    SeqEngineState st;
    seqEngineInit(st);
    seqEngineStart(st, &entry, 0);

    char log[128] = "";
    TEST_ASSERT_EQUAL_INT(1, drainAt(st, 0, log, sizeof(log)));
    TEST_ASSERT_EQUAL_STRING("<rot:-20:5000>", log);

    log[0] = '\0';
    TEST_ASSERT_EQUAL_INT(1, drainAt(st, 100, log, sizeof(log)));
    TEST_ASSERT_EQUAL_STRING("<rot:0:0>", log);
    TEST_ASSERT_FALSE(seqEngineActive(st));
}

void test_zero_speed_dome_rotate_does_not_emit_cleanup_stop() {
    static const SeqStep steps[] = {
        SEQ_DOME_ROTATE(0, 0, 100),
        SEQ_TERM(100),
    };
    static const SequenceEntry entry = {
        "TEST:ROT_ZERO", steps, (uint8_t)(sizeof(steps) / sizeof(steps[0])),
        3000, TOGGLE_NONE, nullptr, 0,
    };

    SeqEngineState st;
    seqEngineInit(st);
    seqEngineStart(st, &entry, 0);

    char log[128] = "";
    TEST_ASSERT_EQUAL_INT(1, drainAt(st, 0, log, sizeof(log)));
    TEST_ASSERT_EQUAL_STRING("<rot:0:100>", log);

    log[0] = '\0';
    TEST_ASSERT_EQUAL_INT(0, drainAt(st, 100, log, sizeof(log)));
    TEST_ASSERT_FALSE(seqEngineActive(st));
}

// DM:RESET closes the ring with staggered individual closes (never a group
// close, never a pie close) and clears the toggle latches via an explicit
// STEP_CLEAR_LATCHES — not via a :CL00 side effect (2026-06-18 brownout fix).
void test_real_reset_entry_clears_latches_and_resets() {
    const SequenceEntry* e = sequenceCatalogFind("DM:RESET");
    TEST_ASSERT_NOT_NULL(e);

    SeqEngineState st;
    seqEngineInit(st);
    st.latches.piesOpen = true;
    st.latches.ringOpen = true;
    seqEngineStart(st, e, 0);

    // Drain past the authored end (~3900 ms). The inline STEP_CLEAR_LATCHES emits
    // no action, so it is absent from the log but still clears the latches.
    char log[256] = "";
    TEST_ASSERT_EQUAL_INT(11, drainAt(st, 5000, log, sizeof(log)));
    // The sound ends with a Track Stop, never `$s`, which would also turn idle
    // chatter off until reboot (ADR 0010, #354).
    TEST_ASSERT_EQUAL_STRING(
        "<stop>|:CL01|:CL02|:CL03|:CL04|:CL07|:CL11|:CL13|*ST00|@0T1|@0P1", log);
    TEST_ASSERT_NULL(strstr(log, "$s"));

    // Safety invariant: no group close and no pie command anywhere in the stream.
    TEST_ASSERT_NULL(strstr(log, ":CL00"));
    TEST_ASSERT_NULL(strstr(log, ":CL14"));
    TEST_ASSERT_NULL(strstr(log, ":CL15"));
    TEST_ASSERT_NULL(strstr(log, ":CLP"));   // no individual pie close (:CLPn)
    TEST_ASSERT_NULL(strstr(log, ":OP"));    // reset never opens anything

    // Latches cleared by the explicit step, and a clean terminal (no group close).
    TEST_ASSERT_FALSE(st.latches.piesOpen);
    TEST_ASSERT_FALSE(st.latches.ringOpen);
    TEST_ASSERT_FALSE(seqEngineActive(st));
}

// DM:HELLO opens P1 once. It used to send five identical :OP01 160 ms apart,
// which the dome performs as one open, so the routine claimed a wave it never
// made (#287, #354).
void test_real_hello_entry_opens_p1_once() {
    const SequenceEntry* e = sequenceCatalogFind("DM:HELLO");
    TEST_ASSERT_NOT_NULL(e);

    SeqEngineState st;
    seqEngineInit(st);
    seqEngineStart(st, e, 0);

    char log[256] = "";
    drainAt(st, 5000, log, sizeof(log));

    const char* first = strstr(log, ":OP01");
    TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_NULL(strstr(first + 1, ":OP01"));
    TEST_ASSERT_NOT_NULL(strstr(first, ":CL01"));
    TEST_ASSERT_FALSE(seqEngineActive(st));
}

// -----------------------------------------------------------------------------
// STEP_LOOP scheduling
// -----------------------------------------------------------------------------

// Loop at t=100: body = A (rel 0) + B (rel 50), period 200, duration 600
// -> iterations start at rel 0, 200, 400; post-loop step at 800, END 900.
static const SeqStep kLoopSteps[] = {
    SEQ_DOME(0, FX_NONE, "@PRE"),
    SEQ_LOOP(100, 2, 200, 600),
    SEQ_DOME(0, FX_NONE, "@A"),
    SEQ_DOME(50, FX_NONE, "@B"),
    SEQ_DOME(800, FX_NONE, "@POST"),
    SEQ_TERM(900),
};
static const SequenceEntry kLoopEntry = {
    "TEST:LOOP", kLoopSteps,
    (uint8_t)(sizeof(kLoopSteps) / sizeof(kLoopSteps[0])),
    2000, TOGGLE_NONE, nullptr, 0, nullptr,
};

// =============================================================================
// Body Steps (ADR 0049)
// =============================================================================

void test_body_step_emits_part_shape_and_how_far() {
    static const SeqStep steps[] = {
        SEQ_BODY(400, "doorFL", BODY_SHAPE_OPEN, 60, 0),
        SEQ_TERM(1000),
    };
    static const SequenceEntry entry = {
        "TEST:BODY", steps, (uint8_t)(sizeof(steps) / sizeof(steps[0])),
        3000, TOGGLE_NONE, nullptr, 0, nullptr,
    };

    SeqEngineState st;
    seqEngineInit(st);
    seqEngineStart(st, &entry, 1000);

    SeqAction act = {};
    TEST_ASSERT_FALSE(seqEnginePeek(st, 1399, stubRand, act));
    TEST_ASSERT_TRUE(seqEnginePeek(st, 1400, stubRand, act));
    TEST_ASSERT_EQUAL_INT(SEQ_ACT_BODY_MOVE, (int)act.kind);
    TEST_ASSERT_EQUAL_STRING("doorFL", act.payload);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)BODY_SHAPE_OPEN, act.bodyShape);
    TEST_ASSERT_EQUAL_UINT8(60, act.bodyHowFar);
    TEST_ASSERT_EQUAL_UINT16(0, act.bodyFlutterMs);
}

// A step that says nothing about its shape or how far it goes is an open over
// the whole throw, and the engine spends both defaults so no consumer re-decides
// them.
void test_body_step_defaults_resolve_to_open_over_the_whole_throw() {
    static const SeqStep steps[] = {
        SEQ_BODY(0, "dataport", 0, SEQ_BODY_HOWFAR_UNSET, 0),
        SEQ_TERM(1000),
    };
    static const SequenceEntry entry = {
        "TEST:BODY_DEF", steps, (uint8_t)(sizeof(steps) / sizeof(steps[0])),
        3000, TOGGLE_NONE, nullptr, 0, nullptr,
    };

    SeqEngineState st;
    seqEngineInit(st);
    seqEngineStart(st, &entry, 0);

    SeqAction act = {};
    TEST_ASSERT_TRUE(seqEnginePeek(st, 0, stubRand, act));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)BODY_SHAPE_OPEN, act.bodyShape);
    TEST_ASSERT_EQUAL_UINT8(SEQ_BODY_HOWFAR_DEFAULT, act.bodyHowFar);
}

// A Factory-authored body step carries no effect class, and that is the decision
// rather than an omission (ADR 0049): the engine undoes nothing a body step did,
// so there is nothing for activeFx to carry into terminal cleanup.
void test_body_step_carries_no_effect_class() {
    static const SeqStep steps[] = {
        SEQ_BODY(0, "doorFL", BODY_SHAPE_OPEN, 100, 0),
        SEQ_TERM(500),
    };
    TEST_ASSERT_EQUAL_UINT8(FX_NONE, steps[0].effectClass);
}

// The accessor is total: a stored shape this build does not model resolves to
// the default rather than reaching the drive path as a number nobody meant.
void test_body_step_unmodelled_shape_resolves_to_the_default() {
    SeqStepParams p = {};
    p.shape = (uint8_t)BODY_SHAPE_COUNT;  // one past the vocabulary
    TEST_ASSERT_EQUAL_UINT8((uint8_t)SEQ_BODY_SHAPE_DEFAULT, (uint8_t)seqBodyShape(p));
    p.shape = 200;
    TEST_ASSERT_EQUAL_UINT8((uint8_t)SEQ_BODY_SHAPE_DEFAULT, (uint8_t)seqBodyShape(p));
    // And the default is open: a step that says nothing about its shape opens.
    TEST_ASSERT_EQUAL_UINT8((uint8_t)BODY_SHAPE_OPEN, (uint8_t)SEQ_BODY_SHAPE_DEFAULT);
}

// The floor is what stops a how-far meaning "does not move"; it floors rather
// than refusing, because refusing would be a judgement about intent.
void test_body_step_how_far_is_floored_not_refused() {
    static const SeqStep steps[] = {
        SEQ_BODY(0, "utilUp", BODY_SHAPE_CLOSE, 1, 0),
        SEQ_TERM(1000),
    };
    static const SequenceEntry entry = {
        "TEST:BODY_FLOOR", steps, (uint8_t)(sizeof(steps) / sizeof(steps[0])),
        3000, TOGGLE_NONE, nullptr, 0, nullptr,
    };

    SeqEngineState st;
    seqEngineInit(st);
    seqEngineStart(st, &entry, 0);

    SeqAction act = {};
    TEST_ASSERT_TRUE(seqEnginePeek(st, 0, stubRand, act));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)BODY_SHAPE_CLOSE, act.bodyShape);
    TEST_ASSERT_EQUAL_UINT8(SEQ_BODY_HOWFAR_FLOOR, act.bodyHowFar);
}

void test_body_step_flutter_carries_its_duration() {
    static const SeqStep steps[] = {
        SEQ_BODY(0, "doorRR", BODY_SHAPE_FLUTTER, 80, 1200),
        SEQ_BODY(1200, "doorRR", BODY_SHAPE_CLOSE, 0, 0),
        SEQ_TERM(2000),
    };
    static const SequenceEntry entry = {
        "TEST:BODY_FLUT", steps, (uint8_t)(sizeof(steps) / sizeof(steps[0])),
        3000, TOGGLE_NONE, nullptr, 0, nullptr,
    };

    SeqEngineState st;
    seqEngineInit(st);
    seqEngineStart(st, &entry, 0);

    SeqAction act = {};
    TEST_ASSERT_TRUE(seqEnginePeek(st, 0, stubRand, act));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)BODY_SHAPE_FLUTTER, act.bodyShape);
    TEST_ASSERT_EQUAL_UINT16(1200, act.bodyFlutterMs);
}

// The engine undoes nothing a body step did (ADR 0049): a Part left open stays
// open, so normal termination schedules no release and the run goes idle with
// nothing drained.
void test_body_step_schedules_no_release_on_normal_termination() {
    static const SeqStep steps[] = {
        SEQ_BODY(0, "doorFL", BODY_SHAPE_OPEN, 0, 0),
        SEQ_TERM(500),
    };
    static const SequenceEntry entry = {
        "TEST:BODY_TERM", steps, (uint8_t)(sizeof(steps) / sizeof(steps[0])),
        3000, TOGGLE_NONE, nullptr, 0, nullptr,
    };

    SeqEngineState st;
    seqEngineInit(st);
    seqEngineStart(st, &entry, 0);

    char log[128] = "";
    TEST_ASSERT_EQUAL_INT(1, drainAt(st, 0, log, sizeof(log)));
    TEST_ASSERT_EQUAL_STRING("doorFL", log);

    // Terminal transition: nothing at all is queued for the open door.
    log[0] = '\0';
    TEST_ASSERT_EQUAL_INT(0, drainAt(st, 500, log, sizeof(log)));
    TEST_ASSERT_EQUAL_STRING("", log);
    TEST_ASSERT_FALSE(seqEngineActive(st));
}

// Same on an abort: estop aborts the sequence and releases every Output
// (ADR 0043), so the engine has nothing of its own to undo here either.
void test_body_step_abort_emits_no_cleanup() {
    static const SeqStep steps[] = {
        SEQ_BODY(0, "gripArm", BODY_SHAPE_OPEN, 100, 0),
        SEQ_TERM(5000),
    };
    static const SequenceEntry entry = {
        "TEST:BODY_ABORT", steps, (uint8_t)(sizeof(steps) / sizeof(steps[0])),
        6000, TOGGLE_NONE, nullptr, 0, nullptr,
    };

    SeqEngineState st;
    seqEngineInit(st);
    seqEngineStart(st, &entry, 0);
    TEST_ASSERT_EQUAL_INT(1, drainAt(st, 0, nullptr, 0));

    seqEngineAbort(st);
    TEST_ASSERT_EQUAL_INT(0, drainAt(st, 100, nullptr, 0));
    TEST_ASSERT_FALSE(seqEngineActive(st));
}

void test_loop_iterations_fire_at_period_offsets() {
    SeqEngineState st;
    seqEngineInit(st);
    seqEngineStart(st, &kLoopEntry, 1000);

    char log[256] = "";
    drainAt(st, 1000, log, sizeof(log));
    TEST_ASSERT_EQUAL_STRING("@PRE", log);

    // Iteration 1 at abs 1100/1150.
    TEST_ASSERT_EQUAL_INT(0, drainAt(st, 1099, nullptr, 0));
    log[0] = '\0';
    TEST_ASSERT_EQUAL_INT(1, drainAt(st, 1100, log, sizeof(log)));
    TEST_ASSERT_EQUAL_STRING("@A", log);
    log[0] = '\0';
    TEST_ASSERT_EQUAL_INT(1, drainAt(st, 1150, log, sizeof(log)));
    TEST_ASSERT_EQUAL_STRING("@B", log);

    // Iteration 2 at abs 1300/1350; iteration 3 at abs 1500/1550.
    log[0] = '\0';
    TEST_ASSERT_EQUAL_INT(2, drainAt(st, 1350, log, sizeof(log)));
    TEST_ASSERT_EQUAL_STRING("@A|@B", log);
    log[0] = '\0';
    TEST_ASSERT_EQUAL_INT(2, drainAt(st, 1550, log, sizeof(log)));
    TEST_ASSERT_EQUAL_STRING("@A|@B", log);

    // No iteration 4 (rel 600 not < duration 600); post-loop at abs 1800.
    TEST_ASSERT_EQUAL_INT(0, drainAt(st, 1799, nullptr, 0));
    log[0] = '\0';
    TEST_ASSERT_EQUAL_INT(1, drainAt(st, 1800, log, sizeof(log)));
    TEST_ASSERT_EQUAL_STRING("@POST", log);

    TEST_ASSERT_EQUAL_INT(0, drainAt(st, 1900, nullptr, 0));
    TEST_ASSERT_FALSE(seqEngineActive(st));
}

void test_loop_late_tick_catches_up_all_iterations() {
    SeqEngineState st;
    seqEngineInit(st);
    seqEngineStart(st, &kLoopEntry, 0);

    char log[256] = "";
    TEST_ASSERT_EQUAL_INT(8, drainAt(st, 5000, log, sizeof(log)));
    TEST_ASSERT_EQUAL_STRING("@PRE|@A|@B|@A|@B|@A|@B|@POST", log);
    TEST_ASSERT_FALSE(seqEngineActive(st));
}

void test_loop_abort_mid_iteration_finishes_clean() {
    SeqEngineState st;
    seqEngineInit(st);
    seqEngineStart(st, &kLoopEntry, 0);
    drainAt(st, 150, nullptr, 0);  // @PRE + iteration 1

    seqEngineAbort(st);
    TEST_ASSERT_EQUAL_INT(0, drainAt(st, 200, nullptr, 0));  // no FX set
    TEST_ASSERT_FALSE(seqEngineActive(st));
}

void test_real_loop_entries_have_loop_headers() {
    const SequenceEntry* cantina = sequenceCatalogFind("DM:CANTINA");
    TEST_ASSERT_NOT_NULL(cantina);
    // Pre-loop steps: $C + DV:CANTINA (the visual preset replaced the three raw
    // @HPA0029|15/@0T2/@0P2 lines, task #5), so the loop header is at index 2.
    TEST_ASSERT_EQUAL_INT(STEP_LOOP, (int)cantina->steps[2].type);
    TEST_ASSERT_EQUAL_UINT8(26, cantina->steps[2].params.bodyCount);
    TEST_ASSERT_EQUAL_UINT16(1846, cantina->steps[2].params.periodMs);

    const SequenceEntry* rock = sequenceCatalogFind("DM:ROCKMARCH");
    TEST_ASSERT_NOT_NULL(rock);
    // Pre-loop steps: $M + DV:ROCKMARCH (the visual preset replaced the three raw
    // @0T11/@0P11/@HPA0021 lines, task #5), so the loop header is at index 2.
    TEST_ASSERT_EQUAL_INT(STEP_LOOP, (int)rock->steps[2].type);
    TEST_ASSERT_EQUAL_UINT8(14, rock->steps[2].params.bodyCount);
    TEST_ASSERT_EQUAL_UINT32(45000, rock->steps[2].params.durationMs);

    // Loop header + body + post-loop steps + END must fit the table exactly.
    // ROCKMARCH post-loop: a 7-panel physical-assurance close pass (individual
    // :CLnn, staggered) plus one STEP_AUDIO_STOP at 47000 ms (ADR 0010 Track
    // Stop, replacing the old mood-disabling "$s" step at the same timestamp)
    // before the terminal.
    TEST_ASSERT_EQUAL_UINT8(2 + 1 + 26 + 1, cantina->stepCount);
    TEST_ASSERT_EQUAL_UINT8(2 + 1 + 14 + 7 + 1 + 1, rock->stepCount);
}

// ROCKMARCH timing: first beat of iteration 2 lands at period offset 6461.
void test_real_rockmarch_second_pass_timing() {
    const SequenceEntry* e = sequenceCatalogFind("DM:ROCKMARCH");
    TEST_ASSERT_NOT_NULL(e);

    SeqEngineState st;
    seqEngineInit(st);
    seqEngineStart(st, e, 0);
    drainAt(st, 6311, nullptr, 0);  // setup + full first ring pass

    SeqAction act = {};
    TEST_ASSERT_FALSE(seqEnginePeek(st, 6460, stubRand, act));
    TEST_ASSERT_TRUE(seqEnginePeek(st, 6461, stubRand, act));
    TEST_ASSERT_EQUAL_STRING(":OP01", act.payload);
}

// -----------------------------------------------------------------------------
// Toggle sequences (ADR 0004 decision 8)
// -----------------------------------------------------------------------------

static const SeqStep kToggleOpenSteps[] = {
    SEQ_DOME(0, FX_PANEL, ":OPP1"),
    SEQ_DOME(100, FX_NONE, ":OPP2"),
    SEQ_TERM(500),
};
static const SeqStep kToggleCloseSteps[] = {
    SEQ_DOME(0, FX_NONE, ":CLP1"),
    SEQ_DOME(100, FX_NONE, ":CLP2"),
    SEQ_TERM(500),
};
static const SequenceEntry kToggleEntry = {
    "TEST:TOGGLE", kToggleOpenSteps,
    (uint8_t)(sizeof(kToggleOpenSteps) / sizeof(kToggleOpenSteps[0])),
    5000, TOGGLE_PIES, kToggleCloseSteps,
    (uint8_t)(sizeof(kToggleCloseSteps) / sizeof(kToggleCloseSteps[0])), nullptr,
};
static const SequenceEntry kToggleAllEntry = {
    "TEST:TOGGLEALL", kToggleOpenSteps,
    (uint8_t)(sizeof(kToggleOpenSteps) / sizeof(kToggleOpenSteps[0])),
    5000, TOGGLE_ALL, kToggleCloseSteps,
    (uint8_t)(sizeof(kToggleCloseSteps) / sizeof(kToggleCloseSteps[0])), nullptr,
};

void test_toggle_first_press_runs_open_branch_and_latches_open() {
    SeqEngineState st;
    seqEngineInit(st);
    seqEngineStart(st, &kToggleEntry, 0);

    char log[256] = "";
    TEST_ASSERT_EQUAL_INT(2, drainAt(st, 500, log, sizeof(log)));
    // Open branch ends with panels open: no :CL00 despite FX_PANEL.
    TEST_ASSERT_EQUAL_STRING(":OPP1|:OPP2", log);
    TEST_ASSERT_FALSE(seqEngineActive(st));
    TEST_ASSERT_TRUE(st.latches.piesOpen);
}

void test_toggle_second_press_runs_close_branch_and_releases() {
    SeqEngineState st;
    seqEngineInit(st);
    seqEngineStart(st, &kToggleEntry, 0);
    drainAt(st, 500, nullptr, 0);  // open; latch set

    seqEngineStart(st, &kToggleEntry, 1000);
    char log[256] = "";
    TEST_ASSERT_EQUAL_INT(2, drainAt(st, 1500, log, sizeof(log)));
    // Close branch runs its own per-pie closes. No terminal pie close: engine
    // cleanup never auto-closes pies (and never a group :CL14/:CL00). The pies
    // latch still drops via applyToggleLatch on normal close-branch completion.
    TEST_ASSERT_EQUAL_STRING(":CLP1|:CLP2", log);
    TEST_ASSERT_FALSE(st.latches.piesOpen);
}

void test_toggle_close_skips_release_while_another_group_open() {
    SeqEngineState st;
    seqEngineInit(st);
    st.latches.ringOpen = true;  // ring panels latched open by DM:LOW

    seqEngineStart(st, &kToggleEntry, 0);
    drainAt(st, 500, nullptr, 0);  // pies open

    seqEngineStart(st, &kToggleEntry, 1000);
    char log[256] = "";
    TEST_ASSERT_EQUAL_INT(2, drainAt(st, 1500, log, sizeof(log)));
    // No :CL00 — it would slam the ring panels DM:LOW left open.
    TEST_ASSERT_EQUAL_STRING(":CLP1|:CLP2", log);
    TEST_ASSERT_FALSE(st.latches.piesOpen);
    TEST_ASSERT_TRUE(st.latches.ringOpen);
}

void test_toggle_all_carries_pie_and_ring_latches() {
    SeqEngineState st;
    seqEngineInit(st);
    seqEngineStart(st, &kToggleAllEntry, 0);
    drainAt(st, 500, nullptr, 0);
    TEST_ASSERT_TRUE(st.latches.piesOpen);
    TEST_ASSERT_TRUE(st.latches.ringOpen);

    seqEngineStart(st, &kToggleAllEntry, 1000);
    char log[256] = "";
    TEST_ASSERT_EQUAL_INT(2, drainAt(st, 1500, log, sizeof(log)));
    // Close branch runs its own per-pie closes; no terminal pie close (engine
    // cleanup never auto-closes pies). The TOGGLE_ALL latch still carries both
    // groups closed via applyToggleLatch on normal completion.
    TEST_ASSERT_EQUAL_STRING(":CLP1|:CLP2", log);
    TEST_ASSERT_FALSE(st.latches.piesOpen);
    TEST_ASSERT_FALSE(st.latches.ringOpen);
}

void test_toggle_abort_mid_open_closes_and_clears_latches() {
    SeqEngineState st;
    seqEngineInit(st);
    seqEngineStart(st, &kToggleEntry, 0);
    drainAt(st, 100, nullptr, 0);  // both opens fired, END not reached

    seqEngineAbort(st);
    char log[256] = "";
    // Abnormal termination of a pie open: engine cleanup never auto-closes pies,
    // so NO panel close is emitted (the ring mask is empty — only pies were
    // touched). The pies latch was never set true (applyToggleLatch only runs on
    // normal completion, and the open branch had not finished).
    TEST_ASSERT_EQUAL_INT(0, drainAt(st, 200, log, sizeof(log)));
    TEST_ASSERT_EQUAL_STRING("", log);
    TEST_ASSERT_FALSE(st.latches.piesOpen);
    TEST_ASSERT_FALSE(seqEngineActive(st));
}

void test_real_toggle_entries_are_catalog_with_branches() {
    static const char* const names[] = { "DM:PIES", "DM:LOW", "DM:OPENALL" };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        const SequenceEntry* e = sequenceCatalogFind(names[i]);
        TEST_ASSERT_NOT_NULL_MESSAGE(e, names[i]);
        TEST_ASSERT_NOT_EQUAL_MESSAGE(TOGGLE_NONE, e->toggleGroup, names[i]);
        TEST_ASSERT_NOT_NULL_MESSAGE(e->closeSteps, names[i]);
        TEST_ASSERT_GREATER_THAN_MESSAGE(0, e->closeStepCount, names[i]);
    }
}

// No two dome commands in a branch leave at the same instant. DM:LOW sent three
// :OP at t=4400 and DM:OPENALL seven at t=900 -- the same-timestamp burst that
// overflowed the dome's eight-entry command queue on 2026-06-18 (#287, #354).
static void assertNoSharedDomeTimestamp(const SeqStep* steps, uint8_t count,
                                        const char* name) {
    for (uint8_t i = 0; i < count; ++i) {
        if (steps[i].type != STEP_DOME_CMD) continue;
        for (uint8_t j = (uint8_t)(i + 1); j < count; ++j) {
            if (steps[j].type != STEP_DOME_CMD) continue;
            TEST_ASSERT_NOT_EQUAL_MESSAGE(steps[i].tMs, steps[j].tMs, name);
        }
    }
}

void test_real_low_and_openall_send_no_same_timestamp_burst() {
    static const char* const names[] = { "DM:LOW", "DM:OPENALL" };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        const SequenceEntry* e = sequenceCatalogFind(names[i]);
        TEST_ASSERT_NOT_NULL_MESSAGE(e, names[i]);
        assertNoSharedDomeTimestamp(e->steps, e->stepCount, names[i]);
        assertNoSharedDomeTimestamp(e->closeSteps, e->closeStepCount, names[i]);
    }
}

// :SE30..:SE36 are Factory Sequences built from Body Steps (ADR 0049, #354).
// Every one used to run the same open-wait-close state machine in ServoTask;
// the seven names are the specification, so each must be a routine of its own.
void test_body_routines_are_factory_sequences_of_body_steps() {
    for (int id = 30; id <= 36; ++id) {
        const char* name = sequenceBodyRoutineName(id);
        TEST_ASSERT_NOT_NULL(name);
        const SequenceEntry* e = sequenceCatalogFind(name);
        TEST_ASSERT_NOT_NULL_MESSAGE(e, name);
        TEST_ASSERT_EQUAL_MESSAGE(TOGGLE_NONE, e->toggleGroup, name);
        TEST_ASSERT_NOT_NULL_MESSAGE(e->purpose, name);

        uint8_t bodySteps = 0;
        for (uint8_t i = 0; i < e->stepCount; ++i) {
            const SeqStep& s = e->steps[i];
            if (s.type == STEP_END) {
                TEST_ASSERT_EQUAL_UINT8_MESSAGE(e->stepCount - 1, i, name);
                continue;
            }
            // Nothing but Body Steps: no dome command, no sound, no :SE forward.
            TEST_ASSERT_EQUAL_MESSAGE(STEP_BODY, s.type, name);
            TEST_ASSERT_TRUE_MESSAGE(droidPartIdIsKnown(s.payload), s.payload);
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(FX_NONE, s.effectClass, name);
            ++bodySteps;
        }
        TEST_ASSERT_GREATER_THAN_MESSAGE(0, bodySteps, name);
    }
    TEST_ASSERT_NULL(sequenceBodyRoutineName(29));
    TEST_ASSERT_NULL(sequenceBodyRoutineName(37));
}

// The engine undoes nothing a body step did (ADR 0049), so a routine that means
// to put the droid back has to write the closes itself. Every Part one of the
// seven moves ends closed, as it does in the routines they were taken from.
void test_body_routines_leave_every_part_they_move_closed() {
    for (int id = 30; id <= 36; ++id) {
        const SequenceEntry* e = sequenceCatalogFind(sequenceBodyRoutineName(id));
        TEST_ASSERT_NOT_NULL(e);
        for (uint8_t i = 0; i < e->stepCount; ++i) {
            if (e->steps[i].type != STEP_BODY) continue;
            uint8_t last = i;
            for (uint8_t j = (uint8_t)(i + 1); j < e->stepCount; ++j) {
                if (e->steps[j].type == STEP_BODY &&
                    strcmp(e->steps[j].payload, e->steps[i].payload) == 0) {
                    last = j;
                }
            }
            TEST_ASSERT_EQUAL_MESSAGE(BODY_SHAPE_CLOSE, seqBodyShape(e->steps[last].params),
                                      e->steps[i].payload);
        }
    }
}

// No two of the seven are the same routine under different numbers.
void test_no_two_body_routines_are_the_same_routine() {
    for (int a = 30; a <= 36; ++a) {
        const SequenceEntry* ea = sequenceCatalogFind(sequenceBodyRoutineName(a));
        for (int b = a + 1; b <= 36; ++b) {
            const SequenceEntry* eb = sequenceCatalogFind(sequenceBodyRoutineName(b));
            bool same = ea->stepCount == eb->stepCount;
            for (uint8_t i = 0; same && i < ea->stepCount; ++i) {
                same = ea->steps[i].tMs == eb->steps[i].tMs &&
                       ea->steps[i].type == eb->steps[i].type &&
                       strcmp(ea->steps[i].payload, eb->steps[i].payload) == 0 &&
                       ea->steps[i].params.shape == eb->steps[i].params.shape;
            }
            TEST_ASSERT_FALSE_MESSAGE(same, ea->name);
        }
    }
}

// :SE32 runs through the engine as Body Step moves, in order, and ends: the
// doors spring open, wiggle, and shut with the last close.
void test_body_routine_wiggle_close_runs_as_body_moves() {
    const SequenceEntry* e = sequenceCatalogFind(sequenceBodyRoutineName(32));
    TEST_ASSERT_NOT_NULL(e);
    SeqEngineState st;
    seqEngineInit(st);
    seqEngineStart(st, e, 0);

    SeqAction act;
    uint8_t moves = 0;
    uint8_t lastShape = 0xFF;
    while (seqEnginePeek(st, 60000, stubRand, act)) {
        TEST_ASSERT_EQUAL(SEQ_ACT_BODY_MOVE, act.kind);
        lastShape = act.bodyShape;
        ++moves;
        seqEngineCommit(st);
    }
    TEST_ASSERT_EQUAL_UINT8((uint8_t)(e->stepCount - 1), moves);
    TEST_ASSERT_EQUAL_UINT8(BODY_SHAPE_CLOSE, lastShape);
    TEST_ASSERT_FALSE(seqEngineActive(st));
}

// -----------------------------------------------------------------------------
// STEP_RANDOM resolution
// -----------------------------------------------------------------------------

// Scripted RNG: returns successive values for deterministic pick testing.
static uint32_t s_rndSeq[16];
static uint8_t s_rndLen;
static uint8_t s_rndIdx;
static uint32_t scriptedRand() {
    uint32_t v = s_rndSeq[s_rndIdx % (s_rndLen ? s_rndLen : 1)];
    if (s_rndIdx < 255) s_rndIdx++;
    return v;
}
static void scriptRand(const uint32_t* vals, uint8_t n) {
    for (uint8_t i = 0; i < n && i < 16; ++i) s_rndSeq[i] = vals[i];
    s_rndLen = n;
    s_rndIdx = 0;
}

static const SeqStep kRandomSteps[] = {
    SEQ_RAND(0, SLOTSET_RING, RAND_FLUTTER, 0, 300, 0, 1),
    SEQ_RAND(100, SLOTSET_RING, RAND_OPEN, 0, 300, 0, 1),
    SEQ_RAND(200, SLOTSET_HOLD, RAND_CLOSE, 0, 100, 0, 0),
    SEQ_TERM(500),
};
static const SequenceEntry kRandomEntry = {
    "TEST:RANDOM", kRandomSteps,
    (uint8_t)(sizeof(kRandomSteps) / sizeof(kRandomSteps[0])),
    2000, TOGGLE_NONE, nullptr, 0, nullptr,
};

void test_random_step_resolves_target_and_mode_from_rng() {
    SeqEngineState st;
    seqEngineInit(st);
    // target pick: ring[2] = P3 -> :OF03 for flutter mode.
    const uint32_t script[] = { 2 };
    scriptRand(script, 1);

    seqEngineStart(st, &kRandomEntry, 0);
    SeqAction act = {};
    TEST_ASSERT_TRUE(seqEnginePeek(st, 0, scriptedRand, act));
    TEST_ASSERT_EQUAL_INT(SEQ_ACT_DOME_CMD, (int)act.kind);
    TEST_ASSERT_EQUAL_STRING(":OF03", act.payload);
}

void test_random_pick_distinct_rerolls_and_hold_reuses() {
    SeqEngineState st;
    seqEngineInit(st);
    // Step 1: pick ring[0]=P1.
    // Step 2: rolls ring[0] again -> distinct re-roll -> ring[3]=P4.
    // Step 3: SLOTSET_HOLD reuses P4 with close mode.
    const uint32_t script[] = { 0, 0, 3 };
    scriptRand(script, 3);

    seqEngineStart(st, &kRandomEntry, 0);
    SeqAction act = {};
    TEST_ASSERT_TRUE(seqEnginePeek(st, 0, scriptedRand, act));
    TEST_ASSERT_EQUAL_STRING(":OF01", act.payload);
    seqEngineCommit(st);

    TEST_ASSERT_TRUE(seqEnginePeek(st, 100, scriptedRand, act));
    TEST_ASSERT_EQUAL_STRING(":OP04", act.payload);
    seqEngineCommit(st);

    TEST_ASSERT_TRUE(seqEnginePeek(st, 200, scriptedRand, act));
    TEST_ASSERT_EQUAL_STRING(":CL04", act.payload);
}

void test_random_peek_retry_keeps_same_resolution() {
    SeqEngineState st;
    seqEngineInit(st);
    const uint32_t script[] = { 4, 100 };
    scriptRand(script, 2);

    seqEngineStart(st, &kRandomEntry, 0);
    SeqAction a = {};
    SeqAction b = {};
    TEST_ASSERT_TRUE(seqEnginePeek(st, 0, scriptedRand, a));
    TEST_ASSERT_TRUE(seqEnginePeek(st, 10, scriptedRand, b));
    // Uncommitted retry must not re-roll target or mode.
    TEST_ASSERT_EQUAL_STRING(a.payload, b.payload);
}

void test_random_jitter_delays_fire_time() {
    static const SeqStep kJitterSteps[] = {
        SEQ_RAND(100, SLOTSET_RING, RAND_FLUTTER, 0, 300, 500, 0),
        SEQ_TERM(1500),
    };
    static const SequenceEntry kJitterEntry = {
        "TEST:JITTER", kJitterSteps,
        (uint8_t)(sizeof(kJitterSteps) / sizeof(kJitterSteps[0])),
        2000, TOGGLE_NONE, nullptr, 0, nullptr,
    };

    SeqEngineState st;
    seqEngineInit(st);
    // target roll 0 -> P1; jitter roll 300 % 501 = 300 -> fires at 400.
    const uint32_t script[] = { 0, 300 };
    scriptRand(script, 2);

    seqEngineStart(st, &kJitterEntry, 0);
    SeqAction act = {};
    TEST_ASSERT_FALSE(seqEnginePeek(st, 399, scriptedRand, act));
    TEST_ASSERT_TRUE(seqEnginePeek(st, 400, scriptedRand, act));
    TEST_ASSERT_EQUAL_STRING(":OF01", act.payload);
}

void test_real_random_entries_are_catalog() {
    const SequenceEntry* scream = sequenceCatalogFind("DM:SCREAM");
    TEST_ASSERT_NOT_NULL(scream);
    // The four raw visual lines (@HPA0070/@HPA105|5/@0T5/@0P5) collapsed into one
    // DV:SCREAM preset (task #5), so the 13-panel burst + flutter loop shifted up by
    // three: flutter loop header now at index 15, 4-step body, 10 iterations of 380 ms.
    TEST_ASSERT_EQUAL_INT(STEP_LOOP, (int)scream->steps[15].type);
    TEST_ASSERT_EQUAL_UINT8(4, scream->steps[15].params.bodyCount);
    TEST_ASSERT_EQUAL_INT(STEP_RANDOM, (int)scream->steps[16].type);
    TEST_ASSERT_EQUAL_UINT8(SLOTSET_ALL, scream->steps[16].params.slotSet);
    TEST_ASSERT_EQUAL_UINT8(SLOTSET_HOLD, scream->steps[17].params.slotSet);

    const SequenceEntry* overload = sequenceCatalogFind("DM:OVERLOAD");
    TEST_ASSERT_NOT_NULL(overload);
    // The four raw visual lines (@1T4/@2T4/@HPA0070/@0P4) collapsed into one
    // DV:OVERLOAD preset (task #5), so the first flutter is now at index 2.
    TEST_ASSERT_EQUAL_INT(STEP_RANDOM, (int)overload->steps[2].type);
    TEST_ASSERT_EQUAL_UINT8(1, overload->steps[2].params.pickDistinct);
    TEST_ASSERT_EQUAL_UINT16(500, overload->steps[2].params.jitterMs);
}

// Full OVERLOAD run with a scripted RNG: six flutter commands, ring targets
// distinct, then the auto-reset for all effect classes.
void test_real_overload_runs_end_to_end() {
    const SequenceEntry* e = sequenceCatalogFind("DM:OVERLOAD");
    TEST_ASSERT_NOT_NULL(e);

    SeqEngineState st;
    seqEngineInit(st);
    const uint32_t script[] = { 1, 0, 0 };  // varied picks, zero jitter rolls
    scriptRand(script, 3);

    seqEngineStart(st, e, 0);
    char log[512] = "";
    int n = drainAt(st, 8000, log, sizeof(log));
    // 1 audio category (empty payload) + 1 DV:OVERLOAD visual preset (replaced the
    // four raw @1T4/@2T4/@HPA0070/@0P4 lines, task #5) + 6 flutters + 3 auto-reset
    // (@0T1, @0P1, *ST00) + 1 DV:RESET_VISUALS closing the DV lifecycle (issue #9
    // §2). NO panel close: the choreography is all flutters (:OF), which leave
    // panel state uncertain and do NOT mark panels open, so the net-open ring
    // mask is empty and no ring close is emitted — and never a group close.
    // Audio stop is NOT expected (normal end).
    TEST_ASSERT_EQUAL_INT(12, n);
    TEST_ASSERT_NOT_NULL(strstr(log, "DV:OVERLOAD"));
    TEST_ASSERT_NOT_NULL(strstr(log, "DV:RESET_VISUALS"));
    TEST_ASSERT_NULL(strstr(log, ":CL"));
    TEST_ASSERT_NULL(strstr(log, "<stop>"));
    TEST_ASSERT_FALSE(seqEngineActive(st));
}

// -----------------------------------------------------------------------------
// Load-shaped terminal panel cleanup (issue #2, 2026-06-17 brownout fix). A group
// close (:CL15/:CL14/:CL00) drives every servo in the group simultaneously, which
// browns out the dome from a loaded ring. So engine cleanup NEVER emits a group
// close: it closes only the ring panels the run left logically open, one at a
// time at ~500 ms spacing, and never auto-closes pies (pie-close safety is a
// separate open item). :OF flutters do not mark a panel open.
// -----------------------------------------------------------------------------

static const SeqStep kRingOnlySteps[] = {
    SEQ_DOME(0, FX_PANEL, ":OP01"),
    SEQ_DOME(100, FX_NONE, ":OP07"),
    SEQ_TERM(300),
};
static const SequenceEntry kRingOnlyEntry = {
    "TEST:RING", kRingOnlySteps,
    (uint8_t)(sizeof(kRingOnlySteps) / sizeof(kRingOnlySteps[0])),
    1000, TOGGLE_NONE, nullptr, 0, nullptr,
};
static const SeqStep kPieOnlySteps[] = {
    SEQ_DOME(0, FX_PANEL, ":OPP1"),
    SEQ_DOME(100, FX_NONE, ":OPP3"),
    SEQ_TERM(300),
};
static const SequenceEntry kPieOnlyEntry = {
    "TEST:PIE", kPieOnlySteps,
    (uint8_t)(sizeof(kPieOnlySteps) / sizeof(kPieOnlySteps[0])),
    1000, TOGGLE_NONE, nullptr, 0, nullptr,
};
static const SeqStep kAllPanelSteps[] = {
    SEQ_DOME(0, FX_PANEL, ":OP01"),    // ring
    SEQ_DOME(100, FX_NONE, ":OPP1"),   // pie
    SEQ_TERM(300),
};
static const SequenceEntry kAllPanelEntry = {
    "TEST:ALLPANEL", kAllPanelSteps,
    (uint8_t)(sizeof(kAllPanelSteps) / sizeof(kAllPanelSteps[0])),
    1000, TOGGLE_NONE, nullptr, 0, nullptr,
};
static const SeqStep kNonPanelSteps[] = {
    SEQ_DOME(0, FX_LOGIC_PSI, "@0T11"),
    SEQ_TERM(300),
};
static const SequenceEntry kNonPanelEntry = {
    "TEST:NONPANEL", kNonPanelSteps,
    (uint8_t)(sizeof(kNonPanelSteps) / sizeof(kNonPanelSteps[0])),
    1000, TOGGLE_NONE, nullptr, 0, nullptr,
};

void test_scoped_cleanup_ring_only_closes_open_panels_staggered() {
    SeqEngineState st;
    seqEngineInit(st);
    seqEngineStart(st, &kRingOnlyEntry, 0);
    char log[256] = "";
    // Steps fire; terminal cleanup is now staggered individual closes (never a
    // group :CL15). Anchored at the first finishing peek (t=400), the two open
    // ring panels close at +500 ms and +1000 ms.
    TEST_ASSERT_EQUAL_INT(2, drainAt(st, 400, log, sizeof(log)));
    TEST_ASSERT_EQUAL_STRING(":OP01|:OP07", log);
    TEST_ASSERT_EQUAL_INT(0, drainAt(st, 800, nullptr, 0));   // first close not due yet
    log[0] = '\0';
    TEST_ASSERT_EQUAL_INT(1, drainAt(st, 900, log, sizeof(log)));
    TEST_ASSERT_EQUAL_STRING(":CL01", log);
    log[0] = '\0';
    TEST_ASSERT_EQUAL_INT(1, drainAt(st, 1400, log, sizeof(log)));
    TEST_ASSERT_EQUAL_STRING(":CL07", log);
    TEST_ASSERT_FALSE(seqEngineActive(st));
}

void test_scoped_cleanup_pie_only_never_auto_closes_pies() {
    SeqEngineState st;
    seqEngineInit(st);
    seqEngineStart(st, &kPieOnlyEntry, 0);
    char log[256] = "";
    // Pies opened but never auto-closed by engine cleanup; the ring mask is empty
    // so nothing is queued and the engine goes idle immediately (no group :CL14).
    TEST_ASSERT_EQUAL_INT(2, drainAt(st, 400, log, sizeof(log)));
    TEST_ASSERT_EQUAL_STRING(":OPP1|:OPP3", log);
    TEST_ASSERT_FALSE(seqEngineActive(st));
    TEST_ASSERT_EQUAL_INT(0, drainAt(st, 5000, nullptr, 0));  // nothing later either
    TEST_ASSERT_NULL(strstr(log, ":CL"));
}

void test_scoped_cleanup_both_groups_closes_only_ring() {
    SeqEngineState st;
    seqEngineInit(st);
    seqEngineStart(st, &kAllPanelEntry, 0);
    char log[256] = "";
    // Only the open RING panel is closed (staggered); the open pie is left alone
    // (never auto-closed); never a group :CL00.
    TEST_ASSERT_EQUAL_INT(2, drainAt(st, 400, log, sizeof(log)));
    TEST_ASSERT_EQUAL_STRING(":OP01|:OPP1", log);
    log[0] = '\0';
    TEST_ASSERT_EQUAL_INT(1, drainAt(st, 900, log, sizeof(log)));
    TEST_ASSERT_EQUAL_STRING(":CL01", log);
    TEST_ASSERT_FALSE(seqEngineActive(st));
    TEST_ASSERT_NULL(strstr(log, ":CL00"));
    TEST_ASSERT_NULL(strstr(log, ":CLP"));
}

void test_scoped_cleanup_non_panel_emits_no_panel_close() {
    SeqEngineState st;
    seqEngineInit(st);
    seqEngineStart(st, &kNonPanelEntry, 0);
    char log[256] = "";
    drainAt(st, 400, log, sizeof(log));
    // FX_LOGIC_PSI reset only; no panel close of any scope.
    TEST_ASSERT_EQUAL_STRING("@0T11|@0T1|@0P1", log);
    TEST_ASSERT_NULL(strstr(log, ":CL"));
}

// -----------------------------------------------------------------------------
// Latches
// -----------------------------------------------------------------------------

void test_cl00_step_clears_latches() {
    SeqEngineState st;
    seqEngineInit(st);
    st.latches.piesOpen = true;
    st.latches.ringOpen = true;

    seqEngineStart(st, &kFlatEntry, 1000);
    drainAt(st, 1300, nullptr, 0);  // includes the :CL00 release step

    TEST_ASSERT_FALSE(st.latches.piesOpen);
    TEST_ASSERT_FALSE(st.latches.ringOpen);
}

void test_clear_latches_helper() {
    SeqEngineState st;
    seqEngineInit(st);
    st.latches.piesOpen = true;
    seqEngineClearLatches(st);
    TEST_ASSERT_FALSE(st.latches.piesOpen);
}

// =============================================================================
// Runner
// =============================================================================

int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();

    RUN_TEST(test_engine_idle_after_init);
    RUN_TEST(test_engine_active_and_named_after_start);

    RUN_TEST(test_flat_steps_fire_in_order_at_their_times);
    RUN_TEST(test_late_tick_catches_up_all_overdue_steps);
    RUN_TEST(test_end_not_due_keeps_sequence_active);

    RUN_TEST(test_peek_without_commit_returns_same_action);
    RUN_TEST(test_retry_does_not_drift_later_steps);

    RUN_TEST(test_abort_emits_logic_psi_reset);
    RUN_TEST(test_abort_before_any_fx_step_emits_nothing);
    RUN_TEST(test_abort_when_idle_is_noop);
    RUN_TEST(test_restart_after_abort_runs_fresh);

    RUN_TEST(test_catalog_find_returns_entry_for_vader);
    RUN_TEST(test_catalog_find_returns_null_for_unknown);
    RUN_TEST(test_real_vader_entry_runs_through_engine);
    RUN_TEST(test_real_vader_abort_stops_audio_and_resets_holos);
    RUN_TEST(test_bounded_audio_emits_stop_on_normal_termination);
    RUN_TEST(test_plain_audio_no_stop_on_normal_termination);
    RUN_TEST(test_audio_stop_step_emits_stop_at_authored_time);
    RUN_TEST(test_audio_category_step_emits_category_action);
    RUN_TEST(test_dome_rotate_step_emits_typed_rotation_action);
    RUN_TEST(test_abort_after_dome_rotate_emits_neutral_rotation_stop);
    RUN_TEST(test_terminal_cleanup_after_dome_rotate_emits_neutral_rotation_stop);
    RUN_TEST(test_zero_speed_dome_rotate_does_not_emit_cleanup_stop);
    RUN_TEST(test_real_reset_entry_clears_latches_and_resets);

    RUN_TEST(test_body_step_emits_part_shape_and_how_far);
    RUN_TEST(test_body_step_defaults_resolve_to_open_over_the_whole_throw);
    RUN_TEST(test_body_step_carries_no_effect_class);
    RUN_TEST(test_body_step_unmodelled_shape_resolves_to_the_default);
    RUN_TEST(test_body_step_how_far_is_floored_not_refused);
    RUN_TEST(test_body_step_flutter_carries_its_duration);
    RUN_TEST(test_body_step_schedules_no_release_on_normal_termination);
    RUN_TEST(test_body_step_abort_emits_no_cleanup);

    RUN_TEST(test_loop_iterations_fire_at_period_offsets);
    RUN_TEST(test_loop_late_tick_catches_up_all_iterations);
    RUN_TEST(test_loop_abort_mid_iteration_finishes_clean);
    RUN_TEST(test_real_loop_entries_have_loop_headers);
    RUN_TEST(test_real_rockmarch_second_pass_timing);

    RUN_TEST(test_random_step_resolves_target_and_mode_from_rng);
    RUN_TEST(test_random_pick_distinct_rerolls_and_hold_reuses);
    RUN_TEST(test_random_peek_retry_keeps_same_resolution);
    RUN_TEST(test_random_jitter_delays_fire_time);
    RUN_TEST(test_real_random_entries_are_catalog);
    RUN_TEST(test_real_overload_runs_end_to_end);

    RUN_TEST(test_toggle_first_press_runs_open_branch_and_latches_open);
    RUN_TEST(test_toggle_second_press_runs_close_branch_and_releases);
    RUN_TEST(test_toggle_close_skips_release_while_another_group_open);
    RUN_TEST(test_toggle_all_carries_pie_and_ring_latches);
    RUN_TEST(test_toggle_abort_mid_open_closes_and_clears_latches);
    RUN_TEST(test_real_toggle_entries_are_catalog_with_branches);

    RUN_TEST(test_scoped_cleanup_ring_only_closes_open_panels_staggered);
    RUN_TEST(test_scoped_cleanup_pie_only_never_auto_closes_pies);
    RUN_TEST(test_scoped_cleanup_both_groups_closes_only_ring);
    RUN_TEST(test_scoped_cleanup_non_panel_emits_no_panel_close);

    RUN_TEST(test_cl00_step_clears_latches);
    RUN_TEST(test_clear_latches_helper);
    RUN_TEST(test_real_hello_entry_opens_p1_once);
    RUN_TEST(test_real_low_and_openall_send_no_same_timestamp_burst);
    RUN_TEST(test_body_routines_are_factory_sequences_of_body_steps);
    RUN_TEST(test_body_routines_leave_every_part_they_move_closed);
    RUN_TEST(test_no_two_body_routines_are_the_same_routine);
    RUN_TEST(test_body_routine_wiggle_close_runs_as_body_moves);

    return UNITY_END();
}
