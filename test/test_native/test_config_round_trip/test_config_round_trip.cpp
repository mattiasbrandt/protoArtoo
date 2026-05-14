// =============================================================================
// test/test_native/test_config_round_trip/test_config_round_trip.cpp
//
// Round-trip tests for config persistence seam.
// Tests configDeserialize/configSerialize against MapReader/MapWriter.
// =============================================================================
#include <unity.h>

#include "config_store.h"
#include "config_serializer.h"
#include "../../../test/stubs/config/map_config_io.h"

void setUp(void) {}
void tearDown(void) {}

// Test 1: Default snapshot round-trip
void test_default_snapshot_round_trip(void) {
    ConfigSnapshot defaults = {};
    configSnapshotDefaults(&defaults);

    MapWriter writer;
    TEST_ASSERT_TRUE(configSerialize(defaults, writer));

    MapReader reader;
    reader.setSchemaVersion(writer.schemaVersion());
    for (const auto& pair : writer.data()) {
        reader.set(pair.first.c_str(), pair.second);
    }

    ConfigSnapshot loaded = {};
    TEST_ASSERT_TRUE(configDeserialize(reader, &loaded));

    // Verify defaults were loaded correctly
    TEST_ASSERT_EQUAL_STRING(defaults.system.droid_name, loaded.system.droid_name);
    TEST_ASSERT_EQUAL_INT(defaults.drive.speedLimitMax, loaded.drive.speedLimitMax);
    TEST_ASSERT_EQUAL_INT(defaults.drive.speedPresetSlow, loaded.drive.speedPresetSlow);
    TEST_ASSERT_EQUAL_INT(defaults.drive.speedPresetNormal, loaded.drive.speedPresetNormal);
    TEST_ASSERT_EQUAL_INT(defaults.drive.speedPresetTurbo, loaded.drive.speedPresetTurbo);
    TEST_ASSERT_EQUAL_INT(defaults.audio.audioVolume, loaded.audio.audioVolume);
    TEST_ASSERT_EQUAL_INT(defaults.servo.arm1_open_us, loaded.servo.arm1_open_us);
}

// Test 2: Typical snapshot round-trip with non-default values
void test_typical_snapshot_round_trip(void) {
    ConfigSnapshot snap = {};
    configSnapshotDefaults(&snap);

    // Modify representative fields from each domain
    snap.system.droid_name[0] = '\0';
    snprintf(snap.system.droid_name, sizeof(snap.system.droid_name), "r2d2test");
    snap.drive.speedLimitMax = 600;  // exact upper boundary
    snap.audio.audioVolume = 15;
    snap.servo.arm1_open_us = 2200;
    snap.dome.dome_min_speed = 0.25f;
    snap.system.enable_arm1 = true;

    MapWriter writer;
    TEST_ASSERT_TRUE(configSerialize(snap, writer));

    MapReader reader;
    reader.setSchemaVersion(writer.schemaVersion());
    for (const auto& pair : writer.data()) {
        reader.set(pair.first.c_str(), pair.second);
    }

    ConfigSnapshot loaded = {};
    TEST_ASSERT_TRUE(configDeserialize(reader, &loaded));

    TEST_ASSERT_EQUAL_STRING("r2d2test", loaded.system.droid_name);
    TEST_ASSERT_EQUAL_INT(600, loaded.drive.speedLimitMax);
    TEST_ASSERT_EQUAL_INT(15, loaded.audio.audioVolume);
    TEST_ASSERT_EQUAL_INT(2200, loaded.servo.arm1_open_us);
    TEST_ASSERT_EQUAL_INT(1, loaded.system.enable_arm1);
    // Float comparison with tolerance
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.25f, loaded.dome.dome_min_speed);
}

// Test 3: RC trigger binding round-trip
void test_rc_trigger_binding_round_trip(void) {
    ConfigSnapshot snap = {};
    configSnapshotDefaults(&snap);

    // Set up a non-trivial RC trigger binding
    snap.system.rc_arm1 = makeRcTriggerBinding(RC_BINDING_SBUS1, 5, SERVO_ACTION_ARM1_TOGGLE, nullptr,
                                               1000, 1500, 2000, 0, false);

    MapWriter writer;
    TEST_ASSERT_TRUE(configSerialize(snap, writer));

    MapReader reader;
    reader.setSchemaVersion(writer.schemaVersion());
    for (const auto& pair : writer.data()) {
        reader.set(pair.first.c_str(), pair.second);
    }

    ConfigSnapshot loaded = {};
    TEST_ASSERT_TRUE(configDeserialize(reader, &loaded));

    // Verify RC binding fields
    TEST_ASSERT_EQUAL_INT(snap.system.rc_arm1.source, loaded.system.rc_arm1.source);
    TEST_ASSERT_EQUAL_INT(snap.system.rc_arm1.channel, loaded.system.rc_arm1.channel);
    TEST_ASSERT_EQUAL_INT(snap.system.rc_arm1.target, loaded.system.rc_arm1.target);
}

