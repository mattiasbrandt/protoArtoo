// =============================================================================
// test/test_native/test_drive_web_timeout_bridge/test_drive_web_timeout_bridge.cpp
//
// Native unit tests for DriveTask's WEB_TIMEOUT FailsafeGate bridge.
// =============================================================================
#include <cstring>
#include <unity.h>

#include "drive.h"
#include "drive_arbiter.h"
#include "failsafe_gate.h"
#include "robot_state.h"

int mockMux = 0;

extern RobotState robotState;
extern portMUX_TYPE robotStateMux;

void setUp() {
    std::memset(&robotState, 0, sizeof(RobotState));
    robotStateMux = 0;
    failsafeInit(&robotStateMux);
    driveArbiterInit(&robotStateMux);
    driveArbiterReset();
}

void tearDown() {
}

void test_first_timed_out_tick_triggers_web_timeout_once() {
    bool webTimeoutLayerActive = false;

    driveSyncWebTimeoutFailsafe(true, &webTimeoutLayerActive);

    TEST_ASSERT_TRUE(webTimeoutLayerActive);
    TEST_ASSERT_TRUE(failsafeIsActive());
    TEST_ASSERT_TRUE(robotState.webDriveExpired);
    TEST_ASSERT_EQUAL_UINT32(1, robotState.failsafeTriggerCount);
    TEST_ASSERT_EQUAL_INT((int)FS_WEB_TIMEOUT, (int)robotState.failsafeLastTriggerSource);
}

void test_repeated_timed_out_ticks_do_not_increment_trigger_count() {
    bool webTimeoutLayerActive = false;

    driveSyncWebTimeoutFailsafe(true, &webTimeoutLayerActive);
    driveSyncWebTimeoutFailsafe(true, &webTimeoutLayerActive);
    driveSyncWebTimeoutFailsafe(true, &webTimeoutLayerActive);

    TEST_ASSERT_TRUE(webTimeoutLayerActive);
    TEST_ASSERT_TRUE(failsafeIsActive());
    TEST_ASSERT_TRUE(robotState.webDriveExpired);
    TEST_ASSERT_EQUAL_UINT32(1, robotState.failsafeTriggerCount);
}

void test_fresh_web_command_clears_web_timeout_via_submit_path() {
    bool webTimeoutLayerActive = false;
    DriveArbiterConfig cfg = {.speedLimitMax = 600, .webDriveTimeoutMs = 500};

    driveArbiterSubmit(DriveSource::WEB_API, 250, 40, 1000);
    DriveOutput timedOut = driveArbiterResolve(cfg, 1601);
    TEST_ASSERT_TRUE(timedOut.webTimedOut);

    driveSyncWebTimeoutFailsafe(timedOut.webTimedOut, &webTimeoutLayerActive);
    TEST_ASSERT_TRUE(webTimeoutLayerActive);
    TEST_ASSERT_TRUE(robotState.webDriveExpired);

    driveArbiterSubmit(DriveSource::WEB_API, 250, 40, 1700);
    DriveOutput fresh = driveArbiterResolve(cfg, 1710);

    TEST_ASSERT_FALSE(fresh.webTimedOut);
    TEST_ASSERT_FALSE(fresh.failsafeActive);
    TEST_ASSERT_FALSE(failsafeIsActive());
    TEST_ASSERT_FALSE(robotState.webDriveExpired);
}

void test_non_web_failsafe_still_zeroes_output_through_resolve() {
    DriveArbiterConfig cfg = {.speedLimitMax = 600, .webDriveTimeoutMs = 500};

    driveArbiterSubmit(DriveSource::RC, 300, -100, 1000);
    failsafeTrigger(FailsafeLayer::SBUS_HW);

    DriveOutput out = driveArbiterResolve(cfg, 1010);

    TEST_ASSERT_FALSE(out.webTimedOut);
    TEST_ASSERT_TRUE(out.failsafeActive);
    TEST_ASSERT_EQUAL_INT16(0, out.speed);
    TEST_ASSERT_EQUAL_INT16(0, out.steer);
    TEST_ASSERT_TRUE(failsafeIsActive());
    TEST_ASSERT_FALSE(robotState.webDriveExpired);
}

void test_null_layer_state_is_noop() {
    driveSyncWebTimeoutFailsafe(true, nullptr);

    TEST_ASSERT_FALSE(failsafeIsActive());
    TEST_ASSERT_FALSE(robotState.webDriveExpired);
    TEST_ASSERT_EQUAL_UINT32(0, robotState.failsafeTriggerCount);
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_first_timed_out_tick_triggers_web_timeout_once);
    RUN_TEST(test_repeated_timed_out_ticks_do_not_increment_trigger_count);
    RUN_TEST(test_fresh_web_command_clears_web_timeout_via_submit_path);
    RUN_TEST(test_non_web_failsafe_still_zeroes_output_through_resolve);
    RUN_TEST(test_null_layer_state_is_noop);

    return UNITY_END();
}
