// =============================================================================
// test/test_native/test_config_store/test_config_store.cpp
//
// Native unit tests for config_store module.
// Tests cover load/save round-trips, validation, schema versioning, and defaults.
// =============================================================================

#include <cstring>
#include <stdint.h>

#include <unity.h>

#include "config_store.h"
#include "robot_state.h"

// Provided by native_test_stubs.cpp
extern RobotState robotState;

void setUp() {
    memset(&robotState, 0, sizeof(RobotState));
}

void tearDown() {
}

// Test: Load from empty NVS populates snapshot with defaults
void test_configLoad_empty_nvs_returns_defaults() {
    Preferences prefs;
    prefs.begin("proto", false);
    ConfigSnapshot snap = {};
    bool result = configLoad(prefs, &snap);
    prefs.end();

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_INT16(SPEED_LIMIT_MAX, snap.speedLimitMax);
    TEST_ASSERT_EQUAL_INT16(SPEED_PRESET_SLOW, snap.speedPresetSlow);
    TEST_ASSERT_EQUAL_INT16(SPEED_PRESET_NORMAL, snap.speedPresetNormal);
    TEST_ASSERT_EQUAL_INT16(SPEED_PRESET_TURBO, snap.speedPresetTurbo);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)SpeedPresetId::Normal, (uint8_t)snap.speedPresetActive);
    TEST_ASSERT_EQUAL_UINT32(SBUS_TIMEOUT_MS, snap.sbusTimeoutMs);
    TEST_ASSERT_EQUAL_UINT32(WEB_DRIVE_TIMEOUT_MS, snap.webDriveTimeoutMs);
    TEST_ASSERT_EQUAL_UINT8(20, snap.audioVolume);
}

// Test: Save and reload returns same snapshot
void test_configLoad_save_roundtrip() {
    ConfigSnapshot snap1 = {};
    snap1.speedLimitMax = 500;
    snap1.speedPresetSlow = 150;
    snap1.speedPresetNormal = 300;
    snap1.speedPresetTurbo = 500;
    snap1.sbusTimeoutMs = 150;
    snap1.webDriveTimeoutMs = 400;
    snap1.audioVolume = 25;

    Preferences prefs;
    prefs.begin("proto", false);
    bool saveResult = configSave(prefs, snap1);
    TEST_ASSERT_TRUE(saveResult);

    ConfigSnapshot snap2 = {};
    bool loadResult = configLoad(prefs, &snap2);
    prefs.end();

    TEST_ASSERT_TRUE(loadResult);
    TEST_ASSERT_EQUAL_INT16(snap1.speedLimitMax, snap2.speedLimitMax);
    TEST_ASSERT_EQUAL_INT16(snap1.speedPresetSlow, snap2.speedPresetSlow);
    TEST_ASSERT_EQUAL_INT16(snap1.speedPresetNormal, snap2.speedPresetNormal);
    TEST_ASSERT_EQUAL_INT16(snap1.speedPresetTurbo, snap2.speedPresetTurbo);
    TEST_ASSERT_EQUAL_UINT32(snap1.sbusTimeoutMs, snap2.sbusTimeoutMs);
    TEST_ASSERT_EQUAL_UINT32(snap1.webDriveTimeoutMs, snap2.webDriveTimeoutMs);
    TEST_ASSERT_EQUAL_UINT8(snap1.audioVolume, snap2.audioVolume);
}

// Test: configValidate rejects out-of-range speed limit
void test_configValidate_speed_limit_out_of_range() {
    ConfigValidationResult result = configValidate(ConfigKey::SPEED_LIMIT_MAX, SPEED_LIMIT_MAX + 1);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ConfigValidationResult::OUT_OF_RANGE, (uint8_t)result);

    result = configValidate(ConfigKey::SPEED_LIMIT_MAX, -1);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ConfigValidationResult::OUT_OF_RANGE, (uint8_t)result);
}

// Test: configValidate accepts valid speed limit
void test_configValidate_speed_limit_valid() {
    ConfigValidationResult result = configValidate(ConfigKey::SPEED_LIMIT_MAX, 0);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ConfigValidationResult::OK, (uint8_t)result);

    result = configValidate(ConfigKey::SPEED_LIMIT_MAX, SPEED_LIMIT_MAX);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ConfigValidationResult::OK, (uint8_t)result);

    result = configValidate(ConfigKey::SPEED_LIMIT_MAX, 300);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ConfigValidationResult::OK, (uint8_t)result);
}

// Test: configValidate rejects invalid SBUS timeout
void test_configValidate_sbus_timeout_out_of_range() {
    ConfigValidationResult result = configValidate(ConfigKey::SBUS_TIMEOUT_MS, 40);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ConfigValidationResult::OUT_OF_RANGE, (uint8_t)result);

    result = configValidate(ConfigKey::SBUS_TIMEOUT_MS, 5001);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ConfigValidationResult::OUT_OF_RANGE, (uint8_t)result);
}

