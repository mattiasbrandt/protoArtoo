// =============================================================================
// test/test_native/test_rc_action_dispatcher/test_rc_action_dispatcher.cpp
//
// Unity tests for RcActionDispatcher pure dispatch logic.
// =============================================================================

#include <unity.h>
#include <cstring>

#include "rc_action_dispatcher.h"

static RcActionPayload buildPayload(RobotActionId target, const char* payload, bool pressed) {
    RcActionPayload p = {};
    p.target = target;
    p.bindingPayload = payload;
    p.pressed = pressed;
    p.randomSeed = 42;  // deterministic seed
    p.categories = {};
    p.estopActive = false;
    p.currentSleepMode = false;
    p.currentSpeedPreset = SpeedPresetId::Normal;
    return p;
}

void setUp(void) {}

void tearDown(void) {}

void test_none_action_returns_empty_result(void) {
    RcActionPayload p = buildPayload(ROBOT_ACTION_NONE, nullptr, false);
    RcActionResult res = rcDispatchAction(p);

    TEST_ASSERT_EQUAL_INT(0, res.audioTrack);
    TEST_ASSERT_EQUAL_INT(-1, res.servoIndex);
    TEST_ASSERT_FALSE(res.triggerEstop);
    TEST_ASSERT_FALSE(res.setSleep);
    TEST_ASSERT_EQUAL_CHAR('\0', res.marcduinoCmd[0]);
    TEST_ASSERT_EQUAL_CHAR('\0', res.domeTxCmd[0]);
}

void test_random_general_selects_track_in_range(void) {
    RcActionPayload p = buildPayload(SOUND_ACTION_RANDOM_GENERAL, nullptr, true);
    p.categories.gen_lo = 100;
    p.categories.gen_hi = 200;

    RcActionResult res = rcDispatchAction(p);

    TEST_ASSERT_NOT_EQUAL(0, res.audioTrack);
    TEST_ASSERT_TRUE(res.audioTrack >= 100 && res.audioTrack <= 200);
}

void test_random_sound_not_pressed_is_noop(void) {
    RcActionPayload p = buildPayload(SOUND_ACTION_RANDOM_GENERAL, nullptr, false);
    p.categories.gen_lo = 10;
    p.categories.gen_hi = 20;

    RcActionResult res = rcDispatchAction(p);

    TEST_ASSERT_EQUAL_INT(0, res.audioTrack);
}

void test_droid_seq_sets_dome_cmd(void) {
    RcActionPayload p = buildPayload(DROID_SEQ_SCREAM, nullptr, true);

    RcActionResult res = rcDispatchAction(p);

    TEST_ASSERT_EQUAL_STRING(":SE01", res.domeTxCmd);
}

void test_droid_seq_estop_blocks_servo(void) {
    RcActionPayload p = buildPayload(DROID_SEQ_WAVE, nullptr, true);
    p.estopActive = true;

    RcActionResult res = rcDispatchAction(p);

    TEST_ASSERT_EQUAL_INT(-1, res.servoIndex);
    TEST_ASSERT_EQUAL_CHAR('\0', res.audioDollarCmd[0]);
    // domeTxCmd should still be set
    TEST_ASSERT_EQUAL_STRING(":SE02", res.domeTxCmd);
}

void test_droid_seq_no_estop_queues_servo(void) {
    RcActionPayload p = buildPayload(DROID_SEQ_LEIA, nullptr, true);
    p.estopActive = false;

    RcActionResult res = rcDispatchAction(p);

    TEST_ASSERT_TRUE(res.servoIsSequence);
    TEST_ASSERT_GREATER_THAN(0, res.servoSequenceId);
    TEST_ASSERT_GREATER_OR_EQUAL(res.servoIndex, 0);
    TEST_ASSERT_EQUAL_STRING(":SE08", res.domeTxCmd);
}

void test_estop_action_sets_flag(void) {
    RcActionPayload p = buildPayload(SYSTEM_ACTION_ESTOP, nullptr, true);

    RcActionResult res = rcDispatchAction(p);

    TEST_ASSERT_TRUE(res.triggerEstop);
}

void test_estop_action_not_pressed_is_noop(void) {
    RcActionPayload p = buildPayload(SYSTEM_ACTION_ESTOP, nullptr, false);

    RcActionResult res = rcDispatchAction(p);

    TEST_ASSERT_FALSE(res.triggerEstop);
}

void test_sleep_toggle_flips_mode(void) {
    RcActionPayload p = buildPayload(SYSTEM_ACTION_SLEEP_TOGGLE, nullptr, true);
    p.currentSleepMode = false;

    RcActionResult res = rcDispatchAction(p);

    TEST_ASSERT_TRUE(res.setSleep);
    TEST_ASSERT_TRUE(res.newSleepMode);
}