// Test 4: RC analog binding round-trip
void test_rc_analog_binding_round_trip(void) {
    ConfigSnapshot snap = {};
    configSnapshotDefaults(&snap);

    // Set up a non-trivial RC analog binding
    snap.system.rc_sbus_drive_speed = {
        RC_BINDING_SBUS1, 1, 172, 992, 1811, 50, false
    };

    MapWriter writer;
    TEST_ASSERT_TRUE(configSerialize(snap, writer));

    MapReader reader;
    reader.setSchemaVersion(writer.schemaVersion());
    for (const auto& pair : writer.data()) {
        reader.set(pair.first.c_str(), pair.second);
    }

    ConfigSnapshot loaded = {};
    TEST_ASSERT_TRUE(configDeserialize(reader, &loaded));

    TEST_ASSERT_EQUAL_INT(RC_BINDING_SBUS1, loaded.system.rc_sbus_drive_speed.source);
    TEST_ASSERT_EQUAL_INT(1, loaded.system.rc_sbus_drive_speed.channel);
    TEST_ASSERT_EQUAL_INT(172, loaded.system.rc_sbus_drive_speed.min);
    TEST_ASSERT_EQUAL_INT(992, loaded.system.rc_sbus_drive_speed.center);
    TEST_ASSERT_EQUAL_INT(1811, loaded.system.rc_sbus_drive_speed.max);
    TEST_ASSERT_EQUAL_INT(50, loaded.system.rc_sbus_drive_speed.deadband);
}

// Test 5: Schema V0 migration
void test_schema_v0_migration(void) {
    ConfigSnapshot defaults = {};
    configSnapshotDefaults(&defaults);

    MapReader reader;
    reader.setSchemaVersion(0);  // V0 schema
    // Store only a few fields
    reader.set("spd_max", (uint32_t)600);  // upper boundary
    reader.set("aud_vol", (uint32_t)20);
    // Other fields absent — should be filled from defaults

    ConfigSnapshot loaded = {};
    TEST_ASSERT_TRUE(configDeserialize(reader, &loaded));

    // Loaded values should be present
    TEST_ASSERT_EQUAL_INT(600, loaded.drive.speedLimitMax);
    TEST_ASSERT_EQUAL_INT(20, loaded.audio.audioVolume);

    // Values not in the map should have defaults
    TEST_ASSERT_EQUAL_INT(defaults.drive.speedPresetSlow, loaded.drive.speedPresetSlow);
    TEST_ASSERT_EQUAL_INT(defaults.servo.arm1_open_us, loaded.servo.arm1_open_us);
}

// Test 6: Unknown key tolerance
void test_unknown_key_tolerance(void) {
    ConfigSnapshot defaults = {};
    configSnapshotDefaults(&defaults);

    MapReader reader;
    reader.setSchemaVersion(1);
    reader.set("spd_max", (uint32_t)601);  // one above max — clamp to 600
    reader.set("unknown_key_xyz", "999");  // Extra unknown key
    reader.set("aud_vol", (uint32_t)18);

    ConfigSnapshot loaded = {};
    TEST_ASSERT_TRUE(configDeserialize(reader, &loaded));

    // Known fields should load normally despite unknown key
    TEST_ASSERT_EQUAL_INT(600, loaded.drive.speedLimitMax);  // clamped at boundary
    TEST_ASSERT_EQUAL_INT(18, loaded.audio.audioVolume);
    // Unknown key should not corrupt anything
    TEST_ASSERT_EQUAL_INT(defaults.servo.seq_open_ms, loaded.servo.seq_open_ms);
}

// Test 7: Moodcat mask — upper nibble must be stripped on both serialize and deserialize
void test_moodcat_mask_round_trip(void) {
    ConfigSnapshot snap = {};
    configSnapshotDefaults(&snap);

    // Upper nibble carries category flags; only the lower 12 bits are the track ID
    snap.audio.snd_moodcat_quiet     = 0x1ABC;
    snap.audio.snd_moodcat_mid       = 0x2DEF;
    snap.audio.snd_moodcat_full      = 0x3111;
    snap.audio.snd_moodcat_awakeplus = 0xFFFF;

    MapWriter writer;
    TEST_ASSERT_TRUE(configSerialize(snap, writer));

    MapReader reader;
    reader.setSchemaVersion(writer.schemaVersion());
    for (const auto& pair : writer.data()) {
        reader.set(pair.first.c_str(), pair.second);
    }

    ConfigSnapshot loaded = {};
    TEST_ASSERT_TRUE(configDeserialize(reader, &loaded));

    TEST_ASSERT_EQUAL_INT(0x0ABC, loaded.audio.snd_moodcat_quiet);
    TEST_ASSERT_EQUAL_INT(0x0DEF, loaded.audio.snd_moodcat_mid);
    TEST_ASSERT_EQUAL_INT(0x0111, loaded.audio.snd_moodcat_full);
    TEST_ASSERT_EQUAL_INT(0x0FFF, loaded.audio.snd_moodcat_awakeplus);
}

// Test initialization function for Unity
int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_default_snapshot_round_trip);
    RUN_TEST(test_typical_snapshot_round_trip);
    RUN_TEST(test_rc_trigger_binding_round_trip);
    RUN_TEST(test_rc_analog_binding_round_trip);
    RUN_TEST(test_schema_v0_migration);
    RUN_TEST(test_unknown_key_tolerance);
    RUN_TEST(test_moodcat_mask_round_trip);
    return UNITY_END();
}
