// =============================================================================
// test/test_native/test_api_config_json/test_api_config_json.cpp
//
// Native unit tests for populateConfigJson().
// Exercises the pure serialization function against a ConfigSnapshot
// without FreeRTOS or hardware dependencies.
// =============================================================================
#include <ArduinoJson.h>
#include <unity.h>

#include <cstring>

#include "api_config_snapshot.h"
#include "rc_mapping.h"


static constexpr size_t kConfigJsonBudget = 3072;
// Helper: build a default snapshot with all binding fields explicitly disabled.
// Scalar fields stay zero-initialized (speedLimitMax=0, booleans=false, etc.).
static ConfigSnapshot makeDefaultSnap() {
    ConfigSnapshot snap = {};

    // 12 RcBindingConfig fields
    snap.system.rc_pwm_drive_speed = disabledRcBinding();
    snap.system.rc_pwm_drive_steer = disabledRcBinding();
    snap.system.rc_pwm_dome_speed = disabledRcBinding();
    snap.system.rc_pwm_arm1 = disabledRcBinding();
    snap.system.rc_pwm_arm2 = disabledRcBinding();
    snap.system.rc_pwm_sound = disabledRcBinding();
    snap.system.rc_sbus_drive_speed = disabledRcBinding();
    snap.system.rc_sbus_drive_steer = disabledRcBinding();
    snap.system.rc_sbus_dome_speed = disabledRcBinding();
    snap.system.rc_sbus_arm1 = disabledRcBinding();
    snap.system.rc_sbus_arm2 = disabledRcBinding();
    snap.system.rc_sbus_sound = disabledRcBinding();

    // 11 RcTriggerBinding fields — zero-init is a valid disabled state
    snap.system.rc_arm1 = {};
    snap.system.rc_arm2 = {};
    snap.system.rc_aux1 = {};
    snap.system.rc_aux2 = {};
    snap.system.rc_aux3 = {};
    snap.system.rc_sound = {};
    snap.system.rc_opmode = {};
    snap.system.rc_free0 = {};
    snap.system.rc_free1 = {};
    snap.system.rc_free2 = {};
    snap.system.rc_free3 = {};

    snap.servo.aux_led_pin = AUX_LED_PIN_DISABLED;
    snap.servo.aux_led_count = AUX_LED_COUNT_DEFAULT;
    snap.drive.speedPresetActive = SpeedPresetId::Normal;
    snap.drive.sbusTimeoutMs = SBUS_TIMEOUT_MS;
    return snap;
}

