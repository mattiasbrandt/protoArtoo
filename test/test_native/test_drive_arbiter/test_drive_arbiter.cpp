// =============================================================================
// test/test_native/test_drive_arbiter/test_drive_arbiter.cpp
//
// Native unit tests for Drive Output Arbiter.
// Tests arbitration logic, timeout handling, and failsafe interaction.
// =============================================================================
#include <unity.h>

#include "drive_arbiter.h"
#include "failsafe_gate.h"
#include "robot_state.h"

// =============================================================================
// Test fixtures and mocks
// =============================================================================

// Mock spinlock for native testing
int mockMux = 0;

// =============================================================================
// Test setup and teardown
// =============================================================================

void setUp() {
    failsafeInit(&mockMux);
    driveArbiterInit(&mockMux);
    driveArbiterReset();
}

void tearDown() {
}

// =============================================================================
// Helper function to verify output
// =============================================================================

void assertDriveOutput(DriveOutput out,
                       int16_t expectedSpeed,
                       int16_t expectedSteer,
                       bool expectedFailsafe,
                       DriveSource expectedSource,
                       bool expectedWebTimedOut = false,
                       bool expectedRcTimedOut = false) {
    TEST_ASSERT_EQUAL_INT16(expectedSpeed, out.speed);
    TEST_ASSERT_EQUAL_INT16(expectedSteer, out.steer);
    TEST_ASSERT_EQUAL(expectedFailsafe, out.failsafeActive);
    TEST_ASSERT_EQUAL(expectedWebTimedOut, out.webTimedOut);
    TEST_ASSERT_EQUAL(expectedRcTimedOut, out.rcTimedOut);
    TEST_ASSERT_EQUAL_INT((int)expectedSource, (int)out.activeSource);
}

// =============================================================================
// Test cases
// =============================================================================

void test_no_source_is_zeroed() {
    // No command submitted yet - should output zero
    DriveArbiterConfig cfg = {.speedLimitMax = 600, .webDriveTimeoutMs = 500, .rcDriveTimeoutMs = 5000};
    DriveOutput out = driveArbiterResolve(cfg, 1000);
    assertDriveOutput(out, 0, 0, false, DriveSource::RC);
}

void test_rc_source_only() {
    // RC command only - should output RC values
    driveArbiterSubmit(DriveSource::RC, 100, 50, 1000);

    DriveArbiterConfig cfg = {.speedLimitMax = 600, .webDriveTimeoutMs = 500, .rcDriveTimeoutMs = 5000};
    DriveOutput out = driveArbiterResolve(cfg, 1010);

    assertDriveOutput(out, 100, 50, false, DriveSource::RC);
}

void test_web_source_only() {
    // Web command only - should output web values
    driveArbiterSubmit(DriveSource::WEB_API, 200, 100, 1000);

    DriveArbiterConfig cfg = {.speedLimitMax = 600, .webDriveTimeoutMs = 500, .rcDriveTimeoutMs = 5000};
    DriveOutput out = driveArbiterResolve(cfg, 1010);

    assertDriveOutput(out, 200, 100, false, DriveSource::WEB_API);
}

void test_rc_overrides_stale_web() {
    // RC more recent than web - RC should win
    driveArbiterSubmit(DriveSource::WEB_API, 200, 100, 1000);
    driveArbiterSubmit(DriveSource::RC, 100, 50, 1100);  // RC is more recent

    DriveArbiterConfig cfg = {.speedLimitMax = 600, .webDriveTimeoutMs = 500, .rcDriveTimeoutMs = 5000};
    DriveOutput out = driveArbiterResolve(cfg, 1110);

    assertDriveOutput(out, 100, 50, false, DriveSource::RC);
}

void test_web_overrides_stale_rc() {
    // Web more recent than RC - web should win
    driveArbiterSubmit(DriveSource::RC, 100, 50, 1000);
    driveArbiterSubmit(DriveSource::WEB_API, 200, 100, 1100);  // Web is more recent

    DriveArbiterConfig cfg = {.speedLimitMax = 600, .webDriveTimeoutMs = 500, .rcDriveTimeoutMs = 5000};
    DriveOutput out = driveArbiterResolve(cfg, 1110);

    assertDriveOutput(out, 200, 100, false, DriveSource::WEB_API);
}