void test_servo_arm1_open_on_press(void) {
    RcActionPayload p = buildPayload(SERVO_ACTION_ARM1_TOGGLE, nullptr, true);

    RcActionResult res = rcDispatchAction(p);

    TEST_ASSERT_EQUAL_INT(0, res.servoIndex);
    TEST_ASSERT_TRUE(res.servoOpen);
    TEST_ASSERT_FALSE(res.servoIsSequence);
}

void test_servo_arm1_close_on_release(void) {
    RcActionPayload p = buildPayload(SERVO_ACTION_ARM1_TOGGLE, nullptr, false);

    RcActionResult res = rcDispatchAction(p);

    TEST_ASSERT_EQUAL_INT(0, res.servoIndex);
    TEST_ASSERT_FALSE(res.servoOpen);
    TEST_ASSERT_FALSE(res.servoIsSequence);
}

void test_servo_aux3_open_on_press(void) {
    RcActionPayload p = buildPayload(SERVO_ACTION_AUX3_TOGGLE, nullptr, true);

    RcActionResult res = rcDispatchAction(p);

    TEST_ASSERT_EQUAL_INT(4, res.servoIndex);
    TEST_ASSERT_TRUE(res.servoOpen);
}

void test_marcduino_seq_valid_payload(void) {
    RcActionPayload p = buildPayload(DOME_ACTION_MARCDUINO_SEQ, "30", true);

    RcActionResult res = rcDispatchAction(p);

    TEST_ASSERT_EQUAL_STRING(":SE30", res.marcduinoCmd);
}

void test_marcduino_cmd_valid_payload(void) {
    RcActionPayload p = buildPayload(DOME_ACTION_MARCDUINO_CMD, ":OP01", true);

    RcActionResult res = rcDispatchAction(p);

    TEST_ASSERT_EQUAL_STRING(":OP01", res.marcduinoCmd);
}

void test_dome_action_seq_forwards_payload(void) {
    RcActionPayload p = buildPayload(DOME_ACTION_SEQ, "DM:VADER", true);

    RcActionResult res = rcDispatchAction(p);

    TEST_ASSERT_EQUAL_STRING("DM:VADER", res.domeTxCmd);
}

void test_op_mode_stationary_on_press(void) {
    RcActionPayload p = buildPayload(SYSTEM_ACTION_OP_MODE, nullptr, true);

    RcActionResult res = rcDispatchAction(p);

    TEST_ASSERT_TRUE(res.setStationary);
    TEST_ASSERT_TRUE(res.newStationaryMode);
}

void test_op_mode_driving_on_release(void) {
    RcActionPayload p = buildPayload(SYSTEM_ACTION_OP_MODE, nullptr, false);

    RcActionResult res = rcDispatchAction(p);

    TEST_ASSERT_TRUE(res.setStationary);
    TEST_ASSERT_FALSE(res.newStationaryMode);
}

void test_speed_preset_cycle(void) {
    RcActionPayload p = buildPayload(DRIVE_ACTION_SPEED_PRESET_CYCLE, nullptr, true);
    p.currentSpeedPreset = SpeedPresetId::Slow;

    RcActionResult res = rcDispatchAction(p);

    TEST_ASSERT_TRUE(res.setSpeedPreset);
    TEST_ASSERT_EQUAL(SpeedPresetId::Normal, res.newSpeedPreset);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_none_action_returns_empty_result);
    RUN_TEST(test_random_general_selects_track_in_range);
    RUN_TEST(test_random_sound_not_pressed_is_noop);
    RUN_TEST(test_droid_seq_sets_dome_cmd);
    RUN_TEST(test_droid_seq_estop_blocks_servo);
    RUN_TEST(test_droid_seq_no_estop_queues_servo);
    RUN_TEST(test_estop_action_sets_flag);
    RUN_TEST(test_estop_action_not_pressed_is_noop);
    RUN_TEST(test_sleep_toggle_flips_mode);
    RUN_TEST(test_servo_arm1_open_on_press);
    RUN_TEST(test_servo_arm1_close_on_release);
    RUN_TEST(test_servo_aux3_open_on_press);
    RUN_TEST(test_marcduino_seq_valid_payload);
    RUN_TEST(test_marcduino_cmd_valid_payload);
    RUN_TEST(test_dome_action_seq_forwards_payload);
    RUN_TEST(test_op_mode_stationary_on_press);
    RUN_TEST(test_op_mode_driving_on_release);
    RUN_TEST(test_speed_preset_cycle);
    return UNITY_END();
}
