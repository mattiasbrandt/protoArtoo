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

#include "board_output_enabled.h"
#include "component_registry.h"
#include "config_store.h"
#include "config_cache.h"
#include "config_nvsio.h"
#include "config_serializer.h"
#include "output_wire.h"
#include "robot_state.h"
#include "servo_legacy_field_sets.h"

#include "../../../test/stubs/config/map_config_io.h"
#include "../../../test/stubs/config/servo_output_table_writer.h"

// Provided by native_test_stubs.cpp
extern RobotState robotState;

void setUp() {
    memset(&robotState, 0, sizeof(RobotState));
    // The NVS double keeps its keys per namespace for the whole binary, as
    // flash does; every test here starts from an erased partition.
    Preferences::eraseFlash();
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

// Test: with the Droid Name override off (the SBUS-safe boot default), the
// resolved mDNS hostname falls back to the board's WIFI_MDNS_HOST default.
// Native tests always build PA_BOARD_ARTOO_ESP32 (#242), so this pins
// artoo-esp32's board-specific default -- "artoo", unchanged by moving the
// constant into its board section. See test_tools/test_board_mdns_host.py
// for the cross-board proof that firebeetle2 gets a different default.
void test_configResolvedMdnsHostname_falls_back_to_board_default() {
    SystemConfig system = {};
    system.mdns_use_name = false;
    snprintf(system.droid_name, sizeof(system.droid_name), "%s", "r2-unit");

    char hostname[33] = {};
    configResolvedMdnsHostname(system, hostname, sizeof(hostname));

    TEST_ASSERT_EQUAL_STRING(WIFI_MDNS_HOST, hostname);
    TEST_ASSERT_EQUAL_STRING("artoo", hostname);
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

// Test: a config save writes no fixed servo key at all (#345)
//
// The contract half of the refactor, asked of the thing that would betray it
// first. An endpoint lives on a Servo Output row; if any of the fifteen keys
// the five fixed field sets used came back, there would be two places one is
// stored and the reader that lost the race would drive to the older number.
void test_a_saved_config_writes_no_fixed_servo_key() {
    ConfigSnapshot snap = {};
    configSnapshotDefaults(&snap);

    MapWriter writer;
    TEST_ASSERT_TRUE(configSerialize(snap, writer));

    for (size_t i = 0; i < SERVO_LEGACY_FIELD_SET_COUNT; ++i) {
        const ServoLegacyFieldSet& set = SERVO_LEGACY_FIELD_SETS[i];
        TEST_ASSERT_EQUAL_size_t(0, writer.data().count(set.nvsOpenKey));
        TEST_ASSERT_EQUAL_size_t(0, writer.data().count(set.nvsCloseKey));
        TEST_ASSERT_EQUAL_size_t(0, writer.data().count(set.nvsTypeKey));
    }
    // And nothing writes the retired single-strip keys either (#413): a light
    // and its settings are an Output's own, on that Output's row.
    TEST_ASSERT_EQUAL_size_t(0, writer.data().count(NVS_KEY_RETIRED_AUX_LED_PIN));
    TEST_ASSERT_EQUAL_size_t(0, writer.data().count(NVS_KEY_RETIRED_AUX_LED_COUNT));
}

// Test: a save removes the sequence dwell nothing reads any more (#362)
//
// seq_op / seq_cl held the dwell ServoTask's body routine state machine waited
// between an open and a close. #354 deleted the state machine, which was their
// only reader, so a controller upgrading with the two keys in NVS would carry
// them forever. The save is what makes them gone on the device rather than
// only absent from the schema - and a config that never had them saves exactly
// as before.
void test_a_saved_config_removes_the_retired_sequence_dwell_keys() {
    Preferences prefs;
    prefs.begin("proto", false);
    prefs.clear();
    prefs.putUShort("seq_op", 2200);
    prefs.putUShort("seq_cl", 900);

    ConfigSnapshot snap = {};
    configSnapshotDefaults(&snap);
    MapWriter writer;
    TEST_ASSERT_TRUE(configSerialize(snap, writer));
    TEST_ASSERT_EQUAL_size_t(0, writer.data().count("seq_op"));
    TEST_ASSERT_EQUAL_size_t(0, writer.data().count("seq_cl"));

    TEST_ASSERT_TRUE(configSave(prefs, snap));
    TEST_ASSERT_FALSE(prefs.isKey("seq_op"));
    TEST_ASSERT_FALSE(prefs.isKey("seq_cl"));

    // And a second save with nothing left to remove still succeeds.
    TEST_ASSERT_TRUE(configSave(prefs, snap));
    prefs.end();
}

// Test: saving the rows removes the key set they replaced (#345)
//
// The other half: a controller upgrading from before ADR 0041 has its
// calibration under the old keys, the loader adopts it, and the first
// successful row save is what makes "no longer in NVS" true on the device
// rather than only in the schema.
void test_a_saved_row_removes_the_key_set_it_replaced() {
    Preferences prefs;
    prefs.begin("proto", false);
    prefs.clear();
    prefs.putUShort("arm1_op", 1850);
    prefs.putUShort("arm1_cl", 1150);
    prefs.putUChar("arm1_type", (uint8_t)SERVO_COMP_MG996R);

    ServoOutputRepairReport report = {};
    configLoadServoOutputs(prefs, &report);

    // Adopted, so the builder's numbers are on the row before anything is
    // removed - which is the whole reason the removal is safe.
    uint16_t openUs = 0;
    uint16_t closeUs = 0;
    TEST_ASSERT_TRUE(
        configCacheReadServoOutputEndpoints(SERVO_DRIVER_LEDC, LEDC_CH_ARM1, &openUs, &closeUs));
    TEST_ASSERT_EQUAL_UINT16(1850, openUs);
    TEST_ASSERT_EQUAL_UINT16(1150, closeUs);

    TEST_ASSERT_TRUE(configSaveServoOutputs(prefs));

    TEST_ASSERT_FALSE(prefs.isKey("arm1_op"));
    TEST_ASSERT_FALSE(prefs.isKey("arm1_cl"));
    TEST_ASSERT_FALSE(prefs.isKey("arm1_type"));
    TEST_ASSERT_TRUE(prefs.isKey("so00"));

    // And the second read finds the same calibration, now on the row alone.
    ServoOutputRepairReport again = {};
    configLoadServoOutputs(prefs, &again);
    TEST_ASSERT_TRUE(
        configCacheReadServoOutputEndpoints(SERVO_DRIVER_LEDC, LEDC_CH_ARM1, &openUs, &closeUs));
    TEST_ASSERT_EQUAL_UINT16(1850, openUs);
    TEST_ASSERT_EQUAL_UINT16(1150, closeUs);
    prefs.end();
}

// Test: a row write that never reached flash keeps the key set it replaces (#375)
//
// The failure the two guards exist for, finally reachable now the stub can fail
// a write. nvs_set_str() fails on a full or fragmented partition, putString()
// returns 0, and while writeStr() called that success `ok` stayed true and the
// five legacy key sets were removed on top of a row that was never stored. The
// next boot then found no `soNN` record and no old keys either, and defaulted
// the row: the builder's calibration gone, with the save reported as succeeded.
void test_a_failed_row_write_keeps_the_legacy_keys() {
    Preferences prefs;
    prefs.begin("proto", false);
    prefs.clear();
    prefs.putUShort("arm1_op", 1850);
    prefs.putUShort("arm1_cl", 1150);
    prefs.putUChar("arm1_type", (uint8_t)SERVO_COMP_MG996R);

    ServoOutputRepairReport report = {};
    configLoadServoOutputs(prefs, &report);

    // Exactly one write fails, and it is the first row's -- the count write is
    // a putUChar and every later row still lands, so this is a partial save
    // rather than an NVS that stopped answering.
    prefs.failNextStringWrites(1);
    TEST_ASSERT_FALSE(configSaveServoOutputs(prefs));

    // The one failed row is carried out of the save as a failure, and the rows
    // after it still land: a later success does not overwrite the earlier one.
    TEST_ASSERT_FALSE(prefs.isKey("so00"));
    TEST_ASSERT_TRUE(prefs.isKey("so01"));
    TEST_ASSERT_TRUE(prefs.isKey("arm1_op"));
    TEST_ASSERT_TRUE(prefs.isKey("arm1_cl"));
    TEST_ASSERT_TRUE(prefs.isKey("arm1_type"));

    // So the bridge is still there to cross: a reload finds the same numbers.
    ServoOutputRepairReport again = {};
    configLoadServoOutputs(prefs, &again);
    uint16_t openUs = 0;
    uint16_t closeUs = 0;
    TEST_ASSERT_TRUE(
        configCacheReadServoOutputEndpoints(SERVO_DRIVER_LEDC, LEDC_CH_ARM1, &openUs, &closeUs));
    TEST_ASSERT_EQUAL_UINT16(1850, openUs);
    TEST_ASSERT_EQUAL_UINT16(1150, closeUs);
    prefs.end();
}

// Test: a row write that fails stops the save before the fixed field sets (#424)
//
// The store's own full save, not a copy of its sequence. Rows first, and the
// fixed field sets only once every row is down (#286, ADR 0041): a row that
// did not land must leave the snapshot's keys - and the extras behind them -
// exactly as they were, so both stores still agree on the older value.
void test_a_failed_row_write_stops_the_save_before_the_fixed_field_sets() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    ServoOutputRepairReport report = {};
    configLoadServoOutputs(prefs, &report);  // the five default rows, live
    prefs.failNextStringWrites(1);           // the first row's write
    prefs.end();

    ConfigSnapshot snap = {};
    configSnapshotDefaults(&snap);
    configCacheApply(snap);
    ConfigSaveExtras extras;
    extras.droidBuild = true;
    extras.guidedSetup = true;
    TEST_ASSERT_FALSE(configPersist(snap, extras));

    prefs.begin(NVS_NAMESPACE, true);
    // The rows were written, and the failed one did not land ...
    TEST_ASSERT_FALSE(prefs.isKey("so00"));
    TEST_ASSERT_TRUE(prefs.isKey("so01"));
    // ... so nothing of the fixed field sets was: not the first key of the
    // snapshot, not the system set's, not the schema stamp that closes it.
    TEST_ASSERT_FALSE(prefs.isKey("spd_max"));
    TEST_ASSERT_FALSE(prefs.isKey("droid_name"));
    TEST_ASSERT_FALSE(prefs.isKey(CONFIG_SCHEMA_VERSION_KEY));
    prefs.end();
}

// --- the upgrade from `main` (#417) -------------------------------------------

namespace {

void expectNarrowedFrom(uint8_t channel, uint16_t openUs, uint16_t closeUs) {
    uint16_t o = 0;
    uint16_t c = 0;
    TEST_ASSERT_TRUE(configCacheReadServoOutputNarrowedFrom(SERVO_DRIVER_LEDC, channel, &o, &c));
    TEST_ASSERT_EQUAL_UINT16(openUs, o);
    TEST_ASSERT_EQUAL_UINT16(closeUs, c);
}

void expectNotNarrowed(uint8_t channel) {
    uint16_t o = 0;
    uint16_t c = 0;
    TEST_ASSERT_FALSE(configCacheReadServoOutputNarrowedFrom(SERVO_DRIVER_LEDC, channel, &o, &c));
}

void expectEndpoints(uint8_t channel, uint16_t openUs, uint16_t closeUs) {
    uint16_t o = 0;
    uint16_t c = 0;
    TEST_ASSERT_TRUE(configCacheReadServoOutputEndpoints(SERVO_DRIVER_LEDC, channel, &o, &c));
    TEST_ASSERT_EQUAL_UINT16(openUs, o);
    TEST_ASSERT_EQUAL_UINT16(closeUs, c);
}

void typeEndpoints(uint8_t channel, uint16_t openUs, uint16_t closeUs) {
    ServoOutputEdit edit = {};
    edit.driver = SERVO_DRIVER_LEDC;
    edit.channel = channel;
    edit.fields = (uint16_t)(SERVO_FIELD_OPEN | SERVO_FIELD_CLOSE);
    edit.open_us = openUs;
    edit.close_us = closeUs;
    configCacheApplyServoOutputEdits(&edit, 1);
}

// What loadConfigToState() does with what is in NVS (src/main.cpp, which the
// native build does not compile), in its order: the snapshot, the rows beside
// it, the lit wire's tick, then the cache.
void bootFrom(Preferences& prefs, ConfigSnapshot* snap) {
    prefs.begin(NVS_NAMESPACE, true);
    configLoad(prefs, snap);
    ServoOutputRepairReport report = {};
    configLoadServoOutputs(prefs, &report);
    prefs.end();
    boardOutputTickAdoptedLight(report, &snap->system);
    configCacheApply(*snap);
}

// The Output `main`'s aux_led_pin slot named: the Nth light-capable one.
size_t outputForRetiredSlot(uint8_t slot) {
    uint8_t seen = 0;
    for (size_t i = 0; i < BOARD_OUTPUT_COUNT; ++i) {
        if (BOARD_OUTPUTS[i].lightCapable && ++seen == slot) {
            return i;
        }
    }
    return BOARD_OUTPUT_COUNT;
}

bool stripDriven(const ConfigSnapshot& snap, size_t output) {
    OutputWireInputs in = {};
    in.wired = boardOutputIsWired(snap.system, output);
    in.component =
        configCacheReadServoOutputComponent(SERVO_DRIVER_LEDC, BOARD_OUTPUTS[output].channel);
    return outputWireStripDriven(in, output);
}

}  // namespace

// Test: a pair the band narrowed keeps `main`'s keys until that Output is saved
//
// The operator's call on #417: #286's band stays, and it narrows visibly. The
// keys are the only record of the builder's own numbers, so a save that is
// about anything else must leave them, and a later boot - which reads the row
// as stored, not from the keys - must still know the row is the narrowed one.
void test_a_narrowed_pair_keeps_its_keys_until_that_output_is_saved() {
    Preferences prefs;
    prefs.begin("proto", false);
    prefs.clear();
    prefs.putUShort("arm1_op", 2200);  // legal on `main`, past what an MG996R takes
    prefs.putUShort("arm1_cl", 2100);
    prefs.putUChar("arm1_type", (uint8_t)SERVO_COMP_MG996R);
    prefs.putUShort("arm2_op", 1850);  // inside the band: adopted as it stood
    prefs.putUShort("arm2_cl", 1150);
    prefs.putUChar("arm2_type", (uint8_t)SERVO_COMP_MG996R);

    ServoOutputRepairReport report = {};
    configLoadServoOutputs(prefs, &report);
    expectEndpoints(LEDC_CH_ARM1, 2000, 2000);
    expectNarrowedFrom(LEDC_CH_ARM1, 2200, 2100);
    expectNotNarrowed(LEDC_CH_ARM2);

    TEST_ASSERT_TRUE(configSaveServoOutputs(prefs));
    TEST_ASSERT_TRUE(prefs.isKey("so00"));
    TEST_ASSERT_TRUE(prefs.isKey("arm1_op"));
    TEST_ASSERT_TRUE(prefs.isKey("arm1_cl"));
    TEST_ASSERT_TRUE(prefs.isKey("arm1_type"));
    TEST_ASSERT_FALSE(prefs.isKey("arm2_op"));
    TEST_ASSERT_FALSE(prefs.isKey("arm2_cl"));
    TEST_ASSERT_FALSE(prefs.isKey("arm2_type"));

    ServoOutputRepairReport again = {};
    configLoadServoOutputs(prefs, &again);
    expectEndpoints(LEDC_CH_ARM1, 2000, 2000);
    expectNarrowedFrom(LEDC_CH_ARM1, 2200, 2100);

    // The builder saves that Output. That ends it, and the next save removes
    // the keys.
    typeEndpoints(LEDC_CH_ARM1, 1900, 1100);
    expectNotNarrowed(LEDC_CH_ARM1);
    TEST_ASSERT_TRUE(configSaveServoOutputs(prefs));
    TEST_ASSERT_FALSE(prefs.isKey("arm1_op"));
    TEST_ASSERT_FALSE(prefs.isKey("arm1_cl"));
    TEST_ASSERT_FALSE(prefs.isKey("arm1_type"));

    ServoOutputRepairReport third = {};
    configLoadServoOutputs(prefs, &third);
    expectEndpoints(LEDC_CH_ARM1, 1900, 1100);
    expectNotNarrowed(LEDC_CH_ARM1);
    prefs.end();
}

// Test: the upgrade from `main`, end to end, from what `main` leaves in NVS
//
// This path runs once on a real droid and cannot be taken back - the first
// save removes `main`'s keys - so it is walked here through boot, save and a
// second boot with the real load and save sequence. One lit wire, as `main`
// had exactly one: lit from the stored slot, typed as a strip, and ticked or
// not, since `main` never asked the tick. An MG996R pair `main` held legally
// and the band cannot, and one it can.
static void walkTheUpgradeFromMain(bool litWireTicked) {
    const size_t lit = outputForRetiredSlot(2);
    TEST_ASSERT_TRUE(lit < BOARD_OUTPUT_COUNT);
    const size_t litSet = servoLegacyFieldSetForChannel(BOARD_OUTPUTS[lit].channel);
    TEST_ASSERT_TRUE(litSet < SERVO_LEGACY_FIELD_SET_COUNT);
    // The wired tick's NVS key, as src/config_serializer.cpp spells it: "en_aux2".
    char tickKey[16] = {};
    snprintf(tickKey, sizeof(tickKey), "en_%s", BOARD_OUTPUTS[lit].id);

    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.clear();
    prefs.putUChar(CONFIG_SCHEMA_VERSION_KEY, CONFIG_SCHEMA_VERSION);  // 3 on `main` too
    prefs.putUChar(NVS_KEY_RETIRED_AUX_LED_PIN, 2);
    prefs.putUChar(NVS_KEY_RETIRED_AUX_LED_COUNT, 40);
    prefs.putUChar(SERVO_LEGACY_FIELD_SETS[litSet].nvsTypeKey, (uint8_t)SERVO_COMP_RGB);
    prefs.putBool(tickKey, litWireTicked);
    prefs.putBool("en_arm1", true);
    prefs.putUShort("arm1_op", 2200);
    prefs.putUShort("arm1_cl", 2100);
    prefs.putUChar("arm1_type", (uint8_t)SERVO_COMP_MG996R);
    prefs.putUShort("arm2_op", 1850);
    prefs.putUShort("arm2_cl", 1150);
    prefs.putUChar("arm2_type", (uint8_t)SERVO_COMP_MG996R);
    prefs.end();

    ConfigSnapshot snap = {};
    bootFrom(prefs, &snap);
    TEST_ASSERT_TRUE(stripDriven(snap, lit));
    TEST_ASSERT_EQUAL_UINT8(
        40, configCacheReadServoOutputLedCount(SERVO_DRIVER_LEDC, BOARD_OUTPUTS[lit].channel));
    expectEndpoints(LEDC_CH_ARM1, 2000, 2000);
    expectNarrowedFrom(LEDC_CH_ARM1, 2200, 2100);
    expectEndpoints(LEDC_CH_ARM2, 1850, 1150);
    expectNotNarrowed(LEDC_CH_ARM2);

    // The first save: rows and the snapshot down, `main`'s keys gone except
    // the narrowed pair's.
    TEST_ASSERT_TRUE(saveConfigToNvs());
    prefs.begin(NVS_NAMESPACE, true);
    TEST_ASSERT_FALSE(prefs.isKey(NVS_KEY_RETIRED_AUX_LED_PIN));
    TEST_ASSERT_FALSE(prefs.isKey(NVS_KEY_RETIRED_AUX_LED_COUNT));
    TEST_ASSERT_TRUE(prefs.getBool(tickKey, false));
    TEST_ASSERT_TRUE(prefs.isKey("arm1_op"));
    TEST_ASSERT_FALSE(prefs.isKey("arm2_op"));
    prefs.end();

    // The next boot finds it all on the rows and the tick.
    ConfigSnapshot again = {};
    bootFrom(prefs, &again);
    TEST_ASSERT_TRUE(stripDriven(again, lit));
    TEST_ASSERT_EQUAL_UINT8(
        40, configCacheReadServoOutputLedCount(SERVO_DRIVER_LEDC, BOARD_OUTPUTS[lit].channel));
    expectEndpoints(LEDC_CH_ARM1, 2000, 2000);
    expectNarrowedFrom(LEDC_CH_ARM1, 2200, 2100);
    expectEndpoints(LEDC_CH_ARM2, 1850, 1150);
}

void test_the_upgrade_from_main_keeps_a_ticked_lit_wire_and_both_pairs() {
    walkTheUpgradeFromMain(true);
}

void test_the_upgrade_from_main_keeps_an_unticked_lit_wire_lit() {
    walkTheUpgradeFromMain(false);
}

// Test: the two zeros putString() returns are told apart (#375)
//
// An empty value that stored fine and a write that failed both come back as 0.
// The empty one is a real stored value -- configSerializeDome() writes an empty
// dome_wip for "no peer set" -- so the fix must keep reporting it as success
// while reporting the other as the failure it is.
void test_an_empty_string_stores_and_a_failed_write_does_not() {
    Preferences prefs;
    prefs.begin("proto", false);
    prefs.clear();
    PrefsWriter writer(prefs);

    TEST_ASSERT_TRUE(writer.writeStr("dome_wip", ""));
    TEST_ASSERT_TRUE(prefs.isKey("dome_wip"));
    TEST_ASSERT_EQUAL_STRING("", prefs.getString("dome_wip", "unset").c_str());

    prefs.failNextStringWrites(1);
    TEST_ASSERT_FALSE(writer.writeStr("droid_name", "artoo"));
    TEST_ASSERT_FALSE(prefs.isKey("droid_name"));

    // The write after the scheduled failure lands, and reports so.
    TEST_ASSERT_TRUE(writer.writeStr("droid_name", "artoo"));
    TEST_ASSERT_EQUAL_STRING("artoo", prefs.getString("droid_name", "unset").c_str());

    // nullptr was already an error and still is.
    TEST_ASSERT_FALSE(writer.writeStr("droid_name", nullptr));
    prefs.end();
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
void test_configValidate_light_led_count() {
    ConfigValidationResult result = configValidate(ConfigKey::LIGHT_LED_COUNT, SERVO_LIGHT_LEDS_MIN);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ConfigValidationResult::OK, (uint8_t)result);

    result = configValidate(ConfigKey::LIGHT_LED_COUNT, SERVO_LIGHT_LEDS_MAX);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ConfigValidationResult::OK, (uint8_t)result);

    // Zero is the one that matters: a strip configured to render nothing reads
    // as a strip that is simply off.
    result = configValidate(ConfigKey::LIGHT_LED_COUNT, SERVO_LIGHT_LEDS_MIN - 1);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ConfigValidationResult::OUT_OF_RANGE, (uint8_t)result);
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

    result = configValidate(ConfigKey::RC_INPUT_MODE, RC_INPUT_ELRS);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ConfigValidationResult::OK, (uint8_t)result);

    result = configValidate(ConfigKey::RC_INPUT_MODE, RC_INPUT_ELRS + 1);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ConfigValidationResult::INVALID_VALUE, (uint8_t)result);
}

