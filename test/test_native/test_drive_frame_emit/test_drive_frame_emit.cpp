// =============================================================================
// test/test_native/test_drive_frame_emit/test_drive_frame_emit.cpp
//
// Drive zero-frame continuity (ADR 0014 step) regression test.
// Verifies that the drive tick decision step always decides to emit a frame
// regardless of failsafe state or arbiter output.
// SAFETY: The zero-frame rule requires the hoverboard receive a frame every
// tick to maintain motor control and prevent drift if input is stalled or
// failsafe is active.
// =============================================================================

#include <stdint.h>

#include <unity.h>

#include "drive_frame_emit.h"

void setUp() {}
void tearDown() {}

// Nominal state: failsafe inactive, normal arbiter output
void test_drive_tick_decide_normal_state() {
    DriveTickInputs in{.failsafeActive = false, .arbiterSpeed = 100, .arbiterSteer = 50};
    DriveTickActions actions = driveTickDecide(in);
    TEST_ASSERT_TRUE(actions.shouldEmitFrame);
    TEST_ASSERT_EQUAL_INT16(100, actions.speed);
    TEST_ASSERT_EQUAL_INT16(50, actions.steer);
}

// Failsafe ACTIVE: speed/steer already zeroed by arbiter, but FRAME STILL EMITTED
void test_drive_tick_decide_failsafe_active() {
    DriveTickInputs in{.failsafeActive = true, .arbiterSpeed = 0, .arbiterSteer = 0};
    DriveTickActions actions = driveTickDecide(in);
    TEST_ASSERT_TRUE(actions.shouldEmitFrame);
    TEST_ASSERT_EQUAL_INT16(0, actions.speed);
    TEST_ASSERT_EQUAL_INT16(0, actions.steer);
}

// Negative speed/steer: decision is still to emit
void test_drive_tick_decide_negative_motion() {
    DriveTickInputs in{.failsafeActive = false, .arbiterSpeed = -200, .arbiterSteer = -75};
    DriveTickActions actions = driveTickDecide(in);
    TEST_ASSERT_TRUE(actions.shouldEmitFrame);
    TEST_ASSERT_EQUAL_INT16(-200, actions.speed);
    TEST_ASSERT_EQUAL_INT16(-75, actions.steer);
}

// Edge case: maximum values with failsafe active (zero'd output)
void test_drive_tick_decide_max_values_with_failsafe() {
    DriveTickInputs in{.failsafeActive = true, .arbiterSpeed = 0, .arbiterSteer = 0};
    DriveTickActions actions = driveTickDecide(in);
    TEST_ASSERT_TRUE(actions.shouldEmitFrame);
}

// Systematic sweep: all failsafe states, sample speed values
void test_drive_tick_decide_all_state_combinations() {
    // The zero-frame rule means shouldEmitFrame is ALWAYS true
    int16_t testSpeeds[] = {-500, -250, -1, 0, 1, 250, 500};
    int16_t testSteers[] = {-360, 0, 360};

    for (size_t fs = 0; fs < 2; ++fs) {
        bool failsafeActive = (fs == 1);
        for (size_t s = 0; s < 7; ++s) {
            for (size_t st = 0; st < 3; ++st) {
                DriveTickInputs in{
                    .failsafeActive = failsafeActive,
                    .arbiterSpeed = testSpeeds[s],
                    .arbiterSteer = testSteers[st],
                };
                DriveTickActions actions = driveTickDecide(in);
                TEST_ASSERT_TRUE(actions.shouldEmitFrame);  // MUST always emit
                TEST_ASSERT_EQUAL_INT16(testSpeeds[s], actions.speed);
                TEST_ASSERT_EQUAL_INT16(testSteers[st], actions.steer);
            }
        }
    }
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_drive_tick_decide_normal_state);
    RUN_TEST(test_drive_tick_decide_failsafe_active);
    RUN_TEST(test_drive_tick_decide_negative_motion);
    RUN_TEST(test_drive_tick_decide_max_values_with_failsafe);
    RUN_TEST(test_drive_tick_decide_all_state_combinations);
    return UNITY_END();
}
