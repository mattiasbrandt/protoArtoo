#include <ArduinoJson.h>
#include <string.h>
#include <unity.h>

#include "rc_diagnostics.h"
#include "rc_diagnostics_snapshot.h"
#include "robot_state.h"

namespace {

RcDiagnosticsSnapshot makeTypicalSnapshot() {
    RcDiagnosticsSnapshot snapshot = {};
    snapshot.mode = "dual_sbus";
    snapshot.updatedMs = 1234;

    snapshot.sources[0] = {"sbus1", true, true, 12, 1, false};
    snapshot.sources[1] = {"sbus2", true, false, 44, 3, true};
    snapshot.sources[2] = {"pwm", false, false, 0, 0, false};
    snapshot.sourceCount = 3;

    snapshot.analogChannels[0] = {1,    "driveSpeed", "sbus1", 1,     1024,
                                  1510, 0.039f,       0.021f,  false, false};
    snapshot.analogChannels[1] = {2,    "driveSteer", "sbus1", 2,     990,
                                  1498, -0.012f,      -0.008f, false, false};
    snapshot.analogCount = 2;

    snapshot.digitalChannels[0] = {"sound", "sbus2", 17, true};
    snapshot.digitalCount = 1;

    snapshot.mappingChannels[0].name = "driveSpeed";
    snapshot.mappingChannels[0].binding = defaultSbusBinding(RC_BINDING_SBUS1, 1);
    snapshot.mappingChannels[1].name = "driveSteer";
    snapshot.mappingChannels[1].binding = defaultSbusBinding(RC_BINDING_SBUS1, 2);
    snapshot.mappingChannels[2].name = "sound";
    snapshot.mappingChannels[2].binding = defaultSbusBinding(RC_BINDING_SBUS2, 17);
    snapshot.mappingCount = 3;

    snapshot.hasRawSbus1 = true;
    snapshot.hasRawSbus2 = true;
    snapshot.hasRawPwm = true;
    for (size_t i = 0; i < RC_DIAGNOSTICS_SBUS_RAW_CAPACITY; ++i) {
        snapshot.rawSbus1[i] = (uint16_t)(172 + i);
        snapshot.rawSbus2[i] = (uint16_t)(1811 - i);
    }
    for (size_t i = 0; i < RC_DIAGNOSTICS_PWM_RAW_CAPACITY; ++i) {
        snapshot.rawPwm[i] = (uint16_t)(1000 + (i * 100));
    }

    return snapshot;
}

RcDiagnosticsSnapshot makeWorstCaseDualSbusSnapshot() {
    RcDiagnosticsSnapshot snapshot = {};
    snapshot.mode = "dual_sbus";
    snapshot.updatedMs = 0xFFFFFFFFu;

    snapshot.sources[0] = {"sbus1", true, true, 5000, 99999, true};
    snapshot.sources[1] = {"sbus2", true, true, 5000, 99999, true};
    snapshot.sources[2] = {"pwm", true, true, 5000, 0, false};
    snapshot.sourceCount = RC_DIAGNOSTICS_SOURCE_CAPACITY;

    const char* names[RC_DIAGNOSTICS_CHANNEL_CAPACITY] = {
        "driveSpeed", "driveSteer", "domeSpeed", "arm1", "arm2", "sound"};

    for (size_t i = 0; i < RC_DIAGNOSTICS_CHANNEL_CAPACITY; ++i) {
        snapshot.analogChannels[i].id = (uint8_t)(i + 1);
        snapshot.analogChannels[i].name = names[i];
        snapshot.analogChannels[i].activeSource = (i % 2 == 0) ? "sbus1" : "sbus2";
        snapshot.analogChannels[i].bindingChannel = (uint8_t)(i + 1);
        snapshot.analogChannels[i].raw = 1811;
        snapshot.analogChannels[i].rawUs = 2000;
        snapshot.analogChannels[i].normalized = 1.0f;
        snapshot.analogChannels[i].mapped = 1.0f;
        snapshot.analogChannels[i].inDeadband = false;
        snapshot.analogChannels[i].reverse = true;

        snapshot.digitalChannels[i].name = names[i];
        snapshot.digitalChannels[i].activeSource = (i % 2 == 0) ? "sbus1" : "sbus2";
        snapshot.digitalChannels[i].bindingChannel = (uint8_t)((i % 2 == 0) ? 17 : 18);
        snapshot.digitalChannels[i].pressed = true;

        snapshot.mappingChannels[i].name = names[i];
        snapshot.mappingChannels[i].binding =
            makeRcBindingConfig((i % 2 == 0) ? RC_BINDING_SBUS1 : RC_BINDING_SBUS2,
                                (uint8_t)((i % 16) + 1), 172, 992, 1811, 50, true);
    }

    snapshot.analogCount = RC_DIAGNOSTICS_CHANNEL_CAPACITY;
    snapshot.digitalCount = RC_DIAGNOSTICS_CHANNEL_CAPACITY;
    snapshot.mappingCount = RC_DIAGNOSTICS_CHANNEL_CAPACITY;

    snapshot.hasRawSbus1 = true;
    snapshot.hasRawSbus2 = true;
    snapshot.hasRawPwm = true;
    for (size_t i = 0; i < RC_DIAGNOSTICS_SBUS_RAW_CAPACITY; ++i) {
        snapshot.rawSbus1[i] = 1811;
        snapshot.rawSbus2[i] = 1811;
    }
    for (size_t i = 0; i < RC_DIAGNOSTICS_PWM_RAW_CAPACITY; ++i) {
        snapshot.rawPwm[i] = 2000;
    }

    return snapshot;
}

}  // namespace

