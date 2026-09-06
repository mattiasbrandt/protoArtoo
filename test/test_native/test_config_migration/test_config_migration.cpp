// =============================================================================
// test/test_native/test_config_migration/test_config_migration.cpp
//
// Native tests for config schema migrations.
// Verifies that old NVS keys are properly migrated to new keys and deleted.
// =============================================================================

#include <unity.h>
#include <Preferences.h>

#include "config_store.h"

// Helper to simulate old schema 2 data by directly writing to NVS
static void setupSchema2Data(Preferences& prefs) {
    // Set schema version to 2 (old)
    prefs.putUChar("schema_ver", 2);

    // Write old component toggle keys (schema 2)
    prefs.putBool("en_s1", true);    // will become en_drive
    prefs.putBool("en_dome", true);  // will become en_dome_esc
    prefs.putBool("en_s3", true);    // will become en_r2link
    prefs.putBool("en_s2", true);    // will become en_audio

    // Write old RC audio binding keys (schema 2)
    prefs.putString("rcp_snd", "ch:1");  // will become rcp_aud
    prefs.putString("rcs_snd", "ch:2");  // will become rcs_aud
    prefs.putString("rc_sound", "sw:0:1"); // will become rc_aud
}

void setUp(void) {
    // Initialize preferences for each test
}

void tearDown(void) {
    // Clean up after each test
}

void test_schema2_to_3_migration_renames_component_toggles(void) {
    // Test: verify old component toggle keys are migrated to new keys
    Preferences prefs;
    prefs.begin("test_config1", false);
    prefs.clear();

    setupSchema2Data(prefs);

    // Verify old keys exist before migration
    TEST_ASSERT_TRUE(prefs.isKey("en_s1"));
    TEST_ASSERT_TRUE(prefs.isKey("en_dome"));
    TEST_ASSERT_TRUE(prefs.isKey("en_s3"));
    TEST_ASSERT_TRUE(prefs.isKey("en_s2"));

    // Load config, which triggers migration
    ConfigSnapshot snap = {};
    configLoad(prefs, &snap);

    // Verify new keys now hold the migrated values
    TEST_ASSERT_TRUE(prefs.getBool("en_drive", false));
    TEST_ASSERT_TRUE(prefs.getBool("en_dome_esc", false));
    TEST_ASSERT_TRUE(prefs.getBool("en_r2link", false));
    TEST_ASSERT_TRUE(prefs.getBool("en_audio", false));

    // Verify old keys have been deleted
    TEST_ASSERT_FALSE(prefs.isKey("en_s1"));
    TEST_ASSERT_FALSE(prefs.isKey("en_dome"));
    TEST_ASSERT_FALSE(prefs.isKey("en_s3"));
    TEST_ASSERT_FALSE(prefs.isKey("en_s2"));

    // Verify snapshot holds the migrated values
    TEST_ASSERT_TRUE(snap.system.enable_drive);
    TEST_ASSERT_TRUE(snap.system.enable_dome_esc);
    TEST_ASSERT_TRUE(snap.system.enable_protor2link);
    TEST_ASSERT_TRUE(snap.system.enable_audio);

    prefs.end();
}

void test_schema2_to_3_migration_renames_rc_audio_bindings(void) {
    // Test: verify old RC audio binding keys are migrated to new keys
    Preferences prefs;
    prefs.begin("test_config2", false);
    prefs.clear();

    setupSchema2Data(prefs);

    // Verify old RC audio keys exist before migration
    TEST_ASSERT_TRUE(prefs.isKey("rcp_snd"));
    TEST_ASSERT_TRUE(prefs.isKey("rcs_snd"));
    TEST_ASSERT_TRUE(prefs.isKey("rc_sound"));

    // Load config, which triggers migration
    ConfigSnapshot snap = {};
    configLoad(prefs, &snap);

    // Verify new keys now hold the migrated string values
    String rcp_aud = prefs.getString("rcp_aud", "");
    String rcs_aud = prefs.getString("rcs_aud", "");
    String rc_aud = prefs.getString("rc_aud", "");

    TEST_ASSERT_EQUAL_STRING("ch:1", rcp_aud.c_str());
    TEST_ASSERT_EQUAL_STRING("ch:2", rcs_aud.c_str());
    TEST_ASSERT_EQUAL_STRING("sw:0:1", rc_aud.c_str());

    // Verify old keys have been deleted
    TEST_ASSERT_FALSE(prefs.isKey("rcp_snd"));
    TEST_ASSERT_FALSE(prefs.isKey("rcs_snd"));
    TEST_ASSERT_FALSE(prefs.isKey("rc_sound"));

    prefs.end();
}

void test_schema2_to_3_migration_does_not_repeat(void) {
    // Test: verify migration only runs once (schema version is updated)
    Preferences prefs;
    prefs.begin("test_config3", false);
    prefs.clear();

    setupSchema2Data(prefs);

    // First load: migration happens
    ConfigSnapshot snap1 = {};
    configLoad(prefs, &snap1);

    // Verify schema version is now 3
    uint8_t newVersion = prefs.getUChar("schema_ver", 0);
    TEST_ASSERT_EQUAL_INT(3, newVersion);

    // Second load: migration should NOT run again
    // If it did, it would try to read old keys that no longer exist
    ConfigSnapshot snap2 = {};
    bool ok = configLoad(prefs, &snap2);
    TEST_ASSERT_TRUE(ok);

    // Verify migrated values are still correct
    TEST_ASSERT_TRUE(snap2.system.enable_drive);
    TEST_ASSERT_TRUE(snap2.system.enable_dome_esc);
    TEST_ASSERT_TRUE(snap2.system.enable_protor2link);
    TEST_ASSERT_TRUE(snap2.system.enable_audio);

    prefs.end();
}

void test_schema2_to_3_migration_handles_missing_old_keys(void) {
    // Test: verify migration gracefully handles missing old keys (defaults used)
    Preferences prefs;
    prefs.begin("test_config4", false);
    prefs.clear();

    // Set schema version to 2 but don't set any old keys (they're missing)
    prefs.putUChar("schema_ver", 2);

    // Load config, which triggers migration
    ConfigSnapshot snap = {};
    configLoad(prefs, &snap);

    // Verify snapshot holds default values (false for toggles)
    TEST_ASSERT_FALSE(snap.system.enable_drive);
    TEST_ASSERT_FALSE(snap.system.enable_dome_esc);
    TEST_ASSERT_FALSE(snap.system.enable_protor2link);
    TEST_ASSERT_FALSE(snap.system.enable_audio);

    // Verify schema version is now 3
    uint8_t newVersion = prefs.getUChar("schema_ver", 0);
    TEST_ASSERT_EQUAL_INT(3, newVersion);

    prefs.end();
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_schema2_to_3_migration_renames_component_toggles);
    RUN_TEST(test_schema2_to_3_migration_renames_rc_audio_bindings);
    RUN_TEST(test_schema2_to_3_migration_does_not_repeat);
    RUN_TEST(test_schema2_to_3_migration_handles_missing_old_keys);
    return UNITY_END();
}
