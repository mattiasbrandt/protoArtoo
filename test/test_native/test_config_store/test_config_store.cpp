// =============================================================================
// test/test_native/test_config_store/test_config_store.cpp
//
// Native unit tests for config_store module.
// Tests cover load/save round-trips, validation, schema versioning, and defaults.
// =============================================================================

#include <cstring>
#include <cstdio>
#include <stdint.h>

#include <unity.h>

#include "config_store.h"
#include "config_cache.h"
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
    TEST_ASSERT_EQUAL_INT16(SPEED_LIMIT_MAX, snap.drive.speedLimitMax);
    TEST_ASSERT_EQUAL_INT16(SPEED_PRESET_SLOW, snap.drive.speedPresetSlow);
    TEST_ASSERT_EQUAL_INT16(SPEED_PRESET_NORMAL, snap.drive.speedPresetNormal);
    TEST_ASSERT_EQUAL_INT16(SPEED_PRESET_TURBO, snap.drive.speedPresetTurbo);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)SpeedPresetId::Normal, (uint8_t)snap.drive.speedPresetActive);
    TEST_ASSERT_EQUAL_UINT32(SBUS_TIMEOUT_MS, snap.drive.sbusTimeoutMs);
    TEST_ASSERT_EQUAL_UINT32(WEB_DRIVE_TIMEOUT_MS, snap.drive.webDriveTimeoutMs);
    TEST_ASSERT_EQUAL_UINT8(20, snap.audio.audioVolume);
    TEST_ASSERT_EQUAL_STRING(DROID_NAME_DEFAULT, snap.system.droid_name);
    TEST_ASSERT_FALSE(snap.system.mdns_use_name);
}

// Test: Save and reload returns same snapshot
void test_configLoad_save_roundtrip() {
    ConfigSnapshot snap1 = {};
    snap1.drive.speedLimitMax = 500;
    snap1.drive.speedPresetSlow = 150;
    snap1.drive.speedPresetNormal = 300;
    snap1.drive.speedPresetTurbo = 500;
    snap1.drive.sbusTimeoutMs = 150;
    snap1.drive.webDriveTimeoutMs = 400;
    snap1.audio.audioVolume = 25;
    snprintf(snap1.system.droid_name, sizeof(snap1.system.droid_name), "%s", "r2-unit");
    snap1.system.mdns_use_name = true;

    Preferences prefs;
    prefs.begin("proto", false);
    bool saveResult = configSave(prefs, snap1);
    TEST_ASSERT_TRUE(saveResult);

    ConfigSnapshot snap2 = {};
    bool loadResult = configLoad(prefs, &snap2);
    prefs.end();

    TEST_ASSERT_TRUE(loadResult);
    TEST_ASSERT_EQUAL_INT16(snap1.drive.speedLimitMax, snap2.drive.speedLimitMax);
    TEST_ASSERT_EQUAL_INT16(snap1.drive.speedPresetSlow, snap2.drive.speedPresetSlow);
    TEST_ASSERT_EQUAL_INT16(snap1.drive.speedPresetNormal, snap2.drive.speedPresetNormal);
    TEST_ASSERT_EQUAL_INT16(snap1.drive.speedPresetTurbo, snap2.drive.speedPresetTurbo);
    TEST_ASSERT_EQUAL_UINT32(snap1.drive.sbusTimeoutMs, snap2.drive.sbusTimeoutMs);
    TEST_ASSERT_EQUAL_UINT32(snap1.drive.webDriveTimeoutMs, snap2.drive.webDriveTimeoutMs);
    TEST_ASSERT_EQUAL_UINT8(snap1.audio.audioVolume, snap2.audio.audioVolume);
    TEST_ASSERT_EQUAL_STRING(snap1.system.droid_name, snap2.system.droid_name);
    TEST_ASSERT_TRUE(snap2.system.mdns_use_name);
}

void test_configLoad_save_identity_accepts_lowercase() {
    ConfigSnapshot snap1 = {};
    snprintf(snap1.system.droid_name, sizeof(snap1.system.droid_name), "%s", "r2-unit");
    snap1.system.mdns_use_name = true;

    Preferences prefs;
    prefs.begin("proto", false);
    TEST_ASSERT_TRUE(configSave(prefs, snap1));

    ConfigSnapshot snap2 = {};
    TEST_ASSERT_TRUE(configLoad(prefs, &snap2));
    prefs.end();

    TEST_ASSERT_EQUAL_STRING("r2-unit", snap2.system.droid_name);
    TEST_ASSERT_TRUE(snap2.system.mdns_use_name);
}

void test_configLoad_save_identity_rejects_uppercase_to_default() {
    ConfigSnapshot snap1 = {};
    snprintf(snap1.system.droid_name, sizeof(snap1.system.droid_name), "%s", "R2-Unit");
    snap1.system.mdns_use_name = true;

    Preferences prefs;
    prefs.begin("proto", false);
    TEST_ASSERT_TRUE(configSave(prefs, snap1));

    ConfigSnapshot snap2 = {};
    TEST_ASSERT_TRUE(configLoad(prefs, &snap2));
    prefs.end();

    TEST_ASSERT_EQUAL_STRING(DROID_NAME_DEFAULT, snap2.system.droid_name);
    TEST_ASSERT_TRUE(snap2.system.mdns_use_name);
}

void test_configResolvedMdnsHostname_uses_identity_name() {
    SystemConfig system = {};
    snprintf(system.droid_name, sizeof(system.droid_name), "%s", "r2-unit");
    system.mdns_use_name = true;

    char hostname[33] = {};
    configResolvedMdnsHostname(system, hostname, sizeof(hostname));

    TEST_ASSERT_EQUAL_STRING("r2-unit", hostname);
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
    TEST_ASSERT_EQUAL_INT16(400, snap.drive.speedLimitMax);

    // Verify schema version was stamped
    prefs.begin("proto", true);
    uint8_t storedVersion = prefs.getUChar(CONFIG_SCHEMA_VERSION_KEY, 0);
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
    prefs.putUChar(CONFIG_SCHEMA_VERSION_KEY, badVersion);
    prefs.putShort("spd_max", 400);

    ConfigSnapshot snap = {};
    bool result = configLoad(prefs, &snap);
    prefs.end();

    // Should return false and fill with defaults, not the NVS value
    TEST_ASSERT_FALSE(result);
    TEST_ASSERT_EQUAL_INT16(SPEED_LIMIT_MAX, snap.drive.speedLimitMax);

    // Verify schema version was updated
    prefs.begin("proto", true);
    uint8_t storedVersion = prefs.getUChar(CONFIG_SCHEMA_VERSION_KEY, 0);
    prefs.end();
    TEST_ASSERT_EQUAL_UINT8(CONFIG_SCHEMA_VERSION, storedVersion);
}

