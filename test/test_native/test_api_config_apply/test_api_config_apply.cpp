// =============================================================================
// test/test_native/test_api_config_apply/test_api_config_apply.cpp
//
// Native unit tests for configApply() (ADR 0011 Apply Core, slice 1).
// Exercises the pure validate/apply logic against a ConfigSnapshot through a
// std::map-backed ConfigParamSource, without FreeRTOS, any web-server type,
// or hardware dependencies.
// =============================================================================
#include <unity.h>

#include <cstring>
#include <map>
#include <string>

#include "api_config_apply.h"
#include "drive_speed_preset.h"

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

// Default snapshot: distinct speed presets, active=Normal, dome disabled.
ConfigSnapshot makeDefaultSnap() {
    ConfigSnapshot snap = {};
    snap.drive.speedPresetActive = SpeedPresetId::Normal;
    snap.drive.speedPresetSlow = 150;
    snap.drive.speedPresetNormal = 300;
    snap.drive.speedPresetTurbo = 600;
    snap.drive.speedLimitMax = 300;
    snap.system.enable_dome_esc = false;
    return snap;
}

}  // namespace

void setUp(void) {
}
void tearDown(void) {
}

// --- no-fields case ---
void test_configApply_no_fields_supplied_returns_error(void) {
    std::map<std::string, std::string> m;
    ConfigSnapshot snap = makeDefaultSnap();
    ConfigApplyResult result;
    configApply(makeSource(&m), &snap, false, &result);
    TEST_ASSERT_TRUE(result.error.hasError);
    TEST_ASSERT_EQUAL_STRING("no supported config fields supplied", result.error.message);
    TEST_ASSERT_FALSE(result.changed);
}

// --- scalar range validation (one reject per parser type) ---
void test_configApply_speedLimitMax_updates_and_logs(void) {
    std::map<std::string, std::string> m = {{"speedLimitMax", "250"}};
    ConfigSnapshot snap = makeDefaultSnap();
    ConfigApplyResult result;
    configApply(makeSource(&m), &snap, false, &result);
    TEST_ASSERT_FALSE(result.error.hasError);
    TEST_ASSERT_EQUAL_INT(250, snap.drive.speedLimitMax);
    TEST_ASSERT_EQUAL(1, result.applied.count);
    TEST_ASSERT_EQUAL_STRING("[CFG] speedLimitMax updated to 250", result.applied.lines[0]);
}

void test_configApply_speedLimitMax_out_of_range_rejected(void) {
    std::map<std::string, std::string> m = {{"speedLimitMax", "601"}};
    ConfigSnapshot snap = makeDefaultSnap();
    ConfigApplyResult result;
    configApply(makeSource(&m), &snap, false, &result);
    TEST_ASSERT_TRUE(result.error.hasError);
    TEST_ASSERT_EQUAL_STRING("speedLimitMax must be 0..600", result.error.message);
}

void test_configApply_stationary_bool_reject(void) {
    std::map<std::string, std::string> m = {{"stationary", "sideways"}};
    ConfigSnapshot snap = makeDefaultSnap();
    ConfigApplyResult result;
    configApply(makeSource(&m), &snap, false, &result);
    TEST_ASSERT_TRUE(result.error.hasError);
    TEST_ASSERT_EQUAL_STRING("stationary must be true/false or 1/0", result.error.message);
}

void test_configApply_rcInputMode_enum_reject(void) {
    std::map<std::string, std::string> m = {{"rcInputMode", "bogus"}};
    ConfigSnapshot snap = makeDefaultSnap();
    ConfigApplyResult result;
    configApply(makeSource(&m), &snap, false, &result);
    TEST_ASSERT_TRUE(result.error.hasError);
    TEST_ASSERT_EQUAL_STRING("rcInputMode must be standard_pwm, single_sbus, or dual_sbus",
                             result.error.message);
}

void test_configApply_domeWifiPeerIp_invalid_ipv4_reject(void) {
    std::map<std::string, std::string> m = {{"protoR2linkWifiPeerIp", "not.an.ip"}};
    ConfigSnapshot snap = makeDefaultSnap();
    ConfigApplyResult result;
    configApply(makeSource(&m), &snap, false, &result);
    TEST_ASSERT_TRUE(result.error.hasError);
    TEST_ASSERT_EQUAL_STRING("protoR2linkWifiPeerIp must be empty or a valid IPv4 address",
                             result.error.message);
}