// Test: configValidate accepts valid SBUS timeout
void test_configValidate_sbus_timeout_valid() {
    ConfigValidationResult result = configValidate(ConfigKey::SBUS_TIMEOUT_MS, 50);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ConfigValidationResult::OK, (uint8_t)result);

    result = configValidate(ConfigKey::SBUS_TIMEOUT_MS, 200);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ConfigValidationResult::OK, (uint8_t)result);

    result = configValidate(ConfigKey::SBUS_TIMEOUT_MS, 5000);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ConfigValidationResult::OK, (uint8_t)result);
}

// Test: configValidate audio volume
void test_configValidate_audio_volume() {
    ConfigValidationResult result = configValidate(ConfigKey::AUDIO_VOLUME, 0);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ConfigValidationResult::OK, (uint8_t)result);

    result = configValidate(ConfigKey::AUDIO_VOLUME, 30);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ConfigValidationResult::OK, (uint8_t)result);

    result = configValidate(ConfigKey::AUDIO_VOLUME, 31);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ConfigValidationResult::OUT_OF_RANGE, (uint8_t)result);
}

// Test: configValidate servo pulse widths
void test_configValidate_servo_pulses() {
    ConfigValidationResult result = configValidate(ConfigKey::ARM1_OPEN_US, 500);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ConfigValidationResult::OK, (uint8_t)result);

    result = configValidate(ConfigKey::ARM1_OPEN_US, 2500);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ConfigValidationResult::OK, (uint8_t)result);

    result = configValidate(ConfigKey::ARM1_OPEN_US, 499);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ConfigValidationResult::OUT_OF_RANGE, (uint8_t)result);

    result = configValidate(ConfigKey::ARM1_OPEN_US, 2501);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ConfigValidationResult::OUT_OF_RANGE, (uint8_t)result);
}

// Test: configValidate servo types
void test_configValidate_servo_types() {
    ConfigValidationResult result = configValidate(ConfigKey::ARM1_TYPE, SERVO_COMP_NONE);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ConfigValidationResult::OK, (uint8_t)result);

    result = configValidate(ConfigKey::ARM1_TYPE, SERVO_COMP_RGB);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ConfigValidationResult::OK, (uint8_t)result);

    result = configValidate(ConfigKey::ARM1_TYPE, SERVO_COMP_RGB + 1);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ConfigValidationResult::INVALID_VALUE, (uint8_t)result);
}

// Test: configValidate dome speed limits
void test_configValidate_dome_speed() {
    ConfigValidationResult result = configValidateFloat(ConfigKey::DOME_MIN_SPEED, 0.0f);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ConfigValidationResult::OK, (uint8_t)result);

    result = configValidateFloat(ConfigKey::DOME_MAX_SPEED, 1.0f);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ConfigValidationResult::OK, (uint8_t)result);

    result = configValidateFloat(ConfigKey::DOME_MIN_SPEED, -0.1f);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ConfigValidationResult::OUT_OF_RANGE, (uint8_t)result);

    result = configValidateFloat(ConfigKey::DOME_MAX_SPEED, 1.1f);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ConfigValidationResult::OUT_OF_RANGE, (uint8_t)result);
}

// Test: configValidate booleans
void test_configValidate_booleans() {
    ConfigValidationResult result = configValidateBool(ConfigKey::ENABLE_ARM1, true);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ConfigValidationResult::OK, (uint8_t)result);

    result = configValidateBool(ConfigKey::ENABLE_ARM1, false);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ConfigValidationResult::OK, (uint8_t)result);

    result = configValidateBool(ConfigKey::STATIONARY, true);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ConfigValidationResult::OK, (uint8_t)result);
}

// Test: Schema version 0 (legacy) loads and stamps as v1
void test_configLoad_legacy_schema_v0() {
    Preferences prefs;
    prefs.begin("proto", false);
    prefs.clear();

    // Write a legacy value (no schema version key)
    prefs.putShort("spd_max", 400);

    ConfigSnapshot snap = {};
    bool result = configLoad(prefs, &snap);
    prefs.end();

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_INT16(400, snap.speedLimitMax);

    // Verify schema version was stamped
    prefs.begin("proto", true);
    uint8_t storedVersion = prefs.getUChar("proto.schema_ver", 0);
    prefs.end();
    TEST_ASSERT_EQUAL_UINT8(CONFIG_SCHEMA_VERSION, storedVersion);
}

// Test: Schema version mismatch returns false and fills with defaults
void test_configLoad_schema_mismatch() {
    Preferences prefs;
    prefs.begin("proto", false);
    prefs.clear();

    // Write incompatible schema version
    uint8_t badVersion = CONFIG_SCHEMA_VERSION + 1;
    prefs.putUChar("proto.schema_ver", badVersion);
    prefs.putShort("spd_max", 400);

    ConfigSnapshot snap = {};
    bool result = configLoad(prefs, &snap);
    prefs.end();

    // Should return false and fill with defaults, not the NVS value
    TEST_ASSERT_FALSE(result);
    TEST_ASSERT_EQUAL_INT16(SPEED_LIMIT_MAX, snap.speedLimitMax);

    // Verify schema version was updated
    prefs.begin("proto", true);
    uint8_t storedVersion = prefs.getUChar("proto.schema_ver", 0);
    prefs.end();
    TEST_ASSERT_EQUAL_UINT8(CONFIG_SCHEMA_VERSION, storedVersion);
}

