// =============================================================================
// test/test_native/test_sequence_body_step/test_sequence_body_step.cpp
//
// Native tests for what a Body Step resolves to on the droid in front of it
// (include/sequence_body_step.h, ADR 0049).
//
// The two halves this covers: where a shape and a how-far land on one Output's
// Endpoint Pair, and what a Part no Output claims reports.
// =============================================================================

#include <string.h>

#include <unity.h>

#include "console_record.h"  // consoleReasonString()
#include "sequence_body_step.h"
#include "sequence_dispatcher_step.h"

void setUp()    {}
void tearDown() {}

// -----------------------------------------------------------------------------
// Fixtures
// -----------------------------------------------------------------------------

static SeqAction bodyAction(const char* part, SeqBodyShape shape, uint8_t howFar,
                            uint16_t flutterMs) {
    SeqAction act = {};
    act.kind = SEQ_ACT_BODY_MOVE;
    strncpy(act.payload, part, sizeof(act.payload) - 1);
    act.bodyShape = (uint8_t)shape;
    act.bodyHowFar = howFar;
    act.bodyFlutterMs = flutterMs;
    return act;
}

// An MG996R output on ARM1 driving one Part, with the ends a builder measured.
static ServoOutputRow drivingRow(const char* part, uint16_t openUs, uint16_t closeUs) {
    ServoOutputRow row = {};
    servoOutputRowDefaults(&row, SERVO_DRIVER_LEDC, LEDC_CH_ARM1, SERVO_COMP_MG996R);
    TEST_ASSERT_TRUE(servoOutputAddPart(&row, part));
    row.open_us = openUs;
    row.close_us = closeUs;
    row.calibrated = true;
    return row;
}

// -----------------------------------------------------------------------------
// Where a shape and a how-far land
// -----------------------------------------------------------------------------

void test_open_over_the_whole_throw_lands_on_the_open_end() {
    const ServoOutputRow row = drivingRow("doorFL", 1900, 1100);
    const SeqAction act = bodyAction("doorFL", BODY_SHAPE_OPEN, 100, 0);

    const SeqBodyStepPlan plan = sequenceBodyStepPlan(act, &row);
    TEST_ASSERT_EQUAL_INT(CONSOLE_REASON_NONE, (int)plan.reason);
    TEST_ASSERT_TRUE(plan.drive);
    TEST_ASSERT_EQUAL_UINT8(0, plan.armId);  // ARM1
    TEST_ASSERT_EQUAL_UINT16(1900, plan.targetUs);
}

void test_close_over_the_whole_throw_lands_on_the_close_end() {
    const ServoOutputRow row = drivingRow("doorFL", 1900, 1100);
    const SeqAction act = bodyAction("doorFL", BODY_SHAPE_CLOSE, 100, 0);

    const SeqBodyStepPlan plan = sequenceBodyStepPlan(act, &row);
    TEST_ASSERT_TRUE(plan.drive);
    TEST_ASSERT_EQUAL_UINT16(1100, plan.targetUs);
}

// Half-open and half-closed are one place, which is what makes how-far a
// fraction of the Part's own throw rather than of a shape.
void test_half_throw_is_the_same_place_for_either_shape() {
    const ServoOutputRow row = drivingRow("doorFL", 1900, 1100);

    const SeqBodyStepPlan opened =
        sequenceBodyStepPlan(bodyAction("doorFL", BODY_SHAPE_OPEN, 50, 0), &row);
    const SeqBodyStepPlan closed =
        sequenceBodyStepPlan(bodyAction("doorFL", BODY_SHAPE_CLOSE, 50, 0), &row);

    TEST_ASSERT_EQUAL_UINT16(1500, opened.targetUs);
    TEST_ASSERT_EQUAL_UINT16(1500, closed.targetUs);
}

// The same step on a different linkage means the same gesture: 60% of this
// Part's throw, not 60% of some absolute band.
void test_partial_travel_is_measured_against_this_parts_own_throw() {
    const ServoOutputRow narrow = drivingRow("dataport", 1600, 1400);
    const SeqBodyStepPlan plan =
        sequenceBodyStepPlan(bodyAction("dataport", BODY_SHAPE_OPEN, 60, 0), &narrow);
    TEST_ASSERT_EQUAL_UINT16(1520, plan.targetUs);  // 1400 + 0.6 * 200
}

// A reversed linkage is open < close and nothing else records it, so the pair is
// read as stored: sorting it here would be the invert flag the model refuses.
void test_reversed_pair_travels_the_other_way() {
    const ServoOutputRow row = drivingRow("doorRR", 1100, 1900);

    const SeqBodyStepPlan full =
        sequenceBodyStepPlan(bodyAction("doorRR", BODY_SHAPE_OPEN, 100, 0), &row);
    TEST_ASSERT_EQUAL_UINT16(1100, full.targetUs);

    const SeqBodyStepPlan quarter =
        sequenceBodyStepPlan(bodyAction("doorRR", BODY_SHAPE_OPEN, 25, 0), &row);
    TEST_ASSERT_EQUAL_UINT16(1700, quarter.targetUs);  // 1900 - 0.25 * 800

    const SeqBodyStepPlan closing =
        sequenceBodyStepPlan(bodyAction("doorRR", BODY_SHAPE_CLOSE, 100, 0), &row);
    TEST_ASSERT_EQUAL_UINT16(1900, closing.targetUs);
}

// A flutter ends open, so it resolves where an open of the same how-far does.
void test_flutter_resolves_where_an_open_does() {
    const ServoOutputRow row = drivingRow("doorRR", 1900, 1100);

    const SeqBodyStepPlan flutter =
        sequenceBodyStepPlan(bodyAction("doorRR", BODY_SHAPE_FLUTTER, 70, 1200), &row);
    const SeqBodyStepPlan opened =
        sequenceBodyStepPlan(bodyAction("doorRR", BODY_SHAPE_OPEN, 70, 0), &row);

    TEST_ASSERT_TRUE(flutter.drive);
    TEST_ASSERT_EQUAL_UINT16(opened.targetUs, flutter.targetUs);
}

