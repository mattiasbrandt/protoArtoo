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
#include "servo_backend.h"  // boardOutputAddress(), boardOutputIndexOf()

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

// A light can fight nothing, so it never has an Output Release (CONTEXT.md
// "Output Release", #443) - even when a time is stored on its row from when it
// carried a servo. Every servo component lets go at the time its row holds.
void test_a_light_never_lets_go_and_a_servo_lets_go_at_its_time() {
    const ServoComponentType servos[] = {SERVO_COMP_NONE, SERVO_COMP_MG996R, SERVO_COMP_MG90S};
    for (const ServoComponentType component : servos) {
        ServoOutputRow row = {};
        row.component = component;
        row.release_ms = 2000;
        TEST_ASSERT_EQUAL_UINT16(2000, outputWireReleaseAfterMs(row));
    }
    ServoOutputRow light = {};
    light.component = SERVO_COMP_RGB;
    light.release_ms = 2000;
    TEST_ASSERT_EQUAL_UINT16(SERVO_RELEASE_MS_NEVER, outputWireReleaseAfterMs(light));
}

// One mapping: the board index these questions take and the Output Address the
// servo path speaks (#444) are the same Output read two ways, in both
// directions, and the dome ESC's channel is neither.
void test_board_index_and_address_are_one_mapping() {
    for (size_t index = 0; index < BOARD_OUTPUT_COUNT; ++index) {
        const ServoOutputAddress output = boardOutputAddress(index);
        TEST_ASSERT_EQUAL_UINT8(BOARD_OUTPUTS[index].channel, output.channel);
        TEST_ASSERT_EQUAL(index, boardOutputIndexOf(output));
    }
    TEST_ASSERT_EQUAL(BOARD_OUTPUT_COUNT,
                      boardOutputIndexOf({SERVO_DRIVER_LEDC, (uint8_t)LEDC_CH_DOME}));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_the_two_wires_used_below_are_what_the_board_says);
    RUN_TEST(test_every_combination_answers_as_the_tasks_did);
    RUN_TEST(test_an_unticked_strip_leaves_neither_side_driving_the_pin);
    RUN_TEST(test_an_index_past_the_table_answers_no);
    RUN_TEST(test_a_light_never_lets_go_and_a_servo_lets_go_at_its_time);
    RUN_TEST(test_board_index_and_address_are_one_mapping);
    return UNITY_END();
}