// Test: Save all audio track fields
void test_configLoad_save_audio_tracks() {
    ConfigSnapshot snap1 = {};
    snap1.snd_scream = 100;
    snap1.snd_faint = 101;
    snap1.snd_leia = 102;
    snap1.snd_cantina_s = 103;
    snap1.snd_sw_theme = 104;
    snap1.snd_imp_march = 105;
    snap1.snd_cantina_l = 106;
    snap1.snd_startup = 107;
    snap1.snd_doodoo = 108;

    Preferences prefs;
    prefs.begin("proto", false);
    bool saveResult = configSave(prefs, snap1);
    TEST_ASSERT_TRUE(saveResult);

    ConfigSnapshot snap2 = {};
    bool loadResult = configLoad(prefs, &snap2);
    prefs.end();

    TEST_ASSERT_TRUE(loadResult);
    TEST_ASSERT_EQUAL_UINT16(snap1.snd_scream, snap2.snd_scream);
    TEST_ASSERT_EQUAL_UINT16(snap1.snd_faint, snap2.snd_faint);
    TEST_ASSERT_EQUAL_UINT16(snap1.snd_leia, snap2.snd_leia);
    TEST_ASSERT_EQUAL_UINT16(snap1.snd_doodoo, snap2.snd_doodoo);
}

// Test: Save all servo fields
void test_configLoad_save_servo_config() {
    ConfigSnapshot snap1 = {};
    snap1.arm1_open_us = 2100;
    snap1.arm1_close_us = 900;
    snap1.arm2_open_us = 2200;
    snap1.arm2_close_us = 800;
    snap1.arm1_type = SERVO_COMP_MG996R;
    snap1.arm2_type = SERVO_COMP_MG90S;
    snap1.aux1_type = SERVO_COMP_RGB;

    Preferences prefs;
    prefs.begin("proto", false);
    bool saveResult = configSave(prefs, snap1);
    TEST_ASSERT_TRUE(saveResult);

    ConfigSnapshot snap2 = {};
    bool loadResult = configLoad(prefs, &snap2);
    prefs.end();

    TEST_ASSERT_TRUE(loadResult);
    TEST_ASSERT_EQUAL_UINT16(snap1.arm1_open_us, snap2.arm1_open_us);
    TEST_ASSERT_EQUAL_UINT16(snap1.arm1_close_us, snap2.arm1_close_us);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)snap1.arm1_type, (uint8_t)snap2.arm1_type);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)snap1.aux1_type, (uint8_t)snap2.aux1_type);
}

// Test: Save all feature toggle fields
void test_configLoad_save_feature_toggles() {
    ConfigSnapshot snap1 = {};
    snap1.enable_arm1 = true;
    snap1.enable_arm2 = true;
    snap1.enable_dome = false;
    snap1.enable_rc_ch1 = true;
    snap1.enable_s1_hoverboard = true;
    snap1.stationary = true;

    Preferences prefs;
    prefs.begin("proto", false);
    bool saveResult = configSave(prefs, snap1);
    TEST_ASSERT_TRUE(saveResult);

    ConfigSnapshot snap2 = {};
    bool loadResult = configLoad(prefs, &snap2);
    prefs.end();

    TEST_ASSERT_TRUE(loadResult);
    TEST_ASSERT_EQUAL_INT(true, snap2.enable_arm1);
    TEST_ASSERT_EQUAL_INT(true, snap2.enable_arm2);
    TEST_ASSERT_EQUAL_INT(false, snap2.enable_dome);
    TEST_ASSERT_EQUAL_INT(true, snap2.enable_rc_ch1);
    TEST_ASSERT_EQUAL_INT(true, snap2.enable_s1_hoverboard);
    TEST_ASSERT_EQUAL_INT(true, snap2.stationary);
}

// Test: configValidate dome speed percentage
void test_configValidate_dome_speed_pct() {
    ConfigValidationResult result = configValidate(ConfigKey::DOME_SPEED_LIMIT_PCT, 0);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ConfigValidationResult::OK, (uint8_t)result);

    result = configValidate(ConfigKey::DOME_SPEED_LIMIT_PCT, 100);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ConfigValidationResult::OK, (uint8_t)result);

    result = configValidate(ConfigKey::DOME_SPEED_LIMIT_PCT, 101);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ConfigValidationResult::OUT_OF_RANGE, (uint8_t)result);
}

// Test: configValidate aux LED pin
void test_configValidate_aux_led_pin() {
    ConfigValidationResult result = configValidate(ConfigKey::AUX_LED_PIN, AUX_LED_PIN_DISABLED);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ConfigValidationResult::OK, (uint8_t)result);

    result = configValidate(ConfigKey::AUX_LED_PIN, AUX_LED_PIN_AUX3);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ConfigValidationResult::OK, (uint8_t)result);

    result = configValidate(ConfigKey::AUX_LED_PIN, AUX_LED_PIN_AUX3 + 1);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ConfigValidationResult::INVALID_VALUE, (uint8_t)result);
}

