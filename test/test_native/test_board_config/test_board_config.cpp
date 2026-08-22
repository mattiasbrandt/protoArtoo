// =============================================================================
// test/test_native/test_board_config/test_board_config.cpp
//
// Verify board variant and chip target abstraction (ADR 0028).
//
// ADR 0028: Two-layer board abstraction. Ensures that PA_BOARD is properly
// defined and maps to the correct chip target. These are compile-time checks,
// verified via successful compilation and basic value assertions.
// =============================================================================
#include <unity.h>
#include "config.h"

void setUp() {
}

void tearDown() {
}

// Verify that PA_BOARD is defined (checked at compile time via config.h #error)
void test_pa_board_is_defined() {
    // If PA_BOARD was not defined, config.h would have errored during inclusion.
    // This test passes simply by compiling.
    TEST_PASS();
}

// Verify that PA_BOARD has a valid value (artoo-esp32 for native tests)
void test_pa_board_value_is_valid() {
    TEST_ASSERT_EQUAL_INT(PA_BOARD, PA_BOARD_ARTOO_ESP32);
}

// Verify that chip target is correctly mapped from board variant
void test_chip_target_mapped_for_artoo_esp32() {
#if PA_BOARD == PA_BOARD_ARTOO_ESP32
    // PA_CHIP_TARGET_ESP32 should be defined
    TEST_ASSERT_EQUAL_INT(PA_CHIP_TARGET_ESP32, 1);
#else
    TEST_FAIL_MESSAGE("Test environment should define PA_BOARD_ARTOO_ESP32 for native tests");
#endif
}

// Verify that pin definitions exist and are non-zero
void test_pins_are_defined() {
    // Sample pins from each category to verify board-specific pin map is loaded
    TEST_ASSERT_GREATER_THAN(0, PIN_HOVERBOARD_TX);
    TEST_ASSERT_GREATER_THAN(0, PIN_HOVERBOARD_RX);
    TEST_ASSERT_GREATER_THAN(0, PIN_DOME_TX);
    TEST_ASSERT_GREATER_THAN(0, PIN_DOME_RX);
    TEST_ASSERT_GREATER_THAN(0, PIN_I2C_SCL);
    TEST_ASSERT_GREATER_THAN(0, PIN_I2C_SDA);
}

// Verify that servo pins and LED functions are available
void test_servo_and_led_config_available() {
    TEST_ASSERT_GREATER_THAN(0, PIN_ARM1_SERVO);
    TEST_ASSERT_GREATER_THAN(0, PIN_ARM2_SERVO);
    TEST_ASSERT_TRUE(auxLedPinSettingValid(AUX_LED_PIN_DISABLED));
    TEST_ASSERT_TRUE(auxLedPinSettingValid(AUX_LED_PIN_AUX1));
    TEST_ASSERT_TRUE(auxLedPinSettingValid(AUX_LED_PIN_AUX2));
    TEST_ASSERT_TRUE(auxLedPinSettingValid(AUX_LED_PIN_AUX3));
    TEST_ASSERT_FALSE(auxLedPinSettingValid(AUX_LED_PIN_MAX + 1));
}

// Verify auxLedSelectionToGpio mapping
void test_aux_led_selection_to_gpio_mapping() {
    TEST_ASSERT_EQUAL_INT(auxLedSelectionToGpio(AUX_LED_PIN_DISABLED), 0);
    TEST_ASSERT_EQUAL_INT(auxLedSelectionToGpio(AUX_LED_PIN_AUX1), PIN_ARM3_SERVO);
    TEST_ASSERT_EQUAL_INT(auxLedSelectionToGpio(AUX_LED_PIN_AUX2), PIN_ARM4_SERVO);
    TEST_ASSERT_EQUAL_INT(auxLedSelectionToGpio(AUX_LED_PIN_AUX3), PIN_ARM5_SERVO);
}

// Every pin required by FireBeetle production consumers is also required on
// the shipping artoo-esp32 board. Expanding the production-owned inventory here
// prevents a newly guarded consumer from bypassing native coverage.
void test_required_consumer_pins_are_assigned_on_artoo_esp32() {
#define PA_FIREBEETLE_REQUIRED_PIN(pin, diagnostic) \
    TEST_ASSERT_NOT_EQUAL(PA_PIN_UNASSIGNED, pin);
#include "firebeetle_required_pins.inc"
#undef PA_FIREBEETLE_REQUIRED_PIN
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_pa_board_is_defined);
    RUN_TEST(test_pa_board_value_is_valid);
    RUN_TEST(test_chip_target_mapped_for_artoo_esp32);
    RUN_TEST(test_pins_are_defined);
    RUN_TEST(test_servo_and_led_config_available);
    RUN_TEST(test_aux_led_selection_to_gpio_mapping);
    RUN_TEST(test_required_consumer_pins_are_assigned_on_artoo_esp32);
    return UNITY_END();
}
