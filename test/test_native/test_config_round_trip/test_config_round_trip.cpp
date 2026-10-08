// =============================================================================
// test/test_native/test_config_round_trip/test_config_round_trip.cpp
//
// Round-trip tests for config persistence seam.
// Tests configDeserialize/configSerialize against MapReader/MapWriter.
// =============================================================================
#include <cstdio>
#include <cstring>
#include <string>

#include <unity.h>

#include "config_store.h"
#include "board_outputs.h"
#include "config_serializer.h"
#include "servo_output_row.h"
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
    TEST_ASSERT_EQUAL_INT(defaults.dome.dome_speed_limit_pct, loaded.dome.dome_speed_limit_pct);
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
    snap.dome.dome_speed_limit_pct = 22;
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
    TEST_ASSERT_EQUAL_INT(22, loaded.dome.dome_speed_limit_pct);
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
    TEST_ASSERT_EQUAL_INT(defaults.dome.dome_speed_limit_pct, loaded.dome.dome_speed_limit_pct);
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
    TEST_ASSERT_EQUAL_INT(defaults.dome.dome_speed_limit_pct, loaded.dome.dome_speed_limit_pct);
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

// Test 8: Default Device WiFi Settings represent an Unprovisioned Controller
void test_wifi_defaults_are_unprovisioned(void) {
    ConfigSnapshot defaults = {};
    configSnapshotDefaults(&defaults);

    TEST_ASSERT_FALSE(defaults.wifi.provisioned);
    TEST_ASSERT_EQUAL_STRING("", defaults.wifi.sta_ssid);
    TEST_ASSERT_EQUAL_STRING("", defaults.wifi.sta_password);
    TEST_ASSERT_EQUAL_STRING(WIFI_AP_SSID, defaults.wifi.ap_ssid);
    TEST_ASSERT_EQUAL_STRING(WIFI_DEFAULT_AP_PASSWORD, defaults.wifi.ap_password);
}

// Test 9: Valid WiFi Client Mode settings round-trip through persistence
void test_wifi_client_mode_round_trip(void) {
    ConfigSnapshot snap = {};
    configSnapshotDefaults(&snap);
    snap.wifi.provisioned = true;
    snap.wifi.mode = WifiMode::CLIENT;
    snprintf(snap.wifi.sta_ssid, sizeof(snap.wifi.sta_ssid), "%s", "HomeNetwork");
    snprintf(snap.wifi.sta_password, sizeof(snap.wifi.sta_password), "%s", "correcthorsebattery");

    MapWriter writer;
    TEST_ASSERT_TRUE(configSerialize(snap, writer));

    MapReader reader;
    reader.setSchemaVersion(writer.schemaVersion());
    for (const auto& pair : writer.data()) {
        reader.set(pair.first.c_str(), pair.second);
    }

    ConfigSnapshot loaded = {};
    TEST_ASSERT_TRUE(configDeserialize(reader, &loaded));

    TEST_ASSERT_TRUE(loaded.wifi.provisioned);
    TEST_ASSERT_EQUAL_INT((int)WifiMode::CLIENT, (int)loaded.wifi.mode);
    TEST_ASSERT_EQUAL_STRING("HomeNetwork", loaded.wifi.sta_ssid);
    TEST_ASSERT_EQUAL_STRING("correcthorsebattery", loaded.wifi.sta_password);
}

// Test 10: Valid Standalone AP Mode settings round-trip through persistence
void test_wifi_standalone_ap_mode_round_trip(void) {
    ConfigSnapshot snap = {};
    configSnapshotDefaults(&snap);
    snap.wifi.provisioned = true;
    snap.wifi.mode = WifiMode::STANDALONE_AP;
    snprintf(snap.wifi.ap_ssid, sizeof(snap.wifi.ap_ssid), "%s", "r2-field-unit");
    snprintf(snap.wifi.ap_password, sizeof(snap.wifi.ap_password), "%s", "fieldPassword1");

    MapWriter writer;
    TEST_ASSERT_TRUE(configSerialize(snap, writer));

    MapReader reader;
    reader.setSchemaVersion(writer.schemaVersion());
    for (const auto& pair : writer.data()) {
        reader.set(pair.first.c_str(), pair.second);
    }

    ConfigSnapshot loaded = {};
    TEST_ASSERT_TRUE(configDeserialize(reader, &loaded));

    TEST_ASSERT_TRUE(loaded.wifi.provisioned);
    TEST_ASSERT_EQUAL_INT((int)WifiMode::STANDALONE_AP, (int)loaded.wifi.mode);
    TEST_ASSERT_EQUAL_STRING("r2-field-unit", loaded.wifi.ap_ssid);
    TEST_ASSERT_EQUAL_STRING("fieldPassword1", loaded.wifi.ap_password);
}

