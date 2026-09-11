// =============================================================================
// test/test_native/test_api_config_get/test_api_config_get.cpp
//
// Native unit tests for GET /api/config through the WebRequest seam's
// host-test backend (ADR 0021).
//
// test_api_config_json covers populateConfigJson() as a pure function. What is
// only true of the handler is covered here: the two runtime-state fields it
// adds on top of the pure snapshot, and that the bounded response buffer holds
// a worst-case config instead of truncating it.
// =============================================================================
#include <ArduinoJson.h>
#include <unity.h>

#include <cstring>

#include "api_config.h"
#include "config_cache.h"
#include "web_request_test_backend.h"

namespace {

ConfigSnapshot readSnapshot() {
    ConfigSnapshot snap = {};
    configCacheRead(&snap);
    return snap;
}

}  // namespace

void setUp() {
    ConfigSnapshot snap = {};
    configCacheApply(snap);
    configCacheSetActiveWifi(snap.wifi);
    configCacheSetActiveWifiRecovery(false);
}

void tearDown() {
}

void test_get_returns_config_json() {
    WebRequestTestBackend backend;
    WebRequest req(&backend);

    handleConfigGet(req);

    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    TEST_ASSERT_EQUAL_STRING("application/json", backend.sentContentType);
    TEST_ASSERT_EQUAL_UINT(1, backend.sendCalls);
    TEST_ASSERT_FALSE(backend.sentChunked);

    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, backend.sentBody));
    TEST_ASSERT_FALSE(doc["drive"].isNull());
    TEST_ASSERT_FALSE(doc["components"].isNull());
    TEST_ASSERT_FALSE(doc["wifi"].isNull());
}

void test_pending_apply_is_false_when_staged_matches_active() {
    ConfigSnapshot snap = readSnapshot();
    configCacheSetActiveWifi(snap.wifi);
    configCacheSetActiveWifiRecovery(false);

    WebRequestTestBackend backend;
    WebRequest req(&backend);
    handleConfigGet(req);

    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, backend.sentBody));
    TEST_ASSERT_FALSE(doc["wifi"]["pendingApply"].as<bool>());
    TEST_ASSERT_FALSE(doc["wifi"]["networkRecovery"].as<bool>());
}

void test_pending_apply_is_true_when_staged_differs_from_active() {
    // Stage a network switch: the persisted settings move, the active ones do
    // not. That difference is runtime state populateConfigJson() cannot see,
    // so only the handler can report it.
    ConfigSnapshot staged = readSnapshot();
    snprintf(staged.wifi.sta_ssid, sizeof(staged.wifi.sta_ssid), "%s", "bench-net");
    configCacheApply(staged);

    WifiConfig active = {};
    configCacheSetActiveWifi(active);
    configCacheSetActiveWifiRecovery(true);

    WebRequestTestBackend backend;
    WebRequest req(&backend);
    handleConfigGet(req);

    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, backend.sentBody));
    TEST_ASSERT_TRUE(doc["wifi"]["pendingApply"].as<bool>());
    TEST_ASSERT_TRUE(doc["wifi"]["networkRecovery"].as<bool>());
}

// The five fixed field sets are gone from the schema, not from the browser:
// data/servo.js still reads arm1OpenUs and its nine siblings, and
// components.arm1.type beside them, until the C1 wave rebuilds those pages onto
// the rows. The handler answers them FROM the rows, so what a surface renders
// is what the droid will drive to - and there is still exactly one place the
// number is stored (#345, ADR 0041).
void test_the_old_field_names_are_answered_from_the_rows() {
    Preferences prefs;
    prefs.begin("proto", false);
    prefs.clear();
    ServoOutputRepairReport report = {};
    configLoadServoOutputs(prefs, &report);
    prefs.end();

    // Move one row, addressed, the only way an endpoint can change now.
    ServoOutputEdit edit = {};
    edit.driver = SERVO_DRIVER_LEDC;
    edit.channel = LEDC_CH_ARM1;
    edit.fields = SERVO_FIELD_OPEN | SERVO_FIELD_CLOSE | SERVO_FIELD_COMPONENT;
    edit.open_us = 1850;
    edit.close_us = 1150;
    edit.component = SERVO_COMP_MG996R;
    configCacheApplyServoOutputEdits(&edit, 1);

    WebRequestTestBackend backend;
    WebRequest req(&backend);
    handleConfigGet(req);

    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, backend.sentBody));

    TEST_ASSERT_EQUAL_UINT(1850, doc["arm1OpenUs"].as<unsigned>());
    TEST_ASSERT_EQUAL_UINT(1150, doc["arm1CloseUs"].as<unsigned>());
    TEST_ASSERT_EQUAL_STRING("mg996r", doc["components"]["arm1"]["type"] | "");

    // Every set the default table addresses is answered, not just the one that
    // was edited - a page that asks for all ten still gets all ten.
    TEST_ASSERT_FALSE(doc["arm2OpenUs"].isNull());
    TEST_ASSERT_FALSE(doc["aux1OpenUs"].isNull());
    TEST_ASSERT_FALSE(doc["aux2CloseUs"].isNull());
    TEST_ASSERT_FALSE(doc["aux3CloseUs"].isNull());
    TEST_ASSERT_EQUAL_STRING("none", doc["components"]["aux3"]["type"] | "");
}

void test_worst_case_config_fits_the_response_buffer() {
    // Every length-bounded string at capacity. If this ever overflows the
    // handler's buffer the response is a 500, not a truncated config -- assert
    // it does not overflow in the first place.
    ConfigSnapshot snap = readSnapshot();
    memset(snap.wifi.sta_ssid, 'S', sizeof(snap.wifi.sta_ssid) - 1);
    memset(snap.wifi.ap_ssid, 'A', sizeof(snap.wifi.ap_ssid) - 1);
    memset(snap.wifi.sta_password, 'P', sizeof(snap.wifi.sta_password) - 1);
    memset(snap.wifi.ap_password, 'Q', sizeof(snap.wifi.ap_password) - 1);
    memset(snap.dome.dome_wifi_peer_ip, '9', sizeof(snap.dome.dome_wifi_peer_ip) - 1);
    configCacheApply(snap);

    WebRequestTestBackend backend;
    WebRequest req(&backend);
    handleConfigGet(req);

    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, backend.sentBody));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_get_returns_config_json);
    RUN_TEST(test_pending_apply_is_false_when_staged_matches_active);
    RUN_TEST(test_the_old_field_names_are_answered_from_the_rows);
    RUN_TEST(test_pending_apply_is_true_when_staged_differs_from_active);
    RUN_TEST(test_worst_case_config_fits_the_response_buffer);
    return UNITY_END();
}
