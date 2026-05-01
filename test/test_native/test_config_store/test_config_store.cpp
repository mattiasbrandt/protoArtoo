// =============================================================================
// test/test_native/test_config_store/test_config_store.cpp
//
// Native unit tests for config_store module.
// Tests cover load/save round-trips, validation, schema versioning, and defaults.
// =============================================================================

#include <stdint.h>

#include <unity.h>

#include "config_store.h"

void setUp() {
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
    return UNITY_END();
}