// Test: configValidate sequence timing
void test_configValidate_sequence_timing() {
    ConfigValidationResult result = configValidate(ConfigKey::SEQ_OPEN_MS, 100);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ConfigValidationResult::OK, (uint8_t)result);

    result = configValidate(ConfigKey::SEQ_OPEN_MS, 5000);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ConfigValidationResult::OK, (uint8_t)result);

    result = configValidate(ConfigKey::SEQ_OPEN_MS, 99);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ConfigValidationResult::OUT_OF_RANGE, (uint8_t)result);

    result = configValidate(ConfigKey::SEQ_OPEN_MS, 5001);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ConfigValidationResult::OUT_OF_RANGE, (uint8_t)result);
}

// Test: configValidate RC input mode
void test_configValidate_rc_input_mode() {
    ConfigValidationResult result = configValidate(ConfigKey::RC_INPUT_MODE, RC_INPUT_STANDARD_PWM);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ConfigValidationResult::OK, (uint8_t)result);

    result = configValidate(ConfigKey::RC_INPUT_MODE, RC_INPUT_DUAL_SBUS);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ConfigValidationResult::OK, (uint8_t)result);

    result = configValidate(ConfigKey::RC_INPUT_MODE, RC_INPUT_DUAL_SBUS + 1);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ConfigValidationResult::INVALID_VALUE, (uint8_t)result);
}

// Test: Save dome wifi peer IP
void test_configLoad_save_dome_wifi_peer_ip() {
    ConfigSnapshot snap1 = {};
    snprintf(snap1.dome_wifi_peer_ip, sizeof(snap1.dome_wifi_peer_ip), "192.168.1.42");

    Preferences prefs;
    prefs.begin("proto", false);
    bool saveResult = configSave(prefs, snap1);
    TEST_ASSERT_TRUE(saveResult);

    ConfigSnapshot snap2 = {};
    bool loadResult = configLoad(prefs, &snap2);
    prefs.end();

    TEST_ASSERT_TRUE(loadResult);
    TEST_ASSERT_EQUAL_STRING(snap1.dome_wifi_peer_ip, snap2.dome_wifi_peer_ip);
}

// Test: Empty dome wifi IP is preserved
void test_configLoad_save_dome_wifi_peer_ip_empty() {
    ConfigSnapshot snap1 = {};
    snap1.dome_wifi_peer_ip[0] = '\0';

    Preferences prefs;
    prefs.begin("proto", false);
    bool saveResult = configSave(prefs, snap1);
    TEST_ASSERT_TRUE(saveResult);

    ConfigSnapshot snap2 = {};
    bool loadResult = configLoad(prefs, &snap2);
    prefs.end();

    TEST_ASSERT_TRUE(loadResult);
    TEST_ASSERT_EQUAL_CHAR('\0', snap2.dome_wifi_peer_ip[0]);
}

// Test: Mood category masks are truncated to 12-bit
void test_configLoad_save_moodcat_12bit_mask() {
    ConfigSnapshot snap1 = {};
    snap1.snd_moodcat_quiet = 0x1234;  // Will be truncated to 0x0234
    snap1.snd_moodcat_mid = 0xFFFF;    // Will be truncated to 0x0FFF

    Preferences prefs;
    prefs.begin("proto", false);
    bool saveResult = configSave(prefs, snap1);
    TEST_ASSERT_TRUE(saveResult);

    ConfigSnapshot snap2 = {};
    bool loadResult = configLoad(prefs, &snap2);
    prefs.end();

    TEST_ASSERT_TRUE(loadResult);
    TEST_ASSERT_EQUAL_UINT16(0x0234, snap2.snd_moodcat_quiet);
    TEST_ASSERT_EQUAL_UINT16(0x0FFF, snap2.snd_moodcat_mid);
}

