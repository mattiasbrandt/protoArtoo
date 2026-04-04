#include <unity.h>

#include "rc_mapping.h"

void test_parse_pwm_binding() {
    RcBindingConfig binding = {};
    TEST_ASSERT_TRUE(parseRcBindingConfig("pwm:4:1000:1500:2000:25:1", &binding));
    TEST_ASSERT_EQUAL_UINT8(RC_BINDING_PWM, binding.source);
    TEST_ASSERT_EQUAL_UINT8(4, binding.channel);
    TEST_ASSERT_EQUAL_UINT16(25, binding.deadband);
    TEST_ASSERT_TRUE(binding.reverse);
}

void test_parse_rejects_invalid_channel() {
    RcBindingConfig binding = {};
    TEST_ASSERT_FALSE(parseRcBindingConfig("pwm:7:1000:1500:2000:0:0", &binding));
}

void test_format_round_trip_preserves_binding() {
    RcBindingConfig input = defaultSbusBinding(RC_BINDING_SBUS2, 18);
    input.deadband = 12;
    char encoded[48] = {};
    RcBindingConfig decoded = {};

    TEST_ASSERT_TRUE(formatRcBindingConfig(encoded, sizeof(encoded), input));
    TEST_ASSERT_TRUE(parseRcBindingConfig(encoded, &decoded));
    TEST_ASSERT_EQUAL_UINT8(input.source, decoded.source);
    TEST_ASSERT_EQUAL_UINT8(input.channel, decoded.channel);
    TEST_ASSERT_EQUAL_UINT16(input.min, decoded.min);
    TEST_ASSERT_EQUAL_UINT16(input.center, decoded.center);
    TEST_ASSERT_EQUAL_UINT16(input.max, decoded.max);
    TEST_ASSERT_EQUAL_UINT16(input.deadband, decoded.deadband);
}

void test_apply_calibration_maps_center_to_zero() {
    RcBindingConfig binding = defaultPwmBinding(1);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, applyRcAnalogCalibration(1500, binding, nullptr));
}

void test_apply_calibration_respects_reverse() {
    RcBindingConfig binding = defaultPwmBinding(1);
    binding.reverse = true;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -1.0f, applyRcAnalogCalibration(2000, binding, nullptr));
}

void test_apply_calibration_reports_deadband() {
    RcBindingConfig binding = defaultPwmBinding(1);
    binding.deadband = 40;
    bool inDeadband = false;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, applyRcAnalogCalibration(1530, binding, &inDeadband));
    TEST_ASSERT_TRUE(inDeadband);
}

void test_sbus_digital_channels_are_flagged_digital() {
    TEST_ASSERT_TRUE(rcBindingIsDigital(defaultSbusBinding(RC_BINDING_SBUS1, 17)));
    TEST_ASSERT_TRUE(rcBindingIsDigital(defaultSbusBinding(RC_BINDING_SBUS2, 18)));
    TEST_ASSERT_FALSE(rcBindingIsDigital(defaultSbusBinding(RC_BINDING_SBUS2, 3)));
}

void test_switch_state_uses_thresholds() {
    RcBindingConfig binding = defaultPwmBinding(1);
    TEST_ASSERT_EQUAL_UINT8(RC_SWITCH_LOW, rcAnalogToSwitchState(1200, binding));
    TEST_ASSERT_EQUAL_UINT8(RC_SWITCH_MID, rcAnalogToSwitchState(1500, binding));
    TEST_ASSERT_EQUAL_UINT8(RC_SWITCH_HIGH, rcAnalogToSwitchState(1800, binding));
}

// --- Edge case tests for parseRcBindingConfig ---

void test_parse_rejects_empty_string() {
    RcBindingConfig binding = {};
    TEST_ASSERT_FALSE(parseRcBindingConfig("", &binding));
}

void test_parse_rejects_null() {
    RcBindingConfig binding = {};
    TEST_ASSERT_FALSE(parseRcBindingConfig(nullptr, &binding));
}

void test_parse_rejects_invalid_source() {
    RcBindingConfig binding = {};
    TEST_ASSERT_FALSE(parseRcBindingConfig("invalid:1:1000:1500:2000:0:0", &binding));
}

void test_parse_rejects_malformed_missing_fields() {
    RcBindingConfig binding = {};
    TEST_ASSERT_FALSE(parseRcBindingConfig("pwm:1:1000:1500", &binding));
}

void test_parse_accepts_extra_fields_ignored() {
    RcBindingConfig binding = {};
    // sscanf stops at 7 fields, extra fields are ignored - this is acceptable behavior
    TEST_ASSERT_TRUE(parseRcBindingConfig("pwm:1:1000:1500:2000:0:0:extra", &binding));
    TEST_ASSERT_EQUAL_UINT8(RC_BINDING_PWM, binding.source);
}

