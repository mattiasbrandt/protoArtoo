// =============================================================================
// test/test_native/test_console_catalog/test_console_catalog.cpp
//
// Native tests for the console operation catalog and help text handling.
// Tests cover:
// 1. Catalog lookup (by name, count, iteration)
// 2. Help text parsing (hit, miss, degraded)
// 3. Registry -> catalog sync (availability flags, build flags, parameter
//    descriptors, aliases, write-exclusion) - this file carries no
//    allocation evidence: the help-path allocation proof (#225) lives in
//    test/test_native/test_console_help_reader/test_console_help_reader.cpp,
//    the one binary with the operator-new-counting override, per that
//    ticket's own reasoning against a second override here.
// =============================================================================

#include <unity.h>
#include <string.h>

#include "console_catalog.h"
#include "console_module.h"

// =============================================================================
// Test: Catalog Lookup
// =============================================================================

void test_catalog_lookup_by_name() {
    // Valid entry
    const ConsoleCatalogEntry* entry = consoleCatalogFindByName("drive.action.move");
    TEST_ASSERT_NOT_NULL(entry);
    TEST_ASSERT_EQUAL_STRING("drive.action.move", entry->name);
    TEST_ASSERT_EQUAL_STRING("action", entry->type);
    TEST_ASSERT_TRUE(entry->requires_web_control);
    TEST_ASSERT_TRUE(entry->safety_critical);

    // Non-existent entry
    entry = consoleCatalogFindByName("nonexistent.operation");
    TEST_ASSERT_NULL(entry);

    // NULL input
    entry = consoleCatalogFindByName(NULL);
    TEST_ASSERT_NULL(entry);
}

void test_catalog_count_and_iteration() {
    size_t count = consoleCatalogGetCount();
    TEST_ASSERT_GREATER_THAN(0, count);
    TEST_ASSERT_EQUAL_INT(193, count);  // Registry has 193 entries (#225 added system.config.log-level)

    // Verify we can iterate all entries
    size_t count_via_api = 0;
    const ConsoleCatalogEntry* entries = consoleCatalogGetEntries(&count_via_api);
    TEST_ASSERT_NOT_NULL(entries);
    TEST_ASSERT_EQUAL_INT(count, count_via_api);
}

void test_catalog_parameter_descriptors() {
    // Entry with parameters
    const ConsoleCatalogEntry* entry = consoleCatalogFindByName("drive.action.move");
    TEST_ASSERT_NOT_NULL(entry->params);

    // First parameter: speed
    TEST_ASSERT_EQUAL_STRING("speed", entry->params[0].name);
    TEST_ASSERT_EQUAL_STRING("int16", entry->params[0].type);
    TEST_ASSERT_TRUE(entry->params[0].required);

    // Second parameter: steer
    TEST_ASSERT_EQUAL_STRING("steer", entry->params[1].name);
    TEST_ASSERT_EQUAL_STRING("int16", entry->params[1].type);
    TEST_ASSERT_TRUE(entry->params[1].required);

    // Terminator
    TEST_ASSERT_NULL(entry->params[2].name);

    // Entry without parameters
    entry = consoleCatalogFindByName("drive.status.current");
    TEST_ASSERT_NULL(entry->params);
}

void test_catalog_availability_flags() {
    // All entries should have availability information
    size_t count = 0;
    const ConsoleCatalogEntry* entries = consoleCatalogGetEntries(&count);

    for (size_t i = 0; i < count; ++i) {
        const ConsoleCatalogEntry* entry = &entries[i];
        // available_on_board should be true for all entries (no board_capability in registry)
        TEST_ASSERT_TRUE(entry->available_on_board);
        // available_in_build may be false for entries with build_flag requirements
        // (PA_HEAP_PROFILE, PA_HEAP_TRACING, PA_ADMISSION_TRACE)
    }

    // Specific check: entries without build_flag should be available
    const ConsoleCatalogEntry* move = consoleCatalogFindByName("drive.action.move");
    TEST_ASSERT_NOT_NULL(move);
    TEST_ASSERT_TRUE(move->available_in_build);  // No build_flag requirement
}