// Test: the Sound Component Member is validated against the Component Registry,
// not against a numeric range. The picker offers the registry's rows, so the
// door has to refuse exactly the rows it does not offer: a roadmap part, and a
// selectable part belonging to another family.
void test_configValidate_sound_member_asks_the_registry() {
    const ComponentPartEntry* chirp = componentPartById("chirp");
    TEST_ASSERT_NOT_NULL(chirp);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ConfigValidationResult::OK,
                            (uint8_t)configValidate(ConfigKey::SOUND_MEMBER, chirp->value));

    const ComponentPartEntry* dfplayer = componentPartById("dfplayer_mini");
    TEST_ASSERT_NOT_NULL(dfplayer);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ConfigValidationResult::INVALID_VALUE,
                            (uint8_t)configValidate(ConfigKey::SOUND_MEMBER, dfplayer->value));

    const ComponentPartEntry* hoverboard = componentPartById("hoverboard");
    TEST_ASSERT_NOT_NULL(hoverboard);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ConfigValidationResult::INVALID_VALUE,
                            (uint8_t)configValidate(ConfigKey::SOUND_MEMBER, hoverboard->value));

    TEST_ASSERT_EQUAL_UINT8((uint8_t)ConfigValidationResult::INVALID_VALUE,
                            (uint8_t)configValidate(ConfigKey::SOUND_MEMBER, 250));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ConfigValidationResult::OUT_OF_RANGE,
                            (uint8_t)configValidate(ConfigKey::SOUND_MEMBER, 256));
}