// Test: Save all audio track fields
void test_configLoad_save_audio_tracks() {
    ConfigSnapshot snap1 = {};
    snap1.audio.snd_scream = 100;
    snap1.audio.snd_faint = 101;
    snap1.audio.snd_leia = 102;
    snap1.audio.snd_cantina_s = 103;
    snap1.audio.snd_sw_theme = 104;
    snap1.audio.snd_imp_march = 105;
    snap1.audio.snd_cantina_l = 106;
    snap1.audio.snd_startup = 107;
    snap1.audio.snd_doodoo = 108;

    Preferences prefs;
    prefs.begin("proto", false);
    bool saveResult = configSave(prefs, snap1);
    TEST_ASSERT_TRUE(saveResult);

    ConfigSnapshot snap2 = {};
    bool loadResult = configLoad(prefs, &snap2);
    prefs.end();

    TEST_ASSERT_TRUE(loadResult);
    TEST_ASSERT_EQUAL_UINT16(snap1.audio.snd_scream, snap2.audio.snd_scream);
    TEST_ASSERT_EQUAL_UINT16(snap1.audio.snd_faint, snap2.audio.snd_faint);
    TEST_ASSERT_EQUAL_UINT16(snap1.audio.snd_leia, snap2.audio.snd_leia);
    TEST_ASSERT_EQUAL_UINT16(snap1.audio.snd_doodoo, snap2.audio.snd_doodoo);
}

// Test: Save all servo fields
void test_configLoad_save_servo_config() {
    ConfigSnapshot snap1 = {};
    snap1.servo.arm1_open_us = 2100;
    snap1.servo.arm1_close_us = 900;
    snap1.servo.arm2_open_us = 2200;
    snap1.servo.arm2_close_us = 800;
    snap1.servo.arm1_type = SERVO_COMP_MG996R;
    snap1.servo.arm2_type = SERVO_COMP_MG90S;
    snap1.servo.aux1_type = SERVO_COMP_RGB;

    Preferences prefs;
    prefs.begin("proto", false);
    bool saveResult = configSave(prefs, snap1);
    TEST_ASSERT_TRUE(saveResult);

    ConfigSnapshot snap2 = {};
    bool loadResult = configLoad(prefs, &snap2);
    prefs.end();

    TEST_ASSERT_TRUE(loadResult);
    TEST_ASSERT_EQUAL_UINT16(snap1.servo.arm1_open_us, snap2.servo.arm1_open_us);
    TEST_ASSERT_EQUAL_UINT16(snap1.servo.arm1_close_us, snap2.servo.arm1_close_us);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)snap1.servo.arm1_type, (uint8_t)snap2.servo.arm1_type);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)snap1.servo.aux1_type, (uint8_t)snap2.servo.aux1_type);
}

// Test: Save all feature toggle fields
void test_configLoad_save_feature_toggles() {
    ConfigSnapshot snap1 = {};
    snap1.system.enable_arm1 = true;
    snap1.system.enable_arm2 = true;
    snap1.system.enable_dome_esc = false;
    snap1.system.enable_rc_ch1 = true;
    snap1.system.enable_drive = true;
    snap1.system.stationary = true;

    Preferences prefs;
    prefs.begin("proto", false);
    bool saveResult = configSave(prefs, snap1);
    TEST_ASSERT_TRUE(saveResult);

    ConfigSnapshot snap2 = {};
    bool loadResult = configLoad(prefs, &snap2);
    prefs.end();

    TEST_ASSERT_TRUE(loadResult);
    TEST_ASSERT_EQUAL_INT(true, snap2.system.enable_arm1);
    TEST_ASSERT_EQUAL_INT(true, snap2.system.enable_arm2);
    TEST_ASSERT_EQUAL_INT(false, snap2.system.enable_dome_esc);
    TEST_ASSERT_EQUAL_INT(true, snap2.system.enable_rc_ch1);
    TEST_ASSERT_EQUAL_INT(true, snap2.system.enable_drive);
    TEST_ASSERT_EQUAL_INT(true, snap2.system.stationary);
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
    snprintf(snap1.dome.dome_wifi_peer_ip, sizeof(snap1.dome.dome_wifi_peer_ip), "192.168.1.42");

    Preferences prefs;
    prefs.begin("proto", false);
    bool saveResult = configSave(prefs, snap1);
    TEST_ASSERT_TRUE(saveResult);

    ConfigSnapshot snap2 = {};
    bool loadResult = configLoad(prefs, &snap2);
    prefs.end();

    TEST_ASSERT_TRUE(loadResult);
    TEST_ASSERT_EQUAL_STRING(snap1.dome.dome_wifi_peer_ip, snap2.dome.dome_wifi_peer_ip);
}

// Test: Empty dome wifi IP is preserved
void test_configLoad_save_dome_wifi_peer_ip_empty() {
    ConfigSnapshot snap1 = {};
    snap1.dome.dome_wifi_peer_ip[0] = '\0';

    Preferences prefs;
    prefs.begin("proto", false);
    bool saveResult = configSave(prefs, snap1);
    TEST_ASSERT_TRUE(saveResult);

    ConfigSnapshot snap2 = {};
    bool loadResult = configLoad(prefs, &snap2);
    prefs.end();

    TEST_ASSERT_TRUE(loadResult);
    TEST_ASSERT_EQUAL_CHAR('\0', snap2.dome.dome_wifi_peer_ip[0]);
}

// Test: Mood category masks are truncated to 12-bit
void test_configLoad_save_moodcat_12bit_mask() {
    ConfigSnapshot snap1 = {};
    snap1.audio.snd_moodcat_quiet = 0x1234;  // Will be truncated to 0x0234
    snap1.audio.snd_moodcat_mid = 0xFFFF;    // Will be truncated to 0x0FFF

    Preferences prefs;
    prefs.begin("proto", false);
    bool saveResult = configSave(prefs, snap1);
    TEST_ASSERT_TRUE(saveResult);

    ConfigSnapshot snap2 = {};
    bool loadResult = configLoad(prefs, &snap2);
    prefs.end();

    TEST_ASSERT_TRUE(loadResult);
    TEST_ASSERT_EQUAL_UINT16(0x0234, snap2.audio.snd_moodcat_quiet);
    TEST_ASSERT_EQUAL_UINT16(0x0FFF, snap2.audio.snd_moodcat_mid);
}