void test_web_timeout_after_threshold() {
    // Web command submitted, then timeout threshold exceeded
    driveArbiterSubmit(DriveSource::WEB_API, 200, 100, 1000);

    DriveArbiterConfig cfg = {.speedLimitMax = 600, .webDriveTimeoutMs = 500, .rcDriveTimeoutMs = 5000};

    // Just before timeout
    DriveOutput out1 = driveArbiterResolve(cfg, 1499);
    assertDriveOutput(out1, 200, 100, false, DriveSource::WEB_API);

    // Just after timeout — output is zeroed, without mutating FailsafeGate.
    DriveOutput out2 = driveArbiterResolve(cfg, 1501);
    TEST_ASSERT_TRUE(out2.failsafeActive);
    TEST_ASSERT_TRUE(out2.webTimedOut);
    TEST_ASSERT_EQUAL_INT16(0, out2.speed);
    TEST_ASSERT_EQUAL_INT16(0, out2.steer);
    TEST_ASSERT_FALSE(failsafeIsActive());
    TEST_ASSERT_FALSE(robotState.webDriveExpired);
}

void test_web_timeout_resolve_has_no_side_effects() {
    // Web timeout is reported by resolve(), but FailsafeGate is updated by DriveTask.
    driveArbiterSubmit(DriveSource::WEB_API, 200, 100, 1000);

    DriveArbiterConfig cfg = {.speedLimitMax = 600, .webDriveTimeoutMs = 500, .rcDriveTimeoutMs = 5000};

    uint32_t triggerCountBefore = robotState.failsafeTriggerCount;

    // Calls after timeout return the same resolved state and do not trigger the gate.
    DriveOutput out1 = driveArbiterResolve(cfg, 1501);
    TEST_ASSERT_TRUE(out1.failsafeActive);
    TEST_ASSERT_TRUE(out1.webTimedOut);
    TEST_ASSERT_FALSE(failsafeIsActive());
    TEST_ASSERT_EQUAL_UINT32(triggerCountBefore, robotState.failsafeTriggerCount);

    DriveOutput out2 = driveArbiterResolve(cfg, 2000);
    TEST_ASSERT_TRUE(out2.failsafeActive);
    TEST_ASSERT_TRUE(out2.webTimedOut);
    TEST_ASSERT_FALSE(failsafeIsActive());
    TEST_ASSERT_EQUAL_UINT32(triggerCountBefore, robotState.failsafeTriggerCount);

    // New web command is fresh again.
    driveArbiterSubmit(DriveSource::WEB_API, 200, 100, 2500);

    DriveOutput out3 = driveArbiterResolve(cfg, 2510);
    TEST_ASSERT_FALSE(out3.failsafeActive);
    assertDriveOutput(out3, 200, 100, false, DriveSource::WEB_API);
}

void test_rc_does_not_timeout_within_window() {
    // RC command should not timeout when within the configured window
    driveArbiterSubmit(DriveSource::RC, 100, 50, 1000);

    DriveArbiterConfig cfg = {.speedLimitMax = 600, .webDriveTimeoutMs = 500, .rcDriveTimeoutMs = 5000};

    // RC is 4 seconds old (within 5000ms window)
    DriveOutput out = driveArbiterResolve(cfg, 5000);
    assertDriveOutput(out, 100, 50, false, DriveSource::RC);
}

void test_speed_limit_clamping() {
    // Speed/steer should be clamped to ±speedLimitMax
    driveArbiterSubmit(DriveSource::RC, 1000, -800, 1000);  // Over limits

    DriveArbiterConfig cfg = {.speedLimitMax = 600, .webDriveTimeoutMs = 500, .rcDriveTimeoutMs = 5000};
    DriveOutput out = driveArbiterResolve(cfg, 1010);

    assertDriveOutput(out, 600, -600, false, DriveSource::RC);
}

void test_failsafe_active_zeros_output() {
    // When failsafe is active, output should be zeroed regardless of command
    driveArbiterSubmit(DriveSource::RC, 100, 50, 1000);
    failsafeTrigger(FailsafeLayer::SBUS_HW);  // Trigger a failsafe layer

    DriveArbiterConfig cfg = {.speedLimitMax = 600, .webDriveTimeoutMs = 500, .rcDriveTimeoutMs = 5000};
    DriveOutput out = driveArbiterResolve(cfg, 1010);

    assertDriveOutput(out, 0, 0, true, DriveSource::RC);
    TEST_ASSERT_TRUE(out.failsafeActive);

    // Clean up for next test
    failsafeClear(FailsafeLayer::SBUS_HW);
}

