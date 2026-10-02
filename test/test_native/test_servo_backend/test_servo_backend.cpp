// =============================================================================
// test/test_native/test_servo_backend/test_servo_backend.cpp
//
// The servo backend seam's mapping (include/servo_backend.h, #444).
//
// Every surface names an Output by its address now, and ServoTask turns that
// address into the slot it keeps the Output's state and mirror entry in. A
// mapping that sent one address to another Output's slot would move the wrong
// servo and report it as the right one, so what these pin is that the mapping
// is one-to-one over the Outputs that exist and answers "none" for every
// address that is not one.
// =============================================================================
#include <unity.h>

#include "servo_backend.h"

void setUp() {}
void tearDown() {}

// Each of the board's Outputs has its own slot, and that slot drives the same
// address back: no two Outputs share one, and none is lost on the way round.
void test_every_board_output_has_its_own_slot_and_back() {
    TEST_ASSERT_EQUAL_UINT8(BOARD_OUTPUT_COUNT + PCA9685_CHANNEL_COUNT, SERVO_OUTPUT_SLOT_COUNT);
    bool seen[SERVO_OUTPUT_SLOT_COUNT] = {};
    for (size_t index = 0; index < BOARD_OUTPUT_COUNT; ++index) {
        const ServoOutputAddress output = boardOutputAddress(index);
        TEST_ASSERT_EQUAL_UINT8(SERVO_DRIVER_LEDC, output.driver);
        TEST_ASSERT_EQUAL_UINT8(BOARD_OUTPUTS[index].channel, output.channel);
        TEST_ASSERT_EQUAL(index, boardOutputIndexOf(output));

        const uint8_t slot = servoOutputSlotOf(output);
        TEST_ASSERT_TRUE(slot < SERVO_OUTPUT_SLOT_COUNT);
        TEST_ASSERT_FALSE(seen[slot]);
        seen[slot] = true;
        TEST_ASSERT_TRUE(servoOutputSlotAddress(slot) == output);
    }
}

// Nothing that is not one Output reaches a slot: the dome ESC's channel, the
// `both` broadcast (two Outputs), no Output, and a driver this image has no
// member for. Each would otherwise index ServoTask's state and its mirror.
void test_an_address_that_is_not_one_output_has_no_slot() {
    const ServoOutputAddress notOutputs[] = {
        {SERVO_DRIVER_LEDC, (uint8_t)LEDC_CH_DOME},
        {SERVO_DRIVER_LEDC, (uint8_t)LEDC_CH_MAX},
        SERVO_OUTPUT_BOTH_ARMS,
        SERVO_OUTPUT_NONE,
        {SERVO_DRIVER_COUNT, (uint8_t)LEDC_CH_ARM1},
    };
    for (const ServoOutputAddress& output : notOutputs) {
        TEST_ASSERT_EQUAL_UINT8(SERVO_OUTPUT_SLOT_NONE, servoOutputSlotOf(output));
        TEST_ASSERT_EQUAL(BOARD_OUTPUT_COUNT, boardOutputIndexOf(output));
    }
    TEST_ASSERT_TRUE(servoOutputSlotAddress(SERVO_OUTPUT_SLOT_COUNT) == SERVO_OUTPUT_NONE);
    TEST_ASSERT_TRUE(boardOutputAddress(BOARD_OUTPUT_COUNT) == SERVO_OUTPUT_NONE);
    TEST_ASSERT_TRUE(SERVO_OUTPUT_BOTH_ARMS != SERVO_OUTPUT_NONE);
}

// The catalogue row a driver reaches is that driver's, and a driver with no
// member answers none rather than reading past the table.
void test_a_driver_reaches_its_own_profile() {
    const ServoBackendProfile* ledc = servoBackendProfileOf(SERVO_DRIVER_LEDC);
    TEST_ASSERT_NOT_NULL(ledc);
    TEST_ASSERT_EQUAL_UINT8(SERVO_DRIVER_LEDC, ledc->driver);
    TEST_ASSERT_EQUAL_STRING("esp32_gpio_ledc", ledc->id);
    TEST_ASSERT_EQUAL_UINT8(BOARD_OUTPUT_COUNT, ledc->outputCount);
    TEST_ASSERT_FALSE(ledc->onABus);
    TEST_ASSERT_NULL(servoBackendProfileOf(SERVO_DRIVER_COUNT));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_every_board_output_has_its_own_slot_and_back);
    RUN_TEST(test_an_address_that_is_not_one_output_has_no_slot);
    RUN_TEST(test_a_driver_reaches_its_own_profile);
    return UNITY_END();
}