// Test: configSnapshotFromRobotState captures one field from each category group.
// This guards against a field silently missing from the function body, which
// would cause configSave() to write zeros to NVS for that field.
void test_configSnapshotFromRobotState_captures_all_categories() {
    // Speed
    robotState.cfg_speedLimitMax      = 750;
    robotState.cfg_speedPresetSlow    = 150;
    robotState.cfg_speedPresetNormal  = 350;
    robotState.cfg_speedPresetTurbo   = 750;
    robotState.cfg_speedPresetActive  = SpeedPresetId::Turbo;

    // Timeouts
    robotState.cfg_sbusTimeoutMs      = 250;
    robotState.cfg_webDriveTimeoutMs  = 600;

    // Audio scalars
    robotState.cfg_audioVolume        = 22;
    robotState.cfg_logLevel           = 2;

    // Named audio tracks
    robotState.cfg_snd_scream         = 200;
    robotState.cfg_snd_faint          = 201;
    robotState.cfg_snd_startup        = 255;
    robotState.cfg_snd_rand_min       = 5;
    robotState.cfg_snd_rand_max       = 80;

    // Mood category fields — the ones most likely to be missed
    robotState.cfg_snd_moodcat_quiet  = 0x0ABC;
    robotState.cfg_snd_moodcat_full   = 0x0FFF;
    robotState.cfg_snd_cat_gen_lo     = 10;
    robotState.cfg_snd_cat_gen_hi     = 20;
    robotState.cfg_snd_cat_whis_lo    = 30;
    robotState.cfg_snd_cat_whis_hi    = 40;

    // Servo
    robotState.cfg_arm1_open_us       = 2100;
    robotState.cfg_arm1_close_us      = 900;
    robotState.cfg_arm1_type          = SERVO_COMP_MG996R;
    robotState.cfg_aux3_type          = SERVO_COMP_RGB;
    robotState.cfg_aux1_open_us       = 1800;
    robotState.cfg_aux1_close_us      = 1200;

    // Dome
    robotState.cfg_dome_min_speed     = 0.1f;
    robotState.cfg_dome_max_speed     = 0.9f;
    robotState.cfg_dome_neutral_us    = 1510;
    robotState.cfg_dome_speed_limit_pct = 75;
    robotState.cfg_dome_rnd_enable    = true;
    robotState.cfg_dome_rnd_speed_pct = 35;
    robotState.cfg_dome_rnd_pause_min = 4;
    robotState.cfg_dome_rnd_pause_max = 10;
    robotState.cfg_dome_rnd_move_ms   = 3000;
    snprintf(robotState.cfg_dome_wifi_peer_ip, sizeof(robotState.cfg_dome_wifi_peer_ip),
             "10.0.0.5");

    // Sequence timing
    robotState.cfg_seq_open_ms        = 400;
    robotState.cfg_seq_close_ms       = 600;

    // AUX LED
    robotState.cfg_aux_led_pin        = 2;
    robotState.cfg_aux_led_count      = 8;

    // Feature toggles
    robotState.cfg_enable_arm1        = true;
    robotState.cfg_enable_dome        = true;
    robotState.cfg_stationary         = true;
    robotState.cfg_rc_input_mode      = RC_INPUT_DUAL_SBUS;
    robotState.cfg_single_sbus_use_ch2 = true;
    robotState.cfg_enable_s1_hoverboard = true;

    // RC backbone binding
    robotState.cfg_rc_sbus_drive_speed =
        makeRcBindingConfig(RC_BINDING_SBUS1, 3, 200, 1000, 1800, 50, true);
    robotState.cfg_rc_pwm_drive_steer  =
        makeRcBindingConfig(RC_BINDING_PWM, 2, 1000, 1500, 2000, 0, false);

    // RC trigger binding
    robotState.cfg_rc_arm1.source     = RC_BINDING_SBUS1;
    robotState.cfg_rc_arm1.channel    = 5;
    robotState.cfg_rc_arm1.target     = SERVO_ACTION_ARM1_TOGGLE;
    robotState.cfg_rc_free3.source    = RC_BINDING_SBUS2;
    robotState.cfg_rc_free3.channel   = 7;

    ConfigSnapshot snap = {};
    configSnapshotFromRobotState(&snap);

    // Speed
    TEST_ASSERT_EQUAL_INT16(750, snap.speedLimitMax);
    TEST_ASSERT_EQUAL_INT16(150, snap.speedPresetSlow);
    TEST_ASSERT_EQUAL_INT16(350, snap.speedPresetNormal);
    TEST_ASSERT_EQUAL_INT16(750, snap.speedPresetTurbo);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)SpeedPresetId::Turbo, (uint8_t)snap.speedPresetActive);

    // Timeouts
    TEST_ASSERT_EQUAL_UINT32(250, snap.sbusTimeoutMs);
    TEST_ASSERT_EQUAL_UINT32(600, snap.webDriveTimeoutMs);

    // Audio scalars
    TEST_ASSERT_EQUAL_UINT8(22, snap.audioVolume);
    TEST_ASSERT_EQUAL_UINT8(2, snap.logLevel);

    // Named audio tracks
    TEST_ASSERT_EQUAL_UINT16(200, snap.snd_scream);
    TEST_ASSERT_EQUAL_UINT16(201, snap.snd_faint);
    TEST_ASSERT_EQUAL_UINT16(255, snap.snd_startup);
    TEST_ASSERT_EQUAL_UINT16(5,  snap.snd_rand_min);
    TEST_ASSERT_EQUAL_UINT16(80, snap.snd_rand_max);

    // Mood category fields
    TEST_ASSERT_EQUAL_UINT16(0x0ABC, snap.snd_moodcat_quiet);
    TEST_ASSERT_EQUAL_UINT16(0x0FFF, snap.snd_moodcat_full);
    TEST_ASSERT_EQUAL_UINT16(10, snap.snd_cat_gen_lo);
    TEST_ASSERT_EQUAL_UINT16(20, snap.snd_cat_gen_hi);
    TEST_ASSERT_EQUAL_UINT16(30, snap.snd_cat_whis_lo);
    TEST_ASSERT_EQUAL_UINT16(40, snap.snd_cat_whis_hi);

    // Servo
    TEST_ASSERT_EQUAL_UINT16(2100, snap.arm1_open_us);
    TEST_ASSERT_EQUAL_UINT16(900,  snap.arm1_close_us);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)SERVO_COMP_MG996R, (uint8_t)snap.arm1_type);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)SERVO_COMP_RGB,    (uint8_t)snap.aux3_type);
    TEST_ASSERT_EQUAL_UINT16(1800, snap.aux1_open_us);
    TEST_ASSERT_EQUAL_UINT16(1200, snap.aux1_close_us);

    // Dome
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.1f, snap.dome_min_speed);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.9f, snap.dome_max_speed);
    TEST_ASSERT_EQUAL_UINT16(1510, snap.dome_neutral_us);
    TEST_ASSERT_EQUAL_UINT8(75, snap.dome_speed_limit_pct);
    TEST_ASSERT_EQUAL_INT(true, snap.dome_rnd_enable);
    TEST_ASSERT_EQUAL_UINT8(35, snap.dome_rnd_speed_pct);
    TEST_ASSERT_EQUAL_UINT8(4,  snap.dome_rnd_pause_min);
    TEST_ASSERT_EQUAL_UINT8(10, snap.dome_rnd_pause_max);
    TEST_ASSERT_EQUAL_UINT16(3000, snap.dome_rnd_move_ms);
    TEST_ASSERT_EQUAL_STRING("10.0.0.5", snap.dome_wifi_peer_ip);

    // Sequence timing
    TEST_ASSERT_EQUAL_UINT16(400, snap.seq_open_ms);
    TEST_ASSERT_EQUAL_UINT16(600, snap.seq_close_ms);

    // AUX LED
    TEST_ASSERT_EQUAL_UINT8(2, snap.aux_led_pin);
    TEST_ASSERT_EQUAL_UINT8(8, snap.aux_led_count);

    // Feature toggles
    TEST_ASSERT_EQUAL_INT(true, snap.enable_arm1);
    TEST_ASSERT_EQUAL_INT(true, snap.enable_dome);
    TEST_ASSERT_EQUAL_INT(true, snap.stationary);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RC_INPUT_DUAL_SBUS, (uint8_t)snap.rc_input_mode);
    TEST_ASSERT_EQUAL_INT(true, snap.single_sbus_use_ch2);
    TEST_ASSERT_EQUAL_INT(true, snap.enable_s1_hoverboard);

    // RC backbone binding
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RC_BINDING_SBUS1, (uint8_t)snap.rc_sbus_drive_speed.source);
    TEST_ASSERT_EQUAL_UINT8(3,   snap.rc_sbus_drive_speed.channel);
    TEST_ASSERT_EQUAL_UINT16(200, snap.rc_sbus_drive_speed.min);
    TEST_ASSERT_EQUAL_INT(true,  snap.rc_sbus_drive_speed.reverse);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RC_BINDING_PWM, (uint8_t)snap.rc_pwm_drive_steer.source);
    TEST_ASSERT_EQUAL_UINT8(2, snap.rc_pwm_drive_steer.channel);

    // RC trigger binding
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RC_BINDING_SBUS1, (uint8_t)snap.rc_arm1.source);
    TEST_ASSERT_EQUAL_UINT8(5, snap.rc_arm1.channel);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)SERVO_ACTION_ARM1_TOGGLE, (uint8_t)snap.rc_arm1.target);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RC_BINDING_SBUS2, (uint8_t)snap.rc_free3.source);
    TEST_ASSERT_EQUAL_UINT8(7, snap.rc_free3.channel);
}