void test_rc_fallback_when_web_times_out() {
    // When web times out, failsafe is triggered and output is zeroed
    // RC is still the active source, but output is zero because failsafe is active
    driveArbiterSubmit(DriveSource::RC, 100, 50, 1000);
    driveArbiterSubmit(DriveSource::WEB_API, 200, 100, 1100);

    DriveArbiterConfig cfg = {.speedLimitMax = 600, .webDriveTimeoutMs = 500, .rcDriveTimeoutMs = 5000};

    // Web times out — failsafe is triggered, output is zeroed
    // RC is the active source (most recent non-timed-out), but failsafe overrides output
    DriveOutput out = driveArbiterResolve(cfg, 1601);
    TEST_ASSERT_TRUE(out.failsafeActive);
    TEST_ASSERT_EQUAL_INT16(0, out.speed);
    TEST_ASSERT_EQUAL_INT16(0, out.steer);
    TEST_ASSERT_EQUAL_INT((int)DriveSource::RC, (int)out.activeSource);
}

void test_millis_overflow_handling() {
    // Test that timeout calculation handles millis() overflow correctly
    driveArbiterSubmit(DriveSource::WEB_API, 200, 100, 0xFFFFFFF0);  // Near overflow

    DriveArbiterConfig cfg = {.speedLimitMax = 600, .webDriveTimeoutMs = 500, .rcDriveTimeoutMs = 5000};

    // nowMs wrapped around to 100 (after overflow)
    // Elapsed = 100 - 0xFFFFFFF0 = 0x110 = 272ms < 500ms timeout
    DriveOutput out1 = driveArbiterResolve(cfg, 100);
    assertDriveOutput(out1, 200, 100, false, DriveSource::WEB_API);

    // Now 600ms has elapsed (should timeout)
    DriveOutput out2 = driveArbiterResolve(cfg, 0xFFFFFFF0 + 600);
    assertDriveOutput(out2, 0, 0, true, DriveSource::RC, true);
}

void test_web_zero_command() {
    // Web can submit zero (stop) command
    driveArbiterSubmit(DriveSource::WEB_API, 0, 0, 1000);

    DriveArbiterConfig cfg = {.speedLimitMax = 600, .webDriveTimeoutMs = 500, .rcDriveTimeoutMs = 5000};
    DriveOutput out = driveArbiterResolve(cfg, 1010);

    assertDriveOutput(out, 0, 0, false, DriveSource::WEB_API);
}

void test_source_priority_at_same_timestamp() {
    // If both sources have the same timestamp, they should be equal; RC slightly wins by convention
    // (In practice, timestamps are unlikely to be identical, but we test the edge case)
    driveArbiterSubmit(DriveSource::WEB_API, 200, 100, 1000);
    driveArbiterSubmit(DriveSource::RC, 100, 50, 1000);  // Same timestamp

    DriveArbiterConfig cfg = {.speedLimitMax = 600, .webDriveTimeoutMs = 500, .rcDriveTimeoutMs = 5000};
    DriveOutput out = driveArbiterResolve(cfg, 1010);

    // RC should win due to >= in the comparison
    assertDriveOutput(out, 100, 50, false, DriveSource::RC);
}

void test_negative_values() {
    // Test negative speed/steer values
    driveArbiterSubmit(DriveSource::RC, -300, -200, 1000);

    DriveArbiterConfig cfg = {.speedLimitMax = 600, .webDriveTimeoutMs = 500, .rcDriveTimeoutMs = 5000};
    DriveOutput out = driveArbiterResolve(cfg, 1010);

    assertDriveOutput(out, -300, -200, false, DriveSource::RC);
}

void test_active_timestamp_reflects_winner_not_last_submitted() {
    // RC submits at t=1000, web submits later at t=1100 (web is last submitted).
    // Web then times out — RC wins arbitration.
    // activeTimestampMs must reflect RC's timestamp (1000), not web's (1100).
    driveArbiterSubmit(DriveSource::RC, 300, 0, 1000);
    driveArbiterSubmit(DriveSource::WEB_API, 500, 0, 1100);

    DriveArbiterConfig cfg = {.speedLimitMax = 600, .webDriveTimeoutMs = 500, .rcDriveTimeoutMs = 5000};

    // Web timed out (age = 1700 - 1100 = 600 > 500ms). RC wins.
    DriveOutput out = driveArbiterResolve(cfg, 1700);
    TEST_ASSERT_TRUE(out.failsafeActive);
    TEST_ASSERT_TRUE(out.webTimedOut);
    TEST_ASSERT_EQUAL_INT((int)DriveSource::RC, (int)out.activeSource);
    TEST_ASSERT_EQUAL_UINT32(1000, out.activeTimestampMs);  // RC's timestamp, not web's
}

