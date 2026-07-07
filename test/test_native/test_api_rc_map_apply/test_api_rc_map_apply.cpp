// =============================================================================
// test/test_native/test_api_rc_map_apply/test_api_rc_map_apply.cpp
//
// Native unit tests for rcMapApply() (ADR 0011 Apply Core, slice 2).
// Exercises the pure validate/apply logic against a ConfigSnapshot through a
// std::map-backed ConfigParamSource carrying the "plain" JSON body, without
// FreeRTOS, AsyncWebServerRequest, or hardware dependencies.
// =============================================================================
#include <unity.h>

#include <map>
#include <string>

#include "api_rc_map_apply.h"

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

ConfigSnapshot makeDefaultSnap() {
    ConfigSnapshot snap = {};
    return snap;
}

}  // namespace

void setUp(void) {
}
void tearDown(void) {
}

// --- no-body / malformed-body cases ---
void test_rcMapApply_no_body_returns_error(void) {
    std::map<std::string, std::string> m;
    ConfigSnapshot snap = makeDefaultSnap();
    RcMapApplyResult result;
    rcMapApply(makeSource(&m), &snap, &result);
    TEST_ASSERT_FALSE(result.ok);
    TEST_ASSERT_EQUAL_STRING("map body required", result.errorMessage);
}

void test_rcMapApply_invalid_json_returns_error(void) {
    std::map<std::string, std::string> m = {{"plain", "not json"}};
    ConfigSnapshot snap = makeDefaultSnap();
    RcMapApplyResult result;
    rcMapApply(makeSource(&m), &snap, &result);
    TEST_ASSERT_FALSE(result.ok);
    TEST_ASSERT_EQUAL_STRING("invalid json body", result.errorMessage);
}

void test_rcMapApply_map_not_array_returns_error(void) {
    std::map<std::string, std::string> m = {{"plain", "{\"map\":\"nope\"}"}};
    ConfigSnapshot snap = makeDefaultSnap();
    RcMapApplyResult result;
    rcMapApply(makeSource(&m), &snap, &result);
    TEST_ASSERT_FALSE(result.ok);
    TEST_ASSERT_EQUAL_STRING("map must be array", result.errorMessage);
}

// --- empty map succeeds (clears all slots) ---
void test_rcMapApply_empty_map_succeeds(void) {
    std::map<std::string, std::string> m = {{"plain", "{\"map\":[]}"}};
    ConfigSnapshot snap = makeDefaultSnap();
    RcMapApplyResult result;
    rcMapApply(makeSource(&m), &snap, &result);
    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_EQUAL(RC_BINDING_NONE, snap.system.rc_pwm_drive_speed.source);
}

// --- one reject per parser/validation type ---
void test_rcMapApply_invalid_source_rejected(void) {
    std::map<std::string, std::string> m = {
        {"plain", "{\"map\":[{\"source\":\"bogus\",\"channel\":1,\"action\":\"drive_speed\"}]}"}};
    ConfigSnapshot snap = makeDefaultSnap();
    RcMapApplyResult result;
    rcMapApply(makeSource(&m), &snap, &result);
    TEST_ASSERT_FALSE(result.ok);
    TEST_ASSERT_EQUAL_STRING("invalid source", result.errorMessage);
}

void test_rcMapApply_channel_out_of_range_rejected(void) {
    std::map<std::string, std::string> m = {
        {"plain", "{\"map\":[{\"source\":\"pwm\",\"channel\":99,\"action\":\"drive_speed\"}]}"}};
    ConfigSnapshot snap = makeDefaultSnap();
    RcMapApplyResult result;
    rcMapApply(makeSource(&m), &snap, &result);
    TEST_ASSERT_FALSE(result.ok);
    TEST_ASSERT_EQUAL_STRING("channel out of range", result.errorMessage);
}

void test_rcMapApply_invalid_action_token_rejected(void) {
    std::map<std::string, std::string> m = {
        {"plain", "{\"map\":[{\"source\":\"pwm\",\"channel\":1,\"action\":\"nonsense\"}]}"}};
    ConfigSnapshot snap = makeDefaultSnap();
    RcMapApplyResult result;
    rcMapApply(makeSource(&m), &snap, &result);
    TEST_ASSERT_FALSE(result.ok);
    TEST_ASSERT_EQUAL_STRING("invalid action token", result.errorMessage);
}

void test_rcMapApply_invalid_dome_seq_payload_rejected(void) {
    std::map<std::string, std::string> m = {
        {"plain",
         "{\"map\":[{\"source\":\"pwm\",\"channel\":1,\"action\":\"dome_seq\",\"payload\":\"DM:NOTREAL\"}]}"}};
    ConfigSnapshot snap = makeDefaultSnap();
    RcMapApplyResult result;
    rcMapApply(makeSource(&m), &snap, &result);
    TEST_ASSERT_FALSE(result.ok);
    TEST_ASSERT_EQUAL_STRING("invalid dome sequence payload (expected DM:NAME)", result.errorMessage);
    TEST_ASSERT_TRUE(result.errorEntry.present);
    TEST_ASSERT_EQUAL_STRING("pwm", result.errorEntry.source);
    TEST_ASSERT_EQUAL_STRING("dome_seq", result.errorEntry.action);
}