void test_availability_tracks_build_flags() {
    // The native env sets PA_HEAP_PROFILE=0, PA_HEAP_TRACING=0, PA_ADMISSION_TRACE=1.
    // This test proves that availability is NOT constant - it varies based on
    // the build flags defined at compile time (ADR 0029).
    // If this test fails on availability being hardcoded true, the generator
    // did not emit the macro names correctly.

    // PA_HEAP_PROFILE=0 in the native env
    const ConsoleCatalogEntry* profiler = consoleCatalogFindByName("system.api.get-profiler");
    TEST_ASSERT_NOT_NULL(profiler);
    TEST_ASSERT_FALSE(profiler->available_in_build);

    // PA_HEAP_TRACING=0 in the native env
    const ConsoleCatalogEntry* trace_start = consoleCatalogFindByName("system.action.profiler-trace-start");
    TEST_ASSERT_NOT_NULL(trace_start);
    TEST_ASSERT_FALSE(trace_start->available_in_build);

    const ConsoleCatalogEntry* trace_stop = consoleCatalogFindByName("system.action.profiler-trace-stop");
    TEST_ASSERT_NOT_NULL(trace_stop);
    TEST_ASSERT_FALSE(trace_stop->available_in_build);

    // PA_ADMISSION_TRACE=1 in the native env - same build, opposite answer
    const ConsoleCatalogEntry* admission = consoleCatalogFindByName("system.api.get-admission-trace");
    TEST_ASSERT_NOT_NULL(admission);
    TEST_ASSERT_TRUE(admission->available_in_build);

    // and an unflagged entry is unconditionally in the build
    const ConsoleCatalogEntry* dome_seq = consoleCatalogFindByName("dome.action.dome-sequence");
    TEST_ASSERT_NOT_NULL(dome_seq);
    TEST_ASSERT_TRUE(dome_seq->available_in_build);
}

// =============================================================================
// Test: Help Text Storage and Addressing
// =============================================================================

void test_help_text_offset_and_length() {
    // Each catalog entry should have help_offset and help_length set
    size_t count = 0;
    const ConsoleCatalogEntry* entries = consoleCatalogGetEntries(&count);

    for (size_t i = 0; i < count; ++i) {
        const ConsoleCatalogEntry* entry = &entries[i];
        // All entries should have help text
        TEST_ASSERT_GREATER_THAN(0, entry->help_length);
        // Offset should be reasonable (< 100 KB total help size)
        TEST_ASSERT_LESS_THAN(100000, entry->help_offset + entry->help_length);
    }
}

// =============================================================================
// Test: Aliases (None in current registry, but should handle NULL gracefully)
// =============================================================================

void test_aliases_null_terminated() {
    // Registry has 38 rc_token entries that map to aliases.
    // Others have NULL aliases. All alias arrays are NULL-terminated.
    size_t count = 0;
    const ConsoleCatalogEntry* entries = consoleCatalogGetEntries(&count);

    // Count entries with aliases
    int aliases_count = 0;
    for (size_t i = 0; i < count; ++i) {
        const ConsoleCatalogEntry* entry = &entries[i];
        if (entry->aliases != NULL) {
            aliases_count++;
            // Each alias array must be NULL-terminated
            TEST_ASSERT_NOT_NULL(entry->aliases[0]);  // At least one alias
            // Find the NULL terminator
            int alias_idx = 0;
            while (entry->aliases[alias_idx] != NULL) {
                alias_idx++;
            }
            TEST_ASSERT_GREATER_THAN(0, alias_idx);  // At least one alias before NULL
        }
    }
    TEST_ASSERT_EQUAL_INT(38, aliases_count);  // Exactly 38 rc_token entries
}

// =============================================================================
// Test: Drift Checker - Catalog Entry Completeness
// =============================================================================

void test_catalog_entries_have_names() {
    // All entries must have a name
    size_t count = 0;
    const ConsoleCatalogEntry* entries = consoleCatalogGetEntries(&count);

    for (size_t i = 0; i < count; ++i) {
        TEST_ASSERT_NOT_NULL(entries[i].name);
        TEST_ASSERT_NOT_EQUAL('\0', entries[i].name[0]);
    }
}

void test_catalog_entries_have_types() {
    // All entries must have a valid type
    size_t count = 0;
    const ConsoleCatalogEntry* entries = consoleCatalogGetEntries(&count);

    for (size_t i = 0; i < count; ++i) {
        const char* type = entries[i].type;
        TEST_ASSERT_NOT_NULL(type);
        TEST_ASSERT(strcmp(type, "action") == 0 || strcmp(type, "status") == 0 ||
                   strcmp(type, "config") == 0 || strcmp(type, "event") == 0);
    }
}