// Test 11: Schema/default fill — corrupt/overlong stored WiFi values fall back to defaults
void test_wifi_corrupt_values_fall_back_to_defaults(void) {
    ConfigSnapshot defaults = {};
    configSnapshotDefaults(&defaults);

    std::string overlongSsid(WIFI_SSID_MAX_LEN + 10, 'x');
    std::string overlongPassword(WIFI_PASSWORD_MAX_LEN + 10, 'y');

    MapReader reader;
    reader.setSchemaVersion(1);
    reader.set("wifi_prov", true);
    reader.set("wifi_mode", (uint32_t)9);  // out-of-range enum value
    reader.set("wifi_sta_ssid", overlongSsid);
    reader.set("wifi_ap_ssid", std::string(""));  // empty AP SSID is invalid
    reader.set("wifi_ap_pw", overlongPassword);

    ConfigSnapshot loaded = {};
    TEST_ASSERT_TRUE(configDeserialize(reader, &loaded));

    TEST_ASSERT_EQUAL_INT((int)defaults.wifi.mode, (int)loaded.wifi.mode);
    TEST_ASSERT_EQUAL_STRING(defaults.wifi.sta_ssid, loaded.wifi.sta_ssid);
    TEST_ASSERT_EQUAL_STRING(defaults.wifi.ap_ssid, loaded.wifi.ap_ssid);
    TEST_ASSERT_EQUAL_STRING(defaults.wifi.ap_password, loaded.wifi.ap_password);
    // provisioned is a plain bool — no invalid encoding to reject, loads through
    TEST_ASSERT_TRUE(loaded.wifi.provisioned);
}

// Test 12: Clearing the protoR2link peer IP must overwrite the value a previous save
// stored. NVS keeps every key a later save does not touch, so the backing store is
// seeded with the first save and the clearing save is layered on top -- which is what
// the deserializer sees after a power cycle, not what the in-RAM snapshot shows.
void test_dome_wifi_peer_ip_clear_overwrites_stored_value(void) {
    ConfigSnapshot snap = {};
    configSnapshotDefaults(&snap);
    snprintf(snap.dome.dome_wifi_peer_ip, sizeof(snap.dome.dome_wifi_peer_ip), "%s", "10.0.0.99");

    MapWriter firstSave;
    TEST_ASSERT_TRUE(configSerialize(snap, firstSave));

    MapReader store;
    store.setSchemaVersion(firstSave.schemaVersion());
    for (const auto& pair : firstSave.data()) {
        store.set(pair.first.c_str(), pair.second);
    }

    ConfigSnapshot afterSet = {};
    TEST_ASSERT_TRUE(configDeserialize(store, &afterSet));
    TEST_ASSERT_EQUAL_STRING("10.0.0.99", afterSet.dome.dome_wifi_peer_ip);

    snap.dome.dome_wifi_peer_ip[0] = '\0';
    MapWriter clearingSave;
    TEST_ASSERT_TRUE(configSerialize(snap, clearingSave));
    for (const auto& pair : clearingSave.data()) {
        store.set(pair.first.c_str(), pair.second);
    }

    ConfigSnapshot afterClear = {};
    TEST_ASSERT_TRUE(configDeserialize(store, &afterClear));
    TEST_ASSERT_EQUAL_STRING("", afterClear.dome.dome_wifi_peer_ip);
}

// Test initialization function for Unity
// -----------------------------------------------------------------------------
// The bridge a droid with one lit body light crosses (#413, ADR 0067)
//
// Before this change a droid had exactly one: aux_led_pin named which of the
// light-capable Outputs carried it and aux_led_count said how long the strip
// was. Both are an Output's own now, on its Servo Output row. A builder must
// not have to answer either question again, and the strip must not come back as
// a servo output on the pin it is actually driving - so the adoption is a
// behaviour, not a nicety, and this is the test that says so.
// -----------------------------------------------------------------------------

