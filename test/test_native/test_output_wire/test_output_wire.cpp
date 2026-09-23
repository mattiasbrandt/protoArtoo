// =============================================================================
// test/test_native/test_output_wire/test_output_wire.cpp
//
// What one Output may do (include/output_wire.h, #416).
//
// The three answers differ on purpose, and before #416 they lived in two task
// files the native build does not compile. What these pin is the behaviour the
// tasks had when the rules were moved: every component a row can record, ticked
// or not, on a wire the board allows a light on and on one it does not. The
// case that matters most is the one no test held before - a strip declared on
// a wire nobody ticked in, where neither LEDC nor the strip may drive the pin.
// =============================================================================
#include <unity.h>

#include "output_wire.h"

void setUp() {}
void tearDown() {}

// arm1 is a line no light may go on and aux1 is one that may, on every board
// (include/board_outputs.h). Checked here, so a table change that moves either
// fails as that and not as a rule change.
static constexpr size_t kNotLightCapable = 0;  // arm1
static constexpr size_t kLightCapable = 2;     // aux1

void test_the_two_wires_used_below_are_what_the_board_says() {
    TEST_ASSERT_FALSE(BOARD_OUTPUTS[kNotLightCapable].lightCapable);
    TEST_ASSERT_TRUE(BOARD_OUTPUTS[kLightCapable].lightCapable);
}

struct WireCase {
    size_t boardIndex;
    bool wired;
    ServoComponentType component;
    bool pinKeptForLight;  // ServoTask keeps LEDC off the pin
    bool stripDriven;      // AuxLedTask drives a strip on the wire
    bool centreable;       // a bulk centre moves the row
};

// Light Type x wired tick x lightCapable, every combination, with the answer
// each task gave at 8e49cc9f written out rather than recomputed.
static const WireCase kCases[] = {
    // Not light-capable (arm1).
    {kNotLightCapable, false, SERVO_COMP_NONE, false, false, true},
    {kNotLightCapable, false, SERVO_COMP_MG996R, false, false, true},
    {kNotLightCapable, false, SERVO_COMP_MG90S, false, false, true},
    {kNotLightCapable, false, SERVO_COMP_RGB, true, false, false},
    {kNotLightCapable, true, SERVO_COMP_NONE, false, false, true},
    {kNotLightCapable, true, SERVO_COMP_MG996R, false, false, true},
    {kNotLightCapable, true, SERVO_COMP_MG90S, false, false, true},
    {kNotLightCapable, true, SERVO_COMP_RGB, true, false, false},
    // Light-capable (aux1).
    {kLightCapable, false, SERVO_COMP_NONE, false, false, true},
    {kLightCapable, false, SERVO_COMP_MG996R, false, false, true},
    {kLightCapable, false, SERVO_COMP_MG90S, false, false, true},
    {kLightCapable, false, SERVO_COMP_RGB, true, false, false},
    {kLightCapable, true, SERVO_COMP_NONE, false, false, true},
    {kLightCapable, true, SERVO_COMP_MG996R, false, false, true},
    {kLightCapable, true, SERVO_COMP_MG90S, false, false, true},
    {kLightCapable, true, SERVO_COMP_RGB, true, true, false},
};

void test_every_combination_answers_as_the_tasks_did() {
    char msg[64];
    for (const WireCase& c : kCases) {
        snprintf(msg, sizeof(msg), "index %u wired %d component %u", (unsigned)c.boardIndex,
                 (int)c.wired, (unsigned)c.component);
        const OutputWireInputs in = {c.wired, c.component};
        ServoOutputRow row = {};
        row.component = c.component;
        TEST_ASSERT_EQUAL_MESSAGE(c.pinKeptForLight, outputWirePinKeptForLight(in, c.boardIndex),
                                  msg);
        TEST_ASSERT_EQUAL_MESSAGE(c.stripDriven, outputWireStripDriven(in, c.boardIndex), msg);
        TEST_ASSERT_EQUAL_MESSAGE(c.centreable, outputWireCentreable(row), msg);
    }
}

// The safe way round, said on its own: a strip declared on a light-capable
// wire that is not ticked in keeps LEDC off the pin AND is not driven, so
// nothing is on the pin at all.
void test_an_unticked_strip_leaves_neither_side_driving_the_pin() {
    const OutputWireInputs in = {false, SERVO_COMP_RGB};
    TEST_ASSERT_TRUE(outputWirePinKeptForLight(in, kLightCapable));
    TEST_ASSERT_FALSE(outputWireStripDriven(in, kLightCapable));
}

// An index past the table is no Output: nothing is kept for a light and no
// strip is driven, whatever the inputs say.
void test_an_index_past_the_table_answers_no() {
    const OutputWireInputs in = {true, SERVO_COMP_RGB};
    TEST_ASSERT_FALSE(outputWirePinKeptForLight(in, BOARD_OUTPUT_COUNT));
    TEST_ASSERT_FALSE(outputWireStripDriven(in, BOARD_OUTPUT_COUNT));
}

// One mapping: armId i is BOARD_OUTPUTS index i, and both directions of the
// armId <-> Output Address bridge agree with the table's channel for it.
void test_arm_id_index_and_address_are_one_mapping() {
    for (size_t index = 0; index < BOARD_OUTPUT_COUNT; ++index) {
        TEST_ASSERT_EQUAL_UINT8(BOARD_OUTPUTS[index].channel,
                                servo_arm_id_to_ledc_channel((uint8_t)index));
        uint8_t armId = 0xEE;
        TEST_ASSERT_TRUE(servo_ledc_channel_to_arm_id(BOARD_OUTPUTS[index].channel, &armId));
        TEST_ASSERT_EQUAL_UINT8(index, armId);
    }
    uint8_t untouched = 0xEE;
    TEST_ASSERT_FALSE(servo_ledc_channel_to_arm_id(LEDC_CH_DOME, &untouched));
    TEST_ASSERT_EQUAL_UINT8(0xEE, untouched);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_the_two_wires_used_below_are_what_the_board_says);
    RUN_TEST(test_every_combination_answers_as_the_tasks_did);
    RUN_TEST(test_an_unticked_strip_leaves_neither_side_driving_the_pin);
    RUN_TEST(test_an_index_past_the_table_answers_no);
    RUN_TEST(test_arm_id_index_and_address_are_one_mapping);
    return UNITY_END();
}
