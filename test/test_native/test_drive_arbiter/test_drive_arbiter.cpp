// =============================================================================
// test/test_native/test_drive_arbiter/test_drive_arbiter.cpp
//
// Native unit tests for Drive Output Arbiter.
// Tests arbitration logic, timeout handling, and failsafe interaction.
// =============================================================================
#include <unity.h>

#include "drive_arbiter.h"
#include "failsafe_gate.h"

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
                       DriveSource expectedSource) {
    TEST_ASSERT_EQUAL_INT16(expectedSpeed, out.speed);
    TEST_ASSERT_EQUAL_INT16(expectedSteer, out.steer);
    TEST_ASSERT_EQUAL(expectedFailsafe, out.failsafeActive);
    TEST_ASSERT_EQUAL_INT((int)expectedSource, (int)out.activeSource);
}

// =============================================================================
// Test cases
// =============================================================================

void test_no_source_is_zeroed() {
    // No command submitted yet - should output zero
    DriveArbiterConfig cfg = {.speedLimitMax = 600, .webDriveTimeoutMs = 500};
    DriveOutput out = driveArbiterResolve(cfg, 1000);
    assertDriveOutput(out, 0, 0, false, DriveSource::RC);
}

void test_rc_source_only() {
    // RC command only - should output RC values
    driveArbiterSubmit(DriveSource::RC, 100, 50, 1000);

    DriveArbiterConfig cfg = {.speedLimitMax = 600, .webDriveTimeoutMs = 500};
    DriveOutput out = driveArbiterResolve(cfg, 1010);

    assertDriveOutput(out, 100, 50, false, DriveSource::RC);
}

void test_web_source_only() {
    // Web command only - should output web values
    driveArbiterSubmit(DriveSource::WEB_API, 200, 100, 1000);

    DriveArbiterConfig cfg = {.speedLimitMax = 600, .webDriveTimeoutMs = 500};
    DriveOutput out = driveArbiterResolve(cfg, 1010);

    assertDriveOutput(out, 200, 100, false, DriveSource::WEB_API);
}

void test_rc_overrides_stale_web() {
    // RC more recent than web - RC should win
    driveArbiterSubmit(DriveSource::WEB_API, 200, 100, 1000);
    driveArbiterSubmit(DriveSource::RC, 100, 50, 1100);  // RC is more recent

    DriveArbiterConfig cfg = {.speedLimitMax = 600, .webDriveTimeoutMs = 500};
    DriveOutput out = driveArbiterResolve(cfg, 1110);

    assertDriveOutput(out, 100, 50, false, DriveSource::RC);
}

void test_web_overrides_stale_rc() {
    // Web more recent than RC - web should win
    driveArbiterSubmit(DriveSource::RC, 100, 50, 1000);
    driveArbiterSubmit(DriveSource::WEB_API, 200, 100, 1100);  // Web is more recent

    DriveArbiterConfig cfg = {.speedLimitMax = 600, .webDriveTimeoutMs = 500};
    DriveOutput out = driveArbiterResolve(cfg, 1110);

    assertDriveOutput(out, 200, 100, false, DriveSource::WEB_API);
}

void test_web_timeout_after_threshold() {
    // Web command submitted, then timeout threshold exceeded
    driveArbiterSubmit(DriveSource::WEB_API, 200, 100, 1000);

    DriveArbiterConfig cfg = {.speedLimitMax = 600, .webDriveTimeoutMs = 500};

    // Just before timeout
    DriveOutput out1 = driveArbiterResolve(cfg, 1499);
    assertDriveOutput(out1, 200, 100, false, DriveSource::WEB_API);

    // Just after timeout — failsafe should be triggered
    DriveOutput out2 = driveArbiterResolve(cfg, 1501);
    TEST_ASSERT_TRUE(out2.failsafeActive);
    TEST_ASSERT_EQUAL_INT16(0, out2.speed);
    TEST_ASSERT_EQUAL_INT16(0, out2.steer);
}

void test_web_timeout_only_triggers_once_per_episode() {
    // Web timeout should only trigger failsafe once per episode
    driveArbiterSubmit(DriveSource::WEB_API, 200, 100, 1000);

    DriveArbiterConfig cfg = {.speedLimitMax = 600, .webDriveTimeoutMs = 500};

    // First call after timeout — failsafe activates
    DriveOutput out1 = driveArbiterResolve(cfg, 1501);
    TEST_ASSERT_TRUE(out1.failsafeActive);

    // Second call — failsafe stays active (no new trigger)
    DriveOutput out2 = driveArbiterResolve(cfg, 2000);
    TEST_ASSERT_TRUE(out2.failsafeActive);

    // New web command resets the timeout episode
    failsafeClear(FailsafeLayer::WEB_TIMEOUT);
    driveArbiterSubmit(DriveSource::WEB_API, 200, 100, 2500);

    // Now fresh web command should work
    DriveOutput out3 = driveArbiterResolve(cfg, 2510);
    TEST_ASSERT_FALSE(out3.failsafeActive);
    assertDriveOutput(out3, 200, 100, false, DriveSource::WEB_API);
}

void test_rc_does_not_timeout() {
    // RC command should not timeout, no matter how old
    driveArbiterSubmit(DriveSource::RC, 100, 50, 1000);

    DriveArbiterConfig cfg = {.speedLimitMax = 600, .webDriveTimeoutMs = 500};

    // Very old RC command (5 seconds old)
    DriveOutput out = driveArbiterResolve(cfg, 6000);
    assertDriveOutput(out, 100, 50, false, DriveSource::RC);
}