void test_rcMapApply_sm_diagnostic_payload_rejected(void) {
    std::map<std::string, std::string> m = {
        {"plain", "{\"map\":[{\"source\":\"pwm\",\"channel\":1,\"action\":\"cmd\",\"payload\":\":SM11\"}]}"}};
    ConfigSnapshot snap = makeDefaultSnap();
    RcMapApplyResult result;
    rcMapApply(makeSource(&m), &snap, &result);
    TEST_ASSERT_FALSE(result.ok);
    TEST_ASSERT_EQUAL_STRING(":SM is diagnostic only and cannot be saved as an RC binding",
                             result.errorMessage);
}

// --- conflict rules ---
void test_rcMapApply_duplicate_source_channel_rejected(void) {
    std::map<std::string, std::string> m = {
        {"plain",
         "{\"map\":["
         "{\"source\":\"pwm\",\"channel\":1,\"action\":\"drive_speed\"},"
         "{\"source\":\"pwm\",\"channel\":1,\"action\":\"arm1_toggle\"}"
         "]}"}};
    ConfigSnapshot snap = makeDefaultSnap();
    RcMapApplyResult result;
    rcMapApply(makeSource(&m), &snap, &result);
    TEST_ASSERT_FALSE(result.ok);
    TEST_ASSERT_EQUAL_STRING("conflict: source+channel mapped more than once", result.errorMessage);
}

void test_rcMapApply_duplicate_drive_speed_rejected(void) {
    std::map<std::string, std::string> m = {
        {"plain",
         "{\"map\":["
         "{\"source\":\"pwm\",\"channel\":1,\"action\":\"drive_speed\"},"
         "{\"source\":\"pwm\",\"channel\":2,\"action\":\"drive_speed\"}"
         "]}"}};
    ConfigSnapshot snap = makeDefaultSnap();
    RcMapApplyResult result;
    rcMapApply(makeSource(&m), &snap, &result);
    TEST_ASSERT_FALSE(result.ok);
    TEST_ASSERT_EQUAL_STRING("conflict: drive_speed mapped more than once", result.errorMessage);
}

// --- success path: backbone binding mirrors into both pwm+sbus slots ---
void test_rcMapApply_drive_speed_mirrors_pwm_and_sbus_slots(void) {
    std::map<std::string, std::string> m = {
        {"plain", "{\"map\":[{\"source\":\"pwm\",\"channel\":3,\"action\":\"drive_speed\"}]}"}};
    ConfigSnapshot snap = makeDefaultSnap();
    RcMapApplyResult result;
    rcMapApply(makeSource(&m), &snap, &result);
    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_EQUAL(RC_BINDING_PWM, snap.system.rc_pwm_drive_speed.source);
    TEST_ASSERT_EQUAL(3, snap.system.rc_pwm_drive_speed.channel);
    TEST_ASSERT_EQUAL(RC_BINDING_PWM, snap.system.rc_sbus_drive_speed.source);
    TEST_ASSERT_EQUAL(3, snap.system.rc_sbus_drive_speed.channel);
}

// --- success path: named trigger action fills its dedicated slot ---
void test_rcMapApply_arm1_toggle_fills_dedicated_slot(void) {
    std::map<std::string, std::string> m = {
        {"plain", "{\"map\":[{\"source\":\"sbus1\",\"channel\":5,\"action\":\"arm1_toggle\"}]}"}};
    ConfigSnapshot snap = makeDefaultSnap();
    RcMapApplyResult result;
    rcMapApply(makeSource(&m), &snap, &result);
    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_EQUAL(RC_BINDING_SBUS1, snap.system.rc_arm1.source);
    TEST_ASSERT_EQUAL(5, snap.system.rc_arm1.channel);
}

// --- success path: unnamed trigger action spills into first-free slot ---
void test_rcMapApply_unnamed_trigger_spills_to_first_free_slot(void) {
    std::map<std::string, std::string> m = {
        {"plain", "{\"map\":[{\"source\":\"sbus1\",\"channel\":7,\"action\":\"op_mode\"}]}"}};
    ConfigSnapshot snap = makeDefaultSnap();
    RcMapApplyResult result;
    rcMapApply(makeSource(&m), &snap, &result);
    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_EQUAL(RC_BINDING_SBUS1, snap.system.rc_opmode.source);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_rcMapApply_no_body_returns_error);
    RUN_TEST(test_rcMapApply_invalid_json_returns_error);
    RUN_TEST(test_rcMapApply_map_not_array_returns_error);
    RUN_TEST(test_rcMapApply_empty_map_succeeds);
    RUN_TEST(test_rcMapApply_invalid_source_rejected);
    RUN_TEST(test_rcMapApply_channel_out_of_range_rejected);
    RUN_TEST(test_rcMapApply_invalid_action_token_rejected);
    RUN_TEST(test_rcMapApply_invalid_dome_seq_payload_rejected);
    RUN_TEST(test_rcMapApply_sm_diagnostic_payload_rejected);
    RUN_TEST(test_rcMapApply_duplicate_source_channel_rejected);
    RUN_TEST(test_rcMapApply_duplicate_drive_speed_rejected);
    RUN_TEST(test_rcMapApply_drive_speed_mirrors_pwm_and_sbus_slots);
    RUN_TEST(test_rcMapApply_arm1_toggle_fills_dedicated_slot);
    RUN_TEST(test_rcMapApply_unnamed_trigger_spills_to_first_free_slot);
    return UNITY_END();
}
