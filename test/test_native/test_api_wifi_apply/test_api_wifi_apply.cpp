// =============================================================================
// test/test_native/test_api_wifi_apply/test_api_wifi_apply.cpp
//
// Native unit tests for wifiApply() (ADR 0011 Apply Core / ADR 0015 Device
// WiFi Settings, issue #45). Exercises the pure validate/apply logic against
// a WifiConfig through a std::map-backed ConfigParamSource, without
// FreeRTOS, AsyncWebServerRequest, or WiFi hardware.
// =============================================================================
#include <unity.h>

#include <cstring>
#include <map>
#include <string>

#include "api_wifi_apply.h"

namespace {

const char* mapGet(void* ctx, const char* name) {
    auto* m = static_cast<std::map<std::string, std::string>*>(ctx);
    auto it = m->find(name);
    if (it == m->end()) {
        return nullptr;
    }
    return it->second.c_str();
}

ConfigParamSource makeSource(std::map<std::string, std::string>* m) {
    ConfigParamSource src;
    src.ctx = m;
    src.get = mapGet;
    return src;
}

WifiConfig unprovisionedDefault() {
    WifiConfig cfg = {};
    cfg.provisioned = false;
    cfg.mode = WifiMode::CLIENT;
    cfg.sta_ssid[0] = '\0';
    cfg.sta_password[0] = '\0';
    snprintf(cfg.ap_ssid, sizeof(cfg.ap_ssid), "protoArtoo");
    snprintf(cfg.ap_password, sizeof(cfg.ap_password), "artooDefault");
    return cfg;
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

// --- no-fields case ---
void test_wifiApply_no_fields_supplied_returns_error(void) {
    std::map<std::string, std::string> m;
    WifiConfig cfg = unprovisionedDefault();
    WifiApplyResult result;
    wifiApply(makeSource(&m), &cfg, &result);
    TEST_ASSERT_FALSE(result.ok);
    TEST_ASSERT_EQUAL_STRING("no wifi fields supplied", result.errorMessage);
}

// --- valid WiFi Client Mode ---
void test_wifiApply_valid_client_mode_settings(void) {
    std::map<std::string, std::string> m = {
        {"wifiMode", "client"}, {"staSsid", "HomeNetwork"}, {"staPassword", "supersecret"}};
    WifiConfig cfg = unprovisionedDefault();
    WifiApplyResult result;
    wifiApply(makeSource(&m), &cfg, &result);
    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_TRUE(cfg.provisioned);
    TEST_ASSERT_TRUE(cfg.mode == WifiMode::CLIENT);
    TEST_ASSERT_EQUAL_STRING("HomeNetwork", cfg.sta_ssid);
    TEST_ASSERT_EQUAL_STRING("supersecret", cfg.sta_password);
}

// --- valid Standalone AP Mode ---
void test_wifiApply_valid_standalone_ap_settings(void) {
    std::map<std::string, std::string> m = {
        {"wifiMode", "standalone_ap"}, {"apSsid", "MyArtoo"}, {"apPassword", "changeme1"}};
    WifiConfig cfg = unprovisionedDefault();
    WifiApplyResult result;
    wifiApply(makeSource(&m), &cfg, &result);
    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_TRUE(cfg.provisioned);
    TEST_ASSERT_TRUE(cfg.mode == WifiMode::STANDALONE_AP);
    TEST_ASSERT_EQUAL_STRING("MyArtoo", cfg.ap_ssid);
    TEST_ASSERT_EQUAL_STRING("changeme1", cfg.ap_password);
}

// --- Standalone AP Mode with open (empty) AP password is allowed ---
void test_wifiApply_standalone_ap_open_password_allowed(void) {
    std::map<std::string, std::string> m = {
        {"wifiMode", "standalone_ap"}, {"apSsid", "MyArtoo"}, {"apPassword", ""}};
    WifiConfig cfg = unprovisionedDefault();
    WifiApplyResult result;
    wifiApply(makeSource(&m), &cfg, &result);
    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_EQUAL_STRING("", cfg.ap_password);
}

// --- invalid mode value ---
void test_wifiApply_invalid_mode_rejected(void) {
    std::map<std::string, std::string> m = {{"wifiMode", "bridge"}};
    WifiConfig cfg = unprovisionedDefault();
    WifiApplyResult result;
    wifiApply(makeSource(&m), &cfg, &result);
    TEST_ASSERT_FALSE(result.ok);
    TEST_ASSERT_EQUAL_STRING("wifiMode must be client or standalone_ap", result.errorMessage);
}

// --- empty STA SSID makes WiFi Client Mode unusable ---
void test_wifiApply_client_mode_requires_sta_ssid(void) {
    std::map<std::string, std::string> m = {{"wifiMode", "client"}};
    WifiConfig cfg = unprovisionedDefault();
    WifiApplyResult result;
    wifiApply(makeSource(&m), &cfg, &result);
    TEST_ASSERT_FALSE(result.ok);
    TEST_ASSERT_EQUAL_STRING("staSsid is required for WiFi Client Mode", result.errorMessage);
}

// --- empty AP SSID makes Standalone AP Mode unusable ---
void test_wifiApply_standalone_ap_requires_ap_ssid(void) {
    std::map<std::string, std::string> m = {{"wifiMode", "standalone_ap"}, {"apSsid", ""}};
    WifiConfig cfg = unprovisionedDefault();
    WifiApplyResult result;
    wifiApply(makeSource(&m), &cfg, &result);
    TEST_ASSERT_FALSE(result.ok);
    TEST_ASSERT_EQUAL_STRING("apSsid is required for Standalone AP Mode", result.errorMessage);
}

// --- overlong SSID rejected ---
void test_wifiApply_overlong_sta_ssid_rejected(void) {
    std::string longSsid(33, 'a');  // WIFI_SSID_MAX_LEN is 32
    std::map<std::string, std::string> m = {{"staSsid", longSsid}};
    WifiConfig cfg = unprovisionedDefault();
    WifiApplyResult result;
    wifiApply(makeSource(&m), &cfg, &result);
    TEST_ASSERT_FALSE(result.ok);
    TEST_ASSERT_EQUAL_STRING("staSsid must be at most 32 characters", result.errorMessage);
}

// --- invalid (too-short, non-empty) AP password rejected ---
void test_wifiApply_short_ap_password_rejected(void) {
    std::map<std::string, std::string> m = {{"apPassword", "short"}};
    WifiConfig cfg = unprovisionedDefault();
    WifiApplyResult result;
    wifiApply(makeSource(&m), &cfg, &result);
    TEST_ASSERT_FALSE(result.ok);
    TEST_ASSERT_EQUAL_STRING("apPassword must be empty or 8..63 characters", result.errorMessage);
}

// --- overlong AP password rejected ---
void test_wifiApply_overlong_ap_password_rejected(void) {
    std::string longPw(64, 'a');  // WIFI_PASSWORD_MAX_LEN is 63
    std::map<std::string, std::string> m = {{"apPassword", longPw}};
    WifiConfig cfg = unprovisionedDefault();
    WifiApplyResult result;
    wifiApply(makeSource(&m), &cfg, &result);
    TEST_ASSERT_FALSE(result.ok);
    TEST_ASSERT_EQUAL_STRING("apPassword must be empty or 8..63 characters", result.errorMessage);
}

// --- omitted password preserves existing stored value ---
void test_wifiApply_omitted_sta_password_preserves_existing(void) {
    std::map<std::string, std::string> m = {{"staSsid", "HomeNetwork"}};
    WifiConfig cfg = unprovisionedDefault();
    snprintf(cfg.sta_password, sizeof(cfg.sta_password), "oldpassword");
    WifiApplyResult result;
    wifiApply(makeSource(&m), &cfg, &result);
    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_EQUAL_STRING("oldpassword", cfg.sta_password);
    TEST_ASSERT_EQUAL_STRING("HomeNetwork", cfg.sta_ssid);
}

// --- supplied password updates only that credential ---
void test_wifiApply_supplied_sta_password_updates_only_that_field(void) {
    std::map<std::string, std::string> m = {{"staPassword", "newpassword"}};
    WifiConfig cfg = unprovisionedDefault();
    cfg.provisioned = true;
    cfg.mode = WifiMode::CLIENT;
    snprintf(cfg.sta_ssid, sizeof(cfg.sta_ssid), "HomeNetwork");
    snprintf(cfg.sta_password, sizeof(cfg.sta_password), "oldpassword");
    WifiApplyResult result;
    wifiApply(makeSource(&m), &cfg, &result);
    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_EQUAL_STRING("HomeNetwork", cfg.sta_ssid);
    TEST_ASSERT_EQUAL_STRING("newpassword", cfg.sta_password);
}

// --- explicit empty password overwrites (write-only, not omission) ---
void test_wifiApply_explicit_empty_sta_password_overwrites(void) {
    std::map<std::string, std::string> m = {{"staPassword", ""}};
    WifiConfig cfg = unprovisionedDefault();
    cfg.provisioned = true;
    cfg.mode = WifiMode::CLIENT;
    snprintf(cfg.sta_ssid, sizeof(cfg.sta_ssid), "HomeNetwork");
    snprintf(cfg.sta_password, sizeof(cfg.sta_password), "oldpassword");
    WifiApplyResult result;
    wifiApply(makeSource(&m), &cfg, &result);
    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_EQUAL_STRING("", cfg.sta_password);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_wifiApply_no_fields_supplied_returns_error);
    RUN_TEST(test_wifiApply_valid_client_mode_settings);
    RUN_TEST(test_wifiApply_valid_standalone_ap_settings);
    RUN_TEST(test_wifiApply_standalone_ap_open_password_allowed);
    RUN_TEST(test_wifiApply_invalid_mode_rejected);
    RUN_TEST(test_wifiApply_client_mode_requires_sta_ssid);
    RUN_TEST(test_wifiApply_standalone_ap_requires_ap_ssid);
    RUN_TEST(test_wifiApply_overlong_sta_ssid_rejected);
    RUN_TEST(test_wifiApply_short_ap_password_rejected);
    RUN_TEST(test_wifiApply_overlong_ap_password_rejected);
    RUN_TEST(test_wifiApply_omitted_sta_password_preserves_existing);
    RUN_TEST(test_wifiApply_supplied_sta_password_updates_only_that_field);
    RUN_TEST(test_wifiApply_explicit_empty_sta_password_overwrites);
    return UNITY_END();
}
