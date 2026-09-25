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

#include <cstdio>
#include <cstring>
#include <string>

#include "api_config.h"
#include "component_registry.h"
#include "config_cache.h"
#include "droid_build.h"
#include "web_request_test_backend.h"
#include "config_write_window_check.h"  // the holder check this suite arms (#418)
#include "config_write_window_test_hooks.h"  // ConfigWriteWindowForTest - seeding stands in for a window

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
    // Armed after this setUp()'s own seeding: from here every config write
    // must run inside a Write Window, as it must on the droid after boot (#418).
    configWriteWindowArm(true);
}

void tearDown() {
    const uint32_t misses = configWriteWindowMisses();
    configWriteWindowArm(false);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, misses, "a config write ran outside its Write Window");
}

// --- POST /api/config -------------------------------------------------------

// The addressed Servo Output rows only exist once something has loaded them
// (ADR 0041); on a controller that is main's boot path. Empty storage gives the
// five default rows, which is the state a fresh controller boots into.
void seedServoOutputRows() {
    Preferences prefs;
    prefs.begin("proto", false);
    ServoOutputRepairReport report = {};
    configLoadServoOutputs(prefs, &report);
    prefs.end();
}


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
    // The sentence is the one this route has always answered, and what it says
    // rides beside it as keys a page reads instead of the sentence (#425).
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, backend.sentBody));
    TEST_ASSERT_EQUAL_STRING("speedLimitMax must be 0..600", doc["error"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("speedLimitMax", doc["field"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("out-of-range", doc["reason"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("0..600", doc["accepts"].as<const char*>());
    // The rejected value must not have reached the cache.
    TEST_ASSERT_EQUAL_INT(100, readSnapshot().drive.speedLimitMax);
}

// Presets that are each in range but not distinct clash with each other: that
// is a conflict, not out-of-range, and there is no single value `accepts` could
// name (ADR 0011 amended 2026-09-25).
void test_config_post_refuses_clashing_speed_presets_as_a_conflict() {
    const WebRequestTestParam params[] = {
        {"speedPresetSlow", "300"}, {"speedPresetNormal", "300"}, {"speedPresetTurbo", "500"}};
    WebRequestTestBackend backend;
    backend.params = params;
    backend.paramCount = 3;
    WebRequest req(&backend);
    const int slowBefore = readSnapshot().drive.speedPresetSlow;

    handleConfigPost(req);

    TEST_ASSERT_EQUAL_INT(400, backend.sentCode);
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, backend.sentBody));
    TEST_ASSERT_EQUAL_STRING("speed presets must be distinct values", doc["error"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("conflict", doc["reason"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("speedPresetSlow", doc["field"].as<const char*>());
    TEST_ASSERT_TRUE(doc["accepts"].isNull());
    TEST_ASSERT_EQUAL_INT(slowBefore, readSnapshot().drive.speedPresetSlow);
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

    ConfigCommitOutcome commit = {};
    {
        // Standing where configWriteWindow() would: this test drives the Commit Step directly.
        const ConfigWriteWindowForTest window;
        commit = configCommitApplied(&working, result, SRC_WEB_API);
    }

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

// --- the round trip: what GET reads, POST takes back (ADR 0068, #423) -------

namespace {

std::string readConfigBody() {
    WebRequestTestBackend backend;
    WebRequest req(&backend);
    handleConfigGet(req);
    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    return std::string(backend.sentBody, backend.sentBodyLength);
}

// Every scalar POST /api/config sets, each moved off the value setUp() leaves,
// plus the Droid Build and Guided Setup's record that travel with a backup. A
// field GET reports and POST cannot take back stays at its setUp() value, and
// the comparison after the round trip finds it.
struct Configuration {
    ConfigSnapshot snap;
    DroidBuildConfig build;
    GuidedSetupConfig guided;
};

Configuration readConfiguration() {
    Configuration now = {};
    configCacheRead(&now.snap);
    configCacheReadDroidBuild(&now.build);
    configCacheReadGuidedSetup(&now.guided);
    return now;
}

void applyConfiguration(const Configuration& config) {
    const ConfigWriteWindowForTest window;
    configCacheApply(config.snap);
    configCacheApplyDroidBuild(config.build);
    configCacheApplyGuidedSetup(config.guided);
}

Configuration configurationUnlikeSetUp(const Configuration& base) {
    Configuration want = base;
    DriveConfig& drive = want.snap.drive;
    drive.speedPresetSlow = 120;
    drive.speedPresetNormal = 340;
    drive.speedPresetTurbo = 560;
    drive.speedLimitMax = 560;
    drive.speedPresetActive = SpeedPresetId::Turbo;  // what POST derives from 560
    drive.webDriveTimeoutMs = 750;
    drive.sbusTimeoutMs = 333;

    SystemConfig& system = want.snap.system;
    system.stationary = true;
    system.single_sbus_use_ch2 = true;
    system.rc_input_mode = RC_INPUT_ELRS;
    system.rc_member = componentPartById("rc_transmitter_elrs")->value;
    system.sound_member = componentPartById("mp3_trigger")->value;
    system.logLevel = 4;
    system.enable_dome_esc = true;
    system.enable_rc_ch1 = true;
    system.enable_rc_ch2 = true;
    system.enable_rc_ch3 = true;
    system.enable_rc_ch4 = true;
    system.enable_rc_ch5 = true;
    system.enable_rc_ch6 = true;
    system.enable_drive = true;
    system.enable_audio = true;
    system.enable_protor2link = true;

    DomeConfig& dome = want.snap.dome;
    dome.dome_neutral_us = 1490;
    dome.dome_min_pulse_us = 1100;
    dome.dome_max_pulse_us = 1900;
    dome.dome_speed_limit_pct = 70;
    dome.dome_rnd_enable = true;
    dome.dome_rnd_speed_pct = 40;
    dome.dome_rnd_pause_min = 7;
    dome.dome_rnd_pause_max = 33;
    dome.dome_rnd_move_ms = 2500;
    snprintf(dome.dome_wifi_peer_ip, sizeof(dome.dome_wifi_peer_ip), "%s", "10.1.2.3");

    TEST_ASSERT_TRUE(droidDesignChoiceSet(&want.build.dome, "mk4", "basic"));
    TEST_ASSERT_TRUE(droidDesignChoiceSet(&want.build.body, "own", ""));
    droidFittedPartsClear(&want.build.fitted);
    TEST_ASSERT_TRUE(droidFittedPartsFit(&want.build.fitted, "utilUp"));
    TEST_ASSERT_TRUE(droidFittedPartsFit(&want.build.fitted, "gripArm"));

    want.guided.run = GUIDED_SETUP_COMPLETED;
    want.guided.recorded = true;
    want.guided.summaryDone = true;
    guidedSetupVisitedSet(&want.guided, "drive,sound");
    return want;
}

void assertSameConfiguration(const Configuration& want, const Configuration& got) {
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(&want.snap, &got.snap, sizeof(ConfigSnapshot),
                                     "a scalar GET reported did not come back through POST");
    TEST_ASSERT_EQUAL_STRING(want.build.dome.design, got.build.dome.design);
    TEST_ASSERT_EQUAL_STRING(want.build.dome.variant, got.build.dome.variant);
    TEST_ASSERT_EQUAL_STRING(want.build.body.design, got.build.body.design);
    TEST_ASSERT_EQUAL_STRING(want.build.body.variant, got.build.body.variant);
    TEST_ASSERT_EQUAL_MEMORY(&want.build.fitted, &got.build.fitted, sizeof(DroidFittedParts));
    TEST_ASSERT_EQUAL_UINT8(want.guided.run, got.guided.run);
    TEST_ASSERT_EQUAL(want.guided.recorded, got.guided.recorded);
    TEST_ASSERT_EQUAL(want.guided.summaryDone, got.guided.summaryDone);
    TEST_ASSERT_EQUAL_STRING(want.guided.visited, got.guided.visited);
}

}  // namespace

// The restore is the reader that matters most (ADR 0068): a Configuration read
// through GET and posted back, unchanged, through POST /api/config must come back
// equal. Drop any one field's POST handling - its entry in the GET shape, or its
// check - and that field stays at the setUp() value and this goes red.
void test_a_configuration_read_by_get_comes_back_whole_through_post() {
    const Configuration base = readConfiguration();
    const Configuration want = configurationUnlikeSetUp(base);
    applyConfiguration(want);
    const std::string backup = readConfigBody();

    applyConfiguration(base);
    WebRequestTestBackend backend;
    backend.body = backup.c_str();
    WebRequest req(&backend);
    handleConfigPost(req);

    TEST_ASSERT_EQUAL_INT_MESSAGE(200, backend.sentCode, backend.sentBody);
    assertSameConfiguration(want, readConfiguration());
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

    // persistSystemConfig() (ADR 0036, WebRequest-free since #226) reports its
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

// Pins the ADR 0036 Commit Step (wifiCommitApplied(), #227 phase 1): the
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

// --- the calibration write reaches the addressed rows (#342) ----------------

// A builder's endpoints still arrive as arm1OpenUs and its siblings, and every
// reader of them is now the row. Drop configCacheApplyServoCalibration() from
// the Commit Step and this goes red: the write lands in the snapshot, the droid
// keeps driving to the old number, and the next read hands the old one back.
void test_a_calibration_write_lands_on_the_addressed_row() {
    seedServoOutputRows();

    const WebRequestTestParam params[] = {{"arm1OpenUs", "1750"}, {"arm1CloseUs", "1250"}};
    WebRequestTestBackend backend;
    backend.params = params;
    backend.paramCount = 2;
    WebRequest req(&backend);

    handleConfigPost(req);

    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);

    ServoOutputRow row = {};
    TEST_ASSERT_TRUE(configCacheReadServoOutput(0, &row));
    TEST_ASSERT_EQUAL_UINT8(LEDC_CH_ARM1, row.channel);
    TEST_ASSERT_EQUAL_UINT16(1750, row.open_us);
    TEST_ASSERT_EQUAL_UINT16(1250, row.close_us);
    TEST_ASSERT_EQUAL_UINT16(1500, row.centre_us);
}

// The old form let any output take 500-2500 us. The row's component type
// governs the clamp (#286), so a value an MG996R cannot reach does not reach
// it - and the write still succeeds rather than being refused, because clamping
// is not refusing (ADR 0044).
void test_a_write_the_component_band_cannot_take_is_moved_not_refused() {
    seedServoOutputRows();

    const WebRequestTestParam params[] = {{"aux1Type", "mg996r"}, {"aux1OpenUs", "2500"}};
    WebRequestTestBackend backend;
    backend.params = params;
    backend.paramCount = 2;
    WebRequest req(&backend);

    handleConfigPost(req);

    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);

    ServoOutputRow row = {};
    const uint8_t aux1 = 2;  // the third default row is LEDC_CH_AUX1
    TEST_ASSERT_TRUE(configCacheReadServoOutput(aux1, &row));
    TEST_ASSERT_EQUAL_UINT8(LEDC_CH_AUX1, row.channel);
    TEST_ASSERT_EQUAL_UINT8(SERVO_COMP_MG996R, row.component);
    TEST_ASSERT_EQUAL_UINT16(2000, row.open_us);
}

// What comes back has to be what the droid will do. The row holds the clamped
// number, so the echo does too - a response that repeated the request back
// would tell a builder their 500 us landed while the arm moved to 1000.
void test_the_echo_reports_what_the_row_holds_not_what_was_asked() {
    seedServoOutputRows();

    const WebRequestTestParam params[] = {{"arm1Type", "mg996r"}, {"arm1OpenUs", "500"}};
    WebRequestTestBackend backend;
    backend.params = params;
    backend.paramCount = 2;
    WebRequest req(&backend);

    handleConfigPost(req);

    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, backend.sentBody));
    TEST_ASSERT_EQUAL_INT(1000, doc["arm1OpenUs"].as<int>());

    ServoOutputRow row = {};
    TEST_ASSERT_TRUE(configCacheReadServoOutput(0, &row));
    TEST_ASSERT_EQUAL_UINT16(1000, row.open_us);
}

// The clamp is not silent (#417). A type change pulls both ends of a pair the
// new band cannot take - ends the request never named - and the answer says
// which fields were stored at what, so a builder sees their 2400 us became
// 2000 without comparing numbers. A write that clamped nothing says nothing.
void test_the_answer_names_every_end_the_band_moved() {
    seedServoOutputRows();

    const WebRequestTestParam micro[] = {
        {"aux2Type", "mg90s"}, {"aux2OpenUs", "2400"}, {"aux2CloseUs", "600"}};
    WebRequestTestBackend first;
    first.params = micro;
    first.paramCount = 3;
    WebRequest firstReq(&first);
    handleConfigPost(firstReq);
    TEST_ASSERT_EQUAL_INT(200, first.sentCode);
    JsonDocument firstDoc;
    TEST_ASSERT_FALSE(deserializeJson(firstDoc, first.sentBody));
    TEST_ASSERT_TRUE(firstDoc["clamped"].isNull());

    const WebRequestTestParam typeOnly[] = {{"aux2Type", "mg996r"}};
    WebRequestTestBackend backend;
    backend.params = typeOnly;
    backend.paramCount = 1;
    WebRequest req(&backend);
    handleConfigPost(req);

    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, backend.sentBody));
    JsonObject clamped = doc["clamped"].as<JsonObject>();
    TEST_ASSERT_EQUAL_UINT32(2u, (uint32_t)clamped.size());
    TEST_ASSERT_EQUAL_INT(2000, clamped["aux2OpenUs"] | 0);
    TEST_ASSERT_EQUAL_INT(1000, clamped["aux2CloseUs"] | 0);
}

// An Output's Motion Profile goes out and comes back under the names GET
// /api/config gave it (#414): the page saves by the field it read, and the
// echo is what the row now holds, so the two agree by construction.
void test_a_motion_profile_round_trips_under_the_names_the_config_reports() {
    seedServoOutputRows();

    const WebRequestTestParam params[] = {
        {"arm2ThrowMs", "800"}, {"arm2AccelMs", "150"}, {"arm2Ease", "overshoot"},
        {"arm2Boot", "home-release"}};
    WebRequestTestBackend backend;
    backend.params = params;
    backend.paramCount = 4;
    WebRequest req(&backend);

    handleConfigPost(req);

    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    ServoOutputRow row = {};
    const uint8_t arm2 = 1;  // the second default row is LEDC_CH_ARM2
    TEST_ASSERT_TRUE(configCacheReadServoOutput(arm2, &row));
    TEST_ASSERT_EQUAL_UINT8(LEDC_CH_ARM2, row.channel);
    TEST_ASSERT_EQUAL_UINT16(800, row.throw_ms);
    TEST_ASSERT_EQUAL_UINT16(150, row.accel_ms);
    TEST_ASSERT_EQUAL_UINT8(SERVO_EASE_OVERSHOOT, row.easing);
    TEST_ASSERT_EQUAL_UINT8(SERVO_BOOT_HOME_RELEASE, row.boot);

    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, backend.sentBody));
    JsonObject arm2Entry = doc["components"]["arm2"];
    TEST_ASSERT_EQUAL_STRING("arm2ThrowMs", arm2Entry["throwField"] | "");
    TEST_ASSERT_EQUAL_STRING("arm2AccelMs", arm2Entry["accelField"] | "");
    TEST_ASSERT_EQUAL_STRING("arm2Ease", arm2Entry["easeField"] | "");
    TEST_ASSERT_EQUAL_STRING("arm2Boot", arm2Entry["bootField"] | "");
    TEST_ASSERT_EQUAL_STRING("home-release", arm2Entry["boot"] | "");
    TEST_ASSERT_EQUAL_UINT(800, arm2Entry["throwMs"].as<unsigned>());
    TEST_ASSERT_EQUAL_UINT(150, arm2Entry["accelMs"].as<unsigned>());
    // The builder's choice, not the ease that runs: this row is unmeasured, so
    // it moves as `none`, and the page is the one that says so.
    TEST_ASSERT_EQUAL_STRING("overshoot", arm2Entry["ease"] | "");
    // A neighbour nobody touched still reports its own defaults.
    TEST_ASSERT_EQUAL_UINT(SERVO_THROW_MS_DEFAULT, doc["components"]["arm1"]["throwMs"].as<unsigned>());
    TEST_ASSERT_EQUAL_STRING("none", doc["components"]["arm1"]["ease"] | "");
    // Limp is the default, and a neighbour nobody set stays limp.
    TEST_ASSERT_EQUAL_STRING("limp", doc["components"]["arm1"]["boot"] | "");
}

// Out of range is refused with the field and its range, never clamped into it:
// the bounds are the stored row's own, and nothing of the request lands.
void test_a_motion_profile_out_of_range_is_refused_not_clamped() {
    seedServoOutputRows();

    const struct {
        const char* field;
        const char* value;
    } kRefused[] = {
        {"arm1ThrowMs", "5"},       // under one ServoTask frame
        {"arm1ThrowMs", "20000"},   // over SERVO_THROW_MS_MAX
        {"arm1AccelMs", "0"},       // no time at all to get up to speed
        {"arm1Ease", "wobble"},     // not one of the three
        {"arm1Boot", "home"},       // not one of the three boot modes
    };
    for (const auto& refused : kRefused) {
        // A good ease rides along with every bad value, so a refusal that let
        // the rest of the request land would show up on the row.
        const bool easeRefused = strcmp(refused.field, "arm1Ease") == 0;
        const WebRequestTestParam params[] = {
            {refused.field, refused.value},
            {easeRefused ? "arm1ThrowMs" : "arm1Ease", easeRefused ? "700" : "soft"}};
        WebRequestTestBackend backend;
        backend.params = params;
        backend.paramCount = 2;
        WebRequest req(&backend);

        handleConfigPost(req);

        TEST_ASSERT_EQUAL_INT_MESSAGE(400, backend.sentCode, refused.value);
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(backend.sentBody, refused.field), backend.sentBody);
        ServoOutputRow row = {};
        TEST_ASSERT_TRUE(configCacheReadServoOutput(0, &row));
        TEST_ASSERT_EQUAL_UINT16(SERVO_THROW_MS_DEFAULT, row.throw_ms);
        TEST_ASSERT_EQUAL_UINT16(SERVO_ACCEL_MS_DEFAULT, row.accel_ms);
        TEST_ASSERT_EQUAL_UINT8(SERVO_EASE_NONE, row.easing);
        TEST_ASSERT_EQUAL_UINT8(SERVO_BOOT_LIMP, row.boot);
    }
}

