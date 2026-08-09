// =============================================================================
// test/test_native/test_api_config_write/test_api_config_write.cpp
//
// Native unit tests for the config, RC-map and WiFi write routes driven
// through the WebRequest seam's host-test backend (ADR 0021).
//
// The apply cores already have their own tests; what is only true of the
// handlers is covered here: that a request's values actually reach the core,
// that the core's verdict is turned into the right status and body, and that a
// raw JSON body arrives under the "plain" name whichever backend delivered it.
// =============================================================================
#include <ArduinoJson.h>
#include <unity.h>

#include <cstring>

#include "api_config.h"
#include "config_cache.h"
#include "web_request_test_backend.h"

extern bool g_test_commanded_stationary;
extern unsigned g_test_status_broadcast_count;

namespace {

ConfigSnapshot readSnapshot() {
    ConfigSnapshot snap = {};
    configCacheRead(&snap);
    return snap;
}

}  // namespace

void setUp() {
    ConfigSnapshot snap = {};
    snap.drive.speedLimitMax = 100;
    configCacheApply(snap);
    configCacheSetActiveWifi(snap.wifi);
    configCacheSetActiveWifiRecovery(false);
    g_test_status_broadcast_count = 0;
}

void tearDown() {
}

// --- POST /api/config -------------------------------------------------------

void test_config_post_applies_a_field_and_echoes_the_snapshot() {
    const WebRequestTestParam params[] = {{"speedLimitMax", "80"}};
    WebRequestTestBackend backend;
    backend.params = params;
    backend.paramCount = 1;
    WebRequest req(&backend);

    handleConfigPost(req);

    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    TEST_ASSERT_EQUAL_STRING("application/json", backend.sentContentType);

    // The write took effect in the config cache...
    TEST_ASSERT_EQUAL_INT(80, readSnapshot().drive.speedLimitMax);

    // ...and the response echoes the same snapshot shape the read route
    // returns, which is what data/app.js re-renders from.
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, backend.sentBody));
    TEST_ASSERT_EQUAL_INT(80, doc["drive"]["speedLimitMax"].as<int>());
    TEST_ASSERT_FALSE(doc["wifi"]["pendingApply"].isNull());
    TEST_ASSERT_FALSE(doc["wifi"]["networkRecovery"].isNull());
}

void test_config_post_rejects_an_out_of_range_value_without_applying_it() {
    const WebRequestTestParam params[] = {{"speedLimitMax", "9999"}};
    WebRequestTestBackend backend;
    backend.params = params;
    backend.paramCount = 1;
    WebRequest req(&backend);

    handleConfigPost(req);

    TEST_ASSERT_EQUAL_INT(400, backend.sentCode);
    TEST_ASSERT_NOT_NULL(strstr(backend.sentBody, "\"ok\":false"));
    TEST_ASSERT_NOT_NULL(strstr(backend.sentBody, "\"error\":\""));
    // The rejected value must not have reached the cache.
    TEST_ASSERT_EQUAL_INT(100, readSnapshot().drive.speedLimitMax);
}

void test_config_post_accepts_a_raw_json_body_under_the_plain_name() {
    // The apply cores read a non-form body through the "plain" parameter, and
    // that path handles the nested schema (rc.sbusTimeoutMs and friends)
    // rather than the flat form fields. ESPAsyncWebServer surfaces such a body
    // as a parameter while PsychicHttp keeps it as the body; webParamSource()
    // reconciles that, and this is the test that the reconciliation actually
    // reaches the core.
    WebRequestTestBackend backend;
    backend.body = "{\"rc\":{\"sbusTimeoutMs\":250}}";
    WebRequest req(&backend);

    handleConfigPost(req);

    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    TEST_ASSERT_EQUAL_UINT32(250, readSnapshot().drive.sbusTimeoutMs);
}