void test_rc_diagnostics_normalize_raw_uses_binding_center() {
    RcBindingConfig binding = defaultSbusBinding(RC_BINDING_SBUS1, 1);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, rcDiagnosticsNormalizeRaw(binding.center, binding));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, rcDiagnosticsNormalizeRaw(binding.max, binding));
}

void test_rc_diagnostics_raw_to_pulse_us_maps_sbus_range() {
    RcBindingConfig binding = defaultSbusBinding(RC_BINDING_SBUS1, 1);
    TEST_ASSERT_EQUAL_UINT16(1000, rcDiagnosticsRawToPulseUs(binding.min, binding));
    TEST_ASSERT_EQUAL_UINT16(1500, rcDiagnosticsRawToPulseUs(binding.center, binding));
    TEST_ASSERT_EQUAL_UINT16(2000, rcDiagnosticsRawToPulseUs(binding.max, binding));
}

void test_populateRcDiagnosticsJson_typical_valid() {
    RcDiagnosticsSnapshot snapshot = makeTypicalSnapshot();

    JsonDocument doc;
    TEST_ASSERT_TRUE(populateRcDiagnosticsJson(doc, snapshot));

    char out[3072] = {};
    size_t n = serializeJson(doc, out, sizeof(out));

    TEST_ASSERT_GREATER_THAN(0u, n);
    TEST_ASSERT_LESS_THAN(3072u, n);
    TEST_ASSERT_EQUAL_CHAR('{', out[0]);
    TEST_ASSERT_EQUAL_CHAR('}', out[n - 1]);

    TEST_ASSERT_NOT_NULL(strstr(out, "\"mode\":\"dual_sbus\""));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"sources\":{\"sbus1\""));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"channels\":[{"));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"mappingProfile\":{\"version\":1"));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"raw\":{\"sbus1\":["));
}

void test_populateRcDiagnosticsJson_dual_sbus_fits_buffer() {
    RcDiagnosticsSnapshot snapshot = makeWorstCaseDualSbusSnapshot();

    JsonDocument doc;
    TEST_ASSERT_TRUE(populateRcDiagnosticsJson(doc, snapshot));

    char out[3072] = {};
    size_t n = serializeJson(doc, out, sizeof(out));
    TEST_ASSERT_LESS_THAN(3072u, n);
}

void test_populateRcDiagnosticsJson_rejects_null_mode() {
    RcDiagnosticsSnapshot snapshot = {};
    snapshot.mode = nullptr;

    JsonDocument doc;
    TEST_ASSERT_FALSE(populateRcDiagnosticsJson(doc, snapshot));
}

void test_populateRcDiagnosticsJson_key_order_matches_contract() {
    RcDiagnosticsSnapshot snapshot = makeTypicalSnapshot();

    JsonDocument doc;
    TEST_ASSERT_TRUE(populateRcDiagnosticsJson(doc, snapshot));

    char out[3072] = {};
    serializeJson(doc, out, sizeof(out));

    const char* pMode = strstr(out, "\"mode\"");
    const char* pUpdated = strstr(out, "\"updatedMs\"");
    const char* pSources = strstr(out, "\"sources\"");
    const char* pChannels = strstr(out, "\"channels\"");
    const char* pDigital = strstr(out, "\"digital\"");
    const char* pMapping = strstr(out, "\"mappingProfile\"");
    const char* pRaw = strstr(out, "\"raw\":{");

    TEST_ASSERT_NOT_NULL(pMode);
    TEST_ASSERT_NOT_NULL(pUpdated);
    TEST_ASSERT_NOT_NULL(pSources);
    TEST_ASSERT_NOT_NULL(pChannels);
    TEST_ASSERT_NOT_NULL(pDigital);
    TEST_ASSERT_NOT_NULL(pMapping);
    TEST_ASSERT_NOT_NULL(pRaw);

    TEST_ASSERT_TRUE(pMode < pUpdated);
    TEST_ASSERT_TRUE(pUpdated < pSources);
    TEST_ASSERT_TRUE(pSources < pChannels);
    TEST_ASSERT_TRUE(pChannels < pDigital);
    TEST_ASSERT_TRUE(pDigital < pMapping);
    TEST_ASSERT_TRUE(pMapping < pRaw);
}

