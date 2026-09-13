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
#include "config_nvsio.h"
#include "config_serializer.h"
#include "droid_build.h"
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

// The Droid Build lives outside ConfigSnapshot on its own NVS keys, so it
// reaches this payload through the handler rather than through the pure
// snapshot serializer - which makes the handler the only place its shape, and
// its share of the bounded response buffer, can be held down.
void test_the_droid_build_reaches_the_config_payload() {
    DroidBuildConfig build = {};
    droidBuildDefaults(&build);
    TEST_ASSERT_TRUE(droidDesignChoiceSet(&build.dome, "mk4", "complex"));
    TEST_ASSERT_TRUE(droidDesignChoiceSet(&build.body, "own", ""));
    TEST_ASSERT_TRUE(droidFittedPartsFit(&build.fitted, "gripArm"));
    configCacheApplyDroidBuild(build);

    WebRequestTestBackend backend;
    WebRequest req(&backend);
    handleConfigGet(req);

    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, backend.sentBody));
    TEST_ASSERT_EQUAL_STRING("mk4", doc["droidBuild"]["domeDesign"]);
    TEST_ASSERT_EQUAL_STRING("complex", doc["droidBuild"]["domeVariant"]);
    // A mixed droid is reported as one: nothing compares the halves.
    TEST_ASSERT_EQUAL_STRING("own", doc["droidBuild"]["bodyDesign"]);
    TEST_ASSERT_EQUAL_STRING("", doc["droidBuild"]["bodyVariant"]);

    // The Parts travel as ids, never as the bit indices they are held in:
    // firmware and data/droid_parts.js ship in two separate steps, so an index
    // is the one form that could mean a different Part at each end.
    JsonArray fitted = doc["droidBuild"]["fitted"].as<JsonArray>();
    TEST_ASSERT_EQUAL_UINT32(DROID_BUILD_DEFAULT_FITTED_COUNT + 1, (uint32_t)fitted.size());
    bool sawGripArm = false;
    for (JsonVariant part : fitted) {
        if (strcmp(part.as<const char*>(), "gripArm") == 0) {
            sawGripArm = true;
        }
    }
    TEST_ASSERT_TRUE(sawGripArm);
}

// The worst case this payload can reach: a maximal config AND a droid with
// every Part in the catalog fitted. The response buffer is a fixed 3072 B and
// an overflow is a 500, so the bound is measured here rather than reasoned
// about - the Fitted Parts are the one field in this payload that grows every
// time the catalog does.
void test_a_fully_fitted_droid_build_still_fits_the_response_buffer() {
    ConfigSnapshot snap = {};
    memset(snap.wifi.sta_ssid, 'S', sizeof(snap.wifi.sta_ssid) - 1);
    memset(snap.wifi.ap_ssid, 'A', sizeof(snap.wifi.ap_ssid) - 1);
    memset(snap.wifi.sta_password, 'P', sizeof(snap.wifi.sta_password) - 1);
    memset(snap.wifi.ap_password, 'Q', sizeof(snap.wifi.ap_password) - 1);
    memset(snap.dome.dome_wifi_peer_ip, '9', sizeof(snap.dome.dome_wifi_peer_ip) - 1);
    configCacheApply(snap);

    DroidBuildConfig build = {};
    droidBuildDefaults(&build);
    for (size_t i = 0; i < DROID_PART_COUNT; ++i) {
        TEST_ASSERT_TRUE(droidFittedPartsFit(&build.fitted, droidPartIdAt(i)));
    }
    configCacheApplyDroidBuild(build);

    WebRequestTestBackend backend;
    WebRequest req(&backend);
    handleConfigGet(req);

    // A 500 here is the overflow branch, which is what this test exists to
    // catch before a builder meets it as a blank config page.
    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    TEST_ASSERT_LESS_THAN_UINT32(3072u, (uint32_t)strlen(backend.sentBody));
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, backend.sentBody));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)DROID_PART_COUNT,
                             (uint32_t)doc["droidBuild"]["fitted"].as<JsonArray>().size());
}

// --- GET /api/servo/outputs (ADR 0050, #347) --------------------------------

namespace {

void seedUnwiredServoOutputRows() {
    Preferences prefs;
    prefs.begin("proto", false);
    prefs.clear();
    ServoOutputRepairReport report = {};
    configLoadServoOutputs(prefs, &report);
    prefs.end();
}

void moveOnto(const char* part, uint8_t channel) {
    ServoOutputPartMove move = {};
    snprintf(move.part, sizeof(move.part), "%s", part);
    move.fromOutput = false;
    move.toOutput = true;
    move.toDriver = SERVO_DRIVER_LEDC;
    move.toChannel = channel;
    TEST_ASSERT_EQUAL_UINT8(SERVO_PART_MOVED, configCacheMoveServoOutputPart(move));
}

}  // namespace

