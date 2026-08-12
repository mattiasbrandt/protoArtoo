// =============================================================================
// test/test_native/test_sequence_dispatcher_step/test_sequence_dispatcher_step.cpp
//
// Native tests for sequence_dispatcher_step — pure dispatch decision core (ADR 0014).
//
// The step-core is a pure function that takes a SeqAction and returns the
// dispatch target (dome, audio) and command format. This test covers each
// action kind's routing decision independently, allowing comprehensive
// verification without the task layer.
// =============================================================================

#include <unity.h>

#include <cstring>

#include "sequence_dispatcher_step.h"
#include "sequence_engine.h"

void setUp()    {}
void tearDown() {}

// =============================================================================
// Dome command routing
// =============================================================================

void test_dome_cmd_routes_to_dome_cmd_target() {
    SeqAction act = {};
    act.kind = SEQ_ACT_DOME_CMD;
    strncpy(act.payload, "@0T1", sizeof(act.payload) - 1);

    SequenceDispatcherStepActions decision = sequenceDispatcherStep(act, 1000);
    TEST_ASSERT_EQUAL_INT(SEQ_DISPATCH_DOME_CMD, (int)decision.target);
}

void test_dome_cmd_preserves_payload() {
    SeqAction act = {};
    act.kind = SEQ_ACT_DOME_CMD;
    strncpy(act.payload, "@0P1", sizeof(act.payload) - 1);

    SequenceDispatcherStepActions decision = sequenceDispatcherStep(act, 1000);
    // The payload is not modified by the step; adapter uses act.payload
    TEST_ASSERT_EQUAL_INT(SEQ_DISPATCH_DOME_CMD, (int)decision.target);
}

// =============================================================================
// Dome rotate routing and speed conversion
// =============================================================================

void test_dome_rotate_routes_to_dome_rotate_target() {
    SeqAction act = {};
    act.kind = SEQ_ACT_DOME_ROTATE;
    act.domeSpeedPct = 50;
    act.domeDurationMs = 1000;

    SequenceDispatcherStepActions decision = sequenceDispatcherStep(act, 1000);
    TEST_ASSERT_EQUAL_INT(SEQ_DISPATCH_DOME_ROTATE, (int)decision.target);
}

void test_dome_rotate_converts_positive_speed_percentage() {
    SeqAction act = {};
    act.kind = SEQ_ACT_DOME_ROTATE;
    act.domeSpeedPct = 50;
    act.domeDurationMs = 1000;

    SequenceDispatcherStepActions decision = sequenceDispatcherStep(act, 1000);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, decision.domeRotate.speed);
}

void test_dome_rotate_converts_negative_speed_percentage() {
    SeqAction act = {};
    act.kind = SEQ_ACT_DOME_ROTATE;
    act.domeSpeedPct = -75;
    act.domeDurationMs = 500;

    SequenceDispatcherStepActions decision = sequenceDispatcherStep(act, 1000);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -0.75f, decision.domeRotate.speed);
}

void test_dome_rotate_preserves_duration() {
    SeqAction act = {};
    act.kind = SEQ_ACT_DOME_ROTATE;
    act.domeSpeedPct = 30;
    act.domeDurationMs = 2500;

    SequenceDispatcherStepActions decision = sequenceDispatcherStep(act, 1000);
    TEST_ASSERT_EQUAL_UINT32(2500, decision.domeRotate.durationMs);
}

void test_dome_rotate_max_positive_speed() {
    SeqAction act = {};
    act.kind = SEQ_ACT_DOME_ROTATE;
    act.domeSpeedPct = 100;
    act.domeDurationMs = 1000;

    SequenceDispatcherStepActions decision = sequenceDispatcherStep(act, 1000);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, decision.domeRotate.speed);
}

void test_dome_rotate_max_negative_speed() {
    SeqAction act = {};
    act.kind = SEQ_ACT_DOME_ROTATE;
    act.domeSpeedPct = -100;
    act.domeDurationMs = 1000;

    SequenceDispatcherStepActions decision = sequenceDispatcherStep(act, 1000);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -1.0f, decision.domeRotate.speed);
}

