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

    // 14 RcBindingConfig fields
    snap.rcPwmDriveSpeed = disabledRcBinding();
    snap.rcPwmDriveSteer = disabledRcBinding();
    snap.rcPwmDriveLimit = disabledRcBinding();
    snap.rcPwmDomeSpeed = disabledRcBinding();
    snap.rcPwmArm1 = disabledRcBinding();
    snap.rcPwmArm2 = disabledRcBinding();
    snap.rcPwmSound = disabledRcBinding();
    snap.rcSbusDriveSpeed = disabledRcBinding();
    snap.rcSbusDriveSteer = disabledRcBinding();
    snap.rcSbusDriveLimit = disabledRcBinding();
    snap.rcSbusDomeSpeed = disabledRcBinding();
    snap.rcSbusArm1 = disabledRcBinding();
    snap.rcSbusArm2 = disabledRcBinding();
    snap.rcSbusSound = disabledRcBinding();

    // 11 RcTriggerBinding fields — zero-init is a valid disabled state
    snap.rcArm1 = {};
    snap.rcArm2 = {};
    snap.rcAux1 = {};
    snap.rcAux2 = {};
    snap.rcAux3 = {};
    snap.rcSound = {};
    snap.rcOpmode = {};
    snap.rcFree0 = {};
    snap.rcFree1 = {};
    snap.rcFree2 = {};
    snap.rcFree3 = {};

    return snap;
}

// Helper: build a maximally-large snapshot to probe the buffer ceiling.
static ConfigSnapshot makeWorstCaseSnap() {
    ConfigSnapshot snap = {};

    // Scalar extremes
    snap.webDriveTimeoutMs = 0xFFFFFFFFUL;
    snap.logLevel = 3;
    snap.rcInputMode = RC_INPUT_STANDARD_PWM;

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

    snap.rcPwmDriveSpeed = extreme;
    snap.rcPwmDriveSteer = extreme;
    snap.rcPwmDriveLimit = extreme;
    snap.rcPwmDomeSpeed = extreme;
    snap.rcPwmArm1 = extreme;
    snap.rcPwmArm2 = extreme;
    snap.rcPwmSound = extreme;
    snap.rcSbusDriveSpeed = extreme;
    snap.rcSbusDriveSteer = extreme;
    snap.rcSbusDriveLimit = extreme;
    snap.rcSbusDomeSpeed = extreme;
    snap.rcSbusArm1 = extreme;
    snap.rcSbusArm2 = extreme;
    snap.rcSbusSound = extreme;

    // Extreme trigger binding — 15-char payload, extreme calibration
    RcTriggerBinding xtrig = {};
    xtrig.source = RC_BINDING_SBUS2;
    xtrig.channel = 18;
    xtrig.target = RC_ACTION_MARCDUINO_CMD;
    strncpy(xtrig.marcduinoPayload, "AAAAAAAAAAAAAAA", 15);
    xtrig.marcduinoPayload[15] = '\0';
    xtrig.min = 10000;
    xtrig.center = 32767;
    xtrig.max = 60000;
    xtrig.deadband = 0;  // must be < (max - min) per rcTriggerBindingIsValid()
    xtrig.reverse = true;

    snap.rcArm1 = xtrig;
    snap.rcArm2 = xtrig;
    snap.rcAux1 = xtrig;
    snap.rcAux2 = xtrig;
    snap.rcAux3 = xtrig;
    snap.rcSound = xtrig;
    snap.rcOpmode = xtrig;
    snap.rcFree0 = xtrig;
    snap.rcFree1 = xtrig;
    snap.rcFree2 = xtrig;
    snap.rcFree3 = xtrig;

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
    TEST_ASSERT_TRUE(rc["inputMode"].is<const char*>());
    TEST_ASSERT_EQUAL_STRING("standard_pwm", rc["inputMode"] | "");
    TEST_ASSERT_TRUE(rc["pwm"]["driveSpeed"].is<const char*>());
    TEST_ASSERT_TRUE(rc["sbus"]["driveSpeed"].is<const char*>());
    TEST_ASSERT_TRUE(rc["triggers"]["arm1"].is<const char*>());
    TEST_ASSERT_TRUE(rc["triggers"]["free0"].is<const char*>());
    TEST_ASSERT_TRUE(components["arm1"]["enabled"].is<bool>());
    TEST_ASSERT_EQUAL_STRING("none", components["arm1"]["type"] | "");
    TEST_ASSERT_TRUE(!dome["neutralUs"].isNull());
    TEST_ASSERT_TRUE(!system["logLevel"].isNull());
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

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_populateConfigJson_typical_valid_json);
    RUN_TEST(test_populateConfigJson_worst_case_fits_buffer);
    RUN_TEST(test_populateConfigJson_expected_keys_present);
    RUN_TEST(test_populateConfigJson_disabled_trigger_binding_serializes);
    RUN_TEST(test_populateConfigJson_overflow_is_measurable);
    return UNITY_END();
}
