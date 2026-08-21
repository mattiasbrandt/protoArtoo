// =============================================================================
// test/test_native/test_wifi_boot_decision/test_wifi_boot_decision.cpp
//
// Native unit tests for the pure WiFi Boot Posture decision layer (ADR 0015).
// =============================================================================
#include <unity.h>

#include "wifi_boot_decision.h"

void setUp(void) {}
void tearDown(void) {}

namespace {

WifiConfig unprovisionedSettings() {
    WifiConfig cfg = {};
    cfg.provisioned = false;
    cfg.mode = WifiMode::CLIENT;
    return cfg;
}

WifiConfig provisionedSettings(WifiMode mode) {
    WifiConfig cfg = {};
    cfg.provisioned = true;
    cfg.mode = mode;
    return cfg;
}

}  // namespace

// Unprovisioned Controller with no Developer WiFi Shortcut starts WiFi Provisioning.
void test_unprovisioned_without_shortcut_starts_provisioning(void) {
    WifiBootDecisionInput input;
    input.settings = unprovisionedSettings();

    TEST_ASSERT_EQUAL_INT((int)WifiBootPosture::PROVISIONING, (int)wifiDecideBootPosture(input));
}

// Provisioned WiFi Client Mode settings resolve to CLIENT_MODE.
void test_provisioned_client_mode(void) {
    WifiBootDecisionInput input;
    input.settings = provisionedSettings(WifiMode::CLIENT);

    TEST_ASSERT_EQUAL_INT((int)WifiBootPosture::CLIENT_MODE, (int)wifiDecideBootPosture(input));
}

// Provisioned Standalone AP Mode settings resolve to STANDALONE_AP_MODE.
void test_provisioned_standalone_ap_mode(void) {
    WifiBootDecisionInput input;
    input.settings = provisionedSettings(WifiMode::STANDALONE_AP);

    TEST_ASSERT_EQUAL_INT((int)WifiBootPosture::STANDALONE_AP_MODE, (int)wifiDecideBootPosture(input));
}

// Explicit Network Recovery Mode input wins regardless of provisioning state.
void test_explicit_network_recovery_overrides_unprovisioned(void) {
    WifiBootDecisionInput input;
    input.settings = unprovisionedSettings();
    input.networkRecoveryRequested = true;

    TEST_ASSERT_EQUAL_INT((int)WifiBootPosture::NETWORK_RECOVERY, (int)wifiDecideBootPosture(input));
}

void test_explicit_network_recovery_overrides_provisioned(void) {
    WifiBootDecisionInput input;
    input.settings = provisionedSettings(WifiMode::CLIENT);
    input.networkRecoveryRequested = true;

    TEST_ASSERT_EQUAL_INT((int)WifiBootPosture::NETWORK_RECOVERY, (int)wifiDecideBootPosture(input));
}

// Developer WiFi Shortcut lets an unprovisioned self-build boot straight into
// its compiled-in posture instead of WiFi Provisioning.
void test_developer_shortcut_client_mode_skips_provisioning(void) {
    WifiBootDecisionInput input;
    input.settings = unprovisionedSettings();
    input.developerShortcut.available = true;
    input.developerShortcut.mode = WifiMode::CLIENT;

    TEST_ASSERT_EQUAL_INT((int)WifiBootPosture::CLIENT_MODE, (int)wifiDecideBootPosture(input));
}

void test_developer_shortcut_standalone_ap_skips_provisioning(void) {
    WifiBootDecisionInput input;
    input.settings = unprovisionedSettings();
    input.developerShortcut.available = true;
    input.developerShortcut.mode = WifiMode::STANDALONE_AP;

    TEST_ASSERT_EQUAL_INT((int)WifiBootPosture::STANDALONE_AP_MODE, (int)wifiDecideBootPosture(input));
}

// A Developer WiFi Shortcut only matters while unprovisioned — once Device
// WiFi Settings are provisioned, the saved posture wins even if a shortcut
// is (still) compiled in.
void test_developer_shortcut_ignored_once_provisioned(void) {
    WifiBootDecisionInput input;
    input.settings = provisionedSettings(WifiMode::STANDALONE_AP);
    input.developerShortcut.available = true;
    input.developerShortcut.mode = WifiMode::CLIENT;

    TEST_ASSERT_EQUAL_INT((int)WifiBootPosture::STANDALONE_AP_MODE, (int)wifiDecideBootPosture(input));
}

// Rule: ordinary WiFi Client Mode connection trouble must never become
// automatic AP fallback. The decision input has no "STA connection failed"
// field at all — the same provisioned Client Mode settings always resolve to
// CLIENT_MODE, however many times the (pure, side-effect-free) decision is
// re-evaluated, because there is nothing in the input shape that could carry
// a runtime connectivity failure into the boot posture.
void test_provisioned_client_mode_never_becomes_ap_fallback(void) {
    WifiBootDecisionInput input;
    input.settings = provisionedSettings(WifiMode::CLIENT);

    for (int i = 0; i < 5; ++i) {
        TEST_ASSERT_EQUAL_INT((int)WifiBootPosture::CLIENT_MODE, (int)wifiDecideBootPosture(input));
    }
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_unprovisioned_without_shortcut_starts_provisioning);
    RUN_TEST(test_provisioned_client_mode);
    RUN_TEST(test_provisioned_standalone_ap_mode);
    RUN_TEST(test_explicit_network_recovery_overrides_unprovisioned);
    RUN_TEST(test_explicit_network_recovery_overrides_provisioned);
    RUN_TEST(test_developer_shortcut_client_mode_skips_provisioning);
    RUN_TEST(test_developer_shortcut_standalone_ap_skips_provisioning);
    RUN_TEST(test_developer_shortcut_ignored_once_provisioned);
    RUN_TEST(test_provisioned_client_mode_never_becomes_ap_fallback);
    return UNITY_END();
}