// Every door onto a row goes through the component clamp, this one included: an
// MG996R row cannot be driven to 500 us by any route.
void test_the_rows_component_band_bounds_the_target() {
    ServoOutputRow row = drivingRow("utilUp", 2000, 1000);
    // A stored pair outside the fitted component's band (an older row, or one
    // whose component was changed under it) must not reach the pin as stored.
    row.open_us = 2400;
    const SeqBodyStepPlan plan =
        sequenceBodyStepPlan(bodyAction("utilUp", BODY_SHAPE_OPEN, 100, 0), &row);
    TEST_ASSERT_EQUAL_UINT16(SERVO_BAND_STD.hi, plan.targetUs);
}

// -----------------------------------------------------------------------------
// What the Coordinator reports
// -----------------------------------------------------------------------------

// The answer this ticket exists for: authoring a step for an arm nobody has
// wired is legal, it reports part-not-assigned at run, and the sequence carries
// on.
void test_part_no_output_claims_reports_part_not_assigned() {
    const SeqAction act = bodyAction("gripClaw", BODY_SHAPE_OPEN, 100, 0);
    const SeqBodyStepPlan plan = sequenceBodyStepPlan(act, nullptr);
    TEST_ASSERT_EQUAL_INT(CONSOLE_REASON_PART_NOT_ASSIGNED, (int)plan.reason);
    TEST_ASSERT_FALSE(plan.drive);
    TEST_ASSERT_EQUAL_STRING("part-not-assigned", consoleReasonString(plan.reason));
}

// Not part-not-assigned: an id the catalog never declared is a step that
// outlived its image, not a wiring fault to send a builder to the bench over.
void test_part_the_catalog_never_declared_is_not_reported_as_unwired() {
    const SeqAction act = bodyAction("armOfTheFuture", BODY_SHAPE_OPEN, 100, 0);
    const SeqBodyStepPlan plan = sequenceBodyStepPlan(act, nullptr);
    TEST_ASSERT_EQUAL_INT(CONSOLE_REASON_UNKNOWN_ARGUMENT, (int)plan.reason);
    TEST_ASSERT_FALSE(plan.drive);
}

// LEDC's DOME channel drives a brushless ESC, so no Part is driveable there.
void test_row_addressed_to_a_non_servo_channel_cannot_be_driven() {
    ServoOutputRow row = drivingRow("doorFL", 1900, 1100);
    row.channel = LEDC_CH_DOME;
    const SeqBodyStepPlan plan =
        sequenceBodyStepPlan(bodyAction("doorFL", BODY_SHAPE_OPEN, 100, 0), &row);
    TEST_ASSERT_EQUAL_INT(CONSOLE_REASON_NOT_IN_THIS_BUILD, (int)plan.reason);
    TEST_ASSERT_FALSE(plan.drive);
}

void test_each_ledc_servo_channel_resolves_to_its_arm_id() {
    static const uint8_t kChannels[] = {LEDC_CH_ARM1, LEDC_CH_ARM2, LEDC_CH_AUX1,
                                        LEDC_CH_AUX2, LEDC_CH_AUX3};
    for (uint8_t armId = 0; armId < 5; ++armId) {
        ServoOutputRow row = drivingRow("doorFL", 1900, 1100);
        row.channel = kChannels[armId];
        const SeqBodyStepPlan plan =
            sequenceBodyStepPlan(bodyAction("doorFL", BODY_SHAPE_OPEN, 100, 0), &row);
        TEST_ASSERT_TRUE(plan.drive);
        TEST_ASSERT_EQUAL_UINT8(armId, plan.armId);
    }
    uint8_t out = 0xAA;
    TEST_ASSERT_FALSE(servo_ledc_channel_to_arm_id(LEDC_CH_DOME, &out));
    TEST_ASSERT_EQUAL_UINT8(0xAA, out);  // untouched on a miss
}

// -----------------------------------------------------------------------------
// Routing
// -----------------------------------------------------------------------------

void test_dispatch_step_core_routes_a_body_move() {
    const SeqAction act = bodyAction("doorFL", BODY_SHAPE_OPEN, 100, 0);
    const SequenceDispatcherStepActions decision = sequenceDispatcherStep(act, 1000);
    TEST_ASSERT_EQUAL_INT(SEQ_DISPATCH_BODY_MOVE, (int)decision.target);
}

// -----------------------------------------------------------------------------
int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();

    RUN_TEST(test_open_over_the_whole_throw_lands_on_the_open_end);
    RUN_TEST(test_close_over_the_whole_throw_lands_on_the_close_end);
    RUN_TEST(test_half_throw_is_the_same_place_for_either_shape);
    RUN_TEST(test_partial_travel_is_measured_against_this_parts_own_throw);
    RUN_TEST(test_reversed_pair_travels_the_other_way);
    RUN_TEST(test_flutter_resolves_where_an_open_does);
    RUN_TEST(test_the_rows_component_band_bounds_the_target);

    RUN_TEST(test_part_no_output_claims_reports_part_not_assigned);
    RUN_TEST(test_part_the_catalog_never_declared_is_not_reported_as_unwired);
    RUN_TEST(test_row_addressed_to_a_non_servo_channel_cannot_be_driven);
    RUN_TEST(test_each_ledc_servo_channel_resolves_to_its_arm_id);

    RUN_TEST(test_dispatch_step_core_routes_a_body_move);

    return UNITY_END();
}