// Helper: build a maximally-large snapshot to probe the buffer ceiling.
static ConfigSnapshot makeWorstCaseSnap() {
    ConfigSnapshot snap = {};

    // Scalar extremes
    snap.drive.webDriveTimeoutMs = 0xFFFFFFFFUL;
    snap.system.logLevel = 3;
    snap.system.rc_input_mode = RC_INPUT_STANDARD_PWM;
    snap.drive.speedPresetActive = SpeedPresetId::Turbo;

    // Extreme binding config — SBUS2 ch18, long-format calibration values.
    // deadband must satisfy deadband < (max - min), per rcBindingIsValid().
    RcBindingConfig extreme = {};
    extreme.source = RC_BINDING_SBUS2;
    extreme.channel = 18;
    extreme.min = 10000;
    extreme.center = 32767;
    extreme.max = 60000;
    extreme.deadband = 0;
    extreme.reverse = true;

    snap.system.rc_pwm_drive_speed = extreme;
    snap.system.rc_pwm_drive_steer = extreme;
    snap.system.rc_pwm_dome_speed = extreme;
    snap.system.rc_pwm_arm1 = extreme;
    snap.system.rc_pwm_arm2 = extreme;
    snap.system.rc_pwm_sound = extreme;
    snap.system.rc_sbus_drive_speed = extreme;
    snap.system.rc_sbus_drive_steer = extreme;
    snap.system.rc_sbus_dome_speed = extreme;
    snap.system.rc_sbus_arm1 = extreme;
    snap.system.rc_sbus_arm2 = extreme;
    snap.system.rc_sbus_sound = extreme;

    // Extreme trigger binding — 15-char payload, extreme calibration
    RcTriggerBinding xtrig = {};
    xtrig.source = RC_BINDING_SBUS2;
    xtrig.channel = 18;
    xtrig.target = DOME_ACTION_MARCDUINO_CMD;
    strncpy(xtrig.marcduinoPayload, "AAAAAAAAAAAAAAA", 15);
    xtrig.marcduinoPayload[15] = '\0';
    xtrig.min = 10000;
    xtrig.center = 32767;
    xtrig.max = 60000;
    xtrig.deadband = 0;  // must be < (max - min) per rcTriggerBindingIsValid()
    xtrig.reverse = true;

    snap.system.rc_arm1 = xtrig;
    snap.system.rc_arm2 = xtrig;
    snap.system.rc_aux1 = xtrig;
    snap.system.rc_aux2 = xtrig;
    snap.system.rc_aux3 = xtrig;
    snap.system.rc_sound = xtrig;
    snap.system.rc_opmode = xtrig;
    snap.system.rc_free0 = xtrig;
    snap.system.rc_free1 = xtrig;
    snap.system.rc_free2 = xtrig;
    snap.system.rc_free3 = xtrig;
    snap.servo.aux_led_pin = AUX_LED_PIN_AUX3;
    snap.servo.aux_led_count = AUX_LED_COUNT_MAX;

    // Max-length Device WiFi Settings (32-char SSIDs, 63-char passwords).
    snap.wifi.provisioned = true;
    snap.wifi.mode = WifiMode::STANDALONE_AP;
    std::memset(snap.wifi.sta_ssid, 'a', sizeof(snap.wifi.sta_ssid) - 1);
    std::memset(snap.wifi.sta_password, 'b', sizeof(snap.wifi.sta_password) - 1);
    std::memset(snap.wifi.ap_ssid, 'c', sizeof(snap.wifi.ap_ssid) - 1);
    std::memset(snap.wifi.ap_password, 'd', sizeof(snap.wifi.ap_password) - 1);

    return snap;
}

void setUp(void) {
}
void tearDown(void) {
}

// --- Test 1 ---
// Default snapshot serializes to valid JSON within the grouped-schema size budget.
void test_populateConfigJson_typical_valid_json(void) {
    ConfigSnapshot snap = makeDefaultSnap();
    JsonDocument doc;
    TEST_ASSERT_TRUE(populateConfigJson(doc, snap));

    char out[kConfigJsonBudget] = {};
    size_t n = serializeJson(doc, out, sizeof(out));

    TEST_ASSERT_GREATER_THAN(0u, n);
    TEST_ASSERT_LESS_THAN(kConfigJsonBudget, n);
    TEST_ASSERT_EQUAL_CHAR('{', out[0]);
    TEST_ASSERT_EQUAL_CHAR('}', out[n - 1]);
}

// --- Test 2 ---
// Maximally-large grouped snapshot still serializes within the grouped-schema budget.
void test_populateConfigJson_worst_case_fits_buffer(void) {
    ConfigSnapshot snap = makeWorstCaseSnap();
    JsonDocument doc;
    TEST_ASSERT_TRUE(populateConfigJson(doc, snap));

    char out[kConfigJsonBudget] = {};
    size_t n = serializeJson(doc, out, sizeof(out));

    TEST_ASSERT_LESS_THAN(kConfigJsonBudget, n);
}