// Test: configCacheRead captures one field from each category group.
// This guards against a field silently missing from the function body, which
// would cause configSave() to write zeros to NVS for that field.
void test_configCacheRead_captures_all_categories() {
    ConfigSnapshot seeded = {};

    // Speed
    seeded.drive.speedLimitMax      = 750;
    seeded.drive.speedPresetSlow    = 150;
    seeded.drive.speedPresetNormal  = 350;
    seeded.drive.speedPresetTurbo   = 750;
    seeded.drive.speedPresetActive  = SpeedPresetId::Turbo;

    // Timeouts
    seeded.drive.sbusTimeoutMs      = 250;
    seeded.drive.webDriveTimeoutMs  = 600;

    // Audio scalars
    seeded.audio.audioVolume        = 22;
    seeded.system.logLevel          = 2;

    // Named audio tracks
    seeded.audio.snd_scream         = 200;
    seeded.audio.snd_faint          = 201;
    seeded.audio.snd_startup        = 255;
    seeded.audio.snd_rand_min       = 5;
    seeded.audio.snd_rand_max       = 80;

    // Mood category fields — the ones most likely to be missed
    seeded.audio.snd_moodcat_quiet  = 0x0ABC;
    seeded.audio.snd_moodcat_full   = 0x0FFF;
    seeded.audio.snd_cat_gen_lo     = 10;
    seeded.audio.snd_cat_gen_hi     = 20;
    seeded.audio.snd_cat_whis_lo    = 30;
    seeded.audio.snd_cat_whis_hi    = 40;

    // Servo
    seeded.servo.arm1_open_us       = 2100;
    seeded.servo.arm1_close_us      = 900;
    seeded.servo.arm1_type          = SERVO_COMP_MG996R;
    seeded.servo.aux3_type          = SERVO_COMP_RGB;
    seeded.servo.aux1_open_us       = 1800;
    seeded.servo.aux1_close_us      = 1200;

    // Dome
    seeded.dome.dome_min_speed     = 0.1f;
    seeded.dome.dome_max_speed     = 0.9f;
    seeded.dome.dome_neutral_us    = 1510;
    seeded.dome.dome_speed_limit_pct = 75;
    seeded.dome.dome_rnd_enable    = true;
    seeded.dome.dome_rnd_speed_pct = 35;
    seeded.dome.dome_rnd_pause_min = 4;
    seeded.dome.dome_rnd_pause_max = 10;
    seeded.dome.dome_rnd_move_ms   = 3000;
    snprintf(seeded.dome.dome_wifi_peer_ip, sizeof(seeded.dome.dome_wifi_peer_ip), "10.0.0.5");

    // Sequence timing
    seeded.servo.seq_open_ms        = 400;
    seeded.servo.seq_close_ms       = 600;

    // AUX LED
    seeded.servo.aux_led_pin        = 2;
    seeded.servo.aux_led_count      = 8;

    // Feature toggles
    seeded.system.enable_arm1        = true;
    seeded.system.enable_dome_esc        = true;
    seeded.system.stationary         = true;
    seeded.system.rc_input_mode      = RC_INPUT_DUAL_SBUS;
    seeded.system.single_sbus_use_ch2 = true;
    seeded.system.enable_drive = true;

    // RC backbone binding
    seeded.system.rc_sbus_drive_speed =
        makeRcBindingConfig(RC_BINDING_SBUS1, 3, 200, 1000, 1800, 50, true);
    seeded.system.rc_pwm_drive_steer  =
        makeRcBindingConfig(RC_BINDING_PWM, 2, 1000, 1500, 2000, 0, false);

    // RC trigger binding
    seeded.system.rc_arm1.source     = RC_BINDING_SBUS1;
    seeded.system.rc_arm1.channel    = 5;
    seeded.system.rc_arm1.target     = SERVO_ACTION_ARM1_TOGGLE;
    seeded.system.rc_free3.source    = RC_BINDING_SBUS2;
    seeded.system.rc_free3.channel   = 7;

    configCacheApply(seeded);

    ConfigSnapshot snap = {};
    configCacheRead(&snap);

    // Speed
    TEST_ASSERT_EQUAL_INT16(750, snap.drive.speedLimitMax);
    TEST_ASSERT_EQUAL_INT16(150, snap.drive.speedPresetSlow);
    TEST_ASSERT_EQUAL_INT16(350, snap.drive.speedPresetNormal);
    TEST_ASSERT_EQUAL_INT16(750, snap.drive.speedPresetTurbo);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)SpeedPresetId::Turbo, (uint8_t)snap.drive.speedPresetActive);

    // Timeouts
    TEST_ASSERT_EQUAL_UINT32(250, snap.drive.sbusTimeoutMs);
    TEST_ASSERT_EQUAL_UINT32(600, snap.drive.webDriveTimeoutMs);

    // Audio scalars
    TEST_ASSERT_EQUAL_UINT8(22, snap.audio.audioVolume);
    TEST_ASSERT_EQUAL_UINT8(2, snap.system.logLevel);

    // Named audio tracks
    TEST_ASSERT_EQUAL_UINT16(200, snap.audio.snd_scream);
    TEST_ASSERT_EQUAL_UINT16(201, snap.audio.snd_faint);
    TEST_ASSERT_EQUAL_UINT16(255, snap.audio.snd_startup);
    TEST_ASSERT_EQUAL_UINT16(5,  snap.audio.snd_rand_min);
    TEST_ASSERT_EQUAL_UINT16(80, snap.audio.snd_rand_max);

    // Mood category fields
    TEST_ASSERT_EQUAL_UINT16(0x0ABC, snap.audio.snd_moodcat_quiet);
    TEST_ASSERT_EQUAL_UINT16(0x0FFF, snap.audio.snd_moodcat_full);
    TEST_ASSERT_EQUAL_UINT16(10, snap.audio.snd_cat_gen_lo);
    TEST_ASSERT_EQUAL_UINT16(20, snap.audio.snd_cat_gen_hi);
    TEST_ASSERT_EQUAL_UINT16(30, snap.audio.snd_cat_whis_lo);
    TEST_ASSERT_EQUAL_UINT16(40, snap.audio.snd_cat_whis_hi);

    // Servo
    TEST_ASSERT_EQUAL_UINT16(2100, snap.servo.arm1_open_us);
    TEST_ASSERT_EQUAL_UINT16(900,  snap.servo.arm1_close_us);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)SERVO_COMP_MG996R, (uint8_t)snap.servo.arm1_type);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)SERVO_COMP_RGB,    (uint8_t)snap.servo.aux3_type);
    TEST_ASSERT_EQUAL_UINT16(1800, snap.servo.aux1_open_us);
    TEST_ASSERT_EQUAL_UINT16(1200, snap.servo.aux1_close_us);

    // Dome
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.1f, snap.dome.dome_min_speed);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.9f, snap.dome.dome_max_speed);
    TEST_ASSERT_EQUAL_UINT16(1510, snap.dome.dome_neutral_us);
    TEST_ASSERT_EQUAL_UINT8(75, snap.dome.dome_speed_limit_pct);
    TEST_ASSERT_EQUAL_INT(true, snap.dome.dome_rnd_enable);
    TEST_ASSERT_EQUAL_UINT8(35, snap.dome.dome_rnd_speed_pct);
    TEST_ASSERT_EQUAL_UINT8(4,  snap.dome.dome_rnd_pause_min);
    TEST_ASSERT_EQUAL_UINT8(10, snap.dome.dome_rnd_pause_max);
    TEST_ASSERT_EQUAL_UINT16(3000, snap.dome.dome_rnd_move_ms);
    TEST_ASSERT_EQUAL_STRING("10.0.0.5", snap.dome.dome_wifi_peer_ip);

    // Sequence timing
    TEST_ASSERT_EQUAL_UINT16(400, snap.servo.seq_open_ms);
    TEST_ASSERT_EQUAL_UINT16(600, snap.servo.seq_close_ms);

    // AUX LED
    TEST_ASSERT_EQUAL_UINT8(2, snap.servo.aux_led_pin);
    TEST_ASSERT_EQUAL_UINT8(8, snap.servo.aux_led_count);

    // Feature toggles
    TEST_ASSERT_EQUAL_INT(true, snap.system.enable_arm1);
    TEST_ASSERT_EQUAL_INT(true, snap.system.enable_dome_esc);
    TEST_ASSERT_EQUAL_INT(true, snap.system.stationary);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RC_INPUT_DUAL_SBUS, (uint8_t)snap.system.rc_input_mode);
    TEST_ASSERT_EQUAL_INT(true, snap.system.single_sbus_use_ch2);
    TEST_ASSERT_EQUAL_INT(true, snap.system.enable_drive);

    // RC backbone binding
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RC_BINDING_SBUS1, (uint8_t)snap.system.rc_sbus_drive_speed.source);
    TEST_ASSERT_EQUAL_UINT8(3,   snap.system.rc_sbus_drive_speed.channel);
    TEST_ASSERT_EQUAL_UINT16(200, snap.system.rc_sbus_drive_speed.min);
    TEST_ASSERT_EQUAL_INT(true,  snap.system.rc_sbus_drive_speed.reverse);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RC_BINDING_PWM, (uint8_t)snap.system.rc_pwm_drive_steer.source);
    TEST_ASSERT_EQUAL_UINT8(2, snap.system.rc_pwm_drive_steer.channel);

    // RC trigger binding
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RC_BINDING_SBUS1, (uint8_t)snap.system.rc_arm1.source);
    TEST_ASSERT_EQUAL_UINT8(5, snap.system.rc_arm1.channel);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)SERVO_ACTION_ARM1_TOGGLE, (uint8_t)snap.system.rc_arm1.target);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RC_BINDING_SBUS2, (uint8_t)snap.system.rc_free3.source);
    TEST_ASSERT_EQUAL_UINT8(7, snap.system.rc_free3.channel);
}