// --- the Droid Build through the whole route (ADR 0047) -----------------------

// The commit step is the only place a stated Droid Build meets the live one,
// and the only place a half the request did not name has to survive.
void test_a_stated_droid_build_reaches_the_live_answer_and_the_echo() {
    DroidBuildConfig before = {};
    droidBuildDefaults(&before);
    {
        const ConfigWriteWindowForTest seed;
        configCacheApplyDroidBuild(before);
    }

    const WebRequestTestParam params[] = {
        {"domeDesign", "mk4"}, {"domeVariant", "basic"},
        {"fittedParts", "utilUp,gripArm"},
    };
    WebRequestTestBackend backend;
    backend.params = params;
    backend.paramCount = 3;
    WebRequest req(&backend);

    handleConfigPost(req);

    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    DroidBuildConfig after = {};
    configCacheReadDroidBuild(&after);
    TEST_ASSERT_EQUAL_STRING("basic", after.dome.variant);
    TEST_ASSERT_EQUAL_UINT32(2u, (uint32_t)droidFittedPartsCount(after.fitted));
    // The half the request said nothing about is untouched: changing a Dome
    // Design says nothing about the body.
    TEST_ASSERT_EQUAL_STRING(before.body.design, after.body.design);
    TEST_ASSERT_EQUAL_STRING(before.body.variant, after.body.variant);

    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, backend.sentBody));
    TEST_ASSERT_EQUAL_STRING("basic", doc["droidBuild"]["domeVariant"]);
    TEST_ASSERT_EQUAL_UINT32(2u, (uint32_t)doc["droidBuild"]["fitted"].as<JsonArray>().size());
}