// --- Test 3 ---
// Grouped schema keys and nested fields are present with expected types.
void test_populateConfigJson_expected_keys_present(void) {
    ConfigSnapshot snap = makeDefaultSnap();
    JsonDocument doc;
    TEST_ASSERT_TRUE(populateConfigJson(doc, snap));

    JsonObject drive = doc["drive"].as<JsonObject>();
    JsonObject rc = doc["rc"].as<JsonObject>();
    JsonObject components = doc["components"].as<JsonObject>();
    JsonObject dome = doc["dome"].as<JsonObject>();
    JsonObject system = doc["system"].as<JsonObject>();

    TEST_ASSERT_TRUE(!drive.isNull());
    TEST_ASSERT_TRUE(!rc.isNull());
    TEST_ASSERT_TRUE(!components.isNull());
    TEST_ASSERT_TRUE(!dome.isNull());
    TEST_ASSERT_TRUE(!system.isNull());

    TEST_ASSERT_TRUE(!drive["speedLimitMax"].isNull());
    TEST_ASSERT_TRUE(!drive["webDriveTimeoutMs"].isNull());
    TEST_ASSERT_TRUE(drive["speedPreset"].is<const char*>());
    TEST_ASSERT_EQUAL_STRING("normal", drive["speedPreset"] | "");
    TEST_ASSERT_TRUE(rc["inputMode"].is<const char*>());
    TEST_ASSERT_EQUAL_STRING("standard_pwm", rc["inputMode"] | "");
    TEST_ASSERT_EQUAL_UINT(SBUS_TIMEOUT_MS, rc["sbusTimeoutMs"].as<unsigned>());
    TEST_ASSERT_TRUE(rc["pwm"].isNull());
    TEST_ASSERT_TRUE(rc["triggers"].isNull());
    TEST_ASSERT_TRUE(rc["sbus"]["recvCh2"].is<bool>());
    TEST_ASSERT_FALSE(rc["sbus"]["recvCh2"].as<bool>());
    TEST_ASSERT_TRUE(components["arm1"]["enabled"].is<bool>());
    TEST_ASSERT_EQUAL_STRING("none", components["arm1"]["type"] | "");
    TEST_ASSERT_TRUE(!dome["neutralUs"].isNull());
    TEST_ASSERT_TRUE(!system["logLevel"].isNull());
    TEST_ASSERT_TRUE(!doc["arm1OpenUs"].isNull());
    TEST_ASSERT_TRUE(!doc["arm1CloseUs"].isNull());
    TEST_ASSERT_TRUE(!doc["arm2OpenUs"].isNull());
    TEST_ASSERT_TRUE(!doc["arm2CloseUs"].isNull());
    TEST_ASSERT_TRUE(!doc["aux1OpenUs"].isNull());
    TEST_ASSERT_TRUE(!doc["aux1CloseUs"].isNull());
    TEST_ASSERT_TRUE(!doc["aux2OpenUs"].isNull());
    TEST_ASSERT_TRUE(!doc["aux2CloseUs"].isNull());
    TEST_ASSERT_TRUE(!doc["aux3OpenUs"].isNull());
    TEST_ASSERT_TRUE(!doc["aux3CloseUs"].isNull());
    TEST_ASSERT_TRUE(!doc["aux_led_pin"].isNull());
    TEST_ASSERT_TRUE(!doc["aux_led_count"].isNull());
    TEST_ASSERT_EQUAL_UINT(AUX_LED_PIN_DISABLED, doc["aux_led_pin"].as<unsigned>());
    TEST_ASSERT_EQUAL_UINT(AUX_LED_COUNT_DEFAULT, doc["aux_led_count"].as<unsigned>());
}

// --- Test 3b ---
// AUX LED config fields retain all valid pin selections and count values.
void test_populateConfigJson_aux_led_round_trip(void) {
    const uint8_t pins[] = {AUX_LED_PIN_DISABLED, AUX_LED_PIN_AUX1, AUX_LED_PIN_AUX2, AUX_LED_PIN_AUX3};
    const uint8_t counts[] = {AUX_LED_COUNT_DEFAULT, 32, AUX_LED_COUNT_MAX};

    for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); ++i) {
        for (size_t j = 0; j < sizeof(counts) / sizeof(counts[0]); ++j) {
            ConfigSnapshot snap = makeDefaultSnap();
            snap.servo.aux_led_pin = pins[i];
            snap.servo.aux_led_count = counts[j];

            JsonDocument doc;
            TEST_ASSERT_TRUE(populateConfigJson(doc, snap));
            TEST_ASSERT_EQUAL_UINT(pins[i], doc["aux_led_pin"].as<unsigned>());
            TEST_ASSERT_EQUAL_UINT(counts[j], doc["aux_led_count"].as<unsigned>());
        }
    }
}

// --- Test 4 ---
// All trigger slots set to a zero-initialized (disabled) RcTriggerBinding.
void test_populateConfigJson_disabled_trigger_binding_serializes(void) {
    ConfigSnapshot snap = {};  // all fields zero-initialized = disabled

    JsonDocument doc;
    TEST_ASSERT_TRUE(populateConfigJson(doc, snap));

    char out[kConfigJsonBudget] = {};
    size_t n = serializeJson(doc, out, sizeof(out));

    TEST_ASSERT_GREATER_THAN(0u, n);
    TEST_ASSERT_EQUAL_CHAR('{', out[0]);
    TEST_ASSERT_EQUAL_CHAR('}', out[n - 1]);
}

