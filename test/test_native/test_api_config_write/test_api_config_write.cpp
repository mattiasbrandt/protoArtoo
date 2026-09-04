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

// The Commit Step hands its post-commit snapshot back through `working`
// instead of returning one (ADR 0011's 2026-09-04 amendment), so what the
// caller renders has to be the state the config cache actually ended up in.
// Drop configCacheApply(*working) from configCommitApplied() and this goes
// red: `working` still carries the caller's intent while the cache never
// moved.
void test_config_commit_leaves_working_agreeing_with_the_config_cache() {
    ConfigSnapshot working = readSnapshot();
    working.drive.speedLimitMax = 80;
    working.system.logLevel = 3;
    working.audio.audioVolume = 22;

    // ConfigApplyResult is ~2.5 KB; static here for the same reason the
    // handlers keep theirs static rather than on the stack.
    static ConfigApplyResult result;
    result = ConfigApplyResult{};

    ConfigCommitOutcome commit = configCommitApplied(&working, result, SRC_WEB_API);

    TEST_ASSERT_TRUE(commit.persisted);
    const ConfigSnapshot cached = readSnapshot();
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, memcmp(&working, &cached, sizeof(ConfigSnapshot)),
                                  "working does not hold the committed config cache state");
}

// The REST handler renders `working` after the commit, which must be the
// committed snapshot and not the one the request arrived with. Rendering a
// pre-commit copy instead turns this red: the POST body would still show the
// old speedLimitMax while GET shows the new one.
void test_config_post_body_matches_a_read_of_the_committed_config() {
    const WebRequestTestParam params[] = {{"speedLimitMax", "80"}, {"audioVolume", "22"}};
    WebRequestTestBackend postBackend;
    postBackend.params = params;
    postBackend.paramCount = 2;
    WebRequest postReq(&postBackend);

    handleConfigPost(postReq);
    TEST_ASSERT_EQUAL_INT(200, postBackend.sentCode);

    WebRequestTestBackend getBackend;
    WebRequest getReq(&getBackend);
    handleConfigGet(getReq);
    TEST_ASSERT_EQUAL_INT(200, getBackend.sentCode);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(getBackend.sentBodyLength, postBackend.sentBodyLength,
                                     "POST and GET bodies differ in length");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(getBackend.sentBody, postBackend.sentBody,
                                     "the POST body is not a read of the committed config");
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

void test_rc_map_post_applies_an_empty_map_and_persists() {
    const WebRequestTestParam params[] = {{"plain", "{\"map\":[]}"}};
    WebRequestTestBackend backend;
    backend.params = params;
    backend.paramCount = 1;
    WebRequest req(&backend);

    handleRcMapPost(req);

    // persistSystemConfig() (ADR 0034, WebRequest-free since #226) reports its
    // own failure through this success path unchanged: 200 on a valid empty
    // map, matching the async-era handler's success shape.
    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    TEST_ASSERT_EQUAL_STRING("{\"ok\":true}", backend.sentBody);
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

// Pins the ADR 0034 Commit Step (wifiCommitApplied(), #227 phase 1): the
// handler no longer stages the cache or reads recovery/broadcast state
// inline, so this exercises that the extracted function still does - NVS/
// cache staging a subsequent read would see, the status broadcast, and
// threading the live Network Recovery posture into the response instead of
// a stale or default value.
void test_wifi_post_commit_step_persists_and_reports_runtime_state() {
    configCacheSetActiveWifiRecovery(true);

    const WebRequestTestParam params[] = {
        {"wifiMode", "client"}, {"staSsid", "commit-step-net"}, {"staPassword", "hunter2hunter2"}};
    WebRequestTestBackend backend;
    backend.params = params;
    backend.paramCount = 3;
    WebRequest req(&backend);

    handleWifiPost(req);

    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);

    // Cache staging landed for a subsequent read to see.
    WifiConfig staged = {};
    configCacheReadWifi(&staged);
    TEST_ASSERT_EQUAL_STRING("commit-step-net", staged.sta_ssid);

    // Status broadcast fired.
    TEST_ASSERT_GREATER_THAN(0, g_test_status_broadcast_count);

    // The commit step's own recovery read, not a stale default, reached the
    // response.
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, backend.sentBody));
    TEST_ASSERT_TRUE(doc["wifi"]["networkRecovery"].as<bool>());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_config_post_applies_a_field_and_echoes_the_snapshot);
    RUN_TEST(test_config_post_rejects_an_out_of_range_value_without_applying_it);
    RUN_TEST(test_config_post_accepts_a_raw_json_body_under_the_plain_name);
    RUN_TEST(test_config_post_syncs_stationary_and_broadcasts_status);
    RUN_TEST(test_config_commit_leaves_working_agreeing_with_the_config_cache);
    RUN_TEST(test_config_post_body_matches_a_read_of_the_committed_config);
    RUN_TEST(test_rc_map_get_returns_the_map_shape);
    RUN_TEST(test_rc_map_post_applies_an_empty_map_and_persists);
    RUN_TEST(test_rc_map_post_rejects_a_bad_entry_with_the_cores_message);
    RUN_TEST(test_wifi_post_stages_settings_without_leaking_the_password);
    RUN_TEST(test_wifi_post_rejects_invalid_settings);
    RUN_TEST(test_wifi_post_commit_step_persists_and_reports_runtime_state);
    return UNITY_END();
}