void test_active_rc_config_survives_saved_toggle_and_mode_changes() {
    ConfigSnapshot boot = {};
    boot.system.rc_input_mode = RC_INPUT_SINGLE_SBUS;
    boot.system.single_sbus_use_ch2 = false;
    boot.system.enable_rc_ch1 = true;
    boot.system.enable_rc_ch2 = false;
    boot.system.enable_rc_ch3 = true;
    boot.system.enable_rc_ch4 = false;
    boot.system.enable_rc_ch5 = true;
    boot.system.enable_rc_ch6 = false;
    boot.system.enable_dome_esc = true;
    boot.system.enable_arm1 = true;
    boot.system.enable_arm2 = false;
    boot.system.enable_audio = true;
    configCacheSetActiveRcInput(rcInputActiveConfigFromSystem(boot.system));

    ConfigSnapshot saved = boot;
    saved.system.rc_input_mode = RC_INPUT_DUAL_SBUS;
    saved.system.single_sbus_use_ch2 = true;
    saved.system.enable_rc_ch1 = false;
    saved.system.enable_rc_ch2 = true;
    saved.system.enable_rc_ch3 = false;
    saved.system.enable_rc_ch4 = true;
    saved.system.enable_rc_ch5 = false;
    saved.system.enable_rc_ch6 = true;
    saved.system.enable_dome_esc = false;
    saved.system.enable_arm1 = false;
    saved.system.enable_arm2 = true;
    saved.system.enable_audio = false;
    configCacheApply(saved);

    RcInputActiveConfig active = {};
    configCacheReadActiveRcInput(&active);

    TEST_ASSERT_EQUAL_UINT8((uint8_t)RC_INPUT_SINGLE_SBUS, (uint8_t)active.mode);
    TEST_ASSERT_FALSE(active.useCh2);
    TEST_ASSERT_TRUE(active.enableRc[0]);
    TEST_ASSERT_FALSE(active.enableRc[1]);
    TEST_ASSERT_TRUE(active.enableRc[2]);
    TEST_ASSERT_FALSE(active.enableRc[3]);
    TEST_ASSERT_TRUE(active.enableRc[4]);
    TEST_ASSERT_FALSE(active.enableRc[5]);
    TEST_ASSERT_TRUE(active.enableDome);
    TEST_ASSERT_TRUE(active.enableArm1);
    TEST_ASSERT_FALSE(active.enableArm2);
    TEST_ASSERT_TRUE(active.enableSound);
}

// Component Toggles are staged at reboot (ADR 0027): the Active snapshot
// must keep answering the boot-time value even after a later Console/REST
// write changes the saved config_cache value underneath it - this is the
// exact "saved vs active" divergence the Console's read path (#226) reports.
void test_active_component_toggles_survive_a_later_saved_write() {
    ConfigSnapshot boot = {};
    boot.system.enable_arm1 = true;
    boot.system.enable_arm2 = false;
    boot.system.enable_audio = true;
    boot.system.enable_protor2link = false;
    configCacheSetActiveComponentToggles(boot.system);

    // A later write changes the saved cache without a reboot in between.
    ConfigSnapshot saved = boot;
    saved.system.enable_arm1 = false;
    saved.system.enable_arm2 = true;
    saved.system.enable_audio = false;
    saved.system.enable_protor2link = true;
    configCacheApply(saved);

    // Active still reflects what actually booted...
    TEST_ASSERT_TRUE(configCacheReadActiveComponentToggle(0));   // enable_arm1
    TEST_ASSERT_FALSE(configCacheReadActiveComponentToggle(1));  // enable_arm2
    TEST_ASSERT_TRUE(configCacheReadActiveComponentToggle(13));  // enable_audio
    TEST_ASSERT_FALSE(configCacheReadActiveComponentToggle(14)); // enable_protor2link

    // ...while the saved cache carries the new, not-yet-rebooted values.
    ConfigSnapshot readBack = {};
    configCacheRead(&readBack);
    TEST_ASSERT_FALSE(readBack.system.enable_arm1);
    TEST_ASSERT_TRUE(readBack.system.enable_arm2);
    TEST_ASSERT_FALSE(readBack.system.enable_audio);
    TEST_ASSERT_TRUE(readBack.system.enable_protor2link);
}

// Test: full save path — config cache -> configSave -> configLoad
// Verifies that the complete chain used by saveConfigToNvs() preserves values correctly.
void test_configCacheRead_save_round_trip() {
    ConfigSnapshot seeded = {};
    seeded.drive.speedLimitMax      = 600;
    seeded.audio.audioVolume        = 18;
    seeded.audio.snd_cat_alrm_lo    = 55;
    seeded.audio.snd_cat_alrm_hi    = 65;
    seeded.dome.dome_rnd_enable     = true;
    seeded.dome.dome_rnd_speed_pct  = 40;
    seeded.system.enable_arm2       = true;
    seeded.system.stationary        = false;
    seeded.system.rc_input_mode     = RC_INPUT_SINGLE_SBUS;
    seeded.system.rc_sbus_arm1 =
        makeRcBindingConfig(RC_BINDING_SBUS1, 4, 172, 992, 1811, 10, false);
    configCacheApply(seeded);

    ConfigSnapshot snap1 = {};
    configCacheRead(&snap1);

    Preferences prefs;
    prefs.begin("proto", false);
    bool saveOk = configSave(prefs, snap1);
    TEST_ASSERT_TRUE(saveOk);

    ConfigSnapshot snap2 = {};
    bool loadOk = configLoad(prefs, &snap2);
    prefs.end();
    TEST_ASSERT_TRUE(loadOk);

    TEST_ASSERT_EQUAL_INT16(600, snap2.drive.speedLimitMax);
    TEST_ASSERT_EQUAL_UINT8(18, snap2.audio.audioVolume);
    TEST_ASSERT_EQUAL_UINT16(55, snap2.audio.snd_cat_alrm_lo);
    TEST_ASSERT_EQUAL_UINT16(65, snap2.audio.snd_cat_alrm_hi);
    TEST_ASSERT_EQUAL_INT(true,  snap2.dome.dome_rnd_enable);
    TEST_ASSERT_EQUAL_UINT8(40,  snap2.dome.dome_rnd_speed_pct);
    TEST_ASSERT_EQUAL_INT(true,  snap2.system.enable_arm2);
    TEST_ASSERT_EQUAL_INT(false, snap2.system.stationary);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RC_INPUT_SINGLE_SBUS, (uint8_t)snap2.system.rc_input_mode);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RC_BINDING_SBUS1, (uint8_t)snap2.system.rc_sbus_arm1.source);
    TEST_ASSERT_EQUAL_UINT8(4,   snap2.system.rc_sbus_arm1.channel);
    TEST_ASSERT_EQUAL_UINT16(10, snap2.system.rc_sbus_arm1.deadband);
}

