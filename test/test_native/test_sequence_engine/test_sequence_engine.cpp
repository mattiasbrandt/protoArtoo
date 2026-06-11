// =============================================================================
// test/test_native/test_sequence_engine/test_sequence_engine.cpp
//
// Native tests for the pure DM:* sequence cursor engine (sequence_engine.cpp).
// Synthetic clock, deterministic RNG, local step-table fixtures — no FreeRTOS.
//
// Covers ADR 0004 decisions 4 (preempt cleanup), 7 (auto-reset), and the
// peek/commit retry contract.
// =============================================================================

#include <string.h>

#include <unity.h>

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
    SEQ_DOME(0, FX_NONE, ":SM0,2200,150"),
    SEQ_DOME(150, FX_PANEL, ":SM0,800,150"),
    SEQ_DOME(300, FX_NONE, ":CL00"),
    SEQ_TERM(300),
};
static const SequenceEntry kFlatEntry = {
    "TEST:FLAT", kFlatSteps,
    (uint8_t)(sizeof(kFlatSteps) / sizeof(kFlatSteps[0])),
    3000, TOGGLE_NONE, nullptr, 0,
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
    47000, TOGGLE_NONE, nullptr, 0,
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
    TEST_ASSERT_EQUAL_STRING("$H|@1MYes|:SM0,2200,150", log);

    // Nothing due between scheduled times.
    TEST_ASSERT_EQUAL_INT(0, drainAt(st, 1100, nullptr, 0));

    log[0] = '\0';
    TEST_ASSERT_EQUAL_INT(1, drainAt(st, 1150, log, sizeof(log)));
    TEST_ASSERT_EQUAL_STRING(":SM0,800,150", log);

    // t=300: release step, then STEP_END fires the FX_PANEL auto-reset.
    log[0] = '\0';
    TEST_ASSERT_EQUAL_INT(2, drainAt(st, 1300, log, sizeof(log)));
    TEST_ASSERT_EQUAL_STRING(":CL00|:CL00", log);
    TEST_ASSERT_FALSE(seqEngineActive(st));
}

void test_late_tick_catches_up_all_overdue_steps() {
    SeqEngineState st;
    seqEngineInit(st);
    seqEngineStart(st, &kFlatEntry, 1000);

    // One late tick past the END time drains the whole sequence.
    char log[256] = "";
    TEST_ASSERT_EQUAL_INT(6, drainAt(st, 9000, log, sizeof(log)));
    TEST_ASSERT_EQUAL_STRING("$H|@1MYes|:SM0,2200,150|:SM0,800,150|:CL00|:CL00", log);
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
    TEST_ASSERT_EQUAL_STRING("$H|@1MYes|:SM0,2200,150|:SM0,800,150", log);
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
    TEST_ASSERT_EQUAL_STRING("$H|@1MYes|:SM0,2200,150", log);
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
    TEST_ASSERT_EQUAL_INT(4, drainAt(st, 0, log, sizeof(log)));
    TEST_ASSERT_EQUAL_STRING("$M|@HPA0021|47|@0T11|@0P11", log);

    log[0] = '\0';
    // END at 47000: explicit resets plus the FX_LOGIC_PSI auto-reset.
    TEST_ASSERT_EQUAL_INT(4, drainAt(st, 47000, log, sizeof(log)));
    TEST_ASSERT_EQUAL_STRING("@0T1|@0P1|@0T1|@0P1", log);
    TEST_ASSERT_FALSE(seqEngineActive(st));
}

// -----------------------------------------------------------------------------
// Latches
// -----------------------------------------------------------------------------

void test_cl00_step_clears_latches() {
    SeqEngineState st;
    seqEngineInit(st);
    st.latches.piesOpen = true;
    st.latches.lowOpen = true;
    st.latches.allOpen = true;

    seqEngineStart(st, &kFlatEntry, 1000);
    drainAt(st, 1300, nullptr, 0);  // includes the :CL00 release step

    TEST_ASSERT_FALSE(st.latches.piesOpen);
    TEST_ASSERT_FALSE(st.latches.lowOpen);
    TEST_ASSERT_FALSE(st.latches.allOpen);
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

    RUN_TEST(test_cl00_step_clears_latches);
    RUN_TEST(test_clear_latches_helper);

    return UNITY_END();
}
