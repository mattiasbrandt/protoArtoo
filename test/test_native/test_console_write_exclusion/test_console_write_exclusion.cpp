/**
 * Test: the Console's write-exclusion rule (include/console_write_exclusion.h,
 * #227)
 *
 * Two consequences of one flag, covered here at the seam both adapters share:
 *  - consoleOfferedParamAt(): Tab never offers a write-excluded key, and
 *    skipping one does not truncate the enumeration at that point (the
 *    "reduced completion catalog" trap - the parameter AFTER a skipped one
 *    must still be offered).
 *  - consoleLineAssignsWriteExcludedValue(): a submitted line that assigns a
 *    write-excluded value is recognised from the raw text, so no history can
 *    keep it.
 *
 * The real catalog only carries write-excluded parameters in trailing
 * position today (wifi.config.settings: sta-password, ap-password are the
 * last two, src/console/console_catalog.cpp), which cannot exercise the
 * truncation trap at all - hence the synthetic descriptor tables below for
 * the ordering cases, and the real catalog for everything that must stay true
 * of the shipped registry.
 */

#include <unity.h>
#include <string.h>

#include "console_catalog.h"
#include "console_write_exclusion.h"

void setUp(void) {}
void tearDown(void) {}

// Excluded parameter in the MIDDLE: what a future registry row looks like the
// moment a secret field is declared anywhere but last.
static const ConsoleParamDescriptor kMiddleExcluded[] = {
    {"first", "string", false, false, 0.0, 0.0, nullptr, false},
    {"secret", "string", false, false, 0.0, 0.0, nullptr, true},
    {"third", "string", false, false, 0.0, 0.0, nullptr, false},
    {"fourth", "string", false, false, 0.0, 0.0, nullptr, false},
    {nullptr, nullptr, false, false, 0.0, 0.0, nullptr, false},
};

static const ConsoleParamDescriptor kAllExcluded[] = {
    {"secret-a", "string", false, false, 0.0, 0.0, nullptr, true},
    {"secret-b", "string", false, false, 0.0, 0.0, nullptr, true},
    {nullptr, nullptr, false, false, 0.0, 0.0, nullptr, false},
};

// -----------------------------------------------------------------------
// consoleOfferedParamAt(): the skip must not leave a hole
// -----------------------------------------------------------------------
void test_offered_params_skip_excluded_without_truncating(void) {
    // Dense enumeration: index 0,1,2 are first,third,fourth - "secret" is
    // gone and nothing after it was lost with it.
    const ConsoleParamDescriptor* p0 = consoleOfferedParamAt(kMiddleExcluded, 0);
    const ConsoleParamDescriptor* p1 = consoleOfferedParamAt(kMiddleExcluded, 1);
    const ConsoleParamDescriptor* p2 = consoleOfferedParamAt(kMiddleExcluded, 2);

    TEST_ASSERT_NOT_NULL(p0);
    TEST_ASSERT_NOT_NULL(p1);
    TEST_ASSERT_NOT_NULL(p2);
    TEST_ASSERT_EQUAL_STRING("first", p0->name);
    TEST_ASSERT_EQUAL_STRING("third", p1->name);
    TEST_ASSERT_EQUAL_STRING("fourth", p2->name);

    // Enumeration ends exactly at the offered count (3), not at the array
    // length (4).
    TEST_ASSERT_NULL(consoleOfferedParamAt(kMiddleExcluded, 3));
}

void test_offered_params_never_returns_an_excluded_descriptor(void) {
    for (uint16_t i = 0; i < 8; ++i) {
        const ConsoleParamDescriptor* p = consoleOfferedParamAt(kMiddleExcluded, i);
        if (p == nullptr) continue;
        TEST_ASSERT_FALSE(p->write_excluded);
    }
}

void test_offered_params_all_excluded_yields_nothing(void) {
    TEST_ASSERT_NULL(consoleOfferedParamAt(kAllExcluded, 0));
    TEST_ASSERT_NULL(consoleOfferedParamAt(nullptr, 0));
}

// -----------------------------------------------------------------------
// consoleOfferedParamAt() against the real catalog
// -----------------------------------------------------------------------
void test_real_catalog_wifi_settings_offers_every_settable_key_only(void) {
    const ConsoleCatalogEntry* entry = consoleCatalogFindByName("wifi.config.settings");
    TEST_ASSERT_NOT_NULL(entry);
    TEST_ASSERT_NOT_NULL(entry->params);

    // The registry declares five params for this row, two of them
    // write_excluded (docs/action-registry.yaml).
    int declared = 0;
    int excluded = 0;
    for (const ConsoleParamDescriptor* p = entry->params; p->name != nullptr; ++p) {
        ++declared;
        if (p->write_excluded) ++excluded;
    }
    TEST_ASSERT_EQUAL_INT(5, declared);
    TEST_ASSERT_EQUAL_INT(2, excluded);

    TEST_ASSERT_EQUAL_STRING("mode", consoleOfferedParamAt(entry->params, 0)->name);
    TEST_ASSERT_EQUAL_STRING("sta-ssid", consoleOfferedParamAt(entry->params, 1)->name);
    TEST_ASSERT_EQUAL_STRING("ap-ssid", consoleOfferedParamAt(entry->params, 2)->name);
    TEST_ASSERT_NULL(consoleOfferedParamAt(entry->params, 3));
}

