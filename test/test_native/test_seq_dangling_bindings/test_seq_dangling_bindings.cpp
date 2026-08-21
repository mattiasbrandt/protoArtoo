// =============================================================================
// test/test_native/test_seq_dangling_bindings/test_seq_dangling_bindings.cpp
//
// Native tests for the Memory Wipe dangling RC trigger scan
// (seq_dangling_bindings.cpp): match rules, Factory-shadow suppression, and
// output bounds. Runs the real production scan over fixture slot arrays.
// =============================================================================

#include <string.h>

#include <unity.h>

#include "seq_dangling_bindings.h"

void setUp()    {}
void tearDown() {}

static RcTriggerBinding slot(RcBindingSource source, uint8_t channel,
                             RobotActionId target, const char* payload) {
    RcTriggerBinding b = {};
    b.source = source;
    b.channel = channel;
    b.target = target;
    strncpy(b.marcduinoPayload, payload, sizeof(b.marcduinoPayload) - 1);
    return b;
}

static void test_no_match_returns_zero() {
    RcTriggerBinding slots[2] = {
        slot(RC_BINDING_SBUS1, 5, DOME_ACTION_SEQ, "DM:OTHER"),
        slot(RC_BINDING_NONE, 0, ROBOT_ACTION_NONE, ""),
    };
    SeqDanglingBinding out[4];
    TEST_ASSERT_EQUAL_size_t(
        0, seqDanglingBindings("DM:GONE", false, slots, 2, out, 4));
}

static void test_single_match_reports_source_and_channel() {
    RcTriggerBinding slots[1] = {
        slot(RC_BINDING_SBUS2, 7, DOME_ACTION_SEQ, "DM:GONE"),
    };
    SeqDanglingBinding out[4];
    TEST_ASSERT_EQUAL_size_t(
        1, seqDanglingBindings("DM:GONE", false, slots, 1, out, 4));
    TEST_ASSERT_EQUAL(RC_BINDING_SBUS2, out[0].source);
    TEST_ASSERT_EQUAL_UINT8(7, out[0].channel);
}

static void test_multiple_matches_in_slot_order() {
    RcTriggerBinding slots[3] = {
        slot(RC_BINDING_PWM, 2, DOME_ACTION_SEQ, "DM:GONE"),
        slot(RC_BINDING_SBUS1, 9, DOME_ACTION_SEQ, "DM:OTHER"),
        slot(RC_BINDING_SBUS1, 11, DOME_ACTION_SEQ, "DM:GONE"),
    };
    SeqDanglingBinding out[4];
    TEST_ASSERT_EQUAL_size_t(
        2, seqDanglingBindings("DM:GONE", false, slots, 3, out, 4));
    TEST_ASSERT_EQUAL_UINT8(2, out[0].channel);
    TEST_ASSERT_EQUAL_UINT8(11, out[1].channel);
}

static void test_factory_shadow_suppresses_report() {
    RcTriggerBinding slots[1] = {
        slot(RC_BINDING_SBUS1, 3, DOME_ACTION_SEQ, "DM:GONE"),
    };
    SeqDanglingBinding out[4];
    TEST_ASSERT_EQUAL_size_t(
        0, seqDanglingBindings("DM:GONE", true, slots, 1, out, 4));
}

static void test_non_seq_target_with_same_payload_ignored() {
    RcTriggerBinding slots[1] = {
        slot(RC_BINDING_SBUS1, 4, DOME_ACTION_MARCDUINO_CMD, "DM:GONE"),
    };
    SeqDanglingBinding out[4];
    TEST_ASSERT_EQUAL_size_t(
        0, seqDanglingBindings("DM:GONE", false, slots, 1, out, 4));
}

static void test_output_truncates_at_cap() {
    RcTriggerBinding slots[3] = {
        slot(RC_BINDING_SBUS1, 1, DOME_ACTION_SEQ, "DM:GONE"),
        slot(RC_BINDING_SBUS1, 2, DOME_ACTION_SEQ, "DM:GONE"),
        slot(RC_BINDING_SBUS1, 3, DOME_ACTION_SEQ, "DM:GONE"),
    };
    SeqDanglingBinding out[2];
    TEST_ASSERT_EQUAL_size_t(
        2, seqDanglingBindings("DM:GONE", false, slots, 3, out, 2));
    TEST_ASSERT_EQUAL_UINT8(1, out[0].channel);
    TEST_ASSERT_EQUAL_UINT8(2, out[1].channel);
}

// -----------------------------------------------------------------------------
int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_no_match_returns_zero);
    RUN_TEST(test_single_match_reports_source_and_channel);
    RUN_TEST(test_multiple_matches_in_slot_order);
    RUN_TEST(test_factory_shadow_suppresses_report);
    RUN_TEST(test_non_seq_target_with_same_payload_ignored);
    RUN_TEST(test_output_truncates_at_cap);
    return UNITY_END();
}