// The row the retired aux_led_pin slot named: the Nth light-capable Output in
// BOARD_OUTPUTS order, counting from one, which is what that number always
// meant. Worked out here from the same table the firmware reads rather than
// hardcoded, so a board with a different set of strip-capable Outputs is asked
// the same question.
static const BoardOutput* outputForRetiredSlot(uint8_t slot) {
    uint8_t seen = 0;
    for (size_t i = 0; i < BOARD_OUTPUT_COUNT; ++i) {
        if (BOARD_OUTPUTS[i].lightCapable && ++seen == slot) {
            return &BOARD_OUTPUTS[i];
        }
    }
    return nullptr;
}

static const ServoOutputRow* rowOn(const ServoOutputTable& table, uint8_t channel) {
    const uint8_t index = servoOutputTableFindByAddress(table, SERVO_DRIVER_LEDC, channel);
    return index < SERVO_OUTPUT_ROW_MAX ? &table.rows[index] : nullptr;
}

void test_one_lit_light_survives_the_upgrade(void) {
    const BoardOutput* lit = outputForRetiredSlot(2);
    TEST_ASSERT_NOT_NULL(lit);

    // A controller as it stood before the upgrade: the two retired keys, and no
    // row record at all for the Output they were about.
    MapReader reader;
    reader.setSchemaVersion(1);
    reader.set(NVS_KEY_RETIRED_AUX_LED_PIN, (uint32_t)2);
    reader.set(NVS_KEY_RETIRED_AUX_LED_COUNT, (uint32_t)16);

    ServoOutputTable table = {};
    ServoOutputRepairReport report = {};
    configDeserializeServoOutputs(reader, &table, &report);

    const ServoOutputRow* row = rowOn(table, lit->channel);
    TEST_ASSERT_NOT_NULL(row);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)SERVO_COMP_RGB, (uint8_t)row->component);
    TEST_ASSERT_EQUAL_UINT8(16, row->led_count);

    // And only that Output: the other light-capable ones are untouched, which
    // is what stops the upgrade lighting a wire nobody said carried a strip.
    for (size_t i = 0; i < BOARD_OUTPUT_COUNT; ++i) {
        if (&BOARD_OUTPUTS[i] == lit) {
            continue;
        }
        const ServoOutputRow* other = rowOn(table, BOARD_OUTPUTS[i].channel);
        TEST_ASSERT_NOT_NULL(other);
        TEST_ASSERT_NOT_EQUAL((uint8_t)SERVO_COMP_RGB, (uint8_t)other->component);
    }
}

// The other half of the same behaviour: once the keys are gone - which
// configSaveServoOutputs() does after the rows are safely down - nothing
// re-adopts, so an answer the builder has since changed stands.
void test_a_droid_with_no_retired_keys_adopts_nothing(void) {
    MapReader reader;
    reader.setSchemaVersion(1);

    ServoOutputTable table = {};
    ServoOutputRepairReport report = {};
    configDeserializeServoOutputs(reader, &table, &report);

    for (size_t i = 0; i < BOARD_OUTPUT_COUNT; ++i) {
        const ServoOutputRow* row = rowOn(table, BOARD_OUTPUTS[i].channel);
        TEST_ASSERT_NOT_NULL(row);
        TEST_ASSERT_NOT_EQUAL((uint8_t)SERVO_COMP_RGB, (uint8_t)row->component);
    }
}

// The stored form of `row` at `index`, as this firmware writes it - or, with
// `oldShape`, as a controller wrote it before #413, one field shorter.
static void storeRow(MapReader* reader, uint8_t index, const ServoOutputRow& row, bool oldShape) {
    char encoded[SERVO_OUTPUT_ROW_STR_MAX + 1] = {};
    TEST_ASSERT_TRUE(servoOutputRowFormat(encoded, sizeof(encoded), row));
    if (oldShape) {
        *strrchr(encoded, ':') = '\0';
    }
    char key[8] = {};
    snprintf(key, sizeof(key), "so%02u", (unsigned)index);
    reader->set(key, std::string(encoded));
}