void test_populateRcDiagnosticsJson_empty_raw_object_present() {
    RcDiagnosticsSnapshot snapshot = makeTypicalSnapshot();
    snapshot.hasRawSbus1 = false;
    snapshot.hasRawSbus2 = false;
    snapshot.hasRawPwm = false;

    JsonDocument doc;
    TEST_ASSERT_TRUE(populateRcDiagnosticsJson(doc, snapshot));

    char out[3072] = {};
    serializeJson(doc, out, sizeof(out));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"raw\":{}"));
}

void test_normalization_clamps_below_min() {
    RcBindingConfig binding = defaultSbusBinding(RC_BINDING_SBUS1, 1);
    float result = rcDiagnosticsNormalizeRaw(binding.min - 100, binding);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -1.0f, result);
}

void test_normalization_clamps_above_max() {
    RcBindingConfig binding = defaultSbusBinding(RC_BINDING_SBUS1, 1);
    float result = rcDiagnosticsNormalizeRaw(binding.max + 100, binding);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, result);
}

void test_normalization_handles_reversed_binding() {
    RcBindingConfig binding = defaultSbusBinding(RC_BINDING_SBUS1, 1);
    binding.reverse = true;
    float centerResult = rcDiagnosticsNormalizeRaw(binding.center, binding);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, centerResult);
}

void test_raw_to_pulse_us_clamps_below_min() {
    RcBindingConfig binding = defaultSbusBinding(RC_BINDING_SBUS1, 1);
    uint16_t pulse = rcDiagnosticsRawToPulseUs(binding.min - 100, binding);
    TEST_ASSERT_EQUAL_UINT16(1000, pulse);
}

void test_raw_to_pulse_us_clamps_above_max() {
    RcBindingConfig binding = defaultSbusBinding(RC_BINDING_SBUS1, 1);
    uint16_t pulse = rcDiagnosticsRawToPulseUs(binding.max + 100, binding);
    TEST_ASSERT_EQUAL_UINT16(2000, pulse);
}

void test_rc_diagnostics_source_name_returns_correct_names() {
    TEST_ASSERT_EQUAL_STRING("pwm", rcDiagnosticsSourceName(RC_BINDING_PWM));
    TEST_ASSERT_EQUAL_STRING("sbus1", rcDiagnosticsSourceName(RC_BINDING_SBUS1));
    TEST_ASSERT_EQUAL_STRING("sbus2", rcDiagnosticsSourceName(RC_BINDING_SBUS2));
    TEST_ASSERT_EQUAL_STRING("none", rcDiagnosticsSourceName(RC_BINDING_NONE));
}

void test_rcSourceEnabledForMode_single_sbus_ch1_selected() {
    // useCh2=false, enableRcCh1=true: SBUS1 active, SBUS2 not
    TEST_ASSERT_TRUE(rcSourceEnabledForMode(RC_BINDING_SBUS1, RC_INPUT_SINGLE_SBUS, true, true, false, false));
    TEST_ASSERT_FALSE(rcSourceEnabledForMode(RC_BINDING_SBUS2, RC_INPUT_SINGLE_SBUS, true, true, false, false));
    TEST_ASSERT_FALSE(rcSourceEnabledForMode(RC_BINDING_PWM, RC_INPUT_SINGLE_SBUS, true, true, true, false));
    // useCh2=false, enableRcCh1=false: SBUS1 disabled even though it is selected
    TEST_ASSERT_FALSE(rcSourceEnabledForMode(RC_BINDING_SBUS1, RC_INPUT_SINGLE_SBUS, false, true, false, false));
}