// -----------------------------------------------------------------------
// consoleLineAssignsWriteExcludedValue()
// -----------------------------------------------------------------------
void test_line_with_excluded_key_is_recognised(void) {
    TEST_ASSERT_TRUE(
        consoleLineAssignsWriteExcludedValue("wifi.config.settings sta-password=hunter2"));
    TEST_ASSERT_TRUE(
        consoleLineAssignsWriteExcludedValue("wifi.config.settings ap-password=hunter2"));
    // The excluded key need not be first, and an empty value still counts -
    // the key alone is what makes the line unstorable.
    TEST_ASSERT_TRUE(consoleLineAssignsWriteExcludedValue(
        "wifi.config.settings mode=client sta-ssid=lab sta-password="));
    // Leading whitespace is what the operator's line can genuinely carry.
    TEST_ASSERT_TRUE(
        consoleLineAssignsWriteExcludedValue("  wifi.config.settings sta-password=x"));
}

void test_case_variant_of_an_excluded_key_is_recognised(void) {
    // console_module.cpp refuses this line with secret-not-settable (its
    // secret test is case-insensitive), so history must not be narrower.
    TEST_ASSERT_TRUE(
        consoleLineAssignsWriteExcludedValue("wifi.config.settings sta-Password=hunter2"));
    TEST_ASSERT_TRUE(
        consoleLineAssignsWriteExcludedValue("wifi.config.settings AP-PASSWORD=hunter2"));
}

void test_settable_lines_are_storable(void) {
    TEST_ASSERT_FALSE(consoleLineAssignsWriteExcludedValue(
        "wifi.config.settings mode=client sta-ssid=\"Workshop WiFi\""));
    TEST_ASSERT_FALSE(consoleLineAssignsWriteExcludedValue("wifi.config.settings"));
    TEST_ASSERT_FALSE(consoleLineAssignsWriteExcludedValue("system.status.health"));
    TEST_ASSERT_FALSE(consoleLineAssignsWriteExcludedValue("drive.action.move speed=200 steer=0"));
    TEST_ASSERT_FALSE(consoleLineAssignsWriteExcludedValue(""));
    TEST_ASSERT_FALSE(consoleLineAssignsWriteExcludedValue("   "));
    TEST_ASSERT_FALSE(consoleLineAssignsWriteExcludedValue(nullptr));
}

void test_excluded_key_inside_a_quoted_value_is_not_an_assignment(void) {
    // The whole thing is one SSID, not two arguments: refusing this line
    // would drop a perfectly storable command carrying no secret.
    TEST_ASSERT_FALSE(consoleLineAssignsWriteExcludedValue(
        "wifi.config.settings sta-ssid=\"lab sta-password=x\""));
    // ... and a real assignment AFTER a quoted value is still found, so the
    // quote handling is a skip, not an early exit.
    TEST_ASSERT_TRUE(consoleLineAssignsWriteExcludedValue(
        "wifi.config.settings sta-ssid=\"lab bench\" sta-password=hunter2"));
    // An escaped quote inside the value does not end it.
    TEST_ASSERT_FALSE(consoleLineAssignsWriteExcludedValue(
        "wifi.config.settings sta-ssid=\"lab \\\" sta-password=x\""));
}

void test_unresolvable_operation_and_malformed_tokens(void) {
    // A first token that names no catalog operation has no declared
    // parameters, so nothing on the line can be known write-excluded (the
    // documented per-operation residual).
    TEST_ASSERT_FALSE(consoleLineAssignsWriteExcludedValue("not.a.real.operation sta-password=x"));
    // A bare word between arguments must not stop the scan before the
    // assignment that follows it.
    TEST_ASSERT_TRUE(
        consoleLineAssignsWriteExcludedValue("wifi.config.settings bareword sta-password=x"));
    TEST_ASSERT_TRUE(
        consoleLineAssignsWriteExcludedValue("wifi.config.settings =orphan sta-password=x"));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_offered_params_skip_excluded_without_truncating);
    RUN_TEST(test_offered_params_never_returns_an_excluded_descriptor);
    RUN_TEST(test_offered_params_all_excluded_yields_nothing);
    RUN_TEST(test_real_catalog_wifi_settings_offers_every_settable_key_only);
    RUN_TEST(test_line_with_excluded_key_is_recognised);
    RUN_TEST(test_case_variant_of_an_excluded_key_is_recognised);
    RUN_TEST(test_settable_lines_are_storable);
    RUN_TEST(test_excluded_key_inside_a_quoted_value_is_not_an_assignment);
    RUN_TEST(test_unresolvable_operation_and_malformed_tokens);
    return UNITY_END();
}