// A backup saved before MK4's sparse variant was renamed carries `simple`.
// Restoring it goes through this route, and the answer lands as `basic` rather
// than being refused or reset (#409).
void test_a_restored_legacy_variant_lands_as_the_variant_it_became() {
    DroidBuildConfig before = {};
    droidBuildDefaults(&before);
    {
        const ConfigWriteWindowForTest seed;
        configCacheApplyDroidBuild(before);
    }

    const WebRequestTestParam params[] = {
        {"bodyDesign", "mk4"}, {"bodyVariant", "simple"},
    };
    WebRequestTestBackend backend;
    backend.params = params;
    backend.paramCount = 2;
    WebRequest req(&backend);

    handleConfigPost(req);

    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    DroidBuildConfig after = {};
    configCacheReadDroidBuild(&after);
    TEST_ASSERT_EQUAL_STRING("mk4", after.body.design);
    TEST_ASSERT_EQUAL_STRING("basic", after.body.variant);
}

// The criterion this whole decision turns on, asked of the running route: after
// any Droid Build change, Protocol Check still accepts every Part the catalog
// declares. A design seeds the Parts; it never fences them.
void test_the_part_vocabulary_is_unchanged_by_a_droid_build_write() {
    size_t before = 0;
    for (size_t i = 0; i < DROID_PART_COUNT; ++i) {
        if (droidPartIdIsKnown(droidPartIdAt(i))) {
            before++;
        }
    }

    // The narrowest droid a builder can state: one design that seeds nothing,
    // and not a single Part fitted.
    const WebRequestTestParam params[] = {
        {"domeDesign", "own"}, {"domeVariant", ""},
        {"bodyDesign", "own"}, {"bodyVariant", ""},
        {"fittedParts", ""},
    };
    WebRequestTestBackend backend;
    backend.params = params;
    backend.paramCount = 5;
    WebRequest req(&backend);

    handleConfigPost(req);
    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);

    DroidBuildConfig after = {};
    configCacheReadDroidBuild(&after);
    TEST_ASSERT_EQUAL_UINT32(0u, (uint32_t)droidFittedPartsCount(after.fitted));

    size_t afterCount = 0;
    for (size_t i = 0; i < DROID_PART_COUNT; ++i) {
        if (droidPartIdIsKnown(droidPartIdAt(i))) {
            afterCount++;
        }
    }
    TEST_ASSERT_EQUAL_UINT32((uint32_t)before, (uint32_t)afterCount);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)DROID_PART_COUNT, (uint32_t)afterCount);
}

