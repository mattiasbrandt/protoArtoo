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

// Verify that PA_LOG_LEVEL is defined per-env (checked at compile time via config.h
// #error) and carries a value the logging tiers actually define.
//
// It used to default to DEBUG in config.h when unset, so an environment that forgot
// to declare it shipped verbose logging silently -- extra serial output, timing cost
// and flash, with no diagnostic. Every env now declares its own value (#244).
void test_pa_log_level_is_defined_and_in_range() {
    // If PA_LOG_LEVEL were undefined, config.h would have errored during inclusion.
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, PA_LOG_LEVEL);
    TEST_ASSERT_LESS_OR_EQUAL_INT(4, PA_LOG_LEVEL);
}

// Verify that PA_HEAP_PROFILE is defined per-env and is a Build Feature Flag value.
//
// It is consumed as `#if PA_HEAP_PROFILE`, where an undefined macro evaluates to 0
// silently -- the profiler would vanish from a build that meant to have it. ADR 0029
// requires such flags to be 0 or 1 and tested with #if (#244).
void test_pa_heap_profile_is_defined_as_zero_or_one() {
    TEST_ASSERT_TRUE(PA_HEAP_PROFILE == 0 || PA_HEAP_PROFILE == 1);
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

// Pin the artoo-esp32 SafetyMonitorTask stack at the value the shipping image
// has always had. The P4 branch of SAFETY_MONITOR_STACK_BYTES was raised to
// 4096 on measured evidence (#245); this asserts the ESP32 branch did not move
// with it, which is what keeps that change off the artoo image. Native tests
// compile with PA_BOARD_ARTOO_ESP32, so this is the only branch reachable here.
void test_safety_monitor_stack_unchanged_on_esp32() {
    TEST_ASSERT_EQUAL_UINT32(3072U, SAFETY_MONITOR_STACK_BYTES);
}

// Same guard for the two stacks #248 raised on the P4. Both ESP32 branches must
// stay at the values the shipping artoo image has always had; that is what keeps
// those changes off it.
void test_dome_and_aux_led_stacks_unchanged_on_esp32() {
    TEST_ASSERT_EQUAL_UINT32(3072U, DOME_TASK_STACK_BYTES);
    TEST_ASSERT_EQUAL_UINT32(4096U, AUX_LED_TASK_STACK_BYTES);
}

// Verify that pin definitions exist and are non-zero
void test_pins_are_defined() {
    // Sample pins from each category to verify board-specific pin map is loaded
    TEST_ASSERT_GREATER_THAN(0, PIN_DRIVE_TX);
    TEST_ASSERT_GREATER_THAN(0, PIN_DRIVE_RX);
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

// Verify that the drive backend capability gate is declared for this board
void test_drive_backend_capability_gate_is_declared() {
    TEST_ASSERT_EQUAL_INT(1, PA_CAP_DRIVE_BACKEND_HOVERBOARD);
}

// artoo-esp32 has three HP UART controllers and genuinely needs the share:
// UART0 console, UART1 drive, and one controller left for both the dome link
// and the audio module's RX. Pin the capability at 0 so a change made for the
// ESP32-P4 cannot flip this board onto a path its silicon cannot support --
// audio would try to open a controller that does not exist and go silent
// (#254). Native tests compile with PA_BOARD_ARTOO_ESP32, so this is the only
// branch reachable here; the P4 values are proven by the cross-board compiler
// probe in test/test_tools/test_board_uart_allocation.py.
void test_dedicated_audio_uart_capability_absent_on_artoo_esp32() {
    TEST_ASSERT_EQUAL_INT(0, PA_CAP_DEDICATED_AUDIO_UART);
}

// The allocation that capability describes, on this board: the audio module
// shares the dome link's controller, and the drive lane shares with neither.
void test_uart_controller_allocation_on_artoo_esp32() {
    TEST_ASSERT_EQUAL_UINT8(1, UART_PORT_DRIVE);
    TEST_ASSERT_EQUAL_UINT8(2, UART_PORT_DOME);
    TEST_ASSERT_EQUAL_UINT8(UART_PORT_DOME, UART_PORT_AUDIO);
    TEST_ASSERT_NOT_EQUAL(UART_PORT_DRIVE, UART_PORT_DOME);

    // Highest controller index the classic ESP32 exposes (SOC_UART_HP_NUM = 3).
    TEST_ASSERT_EQUAL_UINT8(2, UART_PORT_MAX);
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
    RUN_TEST(test_drive_backend_capability_gate_is_declared);
    RUN_TEST(test_dedicated_audio_uart_capability_absent_on_artoo_esp32);
    RUN_TEST(test_uart_controller_allocation_on_artoo_esp32);
    RUN_TEST(test_required_consumer_pins_are_assigned_on_artoo_esp32);
    RUN_TEST(test_pa_log_level_is_defined_and_in_range);
    RUN_TEST(test_pa_heap_profile_is_defined_as_zero_or_one);
    RUN_TEST(test_safety_monitor_stack_unchanged_on_esp32);
    RUN_TEST(test_dome_and_aux_led_stacks_unchanged_on_esp32);
    return UNITY_END();
}