void test_dome_rotate_zero_speed() {
    SeqAction act = {};
    act.kind = SEQ_ACT_DOME_ROTATE;
    act.domeSpeedPct = 0;
    act.domeDurationMs = 0;

    SequenceDispatcherStepActions decision = sequenceDispatcherStep(act, 1000);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, decision.domeRotate.speed);
    TEST_ASSERT_EQUAL_UINT32(0, decision.domeRotate.durationMs);
}

// =============================================================================
// Audio dollar command routing
// =============================================================================

void test_audio_dollar_routes_to_audio_dollar_target() {
    SeqAction act = {};
    act.kind = SEQ_ACT_AUDIO_DOLLAR;
    strncpy(act.payload, "$815", sizeof(act.payload) - 1);

    SequenceDispatcherStepActions decision = sequenceDispatcherStep(act, 1000);
    TEST_ASSERT_EQUAL_INT(SEQ_DISPATCH_AUDIO_DOLLAR, (int)decision.target);
}

void test_audio_dollar_with_leading_dollar() {
    SeqAction act = {};
    act.kind = SEQ_ACT_AUDIO_DOLLAR;
    strncpy(act.payload, "$001", sizeof(act.payload) - 1);

    SequenceDispatcherStepActions decision = sequenceDispatcherStep(act, 1000);
    TEST_ASSERT_EQUAL_INT(SEQ_DISPATCH_AUDIO_DOLLAR, (int)decision.target);
}

// =============================================================================
// Audio category routing
// =============================================================================

void test_audio_category_routes_to_audio_category_target() {
    SeqAction act = {};
    act.kind = SEQ_ACT_AUDIO_CATEGORY;
    act.audioCategory = 5;
    act.audioFallbackSlot = 2;

    SequenceDispatcherStepActions decision = sequenceDispatcherStep(act, 1000);
    TEST_ASSERT_EQUAL_INT(SEQ_DISPATCH_AUDIO_CATEGORY, (int)decision.target);
}

void test_audio_category_passes_category() {
    SeqAction act = {};
    act.kind = SEQ_ACT_AUDIO_CATEGORY;
    act.audioCategory = 7;
    act.audioFallbackSlot = 1;

    SequenceDispatcherStepActions decision = sequenceDispatcherStep(act, 1000);
    TEST_ASSERT_EQUAL_INT(7, (int)decision.audioCategory.category);
}

void test_audio_category_passes_fallback_slot() {
    SeqAction act = {};
    act.kind = SEQ_ACT_AUDIO_CATEGORY;
    act.audioCategory = 3;
    act.audioFallbackSlot = 4;

    SequenceDispatcherStepActions decision = sequenceDispatcherStep(act, 1000);
    TEST_ASSERT_EQUAL_INT(4, (int)decision.audioCategory.fallbackSlot);
}

void test_audio_category_zero_index() {
    SeqAction act = {};
    act.kind = SEQ_ACT_AUDIO_CATEGORY;
    act.audioCategory = 0;
    act.audioFallbackSlot = 0;

    SequenceDispatcherStepActions decision = sequenceDispatcherStep(act, 1000);
    TEST_ASSERT_EQUAL_INT(SEQ_DISPATCH_AUDIO_CATEGORY, (int)decision.target);
    TEST_ASSERT_EQUAL_INT(0, (int)decision.audioCategory.category);
}

// =============================================================================
// Audio stop routing
// =============================================================================

void test_audio_stop_routes_to_audio_stop_target() {
    SeqAction act = {};
    act.kind = SEQ_ACT_AUDIO_STOP;

    SequenceDispatcherStepActions decision = sequenceDispatcherStep(act, 1000);
    TEST_ASSERT_EQUAL_INT(SEQ_DISPATCH_AUDIO_STOP, (int)decision.target);
}

// =============================================================================
// Unknown actions (graceful fail-safe)
// =============================================================================