void test_a_droid_build_the_catalog_cannot_name_is_refused_without_applying() {
    DroidBuildConfig before = {};
    droidBuildDefaults(&before);
    {
        const ConfigWriteWindowForTest seed;
        configCacheApplyDroidBuild(before);
    }

    const WebRequestTestParam params[] = {{"domeDesign", "mk9"}, {"domeVariant", "complex"}};
    WebRequestTestBackend backend;
    backend.params = params;
    backend.paramCount = 2;
    WebRequest req(&backend);

    handleConfigPost(req);

    TEST_ASSERT_EQUAL_INT(400, backend.sentCode);
    DroidBuildConfig after = {};
    configCacheReadDroidBuild(&after);
    TEST_ASSERT_EQUAL_STRING(before.dome.design, after.dome.design);
}

// A roadmap design is a card nobody can pick (#368). The route refuses it as a
// stated half exactly the way it refuses a design the catalog never declared,
// because it asks the same predicate - and applies nothing of the request.
void test_a_roadmap_design_is_refused_as_a_stated_half() {
    DroidBuildConfig before = {};
    droidBuildDefaults(&before);
    {
        const ConfigWriteWindowForTest seed;
        configCacheApplyDroidBuild(before);
    }

    const WebRequestTestParam params[] = {{"bodyDesign", "mk3"}, {"bodyVariant", ""}};
    WebRequestTestBackend backend;
    backend.params = params;
    backend.paramCount = 2;
    WebRequest req(&backend);

    handleConfigPost(req);

    TEST_ASSERT_EQUAL_INT(400, backend.sentCode);
    DroidBuildConfig after = {};
    configCacheReadDroidBuild(&after);
    TEST_ASSERT_EQUAL_STRING(before.body.design, after.body.design);
    TEST_ASSERT_EQUAL_STRING(before.body.variant, after.body.variant);
}

