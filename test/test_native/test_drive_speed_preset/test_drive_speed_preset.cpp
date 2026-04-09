// =============================================================================
// test/test_native/test_drive_speed_preset/test_drive_speed_preset.cpp
//
// Native unit tests for drive speed preset helper logic.
// =============================================================================

#include <stdint.h>

#include <unity.h>

#include "drive_speed_preset.h"

void setUp() {
}

void tearDown() {
}

void test_next_speed_preset_cycles_slow_to_normal() {
    TEST_ASSERT_EQUAL_UINT8((uint8_t)SpeedPresetId::Normal,
                            (uint8_t)nextSpeedPreset(SpeedPresetId::Slow));
}

void test_next_speed_preset_cycles_normal_to_turbo() {
    TEST_ASSERT_EQUAL_UINT8((uint8_t)SpeedPresetId::Turbo,
                            (uint8_t)nextSpeedPreset(SpeedPresetId::Normal));
}

void test_next_speed_preset_cycles_turbo_to_slow() {
    TEST_ASSERT_EQUAL_UINT8((uint8_t)SpeedPresetId::Slow,
                            (uint8_t)nextSpeedPreset(SpeedPresetId::Turbo));
}

void test_resolve_speed_preset_for_limit_matches_unique_values() {
    SpeedPresetId preset = SpeedPresetId::Normal;
    TEST_ASSERT_TRUE(resolveSpeedPresetForLimit(350, 200, 350, 600, &preset));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)SpeedPresetId::Normal, (uint8_t)preset);
}

void test_resolve_speed_preset_for_limit_fails_when_no_match() {
    SpeedPresetId preset = SpeedPresetId::Turbo;
    TEST_ASSERT_FALSE(resolveSpeedPresetForLimit(401, 200, 350, 600, &preset));
}

void test_resolve_speed_preset_for_limit_rejects_duplicate_values() {
    SpeedPresetId preset = SpeedPresetId::Normal;
    TEST_ASSERT_FALSE(resolveSpeedPresetForLimit(200, 200, 200, 600, &preset));
}

void test_speed_preset_values_unique_contract() {
    TEST_ASSERT_TRUE(speedPresetValuesAreUnique(120, 240, 480));
    TEST_ASSERT_FALSE(speedPresetValuesAreUnique(120, 120, 480));
    TEST_ASSERT_FALSE(speedPresetValuesAreUnique(120, 240, 240));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_next_speed_preset_cycles_slow_to_normal);
    RUN_TEST(test_next_speed_preset_cycles_normal_to_turbo);
    RUN_TEST(test_next_speed_preset_cycles_turbo_to_slow);
    RUN_TEST(test_resolve_speed_preset_for_limit_matches_unique_values);
    RUN_TEST(test_resolve_speed_preset_for_limit_fails_when_no_match);
    RUN_TEST(test_resolve_speed_preset_for_limit_rejects_duplicate_values);
    RUN_TEST(test_speed_preset_values_unique_contract);
    return UNITY_END();
}