void test_config_post_syncs_stationary_and_broadcasts_status() {
    const WebRequestTestParam params[] = {{"stationary", "true"}};
    WebRequestTestBackend backend;
    backend.params = params;
    backend.paramCount = 1;
    WebRequest req(&backend);

    handleConfigPost(req);

    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    TEST_ASSERT_TRUE(g_test_commanded_stationary);
    TEST_ASSERT_GREATER_THAN(0, g_test_status_broadcast_count);
}

// --- GET/POST /api/rc/map ---------------------------------------------------

void test_rc_map_get_returns_the_map_shape() {
    WebRequestTestBackend backend;
    WebRequest req(&backend);

    handleRcMapGet(req);

    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    TEST_ASSERT_EQUAL_STRING("application/json", backend.sentContentType);

    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, backend.sentBody));
    TEST_ASSERT_FALSE(doc["mode"].isNull());
    TEST_ASSERT_TRUE(doc["map"].is<JsonArray>());
    TEST_ASSERT_FALSE(doc["capacity"]["total"].isNull());
}

void test_rc_map_post_rejects_a_bad_entry_with_the_cores_message() {
    const WebRequestTestParam params[] = {{"map", "not-json-at-all"}};
    WebRequestTestBackend backend;
    backend.params = params;
    backend.paramCount = 1;
    WebRequest req(&backend);

    handleRcMapPost(req);

    TEST_ASSERT_EQUAL_INT(400, backend.sentCode);
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, backend.sentBody));
    TEST_ASSERT_FALSE(doc["ok"].as<bool>());
    // The message is the apply core's, not one the handler invented.
    TEST_ASSERT_FALSE(doc["error"].isNull());
}

// --- POST /api/wifi ---------------------------------------------------------

void test_wifi_post_stages_settings_without_leaking_the_password() {
    const WebRequestTestParam params[] = {
        {"mode", "client"}, {"staSsid", "bench-net"}, {"staPassword", "hunter2hunter2"}};
    WebRequestTestBackend backend;
    backend.params = params;
    backend.paramCount = 3;
    WebRequest req(&backend);

    handleWifiPost(req);

    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);

    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, backend.sentBody));
    TEST_ASSERT_TRUE(doc["ok"].as<bool>());
    TEST_ASSERT_EQUAL_STRING("bench-net", doc["wifi"]["staSsid"]);
    TEST_ASSERT_TRUE(doc["wifi"]["staPasswordSet"].as<bool>());
    // The read shape reports that a password is set; it never returns one.
    TEST_ASSERT_NULL(strstr(backend.sentBody, "hunter2hunter2"));

    // Staged, not applied: pendingApply reports the difference against the
    // WiFi settings actually in force, which is what lets an operator
    // reprovision from the controller's own AP without dropping the request.
    TEST_ASSERT_TRUE(doc["wifi"]["pendingApply"].as<bool>());
}

void test_wifi_post_rejects_invalid_settings() {
    const WebRequestTestParam params[] = {{"mode", "telepathy"}};
    WebRequestTestBackend backend;
    backend.params = params;
    backend.paramCount = 1;
    WebRequest req(&backend);

    handleWifiPost(req);

    TEST_ASSERT_EQUAL_INT(400, backend.sentCode);
    TEST_ASSERT_NOT_NULL(strstr(backend.sentBody, "\"ok\":false"));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_config_post_applies_a_field_and_echoes_the_snapshot);
    RUN_TEST(test_config_post_rejects_an_out_of_range_value_without_applying_it);
    RUN_TEST(test_config_post_accepts_a_raw_json_body_under_the_plain_name);
    RUN_TEST(test_config_post_syncs_stationary_and_broadcasts_status);
    RUN_TEST(test_rc_map_get_returns_the_map_shape);
    RUN_TEST(test_rc_map_post_rejects_a_bad_entry_with_the_cores_message);
    RUN_TEST(test_wifi_post_stages_settings_without_leaking_the_password);
    RUN_TEST(test_wifi_post_rejects_invalid_settings);
    return UNITY_END();
}