// Test: a Component Member survives a reboot -- the acceptance criterion in one
// assertion, at the NVS layer where "survives" actually means something.
void test_sound_member_survives_a_save_and_load() {
    const ComponentPartEntry* mp3 = componentPartById("mp3_trigger");
    TEST_ASSERT_NOT_NULL(mp3);

    ConfigSnapshot saved = {};
    configSnapshotDefaults(&saved);
    saved.system.sound_member = mp3->value;

    Preferences prefs;
    prefs.begin("proto", false);
    TEST_ASSERT_TRUE(configSaveSystem(prefs, saved.system));

    SystemConfig loaded = {};
    configLoadSystem(prefs, &loaded);
    prefs.end();

    TEST_ASSERT_EQUAL_UINT8(mp3->value, loaded.sound_member);
    // And it is independent of the toggle: nothing above touched enable_audio.
    TEST_ASSERT_EQUAL(saved.system.enable_audio, loaded.enable_audio);
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

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.2f, applied.dome.dome_min_speed);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.95f, applied.dome.dome_max_speed);
    TEST_ASSERT_EQUAL_UINT16(1520, applied.dome.dome_neutral_us);
    TEST_ASSERT_EQUAL_UINT8(85, applied.dome.dome_speed_limit_pct);
    TEST_ASSERT_EQUAL_INT(true, applied.dome.dome_rnd_enable);
    TEST_ASSERT_EQUAL_STRING("192.168.0.99", applied.dome.dome_wifi_peer_ip);

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

