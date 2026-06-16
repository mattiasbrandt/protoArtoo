// =============================================================================
// test/test_native/test_protocol_check/test_protocol_check.cpp
//
// Native tests for Protocol Check (src/protocol_check.cpp, issue #2 slice 3).
// Exercises the accept/reject matrix, effect-class inference, loop/random/toggle
// structure rules, and retrain (shadowing) rules against the real catalog.
// =============================================================================

#include <string.h>

#include <unity.h>

#include "audio_playback_policy.h"
#include "protocol_check.h"
#include "sequence_engine.h"

void setUp()    {}
void tearDown() {}

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

// A well-formed flat draft mirroring DM:NOD. Returned by value is fine — the
// arrays are static so `steps`/`closeSteps` stay valid.
static SeqStep gNod[] = {
    SEQ_AUDIO(0, "$H"),
    SEQ_DOME(0, FX_NONE, "@1MYes"),
    SEQ_DOME(0, FX_NONE, ":SM0,150,2200"),
    SEQ_DOME(150, FX_NONE, ":SM0,150,800"),
    SEQ_TERM(300),
};

static SeqDraft makeNodDraft() {
    SeqDraft d = {};
    strcpy(d.name, "DM:MYNOD");
    d.suppressMs = 3000;
    d.toggleGroup = TOGGLE_NONE;
    d.steps = gNod;
    d.stepCount = (uint8_t)(sizeof(gNod) / sizeof(gNod[0]));
    d.closeSteps = nullptr;
    d.closeStepCount = 0;
    return d;
}

// Reset gNod to its canonical contents (tests mutate effectClass / fields).
static void resetNod() {
    SeqStep fresh[] = {
        SEQ_AUDIO(0, "$H"),
        SEQ_DOME(0, FX_NONE, "@1MYes"),
        SEQ_DOME(0, FX_NONE, ":SM0,150,2200"),
        SEQ_DOME(150, FX_NONE, ":SM0,150,800"),
        SEQ_TERM(300),
    };
    memcpy(gNod, fresh, sizeof(fresh));
}

// -----------------------------------------------------------------------------
// Happy path + inference
// -----------------------------------------------------------------------------

static void test_valid_flat_draft_accepts() {
    resetNod();
    SeqDraft d = makeNodDraft();
    ProtocolCheckResult r = protocolCheck(d);
    TEST_ASSERT_TRUE_MESSAGE(r.ok, r.message);
}

static void test_inference_stamps_effect_classes() {
    resetNod();
    SeqDraft d = makeNodDraft();
    ProtocolCheckResult r = protocolCheck(d);
    TEST_ASSERT_TRUE(r.ok);
    // gNod[0] audio -> FX_AUDIO
    TEST_ASSERT_EQUAL_UINT8(FX_AUDIO, gNod[0].effectClass);
    // gNod[1] @1MYes -> FX_LOGIC_PSI
    TEST_ASSERT_EQUAL_UINT8(FX_LOGIC_PSI, gNod[1].effectClass);
    // gNod[2] :SM0,2200 (open) -> FX_PANEL
    TEST_ASSERT_EQUAL_UINT8(FX_PANEL, gNod[2].effectClass);
    // gNod[3] :SM0,800 (close) -> FX_NONE
    TEST_ASSERT_EQUAL_UINT8(FX_NONE, gNod[3].effectClass);
}