void test_parse_rejects_invalid_min_center_max() {
    RcBindingConfig binding = {};
    // min >= center
    TEST_ASSERT_FALSE(parseRcBindingConfig("pwm:1:1500:1500:2000:0:0", &binding));
    // center >= max
    TEST_ASSERT_FALSE(parseRcBindingConfig("pwm:1:1000:2000:2000:0:0", &binding));
    // min > max
    TEST_ASSERT_FALSE(parseRcBindingConfig("pwm:1:2000:1500:1000:0:0", &binding));
}

void test_parse_rejects_deadband_too_large() {
    RcBindingConfig binding = {};
    // Deadband must be < (max - min)
    TEST_ASSERT_FALSE(parseRcBindingConfig("pwm:1:1000:1500:2000:1000:0", &binding));
}

void test_parse_accepts_zero_deadband() {
    RcBindingConfig binding = {};
    TEST_ASSERT_TRUE(parseRcBindingConfig("pwm:1:1000:1500:2000:0:0", &binding));
    TEST_ASSERT_EQUAL_UINT16(0, binding.deadband);
}

void test_parse_accepts_max_valid_deadband() {
    RcBindingConfig binding = {};
    // Deadband = max - min - 1 is valid
    TEST_ASSERT_TRUE(parseRcBindingConfig("pwm:1:1000:1500:2000:999:0", &binding));
    TEST_ASSERT_EQUAL_UINT16(999, binding.deadband);
}

// --- Deadband edge case tests ---

void test_deadband_exactly_at_threshold() {
    RcBindingConfig binding = defaultPwmBinding(1);
    binding.deadband = 50;
    bool inDeadband = false;
    // center + deadband = 1550
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, applyRcAnalogCalibration(1550, binding, &inDeadband));
    TEST_ASSERT_TRUE(inDeadband);
}

void test_deadband_one_past_threshold() {
    RcBindingConfig binding = defaultPwmBinding(1);
    binding.deadband = 50;
    bool inDeadband = false;
    // center + deadband + 1 = 1551
    // delta = 1551 - 1500 = 51, span = 500, mapped = 51/500 = 0.102
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.102f, applyRcAnalogCalibration(1551, binding, &inDeadband));
    TEST_ASSERT_FALSE(inDeadband);
}

void test_deadband_negative_side() {
    RcBindingConfig binding = defaultPwmBinding(1);
    binding.deadband = 50;
    bool inDeadband = false;
    // center - deadband = 1450
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, applyRcAnalogCalibration(1450, binding, &inDeadband));
    TEST_ASSERT_TRUE(inDeadband);
}

void test_deadband_negative_side_one_past() {
    RcBindingConfig binding = defaultPwmBinding(1);
    binding.deadband = 50;
    bool inDeadband = false;
    // center - deadband - 1 = 1449
    // delta = 1449 - 1500 = -51, span = 500, mapped = -51/500 = -0.102
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -0.102f, applyRcAnalogCalibration(1449, binding, &inDeadband));
    TEST_ASSERT_FALSE(inDeadband);
}

// --- SBUS channel validation tests ---

void test_parse_accepts_sbus_channel_1() {
    RcBindingConfig binding = {};
    TEST_ASSERT_TRUE(parseRcBindingConfig("sbus1:1:172:992:1811:0:0", &binding));
    TEST_ASSERT_EQUAL_UINT8(RC_BINDING_SBUS1, binding.source);
    TEST_ASSERT_EQUAL_UINT8(1, binding.channel);
}

void test_parse_accepts_sbus_channel_16() {
    RcBindingConfig binding = {};
    TEST_ASSERT_TRUE(parseRcBindingConfig("sbus1:16:172:992:1811:0:0", &binding));
    TEST_ASSERT_EQUAL_UINT8(16, binding.channel);
}

void test_parse_accepts_sbus_channel_17() {
    RcBindingConfig binding = {};
    // CH17 is valid for digital functions (parse doesn't validate analog vs digital)
    TEST_ASSERT_TRUE(parseRcBindingConfig("sbus1:17:172:992:1811:0:0", &binding));
    TEST_ASSERT_EQUAL_UINT8(17, binding.channel);
}

void test_parse_accepts_sbus_channel_17_for_digital() {
    RcBindingConfig binding = {};
    // When used as digital, CH17 is valid
    TEST_ASSERT_TRUE(parseRcBindingConfig("sbus1:17:172:992:1811:0:0", &binding));
    TEST_ASSERT_TRUE(rcBindingIsDigital(binding));
}