void test_active_timestamp_follows_winning_source() {
    // RC wins — activeTimestampMs must be RC's submit timestamp.
    driveArbiterSubmit(DriveSource::RC, 300, 0, 2000);
    driveArbiterSubmit(DriveSource::WEB_API, 500, 0, 1900);  // RC more recent

    DriveArbiterConfig cfg = {.speedLimitMax = 600, .webDriveTimeoutMs = 500, .rcDriveTimeoutMs = 5000};
    DriveOutput out = driveArbiterResolve(cfg, 2010);

    assertDriveOutput(out, 300, 0, false, DriveSource::RC);
    TEST_ASSERT_EQUAL_UINT32(2000, out.activeTimestampMs);

    // Now web submits more recently — web wins.
    driveArbiterReset();
    driveArbiterSubmit(DriveSource::RC, 300, 0, 3000);
    driveArbiterSubmit(DriveSource::WEB_API, 500, 0, 3100);  // Web more recent

    DriveOutput out2 = driveArbiterResolve(cfg, 3110);
    assertDriveOutput(out2, 500, 0, false, DriveSource::WEB_API);
    TEST_ASSERT_EQUAL_UINT32(3100, out2.activeTimestampMs);
}

void test_rc_timeout_stale_rc_plus_valid_web() {
    // Stale RC + valid web: web should win, not RC
    driveArbiterSubmit(DriveSource::RC, 100, 50, 1000);     // RC at t=1000
    driveArbiterSubmit(DriveSource::WEB_API, 200, 100, 2000); // Web at t=2000

    // At t=3500: RC is 2500ms old (beyond 2000ms timeout), Web is 1500ms old (within 2000ms timeout)
    // Web should win
    DriveArbiterConfig cfg = {.speedLimitMax = 600, .webDriveTimeoutMs = 2000, .rcDriveTimeoutMs = 2000};
    DriveOutput out = driveArbiterResolve(cfg, 3500);

    // Web should win, not stale RC; rcTimedOut reflects RC timeout status
    assertDriveOutput(out, 200, 100, false, DriveSource::WEB_API, false, true);
}

void test_rc_timeout_stale_rc_no_web() {
    // Stale RC only, no valid web: output should be zero, rcTimedOut should be true
    driveArbiterSubmit(DriveSource::RC, 100, 50, 1000);

    DriveArbiterConfig cfg = {.speedLimitMax = 600, .webDriveTimeoutMs = 2000, .rcDriveTimeoutMs = 1000};

    // At t=2500: RC is 1500ms old (beyond 1000ms timeout)
    DriveOutput out = driveArbiterResolve(cfg, 2500);

    // Output should be zero
    TEST_ASSERT_EQUAL_INT16(0, out.speed);
    TEST_ASSERT_EQUAL_INT16(0, out.steer);
    // rcTimedOut should be true for diagnostics
    TEST_ASSERT_TRUE(out.rcTimedOut);
}

void test_rc_timeout_fresh_rc_wins() {
    // Fresh RC within timeout: should win
    driveArbiterSubmit(DriveSource::RC, 100, 50, 1000);

    DriveArbiterConfig cfg = {.speedLimitMax = 600, .webDriveTimeoutMs = 2000, .rcDriveTimeoutMs = 2000};

    // At t=2500: RC is 1500ms old (within 2000ms timeout)
    DriveOutput out = driveArbiterResolve(cfg, 2500);

    assertDriveOutput(out, 100, 50, false, DriveSource::RC);
}

void test_rc_timeout_age_boundary() {
    // RC at exact timeout boundary
    driveArbiterSubmit(DriveSource::RC, 100, 50, 1000);

    DriveArbiterConfig cfg = {.speedLimitMax = 600, .webDriveTimeoutMs = 2000, .rcDriveTimeoutMs = 1000};

    // At t=2000: RC is exactly 1000ms old (at boundary, should still be valid)
    DriveOutput out1 = driveArbiterResolve(cfg, 2000);
    assertDriveOutput(out1, 100, 50, false, DriveSource::RC);

    // At t=2001: RC is 1001ms old (just past boundary, should timeout)
    DriveOutput out2 = driveArbiterResolve(cfg, 2001);
    TEST_ASSERT_EQUAL_INT16(0, out2.speed);
    TEST_ASSERT_EQUAL_INT16(0, out2.steer);
    TEST_ASSERT_TRUE(out2.rcTimedOut);
}