// Test: configCacheApply applies all categories of fields from snapshot
void test_configCacheApply_applies_all_categories() {
    // Create a snapshot with distinct non-default values for every category
    ConfigSnapshot snap = {};

    // Speed
    snap.drive.speedLimitMax      = 650;
    snap.drive.speedPresetSlow    = 120;
    snap.drive.speedPresetNormal  = 320;
    snap.drive.speedPresetTurbo   = 650;
    snap.drive.speedPresetActive  = SpeedPresetId::Slow;

    // Timeouts
    snap.drive.sbusTimeoutMs      = 300;
    snap.drive.webDriveTimeoutMs  = 500;

    // Audio
    snap.audio.audioVolume        = 15;
    snap.system.logLevel           = 3;

    // Audio tracks (sample)
    snap.audio.snd_scream         = 250;
    snap.audio.snd_faint          = 251;

    // Servo pulse widths
    snap.servo.arm1_open_us       = 2050;
    snap.servo.arm1_close_us      = 950;
    snap.servo.arm2_open_us       = 2150;
    snap.servo.arm2_close_us      = 850;
    snap.servo.aux1_open_us       = 2000;
    snap.servo.aux1_close_us      = 1000;
    snap.servo.aux2_open_us       = 2100;
    snap.servo.aux2_close_us      = 900;
    snap.servo.aux3_open_us       = 2200;
    snap.servo.aux3_close_us      = 800;

    // Servo types
    snap.servo.arm1_type          = SERVO_COMP_MG996R;
    snap.servo.arm2_type          = SERVO_COMP_MG90S;
    snap.servo.aux1_type          = SERVO_COMP_RGB;
    snap.servo.aux2_type          = SERVO_COMP_NONE;
    snap.servo.aux3_type          = SERVO_COMP_MG90S;

    // Dome
    snap.dome.dome_min_speed     = 0.2f;
    snap.dome.dome_max_speed     = 0.95f;
    snap.dome.dome_neutral_us    = 1520;
    snap.dome.dome_min_pulse_us  = 1050;
    snap.dome.dome_max_pulse_us  = 1950;
    snap.dome.dome_speed_limit_pct = 85;
    snap.dome.dome_rnd_enable    = true;
    snap.dome.dome_rnd_speed_pct = 40;
    snap.dome.dome_rnd_pause_min = 5;
    snap.dome.dome_rnd_pause_max = 15;
    snap.dome.dome_rnd_move_ms   = 3500;
    snprintf(snap.dome.dome_wifi_peer_ip, sizeof(snap.dome.dome_wifi_peer_ip), "192.168.0.99");

    // Sequence timing
    snap.servo.seq_open_ms        = 800;
    snap.servo.seq_close_ms       = 1200;

    // AUX LED
    snap.servo.aux_led_pin        = 3;
    snap.servo.aux_led_count      = 12;

    // Feature toggles
    snap.system.enable_arm1        = true;
    snap.system.enable_arm2        = false;
    snap.system.enable_aux1        = true;
    snap.system.enable_dome_esc        = true;
    snap.system.enable_rc_ch1      = true;
    snap.system.enable_rc_ch2      = false;
    snap.system.single_sbus_use_ch2 = true;
    snap.system.enable_drive = true;
    snap.system.enable_audio    = false;
    snap.system.enable_protor2link = true;
    snap.system.stationary         = true;
    snap.system.rc_input_mode      = RC_INPUT_DUAL_SBUS;

    // RC bindings (Tier 1)
    snap.system.rc_pwm_drive_speed = defaultPwmBinding(1);
    snap.system.rc_sbus_drive_speed = defaultSbusBinding(RC_BINDING_SBUS1, 1);

    // RC bindings (Tier 2)
    snap.system.rc_arm1 = makeRcTriggerBinding(RC_BINDING_SBUS1, 4, SERVO_ACTION_ARM1_TOGGLE, nullptr,
                                        RC_SBUS_DEFAULT_MIN, RC_SBUS_DEFAULT_CENTER,
                                        RC_SBUS_DEFAULT_MAX, 0,
                                        rcTriggerDefaultReverse(RC_BINDING_SBUS1, 4));
    snap.system.rc_arm2 = disabledRcTriggerBinding();

    // Pre-set a non-cfg sentinel to verify apply does not touch it
    robotState.driveOutputSpeed = 999;

    configCacheApply(snap);

    ConfigSnapshot applied = {};
    configCacheRead(&applied);

    TEST_ASSERT_EQUAL_INT16(650, applied.drive.speedLimitMax);
    TEST_ASSERT_EQUAL_INT16(120, applied.drive.speedPresetSlow);
    TEST_ASSERT_EQUAL_INT16(320, applied.drive.speedPresetNormal);
    TEST_ASSERT_EQUAL_INT16(650, applied.drive.speedPresetTurbo);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)SpeedPresetId::Slow, (uint8_t)applied.drive.speedPresetActive);

    TEST_ASSERT_EQUAL_UINT32(300, applied.drive.sbusTimeoutMs);
    TEST_ASSERT_EQUAL_UINT32(500, applied.drive.webDriveTimeoutMs);

    TEST_ASSERT_EQUAL_UINT8(15, applied.audio.audioVolume);
    TEST_ASSERT_EQUAL_UINT8(3, applied.system.logLevel);

    TEST_ASSERT_EQUAL_UINT16(250, applied.audio.snd_scream);
    TEST_ASSERT_EQUAL_UINT16(251, applied.audio.snd_faint);

    TEST_ASSERT_EQUAL_UINT16(2050, applied.servo.arm1_open_us);
    TEST_ASSERT_EQUAL_UINT16(950, applied.servo.arm1_close_us);
    TEST_ASSERT_EQUAL_UINT16(2150, applied.servo.arm2_open_us);
    TEST_ASSERT_EQUAL_UINT16(850, applied.servo.arm2_close_us);

    TEST_ASSERT_EQUAL_UINT8((uint8_t)SERVO_COMP_MG996R, (uint8_t)applied.servo.arm1_type);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)SERVO_COMP_MG90S, (uint8_t)applied.servo.arm2_type);

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.2f, applied.dome.dome_min_speed);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.95f, applied.dome.dome_max_speed);
    TEST_ASSERT_EQUAL_UINT16(1520, applied.dome.dome_neutral_us);
    TEST_ASSERT_EQUAL_UINT8(85, applied.dome.dome_speed_limit_pct);
    TEST_ASSERT_EQUAL_INT(true, applied.dome.dome_rnd_enable);
    TEST_ASSERT_EQUAL_STRING("192.168.0.99", applied.dome.dome_wifi_peer_ip);

    TEST_ASSERT_EQUAL_UINT16(800, applied.servo.seq_open_ms);
    TEST_ASSERT_EQUAL_UINT16(1200, applied.servo.seq_close_ms);

    TEST_ASSERT_EQUAL_UINT8(3, applied.servo.aux_led_pin);
    TEST_ASSERT_EQUAL_UINT8(12, applied.servo.aux_led_count);

    TEST_ASSERT_EQUAL_INT(true, applied.system.enable_arm1);
    TEST_ASSERT_EQUAL_INT(false, applied.system.enable_arm2);
    TEST_ASSERT_EQUAL_INT(true, applied.system.enable_dome_esc);
    TEST_ASSERT_EQUAL_INT(true, applied.system.stationary);

    // Verify the pre-set non-cfg sentinel was not modified by the apply
    TEST_ASSERT_EQUAL_INT(999, robotState.driveOutputSpeed);
}

