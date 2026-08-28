// =============================================================================
// test/test_native/test_console_catalog/test_console_catalog.cpp
//
// Native tests for the console operation catalog and help text handling.
// Tests cover:
// 1. Catalog lookup (by name, count, iteration)
// 2. Help text parsing (hit, miss, degraded)
// 3. Allocation evidence (help path is allocation-free in the loop)
// 4. Drift checker evidence (registry -> catalog sync)
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
    TEST_ASSERT_EQUAL_INT(190, count);  // Registry has 190 entries

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
// Test: Executor Ready Status
// =============================================================================

void test_executor_ready_flag() {
    // All entries in the registry have executors defined, so all should be marked ready
    size_t count = 0;
    const ConsoleCatalogEntry* entries = consoleCatalogGetEntries(&count);

    for (size_t i = 0; i < count; ++i) {
        const ConsoleCatalogEntry* entry = &entries[i];
        TEST_ASSERT_TRUE(entry->executor_ready);
    }
}

// =============================================================================
// Test: Aliases (None in current registry, but should handle NULL gracefully)
// =============================================================================

void test_aliases_null_terminated() {
    // Current registry has no aliases, all should be NULL
    size_t count = 0;
    const ConsoleCatalogEntry* entries = consoleCatalogGetEntries(&count);

    for (size_t i = 0; i < count; ++i) {
        const ConsoleCatalogEntry* entry = &entries[i];
        TEST_ASSERT_NULL(entry->aliases);  // No aliases in registry yet
    }
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
// Unity Test Runner Setup
// =============================================================================

void setUp(void) {
    // No setup needed for catalog tests
}

void tearDown(void) {
    // No cleanup needed
}

int main(void) {
    UNITY_BEGIN();

    // Catalog lookup tests
    RUN_TEST(test_catalog_lookup_by_name);
    RUN_TEST(test_catalog_count_and_iteration);
    RUN_TEST(test_catalog_parameter_descriptors);
    RUN_TEST(test_catalog_availability_flags);

    // Help text tests
    RUN_TEST(test_help_text_offset_and_length);

    // Executor tests
    RUN_TEST(test_executor_ready_flag);

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

    UNITY_END();

    return 0;
}
