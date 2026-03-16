#include <string.h>
#include <unity.h>

#include "rc_diagnostics.h"

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

void test_format_rc_diagnostics_json_contains_sources_channels_and_mapping() {
    RcDiagnosticsSnapshot snapshot = {};
    snapshot.mode = "dual_sbus";
    snapshot.updatedMs = 1234;
    snapshot.sources[0] = {"sbus1", true, true, 12, 1, false};
    snapshot.sources[1] = {"sbus2", true, false, 44, 3, true};
    snapshot.sources[2] = {"pwm", false, false, 0, 0, false};
    snapshot.sourceCount = 3;
    snapshot.analogChannels[0] = {1,    "driveSpeed", "sbus1", 1,     1024,
                                  1510, 0.039f,       0.021f,  false, false};
    snapshot.analogCount = 1;
    snapshot.digitalChannels[0] = {"sound", "sbus2", 17, true};
    snapshot.digitalCount = 1;
    snapshot.mappingChannels[0].name = "driveSpeed";
    snapshot.mappingChannels[0].binding = defaultSbusBinding(RC_BINDING_SBUS1, 1);
    snapshot.mappingChannels[1].name = "sound";
    snapshot.mappingChannels[1].binding = defaultSbusBinding(RC_BINDING_SBUS2, 17);
    snapshot.mappingCount = 2;

    char out[2048] = {};
    TEST_ASSERT_TRUE(formatRcDiagnosticsJson(out, sizeof(out), snapshot));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"mode\":\"dual_sbus\""));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"sbus1\":{\"enabled\":true,\"linked\":true"));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"name\":\"driveSpeed\""));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"digital\":{\"sound\":"));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"mappingProfile\":{\"version\":1"));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"sound\":{\"source\":\"sbus2\",\"channel\":17"));
}

void test_normalization_clamps_below_min() {
    RcBindingConfig binding = defaultSbusBinding(RC_BINDING_SBUS1, 1);
    // Below min should clamp to -1.0
    float result = rcDiagnosticsNormalizeRaw(binding.min - 100, binding);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -1.0f, result);
}

void test_normalization_clamps_above_max() {
    RcBindingConfig binding = defaultSbusBinding(RC_BINDING_SBUS1, 1);
    // Above max should clamp to 1.0
    float result = rcDiagnosticsNormalizeRaw(binding.max + 100, binding);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, result);
}

void test_normalization_handles_reversed_binding() {
    RcBindingConfig binding = defaultSbusBinding(RC_BINDING_SBUS1, 1);
    binding.reverse = true;
    // Check that normalization produces valid results with reverse binding
    // Center should still map to 0
    float centerResult = rcDiagnosticsNormalizeRaw(binding.center, binding);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, centerResult);
}

void test_raw_to_pulse_us_clamps_below_min() {
    RcBindingConfig binding = defaultSbusBinding(RC_BINDING_SBUS1, 1);
    uint16_t pulse = rcDiagnosticsRawToPulseUs(binding.min - 100, binding);
    TEST_ASSERT_EQUAL_UINT16(1000, pulse);  // Clamped to min pulse
}

void test_raw_to_pulse_us_clamps_above_max() {
    RcBindingConfig binding = defaultSbusBinding(RC_BINDING_SBUS1, 1);
    uint16_t pulse = rcDiagnosticsRawToPulseUs(binding.max + 100, binding);
    TEST_ASSERT_EQUAL_UINT16(2000, pulse);  // Clamped to max pulse
}

void test_format_rc_diagnostics_rejects_null_buffer() {
    RcDiagnosticsSnapshot snapshot = {};
    snapshot.mode = "dual_sbus";
    snapshot.updatedMs = 1234;
    snapshot.sourceCount = 0;
    snapshot.analogCount = 0;
    snapshot.digitalCount = 0;
    snapshot.mappingCount = 0;

    TEST_ASSERT_FALSE(formatRcDiagnosticsJson(nullptr, 1024, snapshot));
}

void test_format_rc_diagnostics_rejects_zero_size_buffer() {
    RcDiagnosticsSnapshot snapshot = {};
    snapshot.mode = "dual_sbus";
    snapshot.updatedMs = 1234;
    snapshot.sourceCount = 0;
    snapshot.analogCount = 0;
    snapshot.digitalCount = 0;
    snapshot.mappingCount = 0;

    char buf[1];
    TEST_ASSERT_FALSE(formatRcDiagnosticsJson(buf, 0, snapshot));
}