// The mixed droid the ticket names: a dome from one design on a body from
// another saves without complaint, both halves as stated (#368).
void test_an_mk41_dome_on_an_mk4_basic_body_saves_as_stated() {
    DroidBuildConfig before = {};
    droidBuildDefaults(&before);
    {
        const ConfigWriteWindowForTest seed;
        configCacheApplyDroidBuild(before);
    }

    const WebRequestTestParam params[] = {
        {"domeDesign", "mk41"}, {"domeVariant", ""},
        {"bodyDesign", "mk4"}, {"bodyVariant", "basic"},
    };
    WebRequestTestBackend backend;
    backend.params = params;
    backend.paramCount = 4;
    WebRequest req(&backend);

    handleConfigPost(req);

    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    DroidBuildConfig after = {};
    configCacheReadDroidBuild(&after);
    TEST_ASSERT_EQUAL_STRING("mk41", after.dome.design);
    TEST_ASSERT_EQUAL_STRING("", after.dome.variant);
    TEST_ASSERT_EQUAL_STRING("mk4", after.body.design);
    TEST_ASSERT_EQUAL_STRING("basic", after.body.variant);
}

// --- Part moves through the whole route (ADR 0050, #347) ---------------------

// Five empty rows, whatever an earlier test left in storage: a move persists,
// so every test here starts from a controller nobody has wired.
void seedUnwiredServoOutputRows() {
    Preferences prefs;
    prefs.begin("proto", false);
    prefs.clear();
    ServoOutputRepairReport report = {};
    configLoadServoOutputs(prefs, &report);
    prefs.end();
}