// A controller that has saved a row since the upgrade, with the retired light
// keys still beside it: the save that wrote the row lost a later row write, so
// the keys were never removed (#417). The row is the builder's answer - here, a
// servo put back on the wire `main` lit - and reading the keys over it on every
// boot would turn the servo back into a strip, and keep LEDC off its pin.
void test_a_saved_row_is_not_overwritten_by_the_retired_light_keys(void) {
    const BoardOutput* lit = outputForRetiredSlot(2);
    TEST_ASSERT_NOT_NULL(lit);

    ServoOutputTable defaults = {};
    servoOutputTableDefaults(&defaults);
    const uint8_t index = servoOutputTableFindByAddress(defaults, SERVO_DRIVER_LEDC, lit->channel);
    TEST_ASSERT_TRUE(index < SERVO_OUTPUT_ROW_MAX);

    ServoOutputRow servo = defaults.rows[index];
    servo.component = SERVO_COMP_MG996R;
    servo.led_count = 10;

    MapReader reader;
    reader.setSchemaVersion(CONFIG_SCHEMA_VERSION);
    reader.set("so_cnt", (uint32_t)defaults.count);
    storeRow(&reader, index, servo, false);
    reader.set(NVS_KEY_RETIRED_AUX_LED_PIN, (uint32_t)2);
    reader.set(NVS_KEY_RETIRED_AUX_LED_COUNT, (uint32_t)40);

    ServoOutputTable table = {};
    ServoOutputRepairReport report = {};
    configDeserializeServoOutputs(reader, &table, &report);

    TEST_ASSERT_EQUAL_UINT8((uint8_t)SERVO_COMP_MG996R, (uint8_t)table.rows[index].component);
    TEST_ASSERT_EQUAL_UINT8(10, table.rows[index].led_count);
    TEST_ASSERT_FALSE(report.litAdopted);
}

// The other side of the same gate: a row stored before #413 has no Light Type
// field to hold the answer, so the keys still land on it. Epic-lineage
// controllers stored rows and `aux_led_pin` together before the LED count
// joined the row, and a gate on "no stored row" alone would drop their strip.
void test_a_row_stored_before_the_light_joined_it_still_adopts_the_light(void) {
    const BoardOutput* lit = outputForRetiredSlot(2);
    TEST_ASSERT_NOT_NULL(lit);

    ServoOutputTable defaults = {};
    servoOutputTableDefaults(&defaults);
    const uint8_t index = servoOutputTableFindByAddress(defaults, SERVO_DRIVER_LEDC, lit->channel);
    TEST_ASSERT_TRUE(index < SERVO_OUTPUT_ROW_MAX);

    MapReader reader;
    reader.setSchemaVersion(CONFIG_SCHEMA_VERSION);
    reader.set("so_cnt", (uint32_t)defaults.count);
    storeRow(&reader, index, defaults.rows[index], true);
    reader.set(NVS_KEY_RETIRED_AUX_LED_PIN, (uint32_t)2);
    reader.set(NVS_KEY_RETIRED_AUX_LED_COUNT, (uint32_t)40);

    ServoOutputTable table = {};
    ServoOutputRepairReport report = {};
    configDeserializeServoOutputs(reader, &table, &report);

    TEST_ASSERT_EQUAL_UINT8((uint8_t)SERVO_COMP_RGB, (uint8_t)table.rows[index].component);
    TEST_ASSERT_EQUAL_UINT8(40, table.rows[index].led_count);
    TEST_ASSERT_TRUE(report.litAdopted);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)(lit - BOARD_OUTPUTS), report.litOutput);
}

// A stored row written before the LED count joined it has thirteen fields, not
// fourteen. That is the shape this firmware used to write, so it is read as
// itself with the count defaulted - never as a damaged record, which would
// throw away a builder's whole calibration on the first boot after an upgrade.
void test_a_thirteen_field_row_is_the_old_shape_not_damage(void) {
    ServoOutputRow fallback = {};
    servoOutputRowDefaults(&fallback, SERVO_DRIVER_LEDC, LEDC_CH_ARM1, SERVO_COMP_MG996R);

    ServoOutputRow parsed = {};
    const uint16_t repaired = servoOutputRowParse(
        "ledc:0:doorFL:1900:1450:1050:800:200:0:none:limp:mg996r:1", fallback, &parsed);

    TEST_ASSERT_EQUAL_UINT16(0, repaired);
    TEST_ASSERT_EQUAL_UINT16(1900, parsed.open_us);
    TEST_ASSERT_EQUAL_UINT16(1050, parsed.close_us);
    TEST_ASSERT_TRUE(parsed.calibrated);
    TEST_ASSERT_EQUAL_UINT8(SERVO_LIGHT_LEDS_DEFAULT, parsed.led_count);
}