// Test: configCacheApply does not touch runtime fields
void test_configCacheApply_does_not_touch_runtime_fields() {
    // Set a non-cfg runtime field to a known value
    robotState.driveOutputSpeed = 123;  // This is a non-cfg field
    robotState.driveOutputSteer = 456;  // Another non-cfg field

    // Create a snapshot with different values for persisted config fields
    ConfigSnapshot snap = {};
    snap.system.stationary = true;

    configCacheApply(snap);

    ConfigSnapshot applied = {};
    configCacheRead(&applied);
    TEST_ASSERT_EQUAL_INT(true, applied.system.stationary);

    // But the non-cfg fields should NOT have changed
    TEST_ASSERT_EQUAL_INT(123, robotState.driveOutputSpeed);
    TEST_ASSERT_EQUAL_INT(456, robotState.driveOutputSteer);
}

void test_config_domain_load_functions_are_independently_callable() {
    ConfigSnapshot snap = {};
    snap.drive.speedLimitMax = 550;
    snap.audio.audioVolume = 12;
    snap.servo.arm1_open_us = 1900;
    snap.dome.dome_speed_limit_pct = 75;
    snap.system.enable_audio = true;

    Preferences prefs;
    prefs.begin("proto", false);
    TEST_ASSERT_TRUE(configSave(prefs, snap));

    DriveConfig drive = {};
    AudioConfig audio = {};
    ServoConfig servo = {};
    DomeConfig dome = {};
    SystemConfig system = {};
    configLoadDrive(prefs, &drive);
    configLoadAudio(prefs, &audio);
    configLoadServo(prefs, &servo);
    configLoadDome(prefs, &dome);
    configLoadSystem(prefs, &system);
    prefs.end();

    TEST_ASSERT_EQUAL_INT16(550, drive.speedLimitMax);
    TEST_ASSERT_EQUAL_UINT8(12, audio.audioVolume);
    TEST_ASSERT_EQUAL_UINT16(1900, servo.arm1_open_us);
    TEST_ASSERT_EQUAL_UINT8(75, dome.dome_speed_limit_pct);
    TEST_ASSERT_EQUAL_INT(true, system.enable_audio);
}

void test_config_domain_save_preserves_other_domains() {
    ConfigSnapshot snap = {};
    snap.drive.speedLimitMax = 500;
    snap.audio.audioVolume = 10;
    snap.system.enable_audio = true;

    Preferences prefs;
    prefs.begin("proto", false);
    TEST_ASSERT_TRUE(configSave(prefs, snap));

    AudioConfig audio = {};
    configLoadAudio(prefs, &audio);
    audio.audioVolume = 27;
    TEST_ASSERT_TRUE(configSaveAudio(prefs, audio));

    ConfigSnapshot loaded = {};
    TEST_ASSERT_TRUE(configLoad(prefs, &loaded));
    prefs.end();

    TEST_ASSERT_EQUAL_INT16(500, loaded.drive.speedLimitMax);
    TEST_ASSERT_EQUAL_UINT8(27, loaded.audio.audioVolume);
    TEST_ASSERT_EQUAL_INT(true, loaded.system.enable_audio);
}

static void seed_domain_round_trip_baseline(Preferences& prefs) {
    prefs.clear();

    ConfigSnapshot baseline = {};
    TEST_ASSERT_TRUE(configLoad(prefs, &baseline));
    baseline.drive.speedLimitMax = 500;
    baseline.audio.audioVolume = 10;
    baseline.servo.arm1_open_us = 1900;
    baseline.dome.dome_speed_limit_pct = 75;
    baseline.system.enable_audio = true;
    TEST_ASSERT_TRUE(configSave(prefs, baseline));
}

static void assert_domain_round_trip_baseline_preserved(const ConfigSnapshot& loaded) {
    TEST_ASSERT_EQUAL_INT16(500, loaded.drive.speedLimitMax);
    TEST_ASSERT_EQUAL_UINT8(10, loaded.audio.audioVolume);
    TEST_ASSERT_EQUAL_UINT16(1900, loaded.servo.arm1_open_us);
    TEST_ASSERT_EQUAL_UINT8(75, loaded.dome.dome_speed_limit_pct);
    TEST_ASSERT_EQUAL_INT(true, loaded.system.enable_audio);
}

