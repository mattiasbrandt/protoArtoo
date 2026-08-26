// =============================================================================
// test/test_native/test_rc_action_dispatcher/test_rc_action_dispatcher.cpp
//
// Unity tests for RcActionDispatcher pure dispatch logic.
// =============================================================================

#include <unity.h>
#include <cstring>

#include "rc_action_dispatcher.h"
#include "rc_audio_category_table.h"

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

// =============================================================================
// Sound Category Table Tests
// =============================================================================

void test_category_table_has_all_12_actions(void) {
    // Verify the table has all 12 sound actions
    TEST_ASSERT_EQUAL_INT(12, rcAudioCategoryTableSize);

    // Verify each action appears in the table
    bool found[12] = {false};
    RobotActionId actions[12] = {
        SOUND_ACTION_RANDOM_GENERAL,      SOUND_ACTION_RANDOM_CHATTY,
        SOUND_ACTION_RANDOM_HAPPY,        SOUND_ACTION_RANDOM_PROCESSING,
        SOUND_ACTION_RANDOM_SAD,          SOUND_ACTION_RANDOM_SENTIMENTAL,
        SOUND_ACTION_RANDOM_HUMMING,      SOUND_ACTION_RANDOM_SCREAM,
        SOUND_ACTION_RANDOM_SURPRISED,    SOUND_ACTION_RANDOM_ALERT,
        SOUND_ACTION_RANDOM_SNARKY,       SOUND_ACTION_RANDOM_WHISTLE,
    };

    for (size_t i = 0; i < rcAudioCategoryTableSize; ++i) {
        for (int j = 0; j < 12; ++j) {
            if (rcAudioCategoryTable[i].action == actions[j]) {
                found[j] = true;
                break;
            }
        }
    }

    for (int j = 0; j < 12; ++j) {
        TEST_ASSERT_TRUE_MESSAGE(found[j], "Action not found in table");
    }
}

void test_category_table_gen_lo_hi_pointers(void) {
    // Find GENERAL entry and verify lo/hi member pointers
    const RcAudioCategoryEntry* genEntry = nullptr;
    for (size_t i = 0; i < rcAudioCategoryTableSize; ++i) {
        if (rcAudioCategoryTable[i].action == SOUND_ACTION_RANDOM_GENERAL) {
            genEntry = &rcAudioCategoryTable[i];
            break;
        }
    }
    TEST_ASSERT_NOT_NULL(genEntry);

    // Verify pointers access correct members
    RcAudioCategorySnapshot snap = {};
    snap.gen_lo = 100;
    snap.gen_hi = 200;

    TEST_ASSERT_EQUAL_INT(100, snap.*(genEntry->lo));
    TEST_ASSERT_EQUAL_INT(200, snap.*(genEntry->hi));
}

void test_category_table_whistle_hi_pointer(void) {
    // Mutation test: verify the WHISTLE entry's hi pointer is correct
    // If the pointer is wrong (e.g., points to whis_lo instead of whis_hi),
    // this test will fail.
    const RcAudioCategoryEntry* whistleEntry = nullptr;
    for (size_t i = 0; i < rcAudioCategoryTableSize; ++i) {
        if (rcAudioCategoryTable[i].action == SOUND_ACTION_RANDOM_WHISTLE) {
            whistleEntry = &rcAudioCategoryTable[i];
            break;
        }
    }
    TEST_ASSERT_NOT_NULL(whistleEntry);

    RcAudioCategorySnapshot snap = {};
    snap.whis_lo = 300;
    snap.whis_hi = 400;

    // The hi pointer must return 400, not 300
    uint16_t hi = snap.*(whistleEntry->hi);
    TEST_ASSERT_EQUAL_INT(400, hi);
    TEST_ASSERT_NOT_EQUAL(300, hi);
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
    RUN_TEST(test_category_table_has_all_12_actions);
    RUN_TEST(test_category_table_gen_lo_hi_pointers);
    RUN_TEST(test_category_table_whistle_hi_pointer);
    return UNITY_END();
}