// Every live row, addressed the way a move names it, with every Part it drives -
// a ganged lead lists both, and an Output driving nothing says so with an empty
// list rather than by being left out.
void test_the_servo_outputs_answer_lists_every_row_and_all_its_parts() {
    seedUnwiredServoOutputRows();
    moveOnto("utilUp", LEDC_CH_ARM1);
    moveOnto("doorFL", LEDC_CH_ARM1);

    WebRequestTestBackend backend;
    WebRequest req(&backend);
    handleServoOutputsGet(req);

    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, backend.sentBody));
    JsonArray outputs = doc["outputs"].as<JsonArray>();
    TEST_ASSERT_EQUAL_UINT32((uint32_t)SERVO_OUTPUT_ROW_DEFAULT_COUNT, (uint32_t)outputs.size());

    TEST_ASSERT_EQUAL_STRING("ledc:0", outputs[0]["address"] | "");
    TEST_ASSERT_EQUAL_STRING("ARM1", outputs[0]["name"] | "");
    TEST_ASSERT_EQUAL_UINT32(2u, (uint32_t)outputs[0]["parts"].as<JsonArray>().size());
    TEST_ASSERT_EQUAL_STRING("utilUp", outputs[0]["parts"][0] | "");
    TEST_ASSERT_EQUAL_STRING("doorFL", outputs[0]["parts"][1] | "");

    TEST_ASSERT_EQUAL_STRING("ledc:3", outputs[2]["address"] | "");
    TEST_ASSERT_EQUAL_STRING("AUX1", outputs[2]["name"] | "");
    TEST_ASSERT_TRUE(outputs[2]["parts"].is<JsonArray>());
    TEST_ASSERT_EQUAL_UINT32(0u, (uint32_t)outputs[2]["parts"].as<JsonArray>().size());
}

// Where each Output has been told to be, against the band both marks are drawn
// across (#362). Both widths are commanded - ServoTask's mirror, read through
// captureServoOutputCommanded() - and an Output with no pulse on it answers null
// for both, rather than a zero that reads as a position.
//
// AUX1 is armId 2 on LEDC channel 3, so a read that used the channel as the
// mirror index would hand AUX1 AUX2's position; the widths below differ on every
// Output so that mistake cannot pass.
void test_the_servo_outputs_answer_carries_each_commanded_position_and_its_band() {
    seedUnwiredServoOutputRows();
    ServoOutputEdit micro = {};
    micro.driver = SERVO_DRIVER_LEDC;
    micro.channel = LEDC_CH_AUX2;
    micro.fields = SERVO_FIELD_COMPONENT;
    micro.component = SERVO_COMP_MG90S;
    configCacheApplyServoOutputEdits(&micro, 1);

    robotState.servoCommanded[0] = {1600, 1900, true, 0};   // ARM1, part way through a move
    robotState.servoCommanded[1] = {1500, 1500, true, 2};   // ARM2, standing, nudged twice
    robotState.servoCommanded[2] = {1100, 1200, false, 1};  // AUX1, no pulse whatever the widths, one nudge refused
    robotState.servoCommanded[3] = {2400, 2400, true, 0};   // AUX2, an MG90S near its top
    robotState.servoCommanded[4] = {0, 0, false, 0};        // AUX3, never driven

    WebRequestTestBackend backend;
    WebRequest req(&backend);
    handleServoOutputsGet(req);
    robotState = RobotState{};

    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, backend.sentBody));
    JsonArray outputs = doc["outputs"].as<JsonArray>();

    TEST_ASSERT_EQUAL_STRING("ledc:0", outputs[0]["address"] | "");
    TEST_ASSERT_EQUAL_UINT16(1000, outputs[0]["bandLoUs"] | 0);
    TEST_ASSERT_EQUAL_UINT16(2000, outputs[0]["bandHiUs"] | 0);
    TEST_ASSERT_EQUAL_UINT16(1600, outputs[0]["commandedUs"] | 0);
    TEST_ASSERT_EQUAL_UINT16(1900, outputs[0]["targetUs"] | 0);

    TEST_ASSERT_EQUAL_UINT16(1500, outputs[1]["commandedUs"] | 0);
    TEST_ASSERT_EQUAL_UINT16(1500, outputs[1]["targetUs"] | 0);
    // The Find by Moving count rides the same answer (#363): a run reads it
    // before it asks and knows the nudge is over when it has gone up.
    TEST_ASSERT_EQUAL_UINT8(2, outputs[1]["nudgesDone"] | 99);
    TEST_ASSERT_EQUAL_UINT8(0, outputs[0]["nudgesDone"] | 99);

    TEST_ASSERT_EQUAL_STRING("ledc:4", outputs[3]["address"] | "");
    TEST_ASSERT_EQUAL_UINT16(500, outputs[3]["bandLoUs"] | 0);
    TEST_ASSERT_EQUAL_UINT16(2500, outputs[3]["bandHiUs"] | 0);
    TEST_ASSERT_EQUAL_UINT16(2400, outputs[3]["commandedUs"] | 0);

    // Not pulsing is said with null on the wire - both keys present, neither a
    // number - so an absent key and a stalled table cannot look the same. The
    // nudge count is a number whatever the pulse: a refused nudge on an Output
    // with no pulse still ended, and a run waiting on it must see that.
    TEST_ASSERT_NOT_NULL(strstr(
        backend.sentBody,
        "{\"address\":\"ledc:3\",\"name\":\"AUX1\",\"parts\":[],\"bandLoUs\":1000,"
        "\"bandHiUs\":2000,\"commandedUs\":null,\"targetUs\":null,\"nudgesDone\":1}"));
    TEST_ASSERT_NOT_NULL(strstr(
        backend.sentBody,
        "{\"address\":\"ledc:5\",\"name\":\"AUX3\",\"parts\":[],\"bandLoUs\":1000,"
        "\"bandHiUs\":2000,\"commandedUs\":null,\"targetUs\":null,\"nudgesDone\":0}"));
}

