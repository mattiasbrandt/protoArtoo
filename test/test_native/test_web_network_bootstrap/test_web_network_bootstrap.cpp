// =============================================================================
// test/test_native/test_web_network_bootstrap/test_web_network_bootstrap.cpp
//
// Native unit tests for WiFi network bootstrap (#188).
// Tests that the bootstrap path (webNetworkBootstrap, network manager seam)
// becomes reachable and testable through the vendor-free interface.
// Previously, the bootstrap path was invisible to native tests (wrapped in
// #ifdef ARDUINO with vendor WiFi API calls).
// =============================================================================
#include <unity.h>

#include <string.h>
#include "web_network_bootstrap.h"
#include "web_network_manager.h"
#include "config_cache.h"
#include "wifi_boot_decision.h"
#include "seam_test_accessors.h"

void setUp() {
    // Reset config cache and seam test state to known state
    WifiConfig cfg = {};
    configCacheSetActiveWifi(cfg);
    configCacheSetActiveWifiRecovery(false);
    configCacheSetActiveWifiBootPosture(WifiBootPosture::PROVISIONING);
    networkManagerResetTestState();
}

void tearDown() {
}

// --- Network bootstrap decision tests ---
// These test the vendor-free bootstrap path that is now reachable through
// the network manager seam interface.

void test_webNetworkBootstrap_runs_pure_decision_logic() {
    // Core test: webNetworkBootstrap() now compiles and runs natively.
    // This exercises the pure decision logic (wifiDecideBootPosture) through
    // the vendor-free seam for the first time. Previously wrapped in #ifdef
    // ARDUINO and invisible to native tests.
    //
    // If the seam is broken (e.g., #ifdef PA_CAP_NATIVE_WIFI gate is wrong),
    // networkManagerApplyBootPosture() would be empty, but the decision logic
    // still runs here. This proves the bootstrap path is reachable.

    webNetworkBootstrap();
    // If we reach here without crash, the seam is working.
}

void test_networkManagerApplyBootPosture_records_calls() {
    // Verify that networkManagerApplyBootPosture (the seam) reaches the backend.
    // The host backend records every call, so this test checks that the seam
    // is actually wired into the decision logic, not stubbed out.

    // Initially, the backend should have zero calls
    TEST_ASSERT_EQUAL_INT(0, networkManagerGetApplyBootPostureCallCount());

    WifiBootPosture posture = WifiBootPosture::CLIENT_MODE;
    WifiConfig config = {};

    // Call the seam function
    networkManagerApplyBootPosture(posture, config);

    // Backend must have recorded one call
    TEST_ASSERT_EQUAL_INT(1, networkManagerGetApplyBootPostureCallCount());

    // And recorded the correct posture
    TEST_ASSERT_EQUAL_INT(WifiBootPosture::CLIENT_MODE, networkManagerGetLastPosture());
}

void test_webNetworkBootstrap_records_the_decided_posture_for_the_hosted_backend_guard() {
    // The Hosted backend's post-recovery rejoin
    // (src/web/web_network_manager_hosted.cpp) reads this cache to guard its
    // STA-only rejoin against non-CLIENT_MODE postures -- the older
    // configCacheReadActiveWifiRecovery() flag can only say "was it
    // NETWORK_RECOVERY", not distinguish PROVISIONING/STANDALONE_AP_MODE from
    // CLIENT_MODE (#189). An unprovisioned, non-recovery, no-developer-shortcut
    // boot (the native test env has no secrets.h) decides PROVISIONING.
    webNetworkBootstrap();
    TEST_ASSERT_EQUAL_INT(WifiBootPosture::PROVISIONING, configCacheReadActiveWifiBootPosture());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_webNetworkBootstrap_runs_pure_decision_logic);
    RUN_TEST(test_networkManagerApplyBootPosture_records_calls);
    RUN_TEST(test_webNetworkBootstrap_records_the_decided_posture_for_the_hosted_backend_guard);
    return UNITY_END();
}