void test_catalog_entries_have_help_text() {
    // All entries must have help text offset > 0 and length > 0
    size_t count = 0;
    const ConsoleCatalogEntry* entries = consoleCatalogGetEntries(&count);

    for (size_t i = 0; i < count; ++i) {
        const ConsoleCatalogEntry* entry = &entries[i];
        TEST_ASSERT_GREATER_THAN(0, entry->help_length);
    }
}

// =============================================================================
// Test: Specific Registry Entries
// =============================================================================

void test_drive_actions_present() {
    TEST_ASSERT_NOT_NULL(consoleCatalogFindByName("drive.action.move"));
    TEST_ASSERT_NOT_NULL(consoleCatalogFindByName("drive.action.speed"));
    TEST_ASSERT_NOT_NULL(consoleCatalogFindByName("drive.action.steer"));
}

void test_dome_sequences_present() {
    TEST_ASSERT_NOT_NULL(consoleCatalogFindByName("dome.seq.rockmarch"));
    TEST_ASSERT_NOT_NULL(consoleCatalogFindByName("dome.seq.cantina"));
}

void test_system_operations_present() {
    TEST_ASSERT_NOT_NULL(consoleCatalogFindByName("system.status.health"));
}

// =============================================================================
// Test: Safety Critical Marking
// =============================================================================

void test_safety_critical_entries() {
    const ConsoleCatalogEntry* move = consoleCatalogFindByName("drive.action.move");
    TEST_ASSERT_NOT_NULL(move);
    TEST_ASSERT_TRUE(move->safety_critical);

    const ConsoleCatalogEntry* speed = consoleCatalogFindByName("drive.action.speed");
    TEST_ASSERT_NOT_NULL(speed);
    TEST_ASSERT_FALSE(speed->safety_critical);
}

// =============================================================================
// Test: Web Control Requirement Marking
// =============================================================================

void test_web_control_requirement() {
    const ConsoleCatalogEntry* move = consoleCatalogFindByName("drive.action.move");
    TEST_ASSERT_NOT_NULL(move);
    TEST_ASSERT_TRUE(move->requires_web_control);

    const ConsoleCatalogEntry* estop = consoleCatalogFindByName("system.action.estop");
    TEST_ASSERT_NOT_NULL(estop);
    // estop may or may not require web control depending on registry
}

// =============================================================================
// Test: Enum Value Integrity (#249)
//
// PyYAML's default resolver reads a bare on/off/yes/no/y/n as a Python bool,
// not the string the registry means. aux.action.led-effect's `off` enum
// value was corrupted to boolean False by exactly that coercion and shipped
// into this catalog as the literal string "False" - a value parseAuxLedEffect()
// (src/tasks/aux_led.cpp) does not accept - while omitting "off", the value
// that does. These tests pin the generated catalog content directly, so a
// future regeneration from an unquoted or re-coerced registry value fails
// here even if the Python-level guard (tools/check_action_registry_drift.py's
// check_no_bool_enum_values(), test/test_tools/test_registry_yaml.py) is
// ever bypassed.
// =============================================================================

static const ConsoleParamDescriptor* findParamByName(const ConsoleCatalogEntry* entry, const char* name) {
    if (!entry || !entry->params) return NULL;
    for (const ConsoleParamDescriptor* p = entry->params; p->name != NULL; ++p) {
        if (strcmp(p->name, name) == 0) {
            return p;
        }
    }
    return NULL;
}