void test_rc_timeout_zero_timestamp_invalid() {
    // RC with zero timestamp should never be valid
    driveArbiterSubmit(DriveSource::RC, 100, 50, 0);  // Zero timestamp

    DriveArbiterConfig cfg = {.speedLimitMax = 600, .webDriveTimeoutMs = 2000, .rcDriveTimeoutMs = 2000};
    DriveOutput out = driveArbiterResolve(cfg, 1000);

    // Should be zero (no valid source)
    TEST_ASSERT_EQUAL_INT16(0, out.speed);
    TEST_ASSERT_EQUAL_INT16(0, out.steer);
}

void test_rc_timeout_wrap_around() {
    // RC timestamp near UINT32_MAX, nowMs wrapped to small value
    driveArbiterSubmit(DriveSource::RC, 100, 50, 0xFFFFFE00);  // Near max

    DriveArbiterConfig cfg = {.speedLimitMax = 600, .webDriveTimeoutMs = 2000, .rcDriveTimeoutMs = 1000};

    // After overflow: nowMs=256, elapsed = 256 - 0xFFFFFE00 = 512ms (within 1000ms timeout)
    DriveOutput out1 = driveArbiterResolve(cfg, 256);
    assertDriveOutput(out1, 100, 50, false, DriveSource::RC);

    // Much later: elapsed = 0xFFFFFE00 + 1500 - 0xFFFFFE00 = 1500ms (beyond 1000ms timeout)
    DriveOutput out2 = driveArbiterResolve(cfg, 0xFFFFFE00 + 1500);
    TEST_ASSERT_EQUAL_INT16(0, out2.speed);
    TEST_ASSERT_TRUE(out2.rcTimedOut);
}

void test_rc_web_timeout_independent() {
    // RC timeout and web timeout are independent
    driveArbiterSubmit(DriveSource::RC, 100, 50, 1000);
    driveArbiterSubmit(DriveSource::WEB_API, 200, 100, 1500);

    // RC timeout = 1000ms, Web timeout = 500ms
    DriveArbiterConfig cfg = {.speedLimitMax = 600, .webDriveTimeoutMs = 500, .rcDriveTimeoutMs = 1000};

    // At t=2100: RC is 1100ms old (stale), Web is 600ms old (stale)
    // Both timed out — output zero
    DriveOutput out = driveArbiterResolve(cfg, 2100);
    TEST_ASSERT_EQUAL_INT16(0, out.speed);
    TEST_ASSERT_EQUAL_INT16(0, out.steer);
    // Both should be marked as timed out
    TEST_ASSERT_TRUE(out.rcTimedOut);
    TEST_ASSERT_TRUE(out.webTimedOut);
}

// =============================================================================
// Test runner
// =============================================================================

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_no_source_is_zeroed);
    RUN_TEST(test_rc_source_only);
    RUN_TEST(test_web_source_only);
    RUN_TEST(test_rc_overrides_stale_web);
    RUN_TEST(test_web_overrides_stale_rc);
    RUN_TEST(test_web_timeout_after_threshold);
    RUN_TEST(test_web_timeout_resolve_has_no_side_effects);
    RUN_TEST(test_rc_does_not_timeout_within_window);
    RUN_TEST(test_speed_limit_clamping);
    RUN_TEST(test_failsafe_active_zeros_output);
    RUN_TEST(test_rc_fallback_when_web_times_out);
    RUN_TEST(test_millis_overflow_handling);
    RUN_TEST(test_web_zero_command);
    RUN_TEST(test_source_priority_at_same_timestamp);
    RUN_TEST(test_negative_values);
    RUN_TEST(test_active_timestamp_reflects_winner_not_last_submitted);
    RUN_TEST(test_active_timestamp_follows_winning_source);
    RUN_TEST(test_rc_timeout_stale_rc_plus_valid_web);
    RUN_TEST(test_rc_timeout_stale_rc_no_web);
    RUN_TEST(test_rc_timeout_fresh_rc_wins);
    RUN_TEST(test_rc_timeout_age_boundary);
    RUN_TEST(test_rc_timeout_zero_timestamp_invalid);
    RUN_TEST(test_rc_timeout_wrap_around);
    RUN_TEST(test_rc_web_timeout_independent);

    return UNITY_END();
}