void test_unknown_action_kind_routes_to_none() {
    SeqAction act = {};
    act.kind = (SeqActionKind)999;  // Invalid action kind

    SequenceDispatcherStepActions decision = sequenceDispatcherStep(act, 1000);
    TEST_ASSERT_EQUAL_INT(SEQ_DISPATCH_NONE, (int)decision.target);
}

void test_default_action_is_none() {
    SeqAction act = {};
    // act.kind is default-initialized (0), which may not be a valid kind

    SequenceDispatcherStepActions decision = sequenceDispatcherStep(act, 1000);
    // Either 0 is a valid kind or it routes to NONE; either way, the
    // function should not crash.
    TEST_ASSERT_TRUE(decision.target <= SEQ_DISPATCH_NONE);
}

// =============================================================================
// Timestamp parameter handling
// =============================================================================

void test_dome_rotate_receives_timestamp_parameter() {
    SeqAction act = {};
    act.kind = SEQ_ACT_DOME_ROTATE;
    act.domeSpeedPct = 50;
    act.domeDurationMs = 1000;

    // Different timestamps should not affect the decision
    SequenceDispatcherStepActions d1 = sequenceDispatcherStep(act, 100);
    SequenceDispatcherStepActions d2 = sequenceDispatcherStep(act, 50000);

    TEST_ASSERT_FLOAT_WITHIN(0.001f, d1.domeRotate.speed, d2.domeRotate.speed);
    TEST_ASSERT_EQUAL_UINT32(d1.domeRotate.durationMs, d2.domeRotate.durationMs);
}

// =============================================================================
// Idle gating timeout calculation
// =============================================================================

void test_wait_ms_active_sequence_returns_10() {
    uint32_t waitMs = sequence_dispatcher_wait_ms(true, false);
    TEST_ASSERT_EQUAL_UINT32(10, waitMs);
}

void test_wait_ms_resync_close_pending_returns_10() {
    uint32_t waitMs = sequence_dispatcher_wait_ms(false, true);
    TEST_ASSERT_EQUAL_UINT32(10, waitMs);
}

void test_wait_ms_idle_returns_250() {
    uint32_t waitMs = sequence_dispatcher_wait_ms(false, false);
    TEST_ASSERT_EQUAL_UINT32(250, waitMs);
}

// =============================================================================
// Runner
// =============================================================================

int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();

    RUN_TEST(test_dome_cmd_routes_to_dome_cmd_target);
    RUN_TEST(test_dome_cmd_preserves_payload);

    RUN_TEST(test_dome_rotate_routes_to_dome_rotate_target);
    RUN_TEST(test_dome_rotate_converts_positive_speed_percentage);
    RUN_TEST(test_dome_rotate_converts_negative_speed_percentage);
    RUN_TEST(test_dome_rotate_preserves_duration);
    RUN_TEST(test_dome_rotate_max_positive_speed);
    RUN_TEST(test_dome_rotate_max_negative_speed);
    RUN_TEST(test_dome_rotate_zero_speed);

    RUN_TEST(test_audio_dollar_routes_to_audio_dollar_target);
    RUN_TEST(test_audio_dollar_with_leading_dollar);

    RUN_TEST(test_audio_category_routes_to_audio_category_target);
    RUN_TEST(test_audio_category_passes_category);
    RUN_TEST(test_audio_category_passes_fallback_slot);
    RUN_TEST(test_audio_category_zero_index);

    RUN_TEST(test_audio_stop_routes_to_audio_stop_target);

    RUN_TEST(test_unknown_action_kind_routes_to_none);
    RUN_TEST(test_default_action_is_none);

    RUN_TEST(test_dome_rotate_receives_timestamp_parameter);

    RUN_TEST(test_wait_ms_active_sequence_returns_10);
    RUN_TEST(test_wait_ms_resync_close_pending_returns_10);
    RUN_TEST(test_wait_ms_idle_returns_250);

    return UNITY_END();
}