void test_config_domain_round_trip_matrix() {
    Preferences prefs;
    prefs.begin("proto", false);

    seed_domain_round_trip_baseline(prefs);
    ConfigSnapshot loaded = {};
    TEST_ASSERT_TRUE(configLoad(prefs, &loaded));
    configCacheApply(loaded);
    ConfigSnapshot fromState = {};
    configCacheRead(&fromState);
    fromState.drive.speedLimitMax = 580;
    configCacheApply(fromState);
    configCacheRead(&fromState);
    TEST_ASSERT_TRUE(configSaveDrive(prefs, fromState.drive));
    TEST_ASSERT_TRUE(configLoad(prefs, &loaded));
    TEST_ASSERT_EQUAL_INT16(580, loaded.drive.speedLimitMax);
    loaded.drive.speedLimitMax = 500;
    assert_domain_round_trip_baseline_preserved(loaded);

    seed_domain_round_trip_baseline(prefs);
    TEST_ASSERT_TRUE(configLoad(prefs, &loaded));
    configCacheApply(loaded);
    configCacheRead(&fromState);
    fromState.audio.audioVolume = 24;
    configCacheApply(fromState);
    configCacheRead(&fromState);
    TEST_ASSERT_TRUE(configSaveAudio(prefs, fromState.audio));
    TEST_ASSERT_TRUE(configLoad(prefs, &loaded));
    TEST_ASSERT_EQUAL_UINT8(24, loaded.audio.audioVolume);
    loaded.audio.audioVolume = 10;
    assert_domain_round_trip_baseline_preserved(loaded);

    seed_domain_round_trip_baseline(prefs);
    TEST_ASSERT_TRUE(configLoad(prefs, &loaded));
    configCacheApply(loaded);
    configCacheRead(&fromState);
    fromState.servo.arm1_open_us = 2100;
    configCacheApply(fromState);
    configCacheRead(&fromState);
    TEST_ASSERT_TRUE(configSaveServo(prefs, fromState.servo));
    TEST_ASSERT_TRUE(configLoad(prefs, &loaded));
    TEST_ASSERT_EQUAL_UINT16(2100, loaded.servo.arm1_open_us);
    loaded.servo.arm1_open_us = 1900;
    assert_domain_round_trip_baseline_preserved(loaded);

    seed_domain_round_trip_baseline(prefs);
    TEST_ASSERT_TRUE(configLoad(prefs, &loaded));
    configCacheApply(loaded);
    configCacheRead(&fromState);
    fromState.dome.dome_speed_limit_pct = 62;
    configCacheApply(fromState);
    configCacheRead(&fromState);
    TEST_ASSERT_TRUE(configSaveDome(prefs, fromState.dome));
    TEST_ASSERT_TRUE(configLoad(prefs, &loaded));
    TEST_ASSERT_EQUAL_UINT8(62, loaded.dome.dome_speed_limit_pct);
    loaded.dome.dome_speed_limit_pct = 75;
    assert_domain_round_trip_baseline_preserved(loaded);

    seed_domain_round_trip_baseline(prefs);
    TEST_ASSERT_TRUE(configLoad(prefs, &loaded));
    configCacheApply(loaded);
    configCacheRead(&fromState);
    fromState.system.enable_audio = false;
    configCacheApply(fromState);
    configCacheRead(&fromState);
    TEST_ASSERT_TRUE(configSaveSystem(prefs, fromState.system));
    TEST_ASSERT_TRUE(configLoad(prefs, &loaded));
    TEST_ASSERT_EQUAL_INT(false, loaded.system.enable_audio);
    loaded.system.enable_audio = true;
    assert_domain_round_trip_baseline_preserved(loaded);

    prefs.end();
}

void test_configAudioTrackByKey_round_trips_named_track() {
    AudioConfig audio = {};

    TEST_ASSERT_TRUE(configAudioSetTrackByKey(&audio, "scream", 321));

    uint16_t value = 0;
    TEST_ASSERT_TRUE(configAudioGetTrackByKey(audio, "scream", &value));
    TEST_ASSERT_EQUAL_UINT16(321, value);
}

void test_configAudioTrackByKey_round_trips_category_bound() {
    AudioConfig audio = {};

    TEST_ASSERT_TRUE(configAudioSetTrackByKey(&audio, "snd_cat_snrk_hi", 654));

    uint16_t value = 0;
    TEST_ASSERT_TRUE(configAudioGetTrackByKey(audio, "snd_cat_snrk_hi", &value));
    TEST_ASSERT_EQUAL_UINT16(654, value);
    TEST_ASSERT_EQUAL_STRING("snd_cat_snrk_lo",
                             configAudioCategoryCompanionKey("snd_cat_snrk_hi"));
}

void test_configAudioTrackByKey_rejects_unknown_key() {
    AudioConfig audio = {};
    uint16_t value = 0xFFFF;

    TEST_ASSERT_FALSE(configAudioSetTrackByKey(&audio, "unknown", 123));
    TEST_ASSERT_FALSE(configAudioGetTrackByKey(audio, "unknown", &value));
    TEST_ASSERT_FALSE(configAudioGetTrackByKey(audio, "scream", nullptr));
    TEST_ASSERT_FALSE(configAudioSetTrackByKey(nullptr, "scream", 123));
    TEST_ASSERT_NULL(configAudioCategoryCompanionKey("unknown"));
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, value);
}

void test_configUpdateAudioMoodMasks_round_trips_through_audio_store() {
    Preferences prefs;
    prefs.begin("proto", false);
    prefs.clear();

    ConfigSnapshot snap = {};
    TEST_ASSERT_TRUE(configLoad(prefs, &snap));
    configCacheApply(snap);

    TEST_ASSERT_TRUE(configUpdateAudioMoodMasks(prefs, 0x0001, 0x0002, 0x0004, 0x0008));

    AudioConfig audio = {};
    configLoadAudio(prefs, &audio);
    prefs.end();

    TEST_ASSERT_EQUAL_UINT16(0x0001, audio.snd_moodcat_quiet);
    TEST_ASSERT_EQUAL_UINT16(0x0002, audio.snd_moodcat_mid);
    TEST_ASSERT_EQUAL_UINT16(0x0004, audio.snd_moodcat_full);
    TEST_ASSERT_EQUAL_UINT16(0x0008, audio.snd_moodcat_awakeplus);

    ConfigSnapshot cached = {};
    configCacheRead(&cached);
    TEST_ASSERT_EQUAL_UINT16(0x0001, cached.audio.snd_moodcat_quiet);
    TEST_ASSERT_EQUAL_UINT16(0x0008, cached.audio.snd_moodcat_awakeplus);
}

void test_wifiConfigToView_sets_password_flags_not_plaintext() {
    WifiConfig wifi = {};
    wifi.provisioned = true;
    wifi.mode = WifiMode::CLIENT;
    snprintf(wifi.sta_ssid, sizeof(wifi.sta_ssid), "%s", "HomeNetwork");
    snprintf(wifi.sta_password, sizeof(wifi.sta_password), "%s", "supersecret");
    snprintf(wifi.ap_ssid, sizeof(wifi.ap_ssid), "%s", "protoArtoo");
    snprintf(wifi.ap_password, sizeof(wifi.ap_password), "%s", "apsecret1");

    WifiConfigView view = wifiConfigToView(wifi);

    TEST_ASSERT_TRUE(view.provisioned);
    TEST_ASSERT_EQUAL_INT((int)WifiMode::CLIENT, (int)view.mode);
    TEST_ASSERT_EQUAL_STRING("HomeNetwork", view.sta_ssid);
    TEST_ASSERT_TRUE(view.sta_password_set);
    TEST_ASSERT_EQUAL_STRING("protoArtoo", view.ap_ssid);
    TEST_ASSERT_TRUE(view.ap_password_set);
}

void test_wifiConfigToView_reports_unset_empty_passwords() {
    WifiConfig wifi = {};
    wifi.provisioned = false;
    wifi.sta_ssid[0] = '\0';
    wifi.sta_password[0] = '\0';
    wifi.ap_password[0] = '\0';

    WifiConfigView view = wifiConfigToView(wifi);

    TEST_ASSERT_FALSE(view.sta_password_set);
    TEST_ASSERT_FALSE(view.ap_password_set);
}