// The Commanded Mode setters sync the config cache BY FIELD (ADR 0011's
// 2026-09-04 amendment). commandedSetStationary() is the Core 1 caller - the
// SBUS drive path calls it once per frame while driving - and it used to read
// all 944 B of ConfigSnapshot out of the cache, set one bool, and write all
// 944 B back through configCacheApply().
//
// Put the round trip back (configCacheRead / set / configCacheApply) and both
// assertions below go red: the RC mapping is marked dirty for a field the RC
// processor config does not contain, and any cache write that happened between
// this caller's read and its write-back is reverted.
void test_configCacheSetStationary_writes_only_that_field() {
    ConfigSnapshot seeded = {};
    configSnapshotDefaults(&seeded);
    seeded.drive.speedLimitMax = 321;
    seeded.audio.audioVolume = 17;
    seeded.system.logLevel = 2;
    seeded.system.stationary = false;
    seeded.system.enable_arm1 = true;
    configCacheApply(seeded);

    configCacheSetStationary(true);

    ConfigSnapshot expected = seeded;
    expected.system.stationary = true;

    ConfigSnapshot after = {};
    configCacheRead(&after);
    TEST_ASSERT_TRUE_MESSAGE(after.system.stationary, "the field the setter owns did not change");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, memcmp(&expected, &after, sizeof(ConfigSnapshot)),
                                  "the by-field setter touched something other than stationary");
}