void test_rcSourceEnabledForMode_single_sbus_ch2_selected() {
    // useCh2=true, enableRcCh2=true: SBUS2 active, SBUS1 not
    TEST_ASSERT_FALSE(rcSourceEnabledForMode(RC_BINDING_SBUS1, RC_INPUT_SINGLE_SBUS, true, true, false, true));
    TEST_ASSERT_TRUE(rcSourceEnabledForMode(RC_BINDING_SBUS2, RC_INPUT_SINGLE_SBUS, true, true, false, true));
    // useCh2=true, enableRcCh2=false: SBUS2 disabled — enable flag is respected
    TEST_ASSERT_FALSE(rcSourceEnabledForMode(RC_BINDING_SBUS2, RC_INPUT_SINGLE_SBUS, false, false, false, true));
    // useCh2=true, enableRcCh2=true: SBUS2 active regardless of enableRcCh1
    TEST_ASSERT_TRUE(rcSourceEnabledForMode(RC_BINDING_SBUS2, RC_INPUT_SINGLE_SBUS, false, true, false, true));
}

void test_rcSourceEnabledForMode_dual_sbus_follows_enable_flags() {
    TEST_ASSERT_TRUE(rcSourceEnabledForMode(RC_BINDING_SBUS1, RC_INPUT_DUAL_SBUS, true, false, false, false));
    TEST_ASSERT_FALSE(rcSourceEnabledForMode(RC_BINDING_SBUS1, RC_INPUT_DUAL_SBUS, false, true, false, false));
    TEST_ASSERT_FALSE(rcSourceEnabledForMode(RC_BINDING_SBUS2, RC_INPUT_DUAL_SBUS, true, false, false, false));
    TEST_ASSERT_TRUE(rcSourceEnabledForMode(RC_BINDING_SBUS2, RC_INPUT_DUAL_SBUS, false, true, false, false));
    // useCh2 has no effect in dual_sbus
    TEST_ASSERT_TRUE(rcSourceEnabledForMode(RC_BINDING_SBUS1, RC_INPUT_DUAL_SBUS, true, false, false, true));
}

void test_rcSourceEnabledForMode_pwm_follows_anyPwmEnabled() {
    TEST_ASSERT_TRUE(rcSourceEnabledForMode(RC_BINDING_PWM, RC_INPUT_STANDARD_PWM, false, false, true, false));
    TEST_ASSERT_FALSE(rcSourceEnabledForMode(RC_BINDING_PWM, RC_INPUT_STANDARD_PWM, false, false, false, false));
    // PWM disabled in non-PWM modes
    TEST_ASSERT_FALSE(rcSourceEnabledForMode(RC_BINDING_PWM, RC_INPUT_DUAL_SBUS, false, false, true, false));
    TEST_ASSERT_FALSE(rcSourceEnabledForMode(RC_BINDING_PWM, RC_INPUT_SINGLE_SBUS, false, false, true, false));
}

void test_rcSourceEnabledForMode_none_always_false() {
    TEST_ASSERT_FALSE(rcSourceEnabledForMode(RC_BINDING_NONE, RC_INPUT_STANDARD_PWM, true, true, true, true));
    TEST_ASSERT_FALSE(rcSourceEnabledForMode(RC_BINDING_NONE, RC_INPUT_DUAL_SBUS, true, true, true, true));
    TEST_ASSERT_FALSE(rcSourceEnabledForMode(RC_BINDING_NONE, RC_INPUT_SINGLE_SBUS, true, true, true, true));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_rc_diagnostics_normalize_raw_uses_binding_center);
    RUN_TEST(test_rc_diagnostics_raw_to_pulse_us_maps_sbus_range);
    RUN_TEST(test_populateRcDiagnosticsJson_typical_valid);
    RUN_TEST(test_populateRcDiagnosticsJson_dual_sbus_fits_buffer);
    RUN_TEST(test_populateRcDiagnosticsJson_rejects_null_mode);
    RUN_TEST(test_populateRcDiagnosticsJson_key_order_matches_contract);
    RUN_TEST(test_populateRcDiagnosticsJson_empty_raw_object_present);

    RUN_TEST(test_normalization_clamps_below_min);
    RUN_TEST(test_normalization_clamps_above_max);
    RUN_TEST(test_normalization_handles_reversed_binding);
    RUN_TEST(test_raw_to_pulse_us_clamps_below_min);
    RUN_TEST(test_raw_to_pulse_us_clamps_above_max);
    RUN_TEST(test_rc_diagnostics_source_name_returns_correct_names);

    RUN_TEST(test_rcSourceEnabledForMode_single_sbus_ch1_selected);
    RUN_TEST(test_rcSourceEnabledForMode_single_sbus_ch2_selected);
    RUN_TEST(test_rcSourceEnabledForMode_dual_sbus_follows_enable_flags);
    RUN_TEST(test_rcSourceEnabledForMode_pwm_follows_anyPwmEnabled);
    RUN_TEST(test_rcSourceEnabledForMode_none_always_false);

    return UNITY_END();
}