// Test: full save path — robotState -> configSnapshotFromRobotState -> configSave -> configLoad
// Verifies that the complete chain used by saveConfigToNvs() preserves values correctly.
void test_configSnapshotFromRobotState_save_round_trip() {
    robotState.cfg_speedLimitMax      = 600;
    robotState.cfg_audioVolume        = 18;
    robotState.cfg_snd_cat_alrm_lo   = 55;
    robotState.cfg_snd_cat_alrm_hi   = 65;
    robotState.cfg_dome_rnd_enable    = true;
    robotState.cfg_dome_rnd_speed_pct = 40;
    robotState.cfg_enable_arm2        = true;
    robotState.cfg_stationary         = false;
    robotState.cfg_rc_input_mode      = RC_INPUT_SINGLE_SBUS;
    robotState.cfg_rc_sbus_arm1 =
        makeRcBindingConfig(RC_BINDING_SBUS1, 4, 172, 992, 1811, 10, false);

    ConfigSnapshot snap1 = {};
    configSnapshotFromRobotState(&snap1);

    Preferences prefs;
    prefs.begin("proto", false);
    bool saveOk = configSave(prefs, snap1);
    TEST_ASSERT_TRUE(saveOk);

    ConfigSnapshot snap2 = {};
    bool loadOk = configLoad(prefs, &snap2);
    prefs.end();
    TEST_ASSERT_TRUE(loadOk);

    TEST_ASSERT_EQUAL_INT16(600, snap2.speedLimitMax);
    TEST_ASSERT_EQUAL_UINT8(18, snap2.audioVolume);
    TEST_ASSERT_EQUAL_UINT16(55, snap2.snd_cat_alrm_lo);
    TEST_ASSERT_EQUAL_UINT16(65, snap2.snd_cat_alrm_hi);
    TEST_ASSERT_EQUAL_INT(true,  snap2.dome_rnd_enable);
    TEST_ASSERT_EQUAL_UINT8(40,  snap2.dome_rnd_speed_pct);
    TEST_ASSERT_EQUAL_INT(true,  snap2.enable_arm2);
    TEST_ASSERT_EQUAL_INT(false, snap2.stationary);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RC_INPUT_SINGLE_SBUS, (uint8_t)snap2.rc_input_mode);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RC_BINDING_SBUS1, (uint8_t)snap2.rc_sbus_arm1.source);
    TEST_ASSERT_EQUAL_UINT8(4,   snap2.rc_sbus_arm1.channel);
    TEST_ASSERT_EQUAL_UINT16(10, snap2.rc_sbus_arm1.deadband);
}