int postMove(const char* part, const char* from, const char* to, WebRequestTestBackend* backend) {
    const WebRequestTestParam params[] = {
        {"movePart", part}, {"movePartFrom", from}, {"movePartTo", to}};
    backend->params = params;
    backend->paramCount = 3;
    WebRequest req(backend);
    handleConfigPost(req);
    backend->params = nullptr;
    backend->paramCount = 0;
    return backend->sentCode;
}

uint8_t rowDriving(const char* part) {
    ServoOutputRow row = {};
    for (uint8_t i = 0; configCacheReadServoOutput(i, &row); ++i) {
        if (servoOutputDrivesPart(row, part)) {
            return row.channel;
        }
    }
    return SERVO_OUTPUT_CHANNEL_UNSET;
}

// A move lands on both rows it touches and runs the Commit Step to its end.
// The status broadcast is the last thing that step does, after the rows are
// written, so seeing it is seeing a commit that did not stop short. (The
// Preferences double keeps each instance's store to itself, so a reload through
// a second instance cannot observe the write; the row record the save writes is
// test_servo_output_row's.)
void test_a_part_move_takes_it_off_one_output_and_is_committed() {
    seedUnwiredServoOutputRows();

    WebRequestTestBackend first;
    TEST_ASSERT_EQUAL_INT(200, postMove("doorFL", "none", "ledc:0", &first));
    TEST_ASSERT_EQUAL_UINT8(LEDC_CH_ARM1, rowDriving("doorFL"));

    const unsigned broadcastsBefore = g_test_status_broadcast_count;
    WebRequestTestBackend second;
    TEST_ASSERT_EQUAL_INT(200, postMove("doorFL", "ledc:0", "ledc:3", &second));
    TEST_ASSERT_EQUAL_UINT8(LEDC_CH_AUX1, rowDriving("doorFL"));
    ServoOutputRow arm1 = {};
    TEST_ASSERT_TRUE(configCacheReadServoOutput(0, &arm1));
    TEST_ASSERT_EQUAL_UINT8(0, servoOutputPartCount(arm1));
    TEST_ASSERT_EQUAL_UINT(broadcastsBefore + 1, g_test_status_broadcast_count);
}