static void test_inference_holo_and_reset() {
    static SeqStep s[] = {
        SEQ_DOME(0, FX_NONE, "@HPA0021|47"),  // holo -> FX_HOLO
        SEQ_DOME(0, FX_NONE, "*ST00"),        // holo reset -> FX_NONE
        SEQ_DOME(0, FX_NONE, "@0T1"),         // logic reset -> FX_NONE
        SEQ_DOME(0, FX_NONE, ":SE09"),        // dome seq -> FX_NONE
        SEQ_TERM(100),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", s, 5);
    TEST_ASSERT_TRUE_MESSAGE(r.ok, r.message);
    TEST_ASSERT_EQUAL_UINT8(FX_HOLO, s[0].effectClass);
    TEST_ASSERT_EQUAL_UINT8(FX_NONE, s[1].effectClass);
    TEST_ASSERT_EQUAL_UINT8(FX_NONE, s[2].effectClass);
    TEST_ASSERT_EQUAL_UINT8(FX_NONE, s[3].effectClass);
}

// -----------------------------------------------------------------------------
// Name + metadata
// -----------------------------------------------------------------------------

static void test_bad_name_rejected() {
    resetNod();
    SeqDraft d = makeNodDraft();
    strcpy(d.name, "BD:NOPE");  // wrong prefix
    ProtocolCheckResult r = protocolCheck(d);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_EQUAL_STRING("name", r.field);
}

static void test_name_lowercase_rejected() {
    resetNod();
    SeqDraft d = makeNodDraft();
    strcpy(d.name, "DM:lower");
    ProtocolCheckResult r = protocolCheck(d);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_EQUAL_STRING("name", r.field);
}

static void test_suppress_below_min_rejected() {
    resetNod();
    SeqDraft d = makeNodDraft();
    d.suppressMs = 500;
    ProtocolCheckResult r = protocolCheck(d);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_EQUAL_STRING("suppressMs", r.field);
}

static void test_suppress_below_end_time_rejected() {
    resetNod();
    SeqDraft d = makeNodDraft();
    d.suppressMs = 200;  // < end time 300, but also < min; raise above min first
    // make end time 5000 and suppress 3000 -> suppress < end
    gNod[4].tMs = 5000;
    d.suppressMs = 3000;
    ProtocolCheckResult r = protocolCheck(d);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_EQUAL_STRING("suppressMs", r.field);
    gNod[4].tMs = 300;
}

// -----------------------------------------------------------------------------
// Command bounds
// -----------------------------------------------------------------------------

static void test_sm_bad_slot_rejected() {
    static SeqStep s[] = {
        SEQ_DOME(0, FX_NONE, ":SM13,150,2200"),  // slot > 12
        SEQ_TERM(100),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", s, 2);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_EQUAL_STRING("steps[0].cmd", r.field);
}

static void test_sm_bad_pulse_rejected() {
    static SeqStep s[] = {
        SEQ_DOME(0, FX_NONE, ":SM0,150,3000"),  // pulse > 2200
        SEQ_TERM(100),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", s, 2);
    TEST_ASSERT_FALSE(r.ok);
}

static void test_sm_malformed_rejected() {
    static SeqStep s[] = {
        SEQ_DOME(0, FX_NONE, ":SM0,2200"),  // missing move field
        SEQ_TERM(100),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", s, 2);
    TEST_ASSERT_FALSE(r.ok);
}

static void test_unknown_command_rejected() {
    static SeqStep s[] = {
        SEQ_DOME(0, FX_NONE, "#bogus"),
        SEQ_TERM(100),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", s, 2);
    TEST_ASSERT_FALSE(r.ok);
}

// :SE accepts only the canonical two-digit Marcduino form (:SE09, :SE10).
static void test_se_digit_count_enforced() {
    static SeqStep one[] = { SEQ_DOME(0, FX_NONE, ":SE9"), SEQ_TERM(100) };
    ProtocolCheckResult r = protocolCheckBranch("steps", one, 2);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_EQUAL_STRING("steps[0].cmd", r.field);

    static SeqStep three[] = { SEQ_DOME(0, FX_NONE, ":SE123"), SEQ_TERM(100) };
    r = protocolCheckBranch("steps", three, 2);
    TEST_ASSERT_FALSE(r.ok);

    static SeqStep two[] = { SEQ_DOME(0, FX_NONE, ":SE10"), SEQ_TERM(100) };
    r = protocolCheckBranch("steps", two, 2);
    TEST_ASSERT_TRUE_MESSAGE(r.ok, r.message);
}

static void test_audio_dollar_charset_rejected() {
    static SeqStep s[] = {
        SEQ_AUDIO(0, "$bad!"),  // '!' not alnum
        SEQ_TERM(100),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", s, 2);
    TEST_ASSERT_FALSE(r.ok);
}

// -----------------------------------------------------------------------------
// Branch structure
// -----------------------------------------------------------------------------

static void test_missing_terminal_end_rejected() {
    static SeqStep s[] = {
        SEQ_AUDIO(0, "$H"),
        SEQ_DOME(0, FX_NONE, ":SM0,150,2200"),  // no STEP_END
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", s, 2);
    TEST_ASSERT_FALSE(r.ok);
}

static void test_mid_branch_end_rejected() {
    static SeqStep s[] = {
        SEQ_TERM(0),    // STEP_END not last
        SEQ_AUDIO(0, "$H"),
        SEQ_TERM(100),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", s, 3);
    TEST_ASSERT_FALSE(r.ok);
}

static void test_non_monotonic_t_rejected() {
    static SeqStep s[] = {
        SEQ_DOME(500, FX_NONE, ":SM0,150,2200"),
        SEQ_DOME(100, FX_NONE, ":SM0,150,800"),  // t goes backwards
        SEQ_TERM(600),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", s, 3);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_EQUAL_STRING("steps[1].t", r.field);
}

// -----------------------------------------------------------------------------
// Loop
// -----------------------------------------------------------------------------

static void test_valid_loop_accepts() {
    static SeqStep s[] = {
        SEQ_LOOP(0, 2, 1846, 14000),
        SEQ_DOME(0, FX_NONE, ":SM0,150,2200"),    // body t relative
        SEQ_DOME(623, FX_NONE, ":SM0,150,800"),
        SEQ_TERM(14000),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", s, 4);
    TEST_ASSERT_TRUE_MESSAGE(r.ok, r.message);
}

static void test_loop_body_overrun_rejected() {
    static SeqStep s[] = {
        SEQ_LOOP(0, 5, 1846, 14000),  // bodyCount 5 overruns
        SEQ_DOME(0, FX_NONE, ":SM0,150,2200"),
        SEQ_TERM(14000),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", s, 3);
    TEST_ASSERT_FALSE(r.ok);
}

static void test_nested_loop_rejected() {
    static SeqStep s[] = {
        SEQ_LOOP(0, 3, 1846, 14000),
        SEQ_DOME(0, FX_NONE, ":SM0,150,2200"),
        SEQ_LOOP(0, 1, 1000, 5000),   // nested
        SEQ_DOME(0, FX_NONE, ":SM1,150,2200"),
        SEQ_TERM(14000),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", s, 5);
    TEST_ASSERT_FALSE(r.ok);
}

static void test_loop_bad_period_rejected() {
    static SeqStep s[] = {
        SEQ_LOOP(0, 1, 50, 14000),  // period < 100
        SEQ_DOME(0, FX_NONE, ":SM0,150,2200"),
        SEQ_TERM(14000),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", s, 3);
    TEST_ASSERT_FALSE(r.ok);
}

// -----------------------------------------------------------------------------
// Random
// -----------------------------------------------------------------------------

static void test_valid_random_accepts() {
    static SeqStep s[] = {
        SEQ_RAND(0, SLOTSET_RING, 1150, 1500, 300, 500, 1),
        SEQ_TERM(100),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", s, 2);
    TEST_ASSERT_TRUE_MESSAGE(r.ok, r.message);
    TEST_ASSERT_EQUAL_UINT8(FX_PANEL, s[0].effectClass);
}

static void test_random_jitter_too_large_rejected() {
    static SeqStep s[] = {
        SEQ_RAND(0, SLOTSET_RING, 1150, 1500, 300, 5000, 1),  // jitter > 2000
        SEQ_TERM(100),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", s, 2);
    TEST_ASSERT_FALSE(r.ok);
}

static void test_random_pulse_out_of_range_rejected() {
    static SeqStep s[] = {
        SEQ_RAND(0, SLOTSET_RING, 500, 1500, 300, 500, 1),  // pulseMin < 800
        SEQ_TERM(100),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", s, 2);
    TEST_ASSERT_FALSE(r.ok);
}

// -----------------------------------------------------------------------------
// Audio category
// -----------------------------------------------------------------------------

static void test_audio_category_out_of_range_rejected() {
    static SeqStep s[] = {
        SEQ_AUDIO_CAT(0, AUDIO_CATEGORY_COUNT, AUDIO_SLOT_NAMED_HAPPY),  // bad cat
        SEQ_TERM(100),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", s, 2);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_EQUAL_STRING("steps[0].category", r.field);
}

// -----------------------------------------------------------------------------
// Toggle structure
// -----------------------------------------------------------------------------

static void test_toggle_without_close_branch_rejected() {
    resetNod();
    SeqDraft d = makeNodDraft();
    strcpy(d.name, "DM:MYTOG");
    d.toggleGroup = TOGGLE_PIES;
    d.closeSteps = nullptr;  // missing close branch
    ProtocolCheckResult r = protocolCheck(d);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_EQUAL_STRING("closeSteps", r.field);
}

static void test_nontoggle_with_close_branch_rejected() {
    resetNod();
    static SeqStep close[] = { SEQ_DOME(0, FX_NONE, ":CL00"), SEQ_TERM(100) };
    SeqDraft d = makeNodDraft();
    d.toggleGroup = TOGGLE_NONE;
    d.closeSteps = close;
    d.closeStepCount = 2;
    ProtocolCheckResult r = protocolCheck(d);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_EQUAL_STRING("closeSteps", r.field);
}

// User latches are reserved until the engine wires branch-pick/latch for them
// (the engine's switch defaults would run such a toggle open-branch-only and
// never latch), so Protocol Check rejects them even with a valid close branch.
static void test_user_toggle_rejected_until_engine_wired() {
    resetNod();
    static SeqStep openB[] = {
        SEQ_DOME(0, FX_NONE, ":SM0,150,2200"), SEQ_TERM(200),
    };
    static SeqStep closeB[] = {
        SEQ_DOME(0, FX_NONE, ":SM0,150,800"), SEQ_TERM(200),
    };
    SeqDraft d = {};
    strcpy(d.name, "DM:MYTOG");
    d.suppressMs = 4000;
    d.toggleGroup = TOGGLE_USER1;
    d.steps = openB;
    d.stepCount = 2;
    d.closeSteps = closeB;
    d.closeStepCount = 2;
    ProtocolCheckResult r = protocolCheck(d);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_EQUAL_STRING("toggleGroup", r.field);

    d.toggleGroup = TOGGLE_USER4;
    r = protocolCheck(d);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_EQUAL_STRING("toggleGroup", r.field);
}

// -----------------------------------------------------------------------------
// Retrain (shadowing) rules — resolved against the real catalog
// -----------------------------------------------------------------------------

static void test_retrain_factory_nontoggle_with_group_rejected() {
    resetNod();
    SeqDraft d = makeNodDraft();
    strcpy(d.name, "DM:VADER");   // factory non-toggle
    d.toggleGroup = TOGGLE_PIES;  // must be none
    ProtocolCheckResult r = protocolCheck(d);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_EQUAL_STRING("toggleGroup", r.field);
}

static void test_retrain_factory_nontoggle_with_none_accepts() {
    resetNod();
    SeqDraft d = makeNodDraft();
    strcpy(d.name, "DM:VADER");   // factory non-toggle, group none -> ok
    d.suppressMs = 47000;
    gNod[4].tMs = 300;
    ProtocolCheckResult r = protocolCheck(d);
    TEST_ASSERT_TRUE_MESSAGE(r.ok, r.message);
}

static void test_retrain_factory_toggle_wrong_group_rejected() {
    static SeqStep openB[] = {
        SEQ_DOME(0, FX_NONE, ":SM8,150,2200"), SEQ_TERM(200),
    };
    static SeqStep closeB[] = {
        SEQ_DOME(0, FX_NONE, ":SM8,150,800"), SEQ_TERM(200),
    };
    SeqDraft d = {};
    strcpy(d.name, "DM:PIES");    // factory toggle (TOGGLE_PIES)
    d.suppressMs = 12000;
    d.toggleGroup = TOGGLE_LOW;   // wrong group
    d.steps = openB;
    d.stepCount = 2;
    d.closeSteps = closeB;
    d.closeStepCount = 2;
    ProtocolCheckResult r = protocolCheck(d);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_EQUAL_STRING("toggleGroup", r.field);
}

static void test_retrain_factory_toggle_same_group_accepts() {
    static SeqStep openB[] = {
        SEQ_DOME(0, FX_NONE, ":SM8,150,2200"), SEQ_TERM(200),
    };
    static SeqStep closeB[] = {
        SEQ_DOME(0, FX_NONE, ":SM8,150,800"), SEQ_TERM(200),
    };
    SeqDraft d = {};
    strcpy(d.name, "DM:PIES");
    d.suppressMs = 12000;
    d.toggleGroup = TOGGLE_PIES;  // matches factory
    d.steps = openB;
    d.stepCount = 2;
    d.closeSteps = closeB;
    d.closeStepCount = 2;
    ProtocolCheckResult r = protocolCheck(d);
    TEST_ASSERT_TRUE_MESSAGE(r.ok, r.message);
}

// -----------------------------------------------------------------------------
// Toggle group helper
// -----------------------------------------------------------------------------

static void test_toggle_group_valid_helper() {
    TEST_ASSERT_TRUE(protocolCheckToggleGroupValid(TOGGLE_NONE));
    TEST_ASSERT_TRUE(protocolCheckToggleGroupValid(TOGGLE_USER4));
    TEST_ASSERT_FALSE(protocolCheckToggleGroupValid((SeqToggleGroup)99));
}

// -----------------------------------------------------------------------------
int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();

    RUN_TEST(test_valid_flat_draft_accepts);
    RUN_TEST(test_inference_stamps_effect_classes);
    RUN_TEST(test_inference_holo_and_reset);

    RUN_TEST(test_bad_name_rejected);
    RUN_TEST(test_name_lowercase_rejected);
    RUN_TEST(test_suppress_below_min_rejected);
    RUN_TEST(test_suppress_below_end_time_rejected);

    RUN_TEST(test_sm_bad_slot_rejected);
    RUN_TEST(test_sm_bad_pulse_rejected);
    RUN_TEST(test_sm_malformed_rejected);
    RUN_TEST(test_unknown_command_rejected);
    RUN_TEST(test_se_digit_count_enforced);
    RUN_TEST(test_audio_dollar_charset_rejected);

    RUN_TEST(test_missing_terminal_end_rejected);
    RUN_TEST(test_mid_branch_end_rejected);
    RUN_TEST(test_non_monotonic_t_rejected);

    RUN_TEST(test_valid_loop_accepts);
    RUN_TEST(test_loop_body_overrun_rejected);
    RUN_TEST(test_nested_loop_rejected);
    RUN_TEST(test_loop_bad_period_rejected);

    RUN_TEST(test_valid_random_accepts);
    RUN_TEST(test_random_jitter_too_large_rejected);
    RUN_TEST(test_random_pulse_out_of_range_rejected);

    RUN_TEST(test_audio_category_out_of_range_rejected);

    RUN_TEST(test_toggle_without_close_branch_rejected);
    RUN_TEST(test_nontoggle_with_close_branch_rejected);
    RUN_TEST(test_user_toggle_rejected_until_engine_wired);

    RUN_TEST(test_retrain_factory_nontoggle_with_group_rejected);
    RUN_TEST(test_retrain_factory_nontoggle_with_none_accepts);
    RUN_TEST(test_retrain_factory_toggle_wrong_group_rejected);
    RUN_TEST(test_retrain_factory_toggle_same_group_accepts);

    RUN_TEST(test_toggle_group_valid_helper);

    return UNITY_END();
}