void test_configApply_domeWifiPeerIp_empty_clears(void) {
    std::map<std::string, std::string> m = {{"protoR2linkWifiPeerIp", ""}};
    ConfigSnapshot snap = makeDefaultSnap();
    strncpy(snap.dome.dome_wifi_peer_ip, "10.0.0.5", sizeof(snap.dome.dome_wifi_peer_ip));
    ConfigApplyResult result;
    configApply(makeSource(&m), &snap, false, &result);
    TEST_ASSERT_FALSE(result.error.hasError);
    TEST_ASSERT_EQUAL_STRING("", snap.dome.dome_wifi_peer_ip);
}

// Note: parseServoCompType() maps any unrecognized string to SERVO_COMP_NONE
// (a valid value), so the "%s must be none/mg996r/mg90s/rgb" reject branch is
// unreachable via the string parser today — this pins the named-value path
// instead, matching what the legacy handler actually validated.
void test_configApply_servoType_named_value_updates(void) {
    std::map<std::string, std::string> m = {{"arm1Type", "mg90s"}};
    ConfigSnapshot snap = makeDefaultSnap();
    ConfigApplyResult result;
    configApply(makeSource(&m), &snap, false, &result);
    TEST_ASSERT_FALSE(result.error.hasError);
    TEST_ASSERT_EQUAL_UINT8(SERVO_COMP_MG90S, snap.servo.arm1_type);
}

// --- cross-field rules ---
void test_configApply_speed_presets_must_be_distinct(void) {
    std::map<std::string, std::string> m = {
        {"speedPresetSlow", "300"}, {"speedPresetNormal", "300"}, {"speedPresetTurbo", "600"}};
    ConfigSnapshot snap = makeDefaultSnap();
    ConfigApplyResult result;
    configApply(makeSource(&m), &snap, false, &result);
    TEST_ASSERT_TRUE(result.error.hasError);
    TEST_ASSERT_EQUAL_STRING("speed presets must be distinct values", result.error.message);
}

void test_configApply_speedLimitMax_derives_from_active_preset_when_omitted(void) {
    std::map<std::string, std::string> m = {
        {"speedPresetSlow", "100"}, {"speedPresetNormal", "350"}, {"speedPresetTurbo", "500"}};
    ConfigSnapshot snap = makeDefaultSnap();  // active = Normal
    ConfigApplyResult result;
    configApply(makeSource(&m), &snap, false, &result);
    TEST_ASSERT_FALSE(result.error.hasError);
    TEST_ASSERT_EQUAL_INT(350, snap.drive.speedLimitMax);
}

void test_configApply_speedLimitMax_resolves_matching_preset(void) {
    std::map<std::string, std::string> m = {{"speedLimitMax", "600"}};  // matches turbo
    ConfigSnapshot snap = makeDefaultSnap();
    ConfigApplyResult result;
    configApply(makeSource(&m), &snap, false, &result);
    TEST_ASSERT_FALSE(result.error.hasError);
    TEST_ASSERT_EQUAL((int)SpeedPresetId::Turbo, (int)snap.drive.speedPresetActive);
}

void test_configApply_speedLimitMax_falls_back_to_normal_when_unmatched(void) {
    std::map<std::string, std::string> m = {{"speedLimitMax", "450"}};  // no exact preset match
    ConfigSnapshot snap = makeDefaultSnap();
    snap.drive.speedPresetActive = SpeedPresetId::Turbo;
    ConfigApplyResult result;
    configApply(makeSource(&m), &snap, false, &result);
    TEST_ASSERT_FALSE(result.error.hasError);
    TEST_ASSERT_EQUAL((int)SpeedPresetId::Normal, (int)snap.drive.speedPresetActive);
}

// --- transition action ---
void test_configApply_dome_enable_transition_queues_dome_on_cue(void) {
    std::map<std::string, std::string> m = {{"enableDome", "1"}};
    ConfigSnapshot snap = makeDefaultSnap();
    snap.system.enable_dome_esc = false;
    ConfigApplyResult result;
    configApply(makeSource(&m), &snap, /*domeEnabledBefore=*/false, &result);
    TEST_ASSERT_FALSE(result.error.hasError);
    TEST_ASSERT_TRUE(result.actions.playDomeOnCue);
}

void test_configApply_dome_already_enabled_no_cue(void) {
    std::map<std::string, std::string> m = {{"enableDome", "1"}};
    ConfigSnapshot snap = makeDefaultSnap();
    snap.system.enable_dome_esc = true;
    ConfigApplyResult result;
    configApply(makeSource(&m), &snap, /*domeEnabledBefore=*/true, &result);
    TEST_ASSERT_FALSE(result.error.hasError);
    TEST_ASSERT_FALSE(result.actions.playDomeOnCue);
}