// The largest answer the table can give: every row it can hold, each at the
// longest address, holding every Part the catalog declares between them. The
// route refuses a payload at its ceiling with a 500, so the bound is measured
// here rather than argued about - the catalog grows, and this is where that
// growth would first show.
void test_a_full_table_of_outputs_fits_under_the_route_ceiling() {
    ServoOutputTable table = {};
    servoOutputTableDefaults(&table);
    table.count = SERVO_OUTPUT_ROW_MAX;
    for (size_t i = 0; i < DROID_PART_COUNT; ++i) {
        TEST_ASSERT_TRUE(servoOutputAddPart(&table.rows[i / SERVO_OUTPUT_PART_SLOTS],
                                            droidPartIdAt(i)));
    }

    Preferences prefs;
    prefs.begin("proto", false);
    prefs.clear();
    PrefsWriter writer(prefs);
    TEST_ASSERT_TRUE(configSerializeServoOutputs(table, writer));
    ServoOutputRepairReport report = {};
    configLoadServoOutputs(prefs, &report);
    prefs.end();
    TEST_ASSERT_EQUAL_UINT8(SERVO_OUTPUT_ROW_MAX, configCacheServoOutputCount());

    WebRequestTestBackend backend;
    WebRequest req(&backend);
    handleServoOutputsGet(req);

    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    // 3229 B measured at #362, when every row gained its band and commanded
    // position, and 3589 B at #363, when every row gained its nudge count;
    // the route refuses at 4096.
    TEST_ASSERT_LESS_THAN_UINT32(4096u, (uint32_t)strlen(backend.sentBody));
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, backend.sentBody));
    JsonArray outputs = doc["outputs"].as<JsonArray>();
    TEST_ASSERT_EQUAL_UINT32((uint32_t)SERVO_OUTPUT_ROW_MAX, (uint32_t)outputs.size());
    size_t parts = 0;
    for (JsonObject output : outputs) {
        parts += output["parts"].as<JsonArray>().size();
    }
    TEST_ASSERT_EQUAL_UINT32((uint32_t)DROID_PART_COUNT, (uint32_t)parts);

    // Leave a controller nobody has wired for whatever runs next.
    seedUnwiredServoOutputRows();
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_the_servo_outputs_answer_lists_every_row_and_all_its_parts);
    RUN_TEST(test_the_servo_outputs_answer_carries_each_commanded_position_and_its_band);
    RUN_TEST(test_a_full_table_of_outputs_fits_under_the_route_ceiling);
    RUN_TEST(test_get_returns_config_json);
    RUN_TEST(test_pending_apply_is_false_when_staged_matches_active);
    RUN_TEST(test_the_old_field_names_are_answered_from_the_rows);
    RUN_TEST(test_pending_apply_is_true_when_staged_differs_from_active);
    RUN_TEST(test_worst_case_config_fits_the_response_buffer);
    RUN_TEST(test_the_droid_build_reaches_the_config_payload);
    RUN_TEST(test_a_fully_fitted_droid_build_still_fits_the_response_buffer);
    return UNITY_END();
}