void test_aux_led_effect_enum_contains_off_not_false() {
    const ConsoleCatalogEntry* entry = consoleCatalogFindByName("aux.action.led-effect");
    TEST_ASSERT_NOT_NULL(entry);

    const ConsoleParamDescriptor* effect_param = findParamByName(entry, "effect");
    TEST_ASSERT_NOT_NULL(effect_param);
    TEST_ASSERT_NOT_NULL(effect_param->enum_values);

    bool found_off = false;
    for (const char* const* v = effect_param->enum_values; *v != NULL; ++v) {
        TEST_ASSERT_TRUE_MESSAGE(strcmp(*v, "False") != 0,
            "aux.action.led-effect enum must never ship \"False\" (#249)");
        TEST_ASSERT_TRUE_MESSAGE(strcmp(*v, "True") != 0,
            "aux.action.led-effect enum must never ship \"True\" (#249)");
        if (strcmp(*v, "off") == 0) {
            found_off = true;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(found_off, "aux.action.led-effect enum must offer \"off\" (#249)");
}

void test_play_banked_bank_enum_has_all_26_letters() {
    const ConsoleCatalogEntry* entry = consoleCatalogFindByName("sound.api.play-banked");
    TEST_ASSERT_NOT_NULL(entry);

    const ConsoleParamDescriptor* page_param = findParamByName(entry, "page");
    TEST_ASSERT_NOT_NULL(page_param);
    TEST_ASSERT_NOT_NULL(page_param->enum_values);

    size_t count = 0;
    bool found_y = false;
    bool found_n = false;
    for (const char* const* v = page_param->enum_values; *v != NULL; ++v, ++count) {
        if (strcmp(*v, "Y") == 0) found_y = true;
        if (strcmp(*v, "N") == 0) found_n = true;
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(26, (int)count,
        "sound.api.play-banked bank enum must carry all 26 letters (#249)");
    TEST_ASSERT_TRUE_MESSAGE(found_y, "bank enum missing \"Y\" (#249)");
    TEST_ASSERT_TRUE_MESSAGE(found_n, "bank enum missing \"N\" (#249)");
}

// =============================================================================
// Unity Test Runner Setup
// =============================================================================

void setUp(void) {
    // No setup needed for catalog tests
}

void tearDown(void) {
    // No cleanup needed
}

// The write-excluded parameter marker (#227). A registry param carrying
// `write_excluded: true` reaches the in-image catalog as a real flag, not
// only as prose in the FS-resident help text - that flag is what `help`
// renders as "write-excluded" and what tells a reader the field is known
// and documented but never settable from the Console
// (docs/console-protocol.md s.4.1). Both WiFi password params carry it;
// every settable param on the same entry must not, or the marker would say
// nothing.
void test_write_excluded_marks_only_the_wifi_password_params() {
    const ConsoleCatalogEntry* entry = consoleCatalogFindByName("wifi.config.settings");
    TEST_ASSERT_NOT_NULL(entry);
    TEST_ASSERT_NOT_NULL(entry->params);

    int excluded = 0;
    int settable = 0;
    for (const ConsoleParamDescriptor* p = entry->params; p->name != NULL; ++p) {
        if (p->write_excluded) {
            excluded++;
            TEST_ASSERT_NOT_NULL_MESSAGE(strstr(p->name, "password"),
                "only a password-valued param may be marked write-excluded");
        } else {
            settable++;
            TEST_ASSERT_NULL_MESSAGE(strstr(p->name, "password"),
                "a password param must never be settable");
        }
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, excluded, "sta-password and ap-password are write-excluded");
    TEST_ASSERT_EQUAL_INT_MESSAGE(3, settable, "mode, sta-ssid and ap-ssid stay settable");
}

int main(void) {
    UNITY_BEGIN();

    // Catalog lookup tests
    RUN_TEST(test_catalog_lookup_by_name);
    RUN_TEST(test_catalog_count_and_iteration);
    RUN_TEST(test_catalog_parameter_descriptors);
    RUN_TEST(test_catalog_availability_flags);
    RUN_TEST(test_availability_tracks_build_flags);

    // Help text tests
    RUN_TEST(test_help_text_offset_and_length);

    // Aliases tests
    RUN_TEST(test_aliases_null_terminated);

    // Drift checker tests
    RUN_TEST(test_catalog_entries_have_names);
    RUN_TEST(test_catalog_entries_have_types);
    RUN_TEST(test_catalog_entries_have_help_text);

    // Specific entry tests
    RUN_TEST(test_drive_actions_present);
    RUN_TEST(test_dome_sequences_present);
    RUN_TEST(test_system_operations_present);

    // Safety and control tests
    RUN_TEST(test_safety_critical_entries);
    RUN_TEST(test_web_control_requirement);

    // Enum value integrity tests (#249)
    RUN_TEST(test_aux_led_effect_enum_contains_off_not_false);
    RUN_TEST(test_play_banked_bank_enum_has_all_26_letters);

    // Write-excluded parameter marker (#227)
    RUN_TEST(test_write_excluded_marks_only_the_wifi_password_params);

    UNITY_END();

    return 0;
}