void test_format_rc_diagnostics_handles_empty_snapshot() {
    RcDiagnosticsSnapshot snapshot = {};
    snapshot.mode = "dual_sbus";
    snapshot.updatedMs = 0;
    snapshot.sourceCount = 0;
    snapshot.analogCount = 0;
    snapshot.digitalCount = 0;
    snapshot.mappingCount = 0;

    char out[512];
    TEST_ASSERT_TRUE(formatRcDiagnosticsJson(out, sizeof(out), snapshot));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"mode\":\"dual_sbus\""));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"updatedMs\":0"));
}

void test_format_rc_diagnostics_outputs_valid_json_structure() {
    RcDiagnosticsSnapshot snapshot = {};
    snapshot.mode = "single_sbus";
    snapshot.updatedMs = 5678;
    snapshot.sources[0] = {"sbus1", true, true, 5, 0, false};
    snapshot.sourceCount = 1;
    snapshot.analogCount = 0;
    snapshot.digitalCount = 0;
    snapshot.mappingCount = 0;

    char out[1024];
    TEST_ASSERT_TRUE(formatRcDiagnosticsJson(out, sizeof(out), snapshot));

    // Verify valid JSON structure
    TEST_ASSERT_EQUAL_CHAR('{', out[0]);
    size_t len = strlen(out);
    TEST_ASSERT_EQUAL_CHAR('}', out[len - 1]);

    // Verify required fields present
    TEST_ASSERT_NOT_NULL(strstr(out, "\"mode\""));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"updatedMs\""));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"sources\""));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"mappingProfile\""));
}

void test_format_rc_diagnostics_fails_with_too_small_buffer() {
    RcDiagnosticsSnapshot snapshot = {};
    snapshot.mode = "dual_sbus";
    snapshot.updatedMs = 1234;

    // Add lots of data to make JSON large
    snapshot.sources[0] = {"sbus1", true, true, 12, 1, false};
    snapshot.sourceCount = 1;
    snapshot.analogChannels[0] = {1,    "driveSpeed", "sbus1", 1,     1024,
                                  1510, 0.0f,         0.0f,    false, false};
    snapshot.analogCount = 1;
    snapshot.digitalChannels[0] = {"sound", "sbus2", 17, true};
    snapshot.digitalCount = 1;
    snapshot.mappingChannels[0].name = "driveSpeed";
    snapshot.mappingChannels[0].binding = defaultSbusBinding(RC_BINDING_SBUS1, 1);
    snapshot.mappingCount = 1;

    // Very small buffer should fail
    char out[64];
    TEST_ASSERT_FALSE(formatRcDiagnosticsJson(out, sizeof(out), snapshot));
}

void test_rc_diagnostics_source_name_returns_correct_names() {
    TEST_ASSERT_EQUAL_STRING("pwm", rcDiagnosticsSourceName(RC_BINDING_PWM));
    TEST_ASSERT_EQUAL_STRING("sbus1", rcDiagnosticsSourceName(RC_BINDING_SBUS1));
    TEST_ASSERT_EQUAL_STRING("sbus2", rcDiagnosticsSourceName(RC_BINDING_SBUS2));
    TEST_ASSERT_EQUAL_STRING("none", rcDiagnosticsSourceName(RC_BINDING_NONE));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_rc_diagnostics_normalize_raw_uses_binding_center);
    RUN_TEST(test_rc_diagnostics_raw_to_pulse_us_maps_sbus_range);
    RUN_TEST(test_format_rc_diagnostics_json_contains_sources_channels_and_mapping);

    // New edge case tests
    RUN_TEST(test_normalization_clamps_below_min);
    RUN_TEST(test_normalization_clamps_above_max);
    RUN_TEST(test_normalization_handles_reversed_binding);
    RUN_TEST(test_raw_to_pulse_us_clamps_below_min);
    RUN_TEST(test_raw_to_pulse_us_clamps_above_max);
    RUN_TEST(test_format_rc_diagnostics_rejects_null_buffer);
    RUN_TEST(test_format_rc_diagnostics_rejects_zero_size_buffer);
    RUN_TEST(test_format_rc_diagnostics_handles_empty_snapshot);
    RUN_TEST(test_format_rc_diagnostics_outputs_valid_json_structure);
    RUN_TEST(test_format_rc_diagnostics_fails_with_too_small_buffer);
    RUN_TEST(test_rc_diagnostics_source_name_returns_correct_names);

    return UNITY_END();
}