void test_speed_limit_clamping() {
    // Speed/steer should be clamped to ±speedLimitMax
    driveArbiterSubmit(DriveSource::RC, 1000, -800, 1000);  // Over limits

    DriveArbiterConfig cfg = {.speedLimitMax = 600, .webDriveTimeoutMs = 500};
    DriveOutput out = driveArbiterResolve(cfg, 1010);

    assertDriveOutput(out, 600, -600, false, DriveSource::RC);
}

void test_failsafe_active_zeros_output() {
    // When failsafe is active, output should be zeroed regardless of command
    driveArbiterSubmit(DriveSource::RC, 100, 50, 1000);
    failsafeTrigger(FailsafeLayer::SBUS_HW);  // Trigger a failsafe layer

    DriveArbiterConfig cfg = {.speedLimitMax = 600, .webDriveTimeoutMs = 500};
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

    DriveArbiterConfig cfg = {.speedLimitMax = 600, .webDriveTimeoutMs = 500};

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

    DriveArbiterConfig cfg = {.speedLimitMax = 600, .webDriveTimeoutMs = 500};

    // nowMs wrapped around to 100 (after overflow)
    // Elapsed = 100 - 0xFFFFFFF0 = 0x110 = 272ms < 500ms timeout
    DriveOutput out1 = driveArbiterResolve(cfg, 100);
    assertDriveOutput(out1, 200, 100, false, DriveSource::WEB_API);

    // Now 600ms has elapsed (should timeout)
    DriveOutput out2 = driveArbiterResolve(cfg, 0xFFFFFFF0 + 600);
    assertDriveOutput(out2, 0, 0, true, DriveSource::RC);
}

void test_web_zero_command() {
    // Web can submit zero (stop) command
    driveArbiterSubmit(DriveSource::WEB_API, 0, 0, 1000);

    DriveArbiterConfig cfg = {.speedLimitMax = 600, .webDriveTimeoutMs = 500};
    DriveOutput out = driveArbiterResolve(cfg, 1010);

    assertDriveOutput(out, 0, 0, false, DriveSource::WEB_API);
}

void test_source_priority_at_same_timestamp() {
    // If both sources have the same timestamp, they should be equal; RC slightly wins by convention
    // (In practice, timestamps are unlikely to be identical, but we test the edge case)
    driveArbiterSubmit(DriveSource::WEB_API, 200, 100, 1000);
    driveArbiterSubmit(DriveSource::RC, 100, 50, 1000);  // Same timestamp

    DriveArbiterConfig cfg = {.speedLimitMax = 600, .webDriveTimeoutMs = 500};
    DriveOutput out = driveArbiterResolve(cfg, 1010);

    // RC should win due to >= in the comparison
    assertDriveOutput(out, 100, 50, false, DriveSource::RC);
}

void test_negative_values() {
    // Test negative speed/steer values
    driveArbiterSubmit(DriveSource::RC, -300, -200, 1000);

    DriveArbiterConfig cfg = {.speedLimitMax = 600, .webDriveTimeoutMs = 500};
    DriveOutput out = driveArbiterResolve(cfg, 1010);

    assertDriveOutput(out, -300, -200, false, DriveSource::RC);
}

void test_active_timestamp_reflects_winner_not_last_submitted() {
    // RC submits at t=1000, web submits later at t=1100 (web is last submitted).
    // Web then times out — RC wins arbitration.
    // activeTimestampMs must reflect RC's timestamp (1000), not web's (1100).
    driveArbiterSubmit(DriveSource::RC, 300, 0, 1000);
    driveArbiterSubmit(DriveSource::WEB_API, 500, 0, 1100);

    DriveArbiterConfig cfg = {.speedLimitMax = 600, .webDriveTimeoutMs = 500};

    // Web timed out (age = 1700 - 1100 = 600 > 500ms). RC wins.
    DriveOutput out = driveArbiterResolve(cfg, 1700);
    TEST_ASSERT_TRUE(out.failsafeActive);  // WEB_TIMEOUT failsafe active
    TEST_ASSERT_EQUAL_INT((int)DriveSource::RC, (int)out.activeSource);
    TEST_ASSERT_EQUAL_UINT32(1000, out.activeTimestampMs);  // RC's timestamp, not web's
}

void test_active_timestamp_follows_winning_source() {
    // RC wins — activeTimestampMs must be RC's submit timestamp.
    driveArbiterSubmit(DriveSource::RC, 300, 0, 2000);
    driveArbiterSubmit(DriveSource::WEB_API, 500, 0, 1900);  // RC more recent

    DriveArbiterConfig cfg = {.speedLimitMax = 600, .webDriveTimeoutMs = 500};
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
    RUN_TEST(test_web_timeout_only_triggers_once_per_episode);
    RUN_TEST(test_rc_does_not_timeout);
    RUN_TEST(test_speed_limit_clamping);
    RUN_TEST(test_failsafe_active_zeros_output);
    RUN_TEST(test_rc_fallback_when_web_times_out);
    RUN_TEST(test_millis_overflow_handling);
    RUN_TEST(test_web_zero_command);
    RUN_TEST(test_source_priority_at_same_timestamp);
    RUN_TEST(test_negative_values);
    RUN_TEST(test_active_timestamp_reflects_winner_not_last_submitted);
    RUN_TEST(test_active_timestamp_follows_winning_source);

    return UNITY_END();
}