void test_configCacheSetStationary_does_not_mark_the_rc_mapping_dirty() {
    ConfigSnapshot seeded = {};
    configSnapshotDefaults(&seeded);
    configCacheApply(seeded);

    // configCacheApply() above legitimately raised the flag; RcInputTask
    // clears it after a rebuild, and this is that cleared state.
    robotState.rcConfigDirty = false;

    configCacheSetStationary(true);
    TEST_ASSERT_FALSE_MESSAGE(robotState.rcConfigDirty,
                              "a stationary toggle marked the RC mapping dirty - stationary is not "
                              "in the RC processor config");

    configCacheSetStationary(false);
    TEST_ASSERT_FALSE_MESSAGE(robotState.rcConfigDirty,
                              "a stationary toggle marked the RC mapping dirty");
}

// The RC speed preset's write (#417): the preset and the limit it names, and
// nothing else - it runs on Core 1, and a whole-snapshot write there is what
// put a config POST's fields back. It does mark the RC mapping dirty, unlike
// stationary: the mapping caches the limit as its maxOut.
void test_configCacheSelectSpeedPreset_writes_only_the_speed_pair() {
    ConfigSnapshot seeded = {};
    configSnapshotDefaults(&seeded);
    seeded.drive.speedPresetSlow = 150;
    seeded.drive.speedPresetNormal = 300;
    seeded.drive.speedPresetTurbo = 600;
    seeded.drive.speedPresetActive = SpeedPresetId::Normal;
    seeded.drive.speedLimitMax = 300;
    seeded.audio.audioVolume = 17;
    configCacheApply(seeded);
    robotState.rcConfigDirty = false;

    TEST_ASSERT_EQUAL_INT(150, configCacheSelectSpeedPreset(SpeedPresetId::Slow));

    ConfigSnapshot expected = seeded;
    expected.drive.speedLimitMax = 150;
    expected.drive.speedPresetActive = SpeedPresetId::Slow;
    ConfigSnapshot after = {};
    configCacheRead(&after);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, memcmp(&expected, &after, sizeof(ConfigSnapshot)),
                                  "the speed preset setter touched something else");
    TEST_ASSERT_TRUE_MESSAGE(robotState.rcConfigDirty,
                             "the RC mapping would keep driving at the old limit");
}