// A dome pulse set stored out of order - saved before the config door refused
// one (#417) - cannot stop the dome, so it loads as the defaults, and the
// loader can tell it did.
void test_an_out_of_order_stored_dome_pulse_set_loads_as_the_defaults(void) {
    MapReader reader;
    reader.setSchemaVersion(CONFIG_SCHEMA_VERSION);
    reader.set("dome_minp", (uint32_t)1800);
    reader.set("dome_neu", (uint32_t)1500);
    reader.set("dome_maxp", (uint32_t)1200);

    ConfigSnapshot snap = {};
    TEST_ASSERT_TRUE(configDeserialize(reader, &snap));
    TEST_ASSERT_EQUAL_UINT16(1000, snap.dome.dome_min_pulse_us);
    TEST_ASSERT_EQUAL_UINT16(1500, snap.dome.dome_neutral_us);
    TEST_ASSERT_EQUAL_UINT16(2000, snap.dome.dome_max_pulse_us);
    TEST_ASSERT_TRUE(configDomePulsesStoredOutOfOrder(reader));

    // An ordered set is kept exactly, and is not reported.
    MapReader ordered;
    ordered.setSchemaVersion(CONFIG_SCHEMA_VERSION);
    ordered.set("dome_minp", (uint32_t)1100);
    ordered.set("dome_neu", (uint32_t)1480);
    ordered.set("dome_maxp", (uint32_t)1900);
    ConfigSnapshot kept = {};
    TEST_ASSERT_TRUE(configDeserialize(ordered, &kept));
    TEST_ASSERT_EQUAL_UINT16(1100, kept.dome.dome_min_pulse_us);
    TEST_ASSERT_EQUAL_UINT16(1480, kept.dome.dome_neutral_us);
    TEST_ASSERT_EQUAL_UINT16(1900, kept.dome.dome_max_pulse_us);
    TEST_ASSERT_FALSE(configDomePulsesStoredOutOfOrder(ordered));
}

// A stored ELRS receiver mode loads as ELRS. The loader's bound once stopped at
// dual_sbus, so every droid set to ELRS came back as dual_sbus after a reboot.
void test_a_stored_elrs_mode_loads_as_elrs(void) {
    MapReader reader;
    reader.setSchemaVersion(CONFIG_SCHEMA_VERSION);
    reader.set("rc_mode", (uint32_t)RC_INPUT_ELRS);
    ConfigSnapshot snap = {};
    TEST_ASSERT_TRUE(configDeserialize(reader, &snap));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RC_INPUT_ELRS, (uint8_t)snap.system.rc_input_mode);

    MapReader unknown;
    unknown.setSchemaVersion(CONFIG_SCHEMA_VERSION);
    unknown.set("rc_mode", (uint32_t)9);
    TEST_ASSERT_TRUE(configDeserialize(unknown, &snap));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RC_INPUT_DUAL_SBUS, (uint8_t)snap.system.rc_input_mode);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_default_snapshot_round_trip);
    RUN_TEST(test_typical_snapshot_round_trip);
    RUN_TEST(test_rc_trigger_binding_round_trip);
    RUN_TEST(test_rc_analog_binding_round_trip);
    RUN_TEST(test_schema_v0_migration);
    RUN_TEST(test_unknown_key_tolerance);
    RUN_TEST(test_moodcat_mask_round_trip);
    RUN_TEST(test_wifi_defaults_are_unprovisioned);
    RUN_TEST(test_wifi_client_mode_round_trip);
    RUN_TEST(test_wifi_standalone_ap_mode_round_trip);
    RUN_TEST(test_wifi_corrupt_values_fall_back_to_defaults);
    RUN_TEST(test_dome_wifi_peer_ip_clear_overwrites_stored_value);
    RUN_TEST(test_one_lit_light_survives_the_upgrade);
    RUN_TEST(test_a_droid_with_no_retired_keys_adopts_nothing);
    RUN_TEST(test_a_thirteen_field_row_is_the_old_shape_not_damage);
    RUN_TEST(test_a_saved_row_is_not_overwritten_by_the_retired_light_keys);
    RUN_TEST(test_a_row_stored_before_the_light_joined_it_still_adopts_the_light);
    RUN_TEST(test_an_out_of_order_stored_dome_pulse_set_loads_as_the_defaults);
    RUN_TEST(test_a_stored_elrs_mode_loads_as_elrs);
    return UNITY_END();
}