void test_wifiConfigsDiffer_true_when_mode_or_ssid_or_password_changes() {
    WifiConfig a = {};
    a.provisioned = true;
    a.mode = WifiMode::CLIENT;
    snprintf(a.sta_ssid, sizeof(a.sta_ssid), "%s", "HomeNetwork");
    snprintf(a.sta_password, sizeof(a.sta_password), "%s", "secret1");

    WifiConfig b = a;
    TEST_ASSERT_FALSE(wifiConfigsDiffer(a, b));

    b.mode = WifiMode::STANDALONE_AP;
    TEST_ASSERT_TRUE(wifiConfigsDiffer(a, b));

    b = a;
    snprintf(b.sta_ssid, sizeof(b.sta_ssid), "%s", "OtherNetwork");
    TEST_ASSERT_TRUE(wifiConfigsDiffer(a, b));

    b = a;
    snprintf(b.sta_password, sizeof(b.sta_password), "%s", "secret2");
    TEST_ASSERT_TRUE(wifiConfigsDiffer(a, b));
}

void test_configLoad_save_wifi_round_trip() {
    Preferences prefs;
    prefs.begin("proto", false);
    prefs.clear();

    ConfigSnapshot snap = {};
    TEST_ASSERT_TRUE(configLoad(prefs, &snap));
    // Unprovisioned by default (empty NVS)
    TEST_ASSERT_FALSE(snap.wifi.provisioned);

    snap.wifi.provisioned = true;
    snap.wifi.mode = WifiMode::STANDALONE_AP;
    snprintf(snap.wifi.ap_ssid, sizeof(snap.wifi.ap_ssid), "%s", "r2-field");
    snprintf(snap.wifi.ap_password, sizeof(snap.wifi.ap_password), "%s", "fieldpass1");
    TEST_ASSERT_TRUE(configSave(prefs, snap));

    ConfigSnapshot loaded = {};
    TEST_ASSERT_TRUE(configLoad(prefs, &loaded));
    prefs.end();

    TEST_ASSERT_TRUE(loaded.wifi.provisioned);
    TEST_ASSERT_EQUAL_INT((int)WifiMode::STANDALONE_AP, (int)loaded.wifi.mode);
    TEST_ASSERT_EQUAL_STRING("r2-field", loaded.wifi.ap_ssid);
    TEST_ASSERT_EQUAL_STRING("fieldpass1", loaded.wifi.ap_password);
}


// Test: schema 1 -> 2 migration renumbers log_level for the inserted WARN tier
// (old 1=Error 2=Info 3=Debug; new 1=Error 2=Warn 3=Info 4=Debug).
void test_configLoad_schema_v1_migrates_info_log_level() {
    Preferences prefs;
    prefs.begin("proto", false);
    prefs.putUChar(CONFIG_SCHEMA_VERSION_KEY, 1);
    prefs.putUChar("log_level", 2);  // old Info

    ConfigSnapshot snap = {};
    TEST_ASSERT_TRUE(configLoad(prefs, &snap));

    TEST_ASSERT_EQUAL_UINT8(3, snap.system.logLevel);              // new Info
    TEST_ASSERT_EQUAL_UINT8(3, prefs.getUChar("log_level", 0));    // stored key rewritten
    TEST_ASSERT_EQUAL_UINT8(CONFIG_SCHEMA_VERSION,
                            prefs.getUChar(CONFIG_SCHEMA_VERSION_KEY, 0));
    prefs.end();
}

void test_configLoad_schema_v1_migrates_debug_log_level() {
    Preferences prefs;
    prefs.begin("proto", false);
    prefs.putUChar(CONFIG_SCHEMA_VERSION_KEY, 1);
    prefs.putUChar("log_level", 3);  // old Debug

    ConfigSnapshot snap = {};
    TEST_ASSERT_TRUE(configLoad(prefs, &snap));

    TEST_ASSERT_EQUAL_UINT8(4, snap.system.logLevel);  // new Debug
    prefs.end();
}

void test_configLoad_schema_v1_leaves_error_log_level_alone() {
    Preferences prefs;
    prefs.begin("proto", false);
    prefs.putUChar(CONFIG_SCHEMA_VERSION_KEY, 1);
    prefs.putUChar("log_level", 1);  // Error: same meaning in both numberings

    ConfigSnapshot snap = {};
    TEST_ASSERT_TRUE(configLoad(prefs, &snap));

    TEST_ASSERT_EQUAL_UINT8(1, snap.system.logLevel);
    TEST_ASSERT_EQUAL_UINT8(1, prefs.getUChar("log_level", 0));
    prefs.end();
}

void test_configLoad_current_schema_does_not_remap_log_level() {
    Preferences prefs;
    prefs.begin("proto", false);
    prefs.putUChar(CONFIG_SCHEMA_VERSION_KEY, CONFIG_SCHEMA_VERSION);
    prefs.putUChar("log_level", 2);  // already new numbering: Warn

    ConfigSnapshot snap = {};
    TEST_ASSERT_TRUE(configLoad(prefs, &snap));

    TEST_ASSERT_EQUAL_UINT8(2, snap.system.logLevel);  // stays Warn
    prefs.end();
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_configLoad_empty_nvs_returns_defaults);
    RUN_TEST(test_configLoad_save_roundtrip);
    RUN_TEST(test_configLoad_save_identity_accepts_lowercase);
    RUN_TEST(test_configLoad_save_identity_rejects_uppercase_to_default);
    RUN_TEST(test_configResolvedMdnsHostname_uses_identity_name);
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
    RUN_TEST(test_configLoad_schema_v1_migrates_info_log_level);
    RUN_TEST(test_configLoad_schema_v1_migrates_debug_log_level);
    RUN_TEST(test_configLoad_schema_v1_leaves_error_log_level_alone);
    RUN_TEST(test_configLoad_current_schema_does_not_remap_log_level);
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
    RUN_TEST(test_configCacheRead_captures_all_categories);
    RUN_TEST(test_active_rc_config_survives_saved_toggle_and_mode_changes);
    RUN_TEST(test_active_component_toggles_survive_a_later_saved_write);
    RUN_TEST(test_configCacheRead_save_round_trip);
    RUN_TEST(test_configCacheApply_applies_all_categories);
    RUN_TEST(test_configCacheApply_does_not_touch_runtime_fields);
    RUN_TEST(test_config_domain_load_functions_are_independently_callable);
    RUN_TEST(test_config_domain_save_preserves_other_domains);
    RUN_TEST(test_config_domain_round_trip_matrix);
    RUN_TEST(test_configAudioTrackByKey_round_trips_named_track);
    RUN_TEST(test_configAudioTrackByKey_round_trips_category_bound);
    RUN_TEST(test_configAudioTrackByKey_rejects_unknown_key);
    RUN_TEST(test_configUpdateAudioMoodMasks_round_trips_through_audio_store);
    RUN_TEST(test_wifiConfigToView_sets_password_flags_not_plaintext);
    RUN_TEST(test_wifiConfigToView_reports_unset_empty_passwords);
    RUN_TEST(test_wifiConfigsDiffer_true_when_mode_or_ssid_or_password_changes);
    RUN_TEST(test_configLoad_save_wifi_round_trip);
    return UNITY_END();
}