// --- Test 5 ---
// populateConfigJson clears any pre-existing document content before rebuilding.
void test_populateConfigJson_clears_existing_document(void) {
    ConfigSnapshot snap = makeDefaultSnap();
    JsonDocument doc;
    doc["legacy"] = true;
    TEST_ASSERT_TRUE(!doc["legacy"].isNull());

    TEST_ASSERT_TRUE(populateConfigJson(doc, snap));
    TEST_ASSERT_TRUE(doc["legacy"].isNull());
    TEST_ASSERT_TRUE(!doc["drive"].isNull());
}

// --- Test 6 ---
// The full JSON is substantially larger than a 64-byte buffer, proving the
// class of bug that snprintf silent truncation produced is measurable.
void test_populateConfigJson_overflow_is_measurable(void) {
    ConfigSnapshot snap = makeWorstCaseSnap();
    JsonDocument doc;
    TEST_ASSERT_TRUE(populateConfigJson(doc, snap));

    // Tiny-buffer serialization should not fit, proving truncation risk exists.
    char tiny[64] = {};
    size_t written = serializeJson(doc, tiny, sizeof(tiny));
    TEST_ASSERT_TRUE(written == sizeof(tiny) || written == (sizeof(tiny) - 1));

    // measureJson() returns the exact byte count without writing to a buffer.
    size_t full_size = measureJson(doc);
    TEST_ASSERT_GREATER_THAN(64u, full_size);
    TEST_ASSERT_TRUE(full_size > written);
}

// --- Test 7 ---
// The "wifi" block exposes password-set flags, never plaintext passwords,
// and reports provisioned/mode/SSIDs (ADR 0015 / issue #45).
void test_populateConfigJson_wifi_block_exposes_password_flags_not_plaintext(void) {
    ConfigSnapshot snap = makeDefaultSnap();
    snap.wifi.provisioned = true;
    snap.wifi.mode = WifiMode::CLIENT;
    snprintf(snap.wifi.sta_ssid, sizeof(snap.wifi.sta_ssid), "HomeNetwork");
    snprintf(snap.wifi.sta_password, sizeof(snap.wifi.sta_password), "supersecret");
    snprintf(snap.wifi.ap_ssid, sizeof(snap.wifi.ap_ssid), "protoArtoo");
    snap.wifi.ap_password[0] = '\0';

    JsonDocument doc;
    TEST_ASSERT_TRUE(populateConfigJson(doc, snap));

    JsonObject wifi = doc["wifi"].as<JsonObject>();
    TEST_ASSERT_TRUE(!wifi.isNull());
    TEST_ASSERT_TRUE(wifi["provisioned"].as<bool>());
    TEST_ASSERT_EQUAL_STRING("client", wifi["mode"] | "");
    TEST_ASSERT_EQUAL_STRING("HomeNetwork", wifi["staSsid"] | "");
    TEST_ASSERT_TRUE(wifi["staPasswordSet"].as<bool>());
    TEST_ASSERT_EQUAL_STRING("protoArtoo", wifi["apSsid"] | "");
    TEST_ASSERT_FALSE(wifi["apPasswordSet"].as<bool>());

    char out[kConfigJsonBudget] = {};
    serializeJson(doc, out, sizeof(out));
    TEST_ASSERT_NULL(strstr(out, "supersecret"));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_populateConfigJson_typical_valid_json);
    RUN_TEST(test_populateConfigJson_worst_case_fits_buffer);
    RUN_TEST(test_populateConfigJson_expected_keys_present);
    RUN_TEST(test_populateConfigJson_aux_led_round_trip);
    RUN_TEST(test_populateConfigJson_wifi_block_exposes_password_flags_not_plaintext);
    RUN_TEST(test_populateConfigJson_disabled_trigger_binding_serializes);
    RUN_TEST(test_populateConfigJson_clears_existing_document);
    RUN_TEST(test_populateConfigJson_overflow_is_measurable);
    return UNITY_END();
}