void test_parse_rejects_sbus_channel_0() {
    RcBindingConfig binding = {};
    TEST_ASSERT_FALSE(parseRcBindingConfig("sbus1:0:172:992:1811:0:0", &binding));
}

void test_parse_rejects_sbus_channel_19() {
    RcBindingConfig binding = {};
    TEST_ASSERT_FALSE(parseRcBindingConfig("sbus1:19:172:992:1811:0:0", &binding));
}

// --- Reverse flag tests ---

void test_reverse_flag_true() {
    RcBindingConfig binding = {};
    TEST_ASSERT_TRUE(parseRcBindingConfig("pwm:1:1000:1500:2000:0:1", &binding));
    TEST_ASSERT_TRUE(binding.reverse);
}

void test_reverse_flag_false() {
    RcBindingConfig binding = {};
    TEST_ASSERT_TRUE(parseRcBindingConfig("pwm:1:1000:1500:2000:0:0", &binding));
    TEST_ASSERT_FALSE(binding.reverse);
}

void test_reverse_applies_to_calibration() {
    RcBindingConfig binding = defaultPwmBinding(1);
    binding.reverse = true;
    // Without reverse, 2000 -> 1.0, with reverse -> -1.0
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -1.0f, applyRcAnalogCalibration(2000, binding, nullptr));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, applyRcAnalogCalibration(1000, binding, nullptr));
}

// --- Format validation tests ---

void test_format_rejects_invalid_binding() {
    RcBindingConfig binding = {};
    binding.source = RC_BINDING_NONE;
    binding.channel = 5;  // Invalid: NONE must have channel 0
    char buf[48];
    TEST_ASSERT_FALSE(formatRcBindingConfig(buf, sizeof(buf), binding));
}

void test_format_rejects_null_buffer() {
    RcBindingConfig binding = defaultPwmBinding(1);
    TEST_ASSERT_FALSE(formatRcBindingConfig(nullptr, 48, binding));
}

void test_format_rejects_zero_size_buffer() {
    RcBindingConfig binding = defaultPwmBinding(1);
    char buf[1];
    TEST_ASSERT_FALSE(formatRcBindingConfig(buf, 0, binding));
}

void test_trigger_parse_accepts_colon_payload() {
    RcTriggerBinding binding = {};
    TEST_ASSERT_TRUE(parseRcTriggerBinding("sbus1:6:cmd::OP01:172:992:1811:0:0", &binding));
    TEST_ASSERT_EQUAL_UINT8(RC_BINDING_SBUS1, binding.source);
    TEST_ASSERT_EQUAL_UINT8(6, binding.channel);
    TEST_ASSERT_EQUAL_UINT8(DOME_ACTION_MARCDUINO_CMD, binding.target);
    TEST_ASSERT_EQUAL_STRING(":OP01", binding.marcduinoPayload);
}

void test_trigger_format_round_trip_keeps_colon_payload() {
    RcTriggerBinding input = makeRcTriggerBinding(RC_BINDING_SBUS1, 7, DOME_ACTION_MARCDUINO_CMD,
                                                  ":MV120", 172, 992, 1811, 0, false);
    char encoded[96] = {};
    RcTriggerBinding decoded = {};

    TEST_ASSERT_TRUE(formatRcTriggerBinding(encoded, sizeof(encoded), input));
    TEST_ASSERT_TRUE(parseRcTriggerBinding(encoded, &decoded));
    TEST_ASSERT_EQUAL_UINT8(input.source, decoded.source);
    TEST_ASSERT_EQUAL_UINT8(input.channel, decoded.channel);
    TEST_ASSERT_EQUAL_UINT8(input.target, decoded.target);
    TEST_ASSERT_EQUAL_STRING(input.marcduinoPayload, decoded.marcduinoPayload);
}

void test_trigger_parse_accepts_dome_seq_payload() {
    RcTriggerBinding binding = {};
    TEST_ASSERT_TRUE(parseRcTriggerBinding("sbus2:3:dome_seq:12:172:992:1811:0:0", &binding));
    TEST_ASSERT_EQUAL_UINT8(DOME_ACTION_SEQ, binding.target);
    TEST_ASSERT_EQUAL_STRING("12", binding.marcduinoPayload);
}

void test_trigger_parse_rejects_channel_overflow_wraparound() {
    RcTriggerBinding binding = {};
    TEST_ASSERT_FALSE(parseRcTriggerBinding("sbus1:257:cmd::OP01:172:992:1811:0:0", &binding));
}