void test_config_domain_load_functions_are_independently_callable() {
    ConfigSnapshot snap = {};
    snap.drive.speedLimitMax = 550;
    snap.audio.audioVolume = 12;
    snap.dome.dome_speed_limit_pct = 75;
    snap.system.enable_audio = true;

    Preferences prefs;
    prefs.begin("proto", false);
    TEST_ASSERT_TRUE(configSave(prefs, snap));

    DriveConfig drive = {};
    AudioConfig audio = {};
    DomeConfig dome = {};
    SystemConfig system = {};
    configLoadDrive(prefs, &drive);
    configLoadAudio(prefs, &audio);
    configLoadDome(prefs, &dome);
    configLoadSystem(prefs, &system);
    prefs.end();

    TEST_ASSERT_EQUAL_INT16(550, drive.speedLimitMax);
    TEST_ASSERT_EQUAL_UINT8(12, audio.audioVolume);
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
    baseline.dome.dome_speed_limit_pct = 75;
    baseline.system.enable_audio = true;
    TEST_ASSERT_TRUE(configSave(prefs, baseline));
}

static void assert_domain_round_trip_baseline_preserved(const ConfigSnapshot& loaded) {
    TEST_ASSERT_EQUAL_INT16(500, loaded.drive.speedLimitMax);
    TEST_ASSERT_EQUAL_UINT8(10, loaded.audio.audioVolume);
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

// --- the row is the only place an endpoint is stored (#345) ------------------

// A ConfigSnapshot cannot carry an endpoint any more, so an edit is the only
// way one reaches a row, and it is addressed. Applying one changes the row it
// names and nothing else - a snapshot written over the cache beside it cannot
// put a stale number back, because it has nowhere to keep one.
void test_an_addressed_edit_is_the_only_way_an_endpoint_changes() {
    Preferences prefs;
    prefs.begin("proto", false);
    prefs.clear();
    ServoOutputRepairReport report = {};
    configLoadServoOutputs(prefs, &report);
    prefs.end();

    ConfigSnapshot snap = {};
    configSnapshotDefaults(&snap);
    configCacheApply(snap);

    uint16_t openUs = 0;
    uint16_t closeUs = 0;
    TEST_ASSERT_TRUE(
        configCacheReadServoOutputEndpoints(SERVO_DRIVER_LEDC, LEDC_CH_ARM1, &openUs, &closeUs));
    TEST_ASSERT_EQUAL_UINT16(2000, openUs);

    ServoOutputEdit edit = {};
    edit.driver = SERVO_DRIVER_LEDC;
    edit.channel = LEDC_CH_ARM1;
    edit.fields = SERVO_FIELD_OPEN;
    edit.open_us = 1750;
    const ServoOutputRepairReport applied = configCacheApplyServoOutputEdits(&edit, 1);
    TEST_ASSERT_EQUAL_UINT8(0, applied.rowsRepaired);  // inside the band, nothing to report

    TEST_ASSERT_TRUE(
        configCacheReadServoOutputEndpoints(SERVO_DRIVER_LEDC, LEDC_CH_ARM1, &openUs, &closeUs));
    TEST_ASSERT_EQUAL_UINT16(1750, openUs);
    TEST_ASSERT_EQUAL_UINT16(1000, closeUs);  // the end nobody named is untouched

    // A whole-snapshot apply cannot undo it: the snapshot has no endpoint.
    configCacheApply(snap);
    TEST_ASSERT_TRUE(
        configCacheReadServoOutputEndpoints(SERVO_DRIVER_LEDC, LEDC_CH_ARM1, &openUs, &closeUs));
    TEST_ASSERT_EQUAL_UINT16(1750, openUs);
}

// An edit the fitted component cannot take is answered rather than applied
// quietly, in the same report the loader fills - and an edit naming an Output
// Address no live row has changes nothing and reports nothing.
void test_an_edit_outside_the_component_band_is_reported() {
    Preferences prefs;
    prefs.begin("proto", false);
    prefs.clear();
    ServoOutputRepairReport report = {};
    configLoadServoOutputs(prefs, &report);
    prefs.end();

    ServoOutputEdit edit = {};
    edit.driver = SERVO_DRIVER_LEDC;
    edit.channel = LEDC_CH_ARM1;  // an MG996R by default: 1000..2000 us
    edit.fields = SERVO_FIELD_OPEN;
    edit.open_us = 500;           // legal to the old fixed validator, not to this part
    const ServoOutputRepairReport applied = configCacheApplyServoOutputEdits(&edit, 1);
    TEST_ASSERT_EQUAL_UINT8(1, applied.rowsRepaired);
    // Open, and centre with it: the row is unmeasured, so its centre follows
    // the ends the edit gave it and is held to the same band on the way.
    TEST_ASSERT_EQUAL_UINT16((uint16_t)(SERVO_FIELD_OPEN | SERVO_FIELD_CENTRE),
                             applied.firstRowMask);
    TEST_ASSERT_EQUAL_UINT16(2, applied.fieldsRepaired);

    uint16_t openUs = 0;
    uint16_t closeUs = 0;
    TEST_ASSERT_TRUE(
        configCacheReadServoOutputEndpoints(SERVO_DRIVER_LEDC, LEDC_CH_ARM1, &openUs, &closeUs));
    TEST_ASSERT_EQUAL_UINT16(1000, openUs);

    // LEDC_CH_DOME drives an ESC, not a servo, so no row is addressed there.
    ServoOutputEdit unaddressed = {};
    unaddressed.driver = SERVO_DRIVER_LEDC;
    unaddressed.channel = LEDC_CH_DOME;
    unaddressed.fields = SERVO_FIELD_OPEN;
    unaddressed.open_us = 1234;
    const ServoOutputRepairReport none = configCacheApplyServoOutputEdits(&unaddressed, 1);
    TEST_ASSERT_EQUAL_UINT8(0, none.rowsRepaired);
    TEST_ASSERT_EQUAL_UINT16(0, none.fieldsRepaired);
}

// The servo drive path's only three doors onto a row, and all answer with values
// rather than with the row: their caller's worst-case static chain is a measured
// constant (ADR 0040) and a ServoOutputRow is 70 B to answer a question whose
// answer is one number, two or four.
void test_the_drive_path_asks_the_cache_for_values_not_a_row() {
    Preferences prefs;
    prefs.begin("proto", false);
    ServoOutputRepairReport report = {};
    configLoadServoOutputs(prefs, &report);
    prefs.end();

    // An MG996R output holds 1000..2000, and the caller is told which part
    // bounded the number without being handed the row it came from.
    ServoComponentType component = SERVO_COMP_RGB;  // poisoned, must be overwritten
    TEST_ASSERT_EQUAL_UINT16(
        1000, configCacheClampServoOutputPulse(SERVO_DRIVER_LEDC, LEDC_CH_ARM1, 500, &component));
    TEST_ASSERT_EQUAL_UINT8(SERVO_COMP_MG996R, component);

    // An address no row claims has no band to be held to: the request comes back
    // untouched and no component is invented for it.
    component = SERVO_COMP_RGB;
    TEST_ASSERT_EQUAL_UINT16(
        500, configCacheClampServoOutputPulse(SERVO_DRIVER_LEDC, LEDC_CH_DOME, 500, &component));
    TEST_ASSERT_EQUAL_UINT8(SERVO_COMP_NONE, component);

    // The pair comes back directional - a reversed linkage stays reversed.
    ServoOutputEdit reversed = {};
    reversed.driver = SERVO_DRIVER_LEDC;
    reversed.channel = LEDC_CH_ARM1;
    reversed.fields = SERVO_FIELD_OPEN | SERVO_FIELD_CLOSE;
    reversed.open_us = 1200;
    reversed.close_us = 1900;
    configCacheApplyServoOutputEdits(&reversed, 1);

    uint16_t openUs = 0;
    uint16_t closeUs = 0;
    TEST_ASSERT_TRUE(
        configCacheReadServoOutputEndpoints(SERVO_DRIVER_LEDC, LEDC_CH_ARM1, &openUs, &closeUs));
    TEST_ASSERT_EQUAL_UINT16(1200, openUs);
    TEST_ASSERT_EQUAL_UINT16(1900, closeUs);

    // And an unclaimed address leaves the caller's own fallback standing.
    openUs = 7;
    closeUs = 9;
    TEST_ASSERT_FALSE(
        configCacheReadServoOutputEndpoints(SERVO_DRIVER_LEDC, LEDC_CH_DOME, &openUs, &closeUs));
    TEST_ASSERT_EQUAL_UINT16(7, openUs);
    TEST_ASSERT_EQUAL_UINT16(9, closeUs);

    // The Motion Profile a move is planned from (#362): the ends of that same
    // reversed pair come back ordered, not as a negative span and not sorted by
    // the caller, and the two times and the calibrated bit are the row's own.
    ServoMotionProfile profile = {};
    profile.calibrated = true;  // poisoned, must be overwritten
    TEST_ASSERT_TRUE(
        configCacheReadServoOutputMotionProfile(SERVO_DRIVER_LEDC, LEDC_CH_ARM1, &profile));
    TEST_ASSERT_EQUAL_UINT16(1200, profile.loUs);
    TEST_ASSERT_EQUAL_UINT16(1900, profile.hiUs);
    TEST_ASSERT_EQUAL_UINT16(SERVO_THROW_MS_DEFAULT, profile.throwMs);
    TEST_ASSERT_EQUAL_UINT16(SERVO_ACCEL_MS_DEFAULT, profile.accelMs);
    TEST_ASSERT_EQUAL_UINT8(SERVO_EASE_NONE, profile.easing);
    TEST_ASSERT_FALSE(profile.calibrated);

    // A row somebody has measured and given its own pace says so. The row is
    // stored and read back the way a controller boots with one - and the two
    // times differ, so an accessor that swapped them could not pass.
    ServoOutputTable table = {};
    servoOutputTableDefaults(&table);
    ServoOutputRow& aux2 = table.rows[3];
    TEST_ASSERT_EQUAL_UINT8(LEDC_CH_AUX2, aux2.channel);
    aux2.open_us = 1900;
    aux2.close_us = 1100;
    aux2.throw_ms = 1400;
    aux2.accel_ms = 300;
    aux2.easing = SERVO_EASE_OVERSHOOT;
    aux2.calibrated = true;
    // And an overshoot on a row nobody measured, which the drive path must be
    // handed as `none` (ADR 0052): the degrade happens at this door, not in
    // ServoTask.
    ServoOutputRow& aux3 = table.rows[4];
    TEST_ASSERT_EQUAL_UINT8(LEDC_CH_AUX3, aux3.channel);
    aux3.easing = SERVO_EASE_OVERSHOOT;
    aux3.calibrated = false;
    prefs.begin("proto", false);
    PrefsWriter writer(prefs);
    TEST_ASSERT_TRUE(writeServoOutputTableForTest(table, writer));
    configLoadServoOutputs(prefs, &report);
    prefs.end();
    TEST_ASSERT_TRUE(
        configCacheReadServoOutputMotionProfile(SERVO_DRIVER_LEDC, LEDC_CH_AUX2, &profile));
    TEST_ASSERT_EQUAL_UINT16(1100, profile.loUs);
    TEST_ASSERT_EQUAL_UINT16(1900, profile.hiUs);
    TEST_ASSERT_EQUAL_UINT16(1400, profile.throwMs);
    TEST_ASSERT_EQUAL_UINT16(300, profile.accelMs);
    TEST_ASSERT_EQUAL_UINT8(SERVO_EASE_OVERSHOOT, profile.easing);
    TEST_ASSERT_TRUE(profile.calibrated);
    TEST_ASSERT_TRUE(
        configCacheReadServoOutputMotionProfile(SERVO_DRIVER_LEDC, LEDC_CH_AUX3, &profile));
    TEST_ASSERT_EQUAL_UINT8(SERVO_EASE_NONE, profile.easing);
    TEST_ASSERT_FALSE(profile.calibrated);

    // And an unclaimed address answers false with the profile left alone.
    profile.throwMs = 8;
    profile.calibrated = true;
    TEST_ASSERT_FALSE(
        configCacheReadServoOutputMotionProfile(SERVO_DRIVER_LEDC, LEDC_CH_DOME, &profile));
    TEST_ASSERT_EQUAL_UINT16(8, profile.throwMs);
    TEST_ASSERT_TRUE(profile.calibrated);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_configLoad_empty_nvs_returns_defaults);
    RUN_TEST(test_configLoad_save_roundtrip);
    RUN_TEST(test_configLoad_save_identity_accepts_lowercase);
    RUN_TEST(test_configLoad_save_identity_rejects_uppercase_to_default);
    RUN_TEST(test_configResolvedMdnsHostname_uses_identity_name);
    RUN_TEST(test_configResolvedMdnsHostname_falls_back_to_board_default);
    RUN_TEST(test_configValidate_speed_limit_out_of_range);
    RUN_TEST(test_configValidate_speed_limit_valid);
    RUN_TEST(test_configValidate_sbus_timeout_out_of_range);
    RUN_TEST(test_configValidate_sbus_timeout_valid);
    RUN_TEST(test_configValidate_audio_volume);
    RUN_TEST(test_a_saved_config_writes_no_fixed_servo_key);
    RUN_TEST(test_a_saved_config_removes_the_retired_sequence_dwell_keys);
    RUN_TEST(test_a_saved_row_removes_the_key_set_it_replaced);
    RUN_TEST(test_a_failed_row_write_keeps_the_legacy_keys);
    RUN_TEST(test_a_failed_row_write_stops_the_save_before_the_fixed_field_sets);
    RUN_TEST(test_a_narrowed_pair_keeps_its_keys_until_that_output_is_saved);
    RUN_TEST(test_the_upgrade_from_main_keeps_a_ticked_lit_wire_and_both_pairs);
    RUN_TEST(test_the_upgrade_from_main_keeps_an_unticked_lit_wire_lit);
    RUN_TEST(test_an_empty_string_stores_and_a_failed_write_does_not);
    RUN_TEST(test_configValidate_dome_speed);
    RUN_TEST(test_configValidate_booleans);
    RUN_TEST(test_configLoad_legacy_schema_v0);
    RUN_TEST(test_configLoad_schema_v1_migrates_info_log_level);
    RUN_TEST(test_configLoad_schema_v1_migrates_debug_log_level);
    RUN_TEST(test_configLoad_schema_v1_leaves_error_log_level_alone);
    RUN_TEST(test_configLoad_current_schema_does_not_remap_log_level);
    RUN_TEST(test_configLoad_schema_mismatch);
    RUN_TEST(test_configLoad_save_audio_tracks);
    RUN_TEST(test_configLoad_save_feature_toggles);
    RUN_TEST(test_configValidate_dome_speed_pct);
    RUN_TEST(test_configValidate_light_led_count);
    RUN_TEST(test_configValidate_sequence_timing);
    RUN_TEST(test_configValidate_rc_input_mode);
    RUN_TEST(test_configValidate_sound_member_asks_the_registry);
    RUN_TEST(test_sound_member_survives_a_save_and_load);
    RUN_TEST(test_configLoad_save_dome_wifi_peer_ip);
    RUN_TEST(test_configLoad_save_dome_wifi_peer_ip_empty);
    RUN_TEST(test_configLoad_save_moodcat_12bit_mask);
    RUN_TEST(test_configCacheRead_captures_all_categories);
    RUN_TEST(test_active_rc_config_survives_saved_toggle_and_mode_changes);
    RUN_TEST(test_active_component_toggles_survive_a_later_saved_write);
    RUN_TEST(test_configCacheRead_save_round_trip);
    RUN_TEST(test_configCacheApply_applies_all_categories);
    RUN_TEST(test_configCacheApply_does_not_touch_runtime_fields);
    RUN_TEST(test_configCacheSetStationary_writes_only_that_field);
    RUN_TEST(test_configCacheSetStationary_does_not_mark_the_rc_mapping_dirty);
    RUN_TEST(test_configCacheSelectSpeedPreset_writes_only_the_speed_pair);
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

    RUN_TEST(test_an_addressed_edit_is_the_only_way_an_endpoint_changes);
    RUN_TEST(test_an_edit_outside_the_component_band_is_reported);
    RUN_TEST(test_the_drive_path_asks_the_cache_for_values_not_a_row);
    return UNITY_END();
}
