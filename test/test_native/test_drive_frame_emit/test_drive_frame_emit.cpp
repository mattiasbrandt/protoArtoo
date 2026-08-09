// =============================================================================
// test/test_native/test_drive_frame_emit/test_drive_frame_emit.cpp
//
// Drive zero-frame continuity (decision path) regression test.
// Verifies that the frame emission decision always returns true regardless
// of failsafe state or command freshness.
// SAFETY: The drive loop emits a frame to the hoverboard every tick
// regardless of failsafe state, command availability, or other factors.
// The hoverboard requires periodic frames to maintain motor control and
// prevent drift if input is stalled.
// =============================================================================

#include <stdint.h>

#include <unity.h>

#include "drive_frame_emit.h"

void setUp() {}
void tearDown() {}

// Frame should be emitted with failsafe inactive, command fresh
void test_drive_frame_emit_normal_state() {
    bool decision = driveFrameShouldEmit(false, true);
    TEST_ASSERT_TRUE(decision);
}

// Frame should be emitted with failsafe ACTIVE, command fresh
void test_drive_frame_emit_failsafe_active() {
    bool decision = driveFrameShouldEmit(true, true);
    TEST_ASSERT_TRUE(decision);
}

// Frame should be emitted with failsafe inactive, command STALE
void test_drive_frame_emit_command_stale() {
    bool decision = driveFrameShouldEmit(false, false);
    TEST_ASSERT_TRUE(decision);
}

// Frame should be emitted with BOTH failsafe active AND command stale
void test_drive_frame_emit_both_failsafe_and_stale() {
    bool decision = driveFrameShouldEmit(true, false);
    TEST_ASSERT_TRUE(decision);
}

// Test all combinations systematically
void test_drive_frame_emit_all_state_combinations() {
    // The zero-frame rule means emission is true for ALL combinations
    for (int failsafe = 0; failsafe <= 1; ++failsafe) {
        for (int cmdFresh = 0; cmdFresh <= 1; ++cmdFresh) {
            bool decision = driveFrameShouldEmit((bool)failsafe, (bool)cmdFresh);
            TEST_ASSERT_TRUE(decision);
        }
    }
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_drive_frame_emit_normal_state);
    RUN_TEST(test_drive_frame_emit_failsafe_active);
    RUN_TEST(test_drive_frame_emit_command_stale);
    RUN_TEST(test_drive_frame_emit_both_failsafe_and_stale);
    RUN_TEST(test_drive_frame_emit_all_state_combinations);
    return UNITY_END();
}