void test_trigger_parse_rejects_reverse_overflow() {
    RcTriggerBinding binding = {};
    TEST_ASSERT_FALSE(parseRcTriggerBinding("sbus1:6:cmd::OP01:172:992:1811:0:2", &binding));
}

void test_trigger_parse_rejects_uint16_field_overflow() {
    RcTriggerBinding binding = {};
    TEST_ASSERT_FALSE(parseRcTriggerBinding("sbus1:6:cmd::OP01:70000:992:1811:0:0", &binding));
}

void test_sound_random_action_tokens_round_trip() {
    const RobotActionId ids[] = {
        SOUND_ACTION_RANDOM_GENERAL,     SOUND_ACTION_RANDOM_CHATTY,
        SOUND_ACTION_RANDOM_HAPPY,       SOUND_ACTION_RANDOM_PROCESSING,
        SOUND_ACTION_RANDOM_SAD,         SOUND_ACTION_RANDOM_SENTIMENTAL,
        SOUND_ACTION_RANDOM_HUMMING,     SOUND_ACTION_RANDOM_SCREAM,
        SOUND_ACTION_RANDOM_SURPRISED,   SOUND_ACTION_RANDOM_ALERT,
        SOUND_ACTION_RANDOM_PFFT,        SOUND_ACTION_RANDOM_WHISTLE,
    };

    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i) {
        const char* token = robotActionIdToString(ids[i]);
        TEST_ASSERT_NOT_NULL(token);
        TEST_ASSERT_NOT_EQUAL(0, strcmp(token, "none"));

        RobotActionId parsed = ROBOT_ACTION_NONE;
        TEST_ASSERT_TRUE(parseRobotActionId(token, &parsed));
        TEST_ASSERT_EQUAL_UINT8(ids[i], parsed);
    }
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_parse_pwm_binding);
    RUN_TEST(test_parse_rejects_invalid_channel);
    RUN_TEST(test_format_round_trip_preserves_binding);
    RUN_TEST(test_apply_calibration_maps_center_to_zero);
    RUN_TEST(test_apply_calibration_respects_reverse);
    RUN_TEST(test_apply_calibration_reports_deadband);
    RUN_TEST(test_sbus_digital_channels_are_flagged_digital);
    RUN_TEST(test_switch_state_uses_thresholds);

    // Edge case tests
    RUN_TEST(test_parse_rejects_empty_string);
    RUN_TEST(test_parse_rejects_null);
    RUN_TEST(test_parse_rejects_invalid_source);
    RUN_TEST(test_parse_rejects_malformed_missing_fields);
    RUN_TEST(test_parse_accepts_extra_fields_ignored);
    RUN_TEST(test_parse_rejects_invalid_min_center_max);
    RUN_TEST(test_parse_rejects_deadband_too_large);
    RUN_TEST(test_parse_accepts_zero_deadband);
    RUN_TEST(test_parse_accepts_max_valid_deadband);

    // Deadband edge cases
    RUN_TEST(test_deadband_exactly_at_threshold);
    RUN_TEST(test_deadband_one_past_threshold);
    RUN_TEST(test_deadband_negative_side);
    RUN_TEST(test_deadband_negative_side_one_past);

    // SBUS channel validation
    RUN_TEST(test_parse_accepts_sbus_channel_1);
    RUN_TEST(test_parse_accepts_sbus_channel_16);
    RUN_TEST(test_parse_accepts_sbus_channel_17);
    RUN_TEST(test_parse_accepts_sbus_channel_17_for_digital);
    RUN_TEST(test_parse_rejects_sbus_channel_0);
    RUN_TEST(test_parse_rejects_sbus_channel_19);

    // Reverse flag tests
    RUN_TEST(test_reverse_flag_true);
    RUN_TEST(test_reverse_flag_false);
    RUN_TEST(test_reverse_applies_to_calibration);

    // Format validation tests
    RUN_TEST(test_format_rejects_invalid_binding);
    RUN_TEST(test_format_rejects_null_buffer);
    RUN_TEST(test_format_rejects_zero_size_buffer);
    RUN_TEST(test_trigger_parse_accepts_colon_payload);
    RUN_TEST(test_trigger_format_round_trip_keeps_colon_payload);
    RUN_TEST(test_trigger_parse_accepts_dome_seq_payload);
    RUN_TEST(test_trigger_parse_rejects_channel_overflow_wraparound);
    RUN_TEST(test_trigger_parse_rejects_reverse_overflow);
    RUN_TEST(test_trigger_parse_rejects_uint16_field_overflow);
    RUN_TEST(test_sound_random_action_tokens_round_trip);

    return UNITY_END();
}
