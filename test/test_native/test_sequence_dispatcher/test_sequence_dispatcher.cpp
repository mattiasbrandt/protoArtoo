// =============================================================================
// test/test_native/test_sequence_dispatcher/test_sequence_dispatcher.cpp
//
// Native tests for sequence_dispatcher routing — sequenceLookup() only.
// Pure function; no FreeRTOS or hardware dependencies.
//
// Covers ADR 0004 decision 10: single sequenceStart() choke point with
// catalog dispatch, alias forward, and dome fallback.
// =============================================================================

#include <string.h>

#include <unity.h>

#include "sequence_dispatcher.h"

void setUp()    {}
void tearDown() {}

// =============================================================================
// Catalog entries — body-owned sequences
// =============================================================================

void test_vader_is_catalog() {
    SequenceLookupResult r = sequenceLookup("DM:VADER");
    TEST_ASSERT_EQUAL_INT(SEQ_CATALOG, (int)r.kind);
}

void test_hello_is_catalog() {
    SequenceLookupResult r = sequenceLookup("DM:HELLO");
    TEST_ASSERT_EQUAL_INT(SEQ_CATALOG, (int)r.kind);
}

void test_nod_is_catalog() {
    SequenceLookupResult r = sequenceLookup("DM:NOD");
    TEST_ASSERT_EQUAL_INT(SEQ_CATALOG, (int)r.kind);
}

void test_slice2_flat_sequences_are_catalog() {
    static const char* const names[] = {
        "DM:FLUTTER", "DM:BLOOM", "DM:LEIA", "DM:ALARM", "DM:HEART", "DM:RESET",
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        SequenceLookupResult r = sequenceLookup(names[i]);
        TEST_ASSERT_EQUAL_INT_MESSAGE(SEQ_CATALOG, (int)r.kind, names[i]);
    }
}

void test_slice2_toggle_loop_random_sequences_are_catalog() {
    static const char* const names[] = {
        "DM:PIES", "DM:LOW", "DM:OPENALL",
        "DM:CANTINA", "DM:ROCKMARCH",
        "DM:SCREAM", "DM:OVERLOAD",
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        SequenceLookupResult r = sequenceLookup(names[i]);
        TEST_ASSERT_EQUAL_INT_MESSAGE(SEQ_CATALOG, (int)r.kind, names[i]);
    }
}

// =============================================================================
// Alias entries — forward to dome via :SE## or $NNN
// =============================================================================

void test_wave_alias_resolves_to_se02() {
    SequenceLookupResult r = sequenceLookup("DM:WAVE");
    TEST_ASSERT_EQUAL_INT(SEQ_ALIAS, (int)r.kind);
    TEST_ASSERT_EQUAL_STRING(":SE02", r.aliasTarget);
}

void test_stop_alias_resolves_to_se00() {
    SequenceLookupResult r = sequenceLookup("DM:STOP");
    TEST_ASSERT_EQUAL_INT(SEQ_ALIAS, (int)r.kind);
    TEST_ASSERT_EQUAL_STRING(":SE00", r.aliasTarget);
}

void test_harlemshake_alias_resolves_to_dollar815() {
    SequenceLookupResult r = sequenceLookup("DM:HARLEMSHAKE");
    TEST_ASSERT_EQUAL_INT(SEQ_ALIAS, (int)r.kind);
    TEST_ASSERT_EQUAL_STRING("$815", r.aliasTarget);
}

void test_disco_alias_resolves_to_se09() {
    SequenceLookupResult r = sequenceLookup("DM:DISCO");
    TEST_ASSERT_EQUAL_INT(SEQ_ALIAS, (int)r.kind);
    TEST_ASSERT_EQUAL_STRING(":SE09", r.aliasTarget);
}

void test_byebye_alias_resolves_to_se58() {
    SequenceLookupResult r = sequenceLookup("DM:BYEBYE");
    TEST_ASSERT_EQUAL_INT(SEQ_ALIAS, (int)r.kind);
    TEST_ASSERT_EQUAL_STRING(":SE58", r.aliasTarget);
}

// =============================================================================
// Fallback — unknown DM:* names forward to dome as-is
// =============================================================================

