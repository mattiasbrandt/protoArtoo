// =============================================================================
// test/test_native/test_wifi_module_update_support/test_wifi_module_update_support.cpp
//
// Native tests for WiFi Module Update Support (#241): the classifier step
// core and the GET /api/status JSON formatter. Assertions are specific
// enough that treating 0.0.0 as unknown, or emitting a version key when
// none was read, goes red.
// =============================================================================

#include <string.h>
#include <unity.h>

#include "api_status.h"
#include "wifi_module_update_support.h"

void setUp() {}
void tearDown() {}

// =============================================================================
// Classifier
// =============================================================================

void test_link_not_ready_is_unknown_and_omits_version_even_if_read_ok() {
    WifiModuleUpdateSupportInput in;
    in.linkReady = false;
    in.versionReadOk = true;
    in.versionMajor = 2;
    in.versionMinor = 12;
    in.versionPatch = 11;

    WifiModuleUpdateSupportResult r = wifiModuleClassifyUpdateSupport(in);

    TEST_ASSERT_EQUAL_INT((int)WifiModuleUpdateSupport::Unknown, (int)r.support);
    TEST_ASSERT_FALSE(r.versionPresent);
    TEST_ASSERT_EQUAL_UINT32(0, r.versionMajor);
    TEST_ASSERT_EQUAL_UINT32(0, r.versionMinor);
    TEST_ASSERT_EQUAL_UINT32(0, r.versionPatch);
}

void test_link_ready_version_rpc_refused_is_not_supported() {
    WifiModuleUpdateSupportInput in;
    in.linkReady = true;
    in.versionReadOk = false;
    in.versionMajor = 2;
    in.versionMinor = 12;
    in.versionPatch = 11;

    WifiModuleUpdateSupportResult r = wifiModuleClassifyUpdateSupport(in);

    TEST_ASSERT_EQUAL_INT((int)WifiModuleUpdateSupport::NotSupported, (int)r.support);
    TEST_ASSERT_FALSE(r.versionPresent);
}

void test_link_ready_version_read_ok_2_12_11_is_supported() {
    WifiModuleUpdateSupportInput in;
    in.linkReady = true;
    in.versionReadOk = true;
    in.versionMajor = 2;
    in.versionMinor = 12;
    in.versionPatch = 11;

    WifiModuleUpdateSupportResult r = wifiModuleClassifyUpdateSupport(in);

    TEST_ASSERT_EQUAL_INT((int)WifiModuleUpdateSupport::Supported, (int)r.support);
    TEST_ASSERT_TRUE(r.versionPresent);
    TEST_ASSERT_EQUAL_UINT32(2, r.versionMajor);
    TEST_ASSERT_EQUAL_UINT32(12, r.versionMinor);
    TEST_ASSERT_EQUAL_UINT32(11, r.versionPatch);
}

void test_link_ready_version_0_0_0_is_supported_not_unknown() {
    // ADR 0034 forbade treating 0.0.0 as a sentinel for "unreadable".
    // This test exists so nobody "fixes" it later.
    WifiModuleUpdateSupportInput in;
    in.linkReady = true;
    in.versionReadOk = true;
    in.versionMajor = 0;
    in.versionMinor = 0;
    in.versionPatch = 0;

    WifiModuleUpdateSupportResult r = wifiModuleClassifyUpdateSupport(in);

    TEST_ASSERT_EQUAL_INT((int)WifiModuleUpdateSupport::Supported, (int)r.support);
    TEST_ASSERT_TRUE(r.versionPresent);
    TEST_ASSERT_EQUAL_UINT32(0, r.versionMajor);
    TEST_ASSERT_EQUAL_UINT32(0, r.versionMinor);
    TEST_ASSERT_EQUAL_UINT32(0, r.versionPatch);
}

void test_update_support_name_strings() {
    TEST_ASSERT_EQUAL_STRING("unknown",
                             wifiModuleUpdateSupportName(WifiModuleUpdateSupport::Unknown));
    TEST_ASSERT_EQUAL_STRING("not_supported",
                             wifiModuleUpdateSupportName(WifiModuleUpdateSupport::NotSupported));
    TEST_ASSERT_EQUAL_STRING("supported",
                             wifiModuleUpdateSupportName(WifiModuleUpdateSupport::Supported));
}

// =============================================================================
// JSON formatter
// =============================================================================