// The steal nobody announced. The request says the door is on nothing, the
// table says ARM1: the whole request is refused, the field riding beside the
// move included, and nothing reaches storage.
void test_a_move_from_an_output_the_part_is_not_on_changes_nothing() {
    seedUnwiredServoOutputRows();
    WebRequestTestBackend seed;
    TEST_ASSERT_EQUAL_INT(200, postMove("doorFL", "none", "ledc:0", &seed));
    const unsigned broadcastsBefore = g_test_status_broadcast_count;

    const WebRequestTestParam params[] = {{"movePart", "doorFL"},
                                          {"movePartFrom", "none"},
                                          {"movePartTo", "ledc:3"},
                                          {"speedLimitMax", "250"}};
    WebRequestTestBackend backend;
    backend.params = params;
    backend.paramCount = 4;
    WebRequest req(&backend);
    handleConfigPost(req);

    TEST_ASSERT_EQUAL_INT(409, backend.sentCode);
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, backend.sentBody));
    TEST_ASSERT_EQUAL_STRING(
        "that Part is not on the Output movePartFrom names - read the outputs again, then move it",
        doc["error"] | "");
    TEST_ASSERT_EQUAL_UINT8(LEDC_CH_ARM1, rowDriving("doorFL"));
    TEST_ASSERT_EQUAL_INT(100, readSnapshot().drive.speedLimitMax);
    // The Commit Step stopped before its save, so it never broadcast either.
    TEST_ASSERT_EQUAL_UINT(broadcastsBefore, g_test_status_broadcast_count);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_a_part_move_takes_it_off_one_output_and_is_committed);
    RUN_TEST(test_a_move_from_an_output_the_part_is_not_on_changes_nothing);
    RUN_TEST(test_config_post_applies_a_field_and_echoes_the_snapshot);
    RUN_TEST(test_config_post_rejects_an_out_of_range_value_without_applying_it);
    RUN_TEST(test_config_post_refuses_clashing_speed_presets_as_a_conflict);
    RUN_TEST(test_config_post_accepts_a_raw_json_body_under_the_plain_name);
    RUN_TEST(test_a_configuration_read_by_get_comes_back_whole_through_post);
    RUN_TEST(test_config_post_syncs_stationary_and_broadcasts_status);
    RUN_TEST(test_config_commit_leaves_working_agreeing_with_the_config_cache);
    RUN_TEST(test_config_post_body_matches_a_read_of_the_committed_config);
    RUN_TEST(test_a_calibration_write_lands_on_the_addressed_row);
    RUN_TEST(test_a_write_the_component_band_cannot_take_is_moved_not_refused);
    RUN_TEST(test_the_echo_reports_what_the_row_holds_not_what_was_asked);
    RUN_TEST(test_the_answer_names_every_end_the_band_moved);
    RUN_TEST(test_a_motion_profile_round_trips_under_the_names_the_config_reports);
    RUN_TEST(test_a_motion_profile_out_of_range_is_refused_not_clamped);
    RUN_TEST(test_a_stated_droid_build_reaches_the_live_answer_and_the_echo);
    RUN_TEST(test_a_restored_legacy_variant_lands_as_the_variant_it_became);
    RUN_TEST(test_the_part_vocabulary_is_unchanged_by_a_droid_build_write);
    RUN_TEST(test_a_droid_build_the_catalog_cannot_name_is_refused_without_applying);
    RUN_TEST(test_a_roadmap_design_is_refused_as_a_stated_half);
    RUN_TEST(test_an_mk41_dome_on_an_mk4_basic_body_saves_as_stated);
    RUN_TEST(test_rc_map_get_returns_the_map_shape);
    RUN_TEST(test_rc_map_post_applies_an_empty_map_and_persists);
    RUN_TEST(test_rc_map_post_rejects_a_bad_entry_with_the_cores_message);
    RUN_TEST(test_wifi_post_stages_settings_without_leaking_the_password);
    RUN_TEST(test_wifi_post_rejects_invalid_settings);
    RUN_TEST(test_wifi_post_commit_step_persists_and_reports_runtime_state);
    return UNITY_END();
}