void test_unknown_dm_is_fallback() {
    SequenceLookupResult r = sequenceLookup("DM:UNKNOWN_XYZZY");
    TEST_ASSERT_EQUAL_INT(SEQ_FALLBACK, (int)r.kind);
}

void test_dm_random_is_fallback() {
    SequenceLookupResult r = sequenceLookup("DM:RANDOM");
    TEST_ASSERT_EQUAL_INT(SEQ_FALLBACK, (int)r.kind);
}

void test_empty_name_is_fallback() {
    SequenceLookupResult r = sequenceLookup("");
    TEST_ASSERT_EQUAL_INT(SEQ_FALLBACK, (int)r.kind);
}

void test_null_name_is_fallback() {
    SequenceLookupResult r = sequenceLookup(nullptr);
    TEST_ASSERT_EQUAL_INT(SEQ_FALLBACK, (int)r.kind);
}

// Non-DM:* strings — should not reach sequenceLookup() in production
// (callers filter on "DM:" prefix), but must not crash.
void test_non_dm_prefix_is_fallback() {
    SequenceLookupResult r = sequenceLookup(":SE01");
    TEST_ASSERT_EQUAL_INT(SEQ_FALLBACK, (int)r.kind);
}

// =============================================================================
// Case sensitivity — catalog is case-sensitive (Marcduino convention)
// =============================================================================

void test_catalog_match_is_case_sensitive() {
    SequenceLookupResult r = sequenceLookup("DM:vader");
    TEST_ASSERT_EQUAL_INT(SEQ_FALLBACK, (int)r.kind);
}

void test_alias_match_is_case_sensitive() {
    SequenceLookupResult r = sequenceLookup("DM:wave");
    TEST_ASSERT_EQUAL_INT(SEQ_FALLBACK, (int)r.kind);
}

// =============================================================================
// Engine action dispatch mapping
// =============================================================================

void test_dome_rotate_action_maps_to_sequence_dome_command() {
    SeqAction act = {};
    act.kind = SEQ_ACT_DOME_ROTATE;
    act.domeSpeedPct = -35;
    act.domeDurationMs = 900;

    DomeCommand cmd = {};
    TEST_ASSERT_TRUE(sequenceActionToDomeCommand(act, 123456, cmd));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, -0.35f, cmd.speed);
    TEST_ASSERT_EQUAL_UINT32(900, cmd.durationMs);
    TEST_ASSERT_EQUAL_INT(SRC_SEQ, (int)cmd.source);
    TEST_ASSERT_EQUAL_UINT32(123456, cmd.timestampMs);
}

void test_non_rotate_action_does_not_map_to_dome_command() {
    SeqAction act = {};
    act.kind = SEQ_ACT_DOME_CMD;
    strncpy(act.payload, "@0T1", sizeof(act.payload) - 1);

    DomeCommand cmd = {};
    TEST_ASSERT_FALSE(sequenceActionToDomeCommand(act, 1, cmd));
}

// =============================================================================
// Runner
// =============================================================================

int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();

    RUN_TEST(test_vader_is_catalog);
    RUN_TEST(test_hello_is_catalog);
    RUN_TEST(test_nod_is_catalog);
    RUN_TEST(test_slice2_flat_sequences_are_catalog);
    RUN_TEST(test_slice2_toggle_loop_random_sequences_are_catalog);

    RUN_TEST(test_wave_alias_resolves_to_se02);
    RUN_TEST(test_stop_alias_resolves_to_se00);
    RUN_TEST(test_harlemshake_alias_resolves_to_dollar815);
    RUN_TEST(test_disco_alias_resolves_to_se09);
    RUN_TEST(test_byebye_alias_resolves_to_se58);

    RUN_TEST(test_unknown_dm_is_fallback);
    RUN_TEST(test_dm_random_is_fallback);
    RUN_TEST(test_empty_name_is_fallback);
    RUN_TEST(test_null_name_is_fallback);
    RUN_TEST(test_non_dm_prefix_is_fallback);

    RUN_TEST(test_catalog_match_is_case_sensitive);
    RUN_TEST(test_alias_match_is_case_sensitive);

    RUN_TEST(test_dome_rotate_action_maps_to_sequence_dome_command);
    RUN_TEST(test_non_rotate_action_does_not_map_to_dome_command);

    return UNITY_END();
}