static WifiModuleUpdateSupportResult makeResult(WifiModuleUpdateSupport support,
                                                bool versionPresent, uint32_t major,
                                                uint32_t minor, uint32_t patch) {
    WifiModuleUpdateSupportResult r{};
    r.support = support;
    r.versionPresent = versionPresent;
    r.versionMajor = major;
    r.versionMinor = minor;
    r.versionPatch = patch;
    return r;
}

void test_format_supported_typical_object() {
    char out[192];
    WifiModuleUpdateSupportResult r =
        makeResult(WifiModuleUpdateSupport::Supported, true, 2, 12, 11);
    formatWifiModuleJson(out, sizeof(out), r, 2, 12, 11);
    TEST_ASSERT_EQUAL_STRING(
        "{\"updateSupport\":\"supported\",\"version\":\"2.12.11\",\"hostVersion\":\"2.12.11\"}",
        out);
}

void test_format_unknown_omits_version_key() {
    char out[192];
    WifiModuleUpdateSupportResult r =
        makeResult(WifiModuleUpdateSupport::Unknown, false, 0, 0, 0);
    formatWifiModuleJson(out, sizeof(out), r, 2, 12, 11);
    TEST_ASSERT_EQUAL_STRING("{\"updateSupport\":\"unknown\",\"hostVersion\":\"2.12.11\"}", out);
    // Specific enough that emitting "0.0.0" (or any version key) for unknown
    // goes red. "hostVersion" does not contain the substring "\"version\":".
    TEST_ASSERT_NULL(strstr(out, "\"version\":"));
}

void test_format_supported_0_0_0_emits_honest_zero_version() {
    char out[192];
    WifiModuleUpdateSupportResult r =
        makeResult(WifiModuleUpdateSupport::Supported, true, 0, 0, 0);
    formatWifiModuleJson(out, sizeof(out), r, 2, 12, 11);
    TEST_ASSERT_EQUAL_STRING(
        "{\"updateSupport\":\"supported\",\"version\":\"0.0.0\",\"hostVersion\":\"2.12.11\"}",
        out);
}

void test_format_not_supported_omits_version_key() {
    char out[192];
    WifiModuleUpdateSupportResult r =
        makeResult(WifiModuleUpdateSupport::NotSupported, false, 0, 0, 0);
    formatWifiModuleJson(out, sizeof(out), r, 2, 12, 11);
    TEST_ASSERT_EQUAL_STRING(
        "{\"updateSupport\":\"not_supported\",\"hostVersion\":\"2.12.11\"}", out);
    TEST_ASSERT_NULL(strstr(out, "\"version\":"));
}

void test_format_serialized_size_budget() {
    // GET /api/status uses a 3072-byte static in handleStatusGet. Typical
    // wifiModule objects are tens of bytes; even worst-case uint32 versions
    // stay well under a 192-byte formatter buffer.
    char typical[192];
    WifiModuleUpdateSupportResult supported =
        makeResult(WifiModuleUpdateSupport::Supported, true, 2, 12, 11);
    formatWifiModuleJson(typical, sizeof(typical), supported, 2, 12, 11);
    TEST_ASSERT_LESS_THAN(96, strlen(typical));
    TEST_ASSERT_EQUAL_CHAR('}', typical[strlen(typical) - 1]);

    char worst[192];
    WifiModuleUpdateSupportResult huge =
        makeResult(WifiModuleUpdateSupport::Supported, true, 4294967295u, 4294967295u,
                   4294967295u);
    formatWifiModuleJson(worst, sizeof(worst), huge, 4294967295u, 4294967295u, 4294967295u);
    TEST_ASSERT_LESS_THAN(sizeof(worst), strlen(worst));
    TEST_ASSERT_EQUAL_CHAR('}', worst[strlen(worst) - 1]);
    TEST_ASSERT_LESS_THAN(160, strlen(worst));
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_link_not_ready_is_unknown_and_omits_version_even_if_read_ok);
    RUN_TEST(test_link_ready_version_rpc_refused_is_not_supported);
    RUN_TEST(test_link_ready_version_read_ok_2_12_11_is_supported);
    RUN_TEST(test_link_ready_version_0_0_0_is_supported_not_unknown);
    RUN_TEST(test_update_support_name_strings);

    RUN_TEST(test_format_supported_typical_object);
    RUN_TEST(test_format_unknown_omits_version_key);
    RUN_TEST(test_format_supported_0_0_0_emits_honest_zero_version);
    RUN_TEST(test_format_not_supported_omits_version_key);
    RUN_TEST(test_format_serialized_size_budget);

    return UNITY_END();
}