// --- JSON body path ---
void test_configApply_json_body_sbusTimeoutMs_updates(void) {
    std::map<std::string, std::string> m = {{"plain", "{\"rc\":{\"sbusTimeoutMs\":777}}"}};
    ConfigSnapshot snap = makeDefaultSnap();
    ConfigApplyResult result;
    configApply(makeSource(&m), &snap, false, &result);
    TEST_ASSERT_FALSE(result.error.hasError);
    TEST_ASSERT_EQUAL_UINT32(777, snap.drive.sbusTimeoutMs);
}

void test_configApply_json_body_sbusTimeoutMs_out_of_range_rejected(void) {
    std::map<std::string, std::string> m = {{"plain", "{\"rc\":{\"sbusTimeoutMs\":10}}"}};
    ConfigSnapshot snap = makeDefaultSnap();
    ConfigApplyResult result;
    configApply(makeSource(&m), &snap, false, &result);
    TEST_ASSERT_TRUE(result.error.hasError);
    TEST_ASSERT_EQUAL_STRING("rc.sbusTimeoutMs must be 50..5000", result.error.message);
}

void test_configApply_json_body_invalid_json_rejected(void) {
    std::map<std::string, std::string> m = {{"plain", "not json"}};
    ConfigSnapshot snap = makeDefaultSnap();
    ConfigApplyResult result;
    configApply(makeSource(&m), &snap, false, &result);
    TEST_ASSERT_TRUE(result.error.hasError);
    TEST_ASSERT_EQUAL_STRING("invalid json body", result.error.message);
}

void test_configApply_json_body_aux_led_pin_type_error(void) {
    std::map<std::string, std::string> m = {{"plain", "{\"aux_led_pin\":\"nope\"}"}};
    ConfigSnapshot snap = makeDefaultSnap();
    ConfigApplyResult result;
    configApply(makeSource(&m), &snap, false, &result);
    TEST_ASSERT_TRUE(result.error.hasError);
    TEST_ASSERT_EQUAL_STRING("aux_led_pin must be integer 0..3", result.error.message);
}

// --- applied-fields record ---
void test_configApply_multiple_fields_record_applied_lines_in_order(void) {
    std::map<std::string, std::string> m = {{"webDriveTimeoutMs", "1000"}, {"sbusTimeoutMs", "200"}};
    ConfigSnapshot snap = makeDefaultSnap();
    ConfigApplyResult result;
    configApply(makeSource(&m), &snap, false, &result);
    TEST_ASSERT_FALSE(result.error.hasError);
    TEST_ASSERT_EQUAL(2, result.applied.count);
    TEST_ASSERT_EQUAL_STRING("[CFG] webDriveTimeoutMs updated to 1000", result.applied.lines[0]);
    TEST_ASSERT_EQUAL_STRING("[CFG] sbusTimeoutMs updated to 200", result.applied.lines[1]);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_configApply_no_fields_supplied_returns_error);
    RUN_TEST(test_configApply_speedLimitMax_updates_and_logs);
    RUN_TEST(test_configApply_speedLimitMax_out_of_range_rejected);
    RUN_TEST(test_configApply_stationary_bool_reject);
    RUN_TEST(test_configApply_rcInputMode_enum_reject);
    RUN_TEST(test_configApply_domeWifiPeerIp_invalid_ipv4_reject);
    RUN_TEST(test_configApply_domeWifiPeerIp_empty_clears);
    RUN_TEST(test_configApply_servoType_named_value_updates);
    RUN_TEST(test_configApply_speed_presets_must_be_distinct);
    RUN_TEST(test_configApply_speedLimitMax_derives_from_active_preset_when_omitted);
    RUN_TEST(test_configApply_speedLimitMax_resolves_matching_preset);
    RUN_TEST(test_configApply_speedLimitMax_falls_back_to_normal_when_unmatched);
    RUN_TEST(test_configApply_dome_enable_transition_queues_dome_on_cue);
    RUN_TEST(test_configApply_dome_already_enabled_no_cue);
    RUN_TEST(test_configApply_json_body_sbusTimeoutMs_updates);
    RUN_TEST(test_configApply_json_body_sbusTimeoutMs_out_of_range_rejected);
    RUN_TEST(test_configApply_json_body_invalid_json_rejected);
    RUN_TEST(test_configApply_json_body_aux_led_pin_type_error);
    RUN_TEST(test_configApply_multiple_fields_record_applied_lines_in_order);
    return UNITY_END();
}