// Test: configApplyToRobotState applies all categories of fields from snapshot
void test_configApplyToRobotState_applies_all_categories() {
    // Create a snapshot with distinct non-default values for every category
    ConfigSnapshot snap = {};

    // Speed
    snap.speedLimitMax      = 650;
    snap.speedPresetSlow    = 120;
    snap.speedPresetNormal  = 320;
    snap.speedPresetTurbo   = 650;
    snap.speedPresetActive  = SpeedPresetId::Slow;

    // Timeouts
    snap.sbusTimeoutMs      = 300;
    snap.webDriveTimeoutMs  = 500;

    // Audio
    snap.audioVolume        = 15;
    snap.logLevel           = 3;

    // Audio tracks (sample)
    snap.snd_scream         = 250;
    snap.snd_faint          = 251;

    // Servo pulse widths
    snap.arm1_open_us       = 2050;
    snap.arm1_close_us      = 950;
    snap.arm2_open_us       = 2150;
    snap.arm2_close_us      = 850;
    snap.aux1_open_us       = 2000;
    snap.aux1_close_us      = 1000;
    snap.aux2_open_us       = 2100;
    snap.aux2_close_us      = 900;
    snap.aux3_open_us       = 2200;
    snap.aux3_close_us      = 800;

    // Servo types
    snap.arm1_type          = SERVO_COMP_MG996R;
    snap.arm2_type          = SERVO_COMP_MG90S;
    snap.aux1_type          = SERVO_COMP_RGB;
    snap.aux2_type          = SERVO_COMP_NONE;
    snap.aux3_type          = SERVO_COMP_MG90S;

    // Dome
    snap.dome_min_speed     = 0.2f;
    snap.dome_max_speed     = 0.95f;
    snap.dome_neutral_us    = 1520;
    snap.dome_min_pulse_us  = 1050;
    snap.dome_max_pulse_us  = 1950;
    snap.dome_speed_limit_pct = 85;
    snap.dome_rnd_enable    = true;
    snap.dome_rnd_speed_pct = 40;
    snap.dome_rnd_pause_min = 5;
    snap.dome_rnd_pause_max = 15;
    snap.dome_rnd_move_ms   = 3500;
    snprintf(snap.dome_wifi_peer_ip, sizeof(snap.dome_wifi_peer_ip), "192.168.0.99");

    // Sequence timing
    snap.seq_open_ms        = 800;
    snap.seq_close_ms       = 1200;

    // AUX LED
    snap.aux_led_pin        = 3;
    snap.aux_led_count      = 12;

    // Feature toggles
    snap.enable_arm1        = true;
    snap.enable_arm2        = false;
    snap.enable_aux1        = true;
    snap.enable_dome        = true;
    snap.enable_rc_ch1      = true;
    snap.enable_rc_ch2      = false;
    snap.single_sbus_use_ch2 = true;
    snap.enable_s1_hoverboard = true;
    snap.enable_s2_sound    = false;
    snap.enable_s3_dome_ctrl = true;
    snap.stationary         = true;
    snap.rc_input_mode      = RC_INPUT_DUAL_SBUS;

    // RC bindings (Tier 1)
    snap.rc_pwm_drive_speed = defaultPwmBinding(1);
    snap.rc_sbus_drive_speed = defaultSbusBinding(RC_BINDING_SBUS1, 1);

    // RC bindings (Tier 2)
    snap.rc_arm1 = makeRcTriggerBinding(RC_BINDING_SBUS1, 4, SERVO_ACTION_ARM1_TOGGLE, nullptr,
                                        RC_SBUS_DEFAULT_MIN, RC_SBUS_DEFAULT_CENTER,
                                        RC_SBUS_DEFAULT_MAX, 0,
                                        rcTriggerDefaultReverse(RC_BINDING_SBUS1, 4));
    snap.rc_arm2 = disabledRcTriggerBinding();

    // Apply the snapshot to robotState
    configApplyToRobotState(snap);

    // Assert every cfg_* field was set correctly
    TEST_ASSERT_EQUAL_INT16(650, robotState.cfg_speedLimitMax);
    TEST_ASSERT_EQUAL_INT16(120, robotState.cfg_speedPresetSlow);
    TEST_ASSERT_EQUAL_INT16(320, robotState.cfg_speedPresetNormal);
    TEST_ASSERT_EQUAL_INT16(650, robotState.cfg_speedPresetTurbo);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)SpeedPresetId::Slow, (uint8_t)robotState.cfg_speedPresetActive);

    TEST_ASSERT_EQUAL_UINT32(300, robotState.cfg_sbusTimeoutMs);
    TEST_ASSERT_EQUAL_UINT32(500, robotState.cfg_webDriveTimeoutMs);

    TEST_ASSERT_EQUAL_UINT8(15, robotState.cfg_audioVolume);
    TEST_ASSERT_EQUAL_UINT8(3, robotState.cfg_logLevel);

    TEST_ASSERT_EQUAL_UINT16(250, robotState.cfg_snd_scream);
    TEST_ASSERT_EQUAL_UINT16(251, robotState.cfg_snd_faint);

    TEST_ASSERT_EQUAL_UINT16(2050, robotState.cfg_arm1_open_us);
    TEST_ASSERT_EQUAL_UINT16(950, robotState.cfg_arm1_close_us);
    TEST_ASSERT_EQUAL_UINT16(2150, robotState.cfg_arm2_open_us);
    TEST_ASSERT_EQUAL_UINT16(850, robotState.cfg_arm2_close_us);

    TEST_ASSERT_EQUAL_UINT8((uint8_t)SERVO_COMP_MG996R, (uint8_t)robotState.cfg_arm1_type);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)SERVO_COMP_MG90S, (uint8_t)robotState.cfg_arm2_type);

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.2f, robotState.cfg_dome_min_speed);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.95f, robotState.cfg_dome_max_speed);
    TEST_ASSERT_EQUAL_UINT16(1520, robotState.cfg_dome_neutral_us);
    TEST_ASSERT_EQUAL_UINT8(85, robotState.cfg_dome_speed_limit_pct);
    TEST_ASSERT_EQUAL_INT(true, robotState.cfg_dome_rnd_enable);
    TEST_ASSERT_EQUAL_STRING("192.168.0.99", robotState.cfg_dome_wifi_peer_ip);

    TEST_ASSERT_EQUAL_UINT16(800, robotState.cfg_seq_open_ms);
    TEST_ASSERT_EQUAL_UINT16(1200, robotState.cfg_seq_close_ms);

    TEST_ASSERT_EQUAL_UINT8(3, robotState.cfg_aux_led_pin);
    TEST_ASSERT_EQUAL_UINT8(12, robotState.cfg_aux_led_count);

    TEST_ASSERT_EQUAL_INT(true, robotState.cfg_enable_arm1);
    TEST_ASSERT_EQUAL_INT(false, robotState.cfg_enable_arm2);
    TEST_ASSERT_EQUAL_INT(true, robotState.cfg_enable_dome);
    TEST_ASSERT_EQUAL_INT(true, robotState.cfg_stationary);

    // Also verify a non-cfg field was NOT modified (contract test)
    // stationary is a cfg field, so check another runtime field
    // Let's verify the robotState was initialized to zero, so non-cfg fields remain at their initial values
    TEST_ASSERT_EQUAL_INT(0, robotState.driveSpeed);  // This is a non-cfg field
}

// Test: configApplyToRobotState does not touch non-cfg fields
void test_configApplyToRobotState_does_not_touch_non_cfg_fields() {
    // Set a non-cfg runtime field to a known value
    robotState.driveSpeed = 123;  // This is a non-cfg field
    robotState.driveSteer = 456;  // Another non-cfg field

    // Create a snapshot with different values for cfg fields
    ConfigSnapshot snap = {};
    snap.stationary = true;

    // Apply the snapshot
    configApplyToRobotState(snap);

    // The cfg field should have been set
    TEST_ASSERT_EQUAL_INT(true, robotState.cfg_stationary);

    // But the non-cfg fields should NOT have changed
    TEST_ASSERT_EQUAL_INT(123, robotState.driveSpeed);
    TEST_ASSERT_EQUAL_INT(456, robotState.driveSteer);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_configLoad_empty_nvs_returns_defaults);
    RUN_TEST(test_configLoad_save_roundtrip);
    RUN_TEST(test_configValidate_speed_limit_out_of_range);
    RUN_TEST(test_configValidate_speed_limit_valid);
    RUN_TEST(test_configValidate_sbus_timeout_out_of_range);
    RUN_TEST(test_configValidate_sbus_timeout_valid);
    RUN_TEST(test_configValidate_audio_volume);
    RUN_TEST(test_configValidate_servo_pulses);
    RUN_TEST(test_configValidate_servo_types);
    RUN_TEST(test_configValidate_dome_speed);
    RUN_TEST(test_configValidate_booleans);
    RUN_TEST(test_configLoad_legacy_schema_v0);
    RUN_TEST(test_configLoad_schema_mismatch);
    RUN_TEST(test_configLoad_save_audio_tracks);
    RUN_TEST(test_configLoad_save_servo_config);
    RUN_TEST(test_configLoad_save_feature_toggles);
    RUN_TEST(test_configValidate_dome_speed_pct);
    RUN_TEST(test_configValidate_aux_led_pin);
    RUN_TEST(test_configValidate_sequence_timing);
    RUN_TEST(test_configValidate_rc_input_mode);
    RUN_TEST(test_configLoad_save_dome_wifi_peer_ip);
    RUN_TEST(test_configLoad_save_dome_wifi_peer_ip_empty);
    RUN_TEST(test_configLoad_save_moodcat_12bit_mask);
    RUN_TEST(test_configSnapshotFromRobotState_captures_all_categories);
    RUN_TEST(test_configSnapshotFromRobotState_save_round_trip);
    RUN_TEST(test_configApplyToRobotState_applies_all_categories);
    RUN_TEST(test_configApplyToRobotState_does_not_touch_non_cfg_fields);
    return UNITY_END();
}
