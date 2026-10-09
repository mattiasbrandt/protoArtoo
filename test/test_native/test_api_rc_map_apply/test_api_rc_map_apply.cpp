// =============================================================================
// test/test_native/test_api_rc_map_apply/test_api_rc_map_apply.cpp
//
// Native unit tests for rcMapApply() (ADR 0011 Apply Core, slice 2).
// Exercises the pure validate/apply logic against a ConfigSnapshot through a
// std::map-backed ConfigParamSource carrying the "plain" JSON body, without
// FreeRTOS, any web-server type, or hardware dependencies.
// =============================================================================
#include <unity.h>

#include <cstring>
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

// A droid saved for two SBUS receivers, the factory receiver type: the map is
// judged for the type the droid has saved (rcRuleMapAdd()).
ConfigSnapshot makeDefaultSnap() {
    ConfigSnapshot snap = {};
    snap.system.rc_input_mode = RC_INPUT_DUAL_SBUS;
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
        {"plain", "{\"map\":[{\"source\":\"sbus1\",\"channel\":1,\"action\":\"cmd\",\"payload\":\":SM11\"}]}"}};
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
         "{\"source\":\"sbus1\",\"channel\":1,\"action\":\"drive_speed\"},"
         "{\"source\":\"sbus1\",\"channel\":1,\"action\":\"arm1_toggle\"}"
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
         "{\"source\":\"sbus1\",\"channel\":1,\"action\":\"drive_speed\"},"
         "{\"source\":\"sbus1\",\"channel\":2,\"action\":\"drive_speed\"}"
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
    snap.system.rc_input_mode = RC_INPUT_STANDARD_PWM;
    RcMapApplyResult result;
    rcMapApply(makeSource(&m), &snap, &result);
    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_EQUAL(RC_BINDING_PWM, snap.system.rc_pwm_drive_speed.source);
    TEST_ASSERT_EQUAL(3, snap.system.rc_pwm_drive_speed.channel);
    TEST_ASSERT_EQUAL(RC_BINDING_PWM, snap.system.rc_sbus_drive_speed.source);
    TEST_ASSERT_EQUAL(3, snap.system.rc_sbus_drive_speed.channel);
}

// --- axis calibration beside the map (#389) ---
static const char* kSbusDriveMap =
    "\"map\":[{\"source\":\"sbus1\",\"channel\":1,\"action\":\"drive_speed\"},"
    "{\"source\":\"sbus1\",\"channel\":2,\"action\":\"drive_steer\"}]";

static RcMapApplyResult applyBody(const std::string& body, ConfigSnapshot* snap) {
    std::map<std::string, std::string> m = {{"plain", body}};
    RcMapApplyResult result;
    rcMapApply(makeSource(&m), snap, &result);
    return result;
}

void test_rcMapApply_calibration_sets_an_axis_in_both_slots(void) {
    ConfigSnapshot snap = makeDefaultSnap();
    RcMapApplyResult result = applyBody(
        std::string("{") + kSbusDriveMap +
            ",\"calibration\":{\"drive_steer\":{\"min\":255,\"center\":1472,\"max\":1919,\"reverse\":true}}}",
        &snap);
    TEST_ASSERT_TRUE_MESSAGE(result.ok, result.errorMessage);
    TEST_ASSERT_EQUAL_UINT16(255, snap.system.rc_sbus_drive_steer.min);
    TEST_ASSERT_EQUAL_UINT16(1472, snap.system.rc_sbus_drive_steer.center);
    TEST_ASSERT_EQUAL_UINT16(1919, snap.system.rc_sbus_drive_steer.max);
    TEST_ASSERT_TRUE(snap.system.rc_sbus_drive_steer.reverse);
    TEST_ASSERT_EQUAL_UINT16(1472, snap.system.rc_pwm_drive_steer.center);
    // The axis the request did not calibrate keeps its defaults.
    TEST_ASSERT_EQUAL_UINT16(RC_SBUS_DEFAULT_CENTER, snap.system.rc_sbus_drive_speed.center);
    TEST_ASSERT_FALSE(snap.system.rc_sbus_drive_speed.reverse);
}

void test_rcMapApply_calibration_keeps_fields_it_leaves_out(void) {
    ConfigSnapshot snap = makeDefaultSnap();
    RcMapApplyResult result = applyBody(
        std::string("{") + kSbusDriveMap + ",\"calibration\":{\"drive_speed\":{\"reverse\":true}}}", &snap);
    TEST_ASSERT_TRUE_MESSAGE(result.ok, result.errorMessage);
    TEST_ASSERT_TRUE(snap.system.rc_sbus_drive_speed.reverse);
    TEST_ASSERT_EQUAL_UINT16(RC_SBUS_DEFAULT_MIN, snap.system.rc_sbus_drive_speed.min);
    TEST_ASSERT_EQUAL_UINT16(RC_SBUS_DEFAULT_MAX, snap.system.rc_sbus_drive_speed.max);
}

void test_rcMapApply_calibration_refuses_what_it_cannot_store(void) {
    const char* const bad[] = {
        ",\"calibration\":{\"drive_speed\":{\"min\":1500,\"center\":992}}}",   // min > center
        ",\"calibration\":{\"drive_speed\":{\"max\":4000}}}",                  // out of range
        ",\"calibration\":{\"drive_speed\":{\"reverse\":1}}}",                 // not a bool
        ",\"calibration\":{\"dome_speed\":{\"center\":992}}}",                 // axis not in the map
        ",\"calibration\":{\"arm1_toggle\":{\"center\":992}}}",                // not an axis
        ",\"calibration\":[]}",                                                // not an object
    };
    for (const char* tail : bad) {
        ConfigSnapshot snap = makeDefaultSnap();
        RcMapApplyResult result = applyBody(std::string("{") + kSbusDriveMap + tail, &snap);
        TEST_ASSERT_FALSE_MESSAGE(result.ok, tail);
    }
}

// A stored deadband the new calibration would leave wider than one side of the
// stick is refused: that side would map to nothing (Codex review, #389).
void test_rcMapApply_calibration_refuses_a_side_inside_the_deadband(void) {
    ConfigSnapshot snap = makeDefaultSnap();
    snap.system.rc_sbus_drive_speed = makeRcBindingConfig(RC_BINDING_SBUS1, 1, 172, 992, 1811, 100, false);
    RcMapApplyResult result = applyBody(
        std::string("{") + kSbusDriveMap + ",\"calibration\":{\"drive_speed\":{\"min\":950}}}", &snap);
    TEST_ASSERT_FALSE(result.ok);
    TEST_ASSERT_EQUAL_STRING("calibration leaves no travel past the deadband", result.errorMessage);

    ConfigSnapshot fits = makeDefaultSnap();
    fits.system.rc_sbus_drive_speed = makeRcBindingConfig(RC_BINDING_SBUS1, 1, 172, 992, 1811, 100, false);
    result = applyBody(
        std::string("{") + kSbusDriveMap + ",\"calibration\":{\"drive_speed\":{\"min\":800}}}", &fits);
    TEST_ASSERT_TRUE_MESSAGE(result.ok, result.errorMessage);
    TEST_ASSERT_EQUAL_UINT16(100, fits.system.rc_sbus_drive_speed.deadband);
}

// An axis the map keeps on its stored calibration is held to the same dead
// zone rule as one it calibrates: a stored dead zone that swallows a side
// would be saved and never read (ADR 0070).
void test_rcMapApply_refuses_a_kept_calibration_the_rules_do_not_take(void) {
    ConfigSnapshot snap = makeDefaultSnap();
    snap.system.rc_sbus_drive_speed = makeRcBindingConfig(RC_BINDING_SBUS1, 1, 172, 300, 1811, 200, false);
    RcMapApplyResult result = applyBody(std::string("{") + kSbusDriveMap + "}", &snap);
    TEST_ASSERT_FALSE(result.ok);
    TEST_ASSERT_EQUAL_STRING("calibration leaves no travel past the deadband", result.errorMessage);

    ConfigSnapshot fixed = makeDefaultSnap();
    fixed.system.rc_sbus_drive_speed = makeRcBindingConfig(RC_BINDING_SBUS1, 1, 172, 300, 1811, 200, false);
    result = applyBody(
        std::string("{") + kSbusDriveMap + ",\"calibration\":{\"drive_speed\":{\"center\":992}}}", &fixed);
    TEST_ASSERT_TRUE_MESSAGE(result.ok, result.errorMessage);
}

// The map is judged for the receiver type the droid has saved: one SBUS
// receiver reads SBUS1 only, so an SBUS2 entry is refused (ADR 0070).
void test_rcMapApply_refuses_a_receiver_the_saved_type_does_not_read(void) {
    ConfigSnapshot snap = makeDefaultSnap();
    snap.system.rc_input_mode = RC_INPUT_SINGLE_SBUS;
    RcMapApplyResult result =
        applyBody("{\"map\":[{\"source\":\"sbus2\",\"channel\":5,\"action\":\"arm1_toggle\"}]}", &snap);
    TEST_ASSERT_FALSE(result.ok);
    TEST_ASSERT_EQUAL_STRING("the RC Receiver type does not read this source", result.errorMessage);
    TEST_ASSERT_TRUE(result.errorEntry.present);
}

// Drive speed and steer are read from one frame of one receiver: a map that
// splits them could never drive, so it is refused (Codex review, #389).
void test_rcMapApply_refuses_drive_split_across_receivers(void) {
    ConfigSnapshot snap = makeDefaultSnap();
    RcMapApplyResult result = applyBody(
        "{\"map\":[{\"source\":\"sbus1\",\"channel\":1,\"action\":\"drive_speed\"},"
        "{\"source\":\"sbus2\",\"channel\":2,\"action\":\"drive_steer\"}]}",
        &snap);
    TEST_ASSERT_FALSE(result.ok);
    TEST_ASSERT_EQUAL_STRING("drive speed and steer must be on the same receiver", result.errorMessage);
}

// Only SBUS1 carries the drive watchdog and the hardware-failsafe stop, so a
// drive axis on SBUS2 is refused (operator, 2026-10-09 on #389). The dome may
// read SBUS2.
void test_rcMapApply_refuses_drive_on_sbus2(void) {
    ConfigSnapshot snap = makeDefaultSnap();
    RcMapApplyResult result = applyBody(
        "{\"map\":[{\"source\":\"sbus2\",\"channel\":1,\"action\":\"drive_speed\"},"
        "{\"source\":\"sbus2\",\"channel\":2,\"action\":\"drive_steer\"}]}",
        &snap);
    TEST_ASSERT_FALSE(result.ok);
    TEST_ASSERT_EQUAL_STRING("drive reads SBUS1, the drive receiver", result.errorMessage);

    ConfigSnapshot dome = makeDefaultSnap();
    result = applyBody("{\"map\":[{\"source\":\"sbus2\",\"channel\":1,\"action\":\"dome_speed\"}]}", &dome);
    TEST_ASSERT_TRUE_MESSAGE(result.ok, result.errorMessage);
}

// A refusal carries its field, reason and accepts beside the sentence, the
// keys every settings refusal carries (ADR 0070, docs/api.md).
void test_rcMapApply_refusal_carries_field_reason_and_accepts(void) {
    ConfigSnapshot snap = makeDefaultSnap();
    RcMapApplyResult result = applyBody(
        "{\"map\":[{\"source\":\"sbus2\",\"channel\":1,\"action\":\"drive_speed\"}]}", &snap);
    TEST_ASSERT_FALSE(result.ok);
    TEST_ASSERT_EQUAL_STRING("map.source", result.refusal.field);
    TEST_ASSERT_EQUAL_STRING("out-of-range", applyRefusalReasonToken(result.refusal.reason));
    TEST_ASSERT_EQUAL_STRING("sbus1", result.refusal.accepts);
    TEST_ASSERT_EQUAL_STRING("drive_speed", result.errorEntry.action);

    ConfigSnapshot cal = makeDefaultSnap();
    result = applyBody(std::string("{") + kSbusDriveMap + ",\"calibration\":{\"drive_steer\":{\"max\":4000}}}",
                       &cal);
    TEST_ASSERT_FALSE(result.ok);
    TEST_ASSERT_EQUAL_STRING("calibration.max", result.refusal.field);
    TEST_ASSERT_EQUAL_STRING("out-of-range", applyRefusalReasonToken(result.refusal.reason));
    TEST_ASSERT_EQUAL_STRING("0..2047", result.refusal.accepts);
    // The axis is said by the entry echoed beside it.
    TEST_ASSERT_TRUE(result.errorEntry.present);
    TEST_ASSERT_EQUAL_STRING("drive_steer", result.errorEntry.action);

    ConfigSnapshot dup = makeDefaultSnap();
    result = applyBody(
        "{\"map\":[{\"source\":\"sbus1\",\"channel\":5,\"action\":\"arm1_toggle\"},"
        "{\"source\":\"sbus1\",\"channel\":6,\"action\":\"arm1_toggle\"}]}",
        &dup);
    TEST_ASSERT_FALSE(result.ok);
    TEST_ASSERT_EQUAL_STRING("map.action", result.refusal.field);
    TEST_ASSERT_EQUAL_STRING("conflict", applyRefusalReasonToken(result.refusal.reason));
    TEST_ASSERT_EQUAL_STRING("", result.refusal.accepts);

    ConfigSnapshot none = makeDefaultSnap();
    std::map<std::string, std::string> empty;
    RcMapApplyResult missing;
    rcMapApply(makeSource(&empty), &none, &missing);
    TEST_ASSERT_EQUAL_STRING("map", missing.refusal.field);
    // A member of the wrong type is a value the field does not take; only a
    // body that did not parse is malformed, and it names "plain".
    ConfigSnapshot typed = makeDefaultSnap();
    result = applyBody("{\"map\":\"nope\"}", &typed);
    TEST_ASSERT_EQUAL_STRING("out-of-range", applyRefusalReasonToken(result.refusal.reason));
    ConfigSnapshot broken = makeDefaultSnap();
    result = applyBody("not json", &broken);
    TEST_ASSERT_EQUAL_STRING("plain", result.refusal.field);
    TEST_ASSERT_EQUAL_STRING("malformed-argument", applyRefusalReasonToken(result.refusal.reason));
    TEST_ASSERT_EQUAL_STRING("missing-argument", applyRefusalReasonToken(missing.refusal.reason));
}

// Every field the save names is one the words check holds to the browser's
// table (kRcMapRefusalFields), or the body itself ("plain"), so no refusal
// reaches the RC page as its wire name.
void test_rcMapApply_every_refusal_names_a_declared_field(void) {
    const char* const bodies[] = {
        "not json",
        "{\"map\":\"nope\"}",
        "{\"map\":[1]}",
        "{\"map\":[{\"source\":\"bogus\",\"channel\":1,\"action\":\"sound_next\"}]}",
        "{\"map\":[{\"source\":\"sbus1\",\"channel\":300,\"action\":\"sound_next\"}]}",
        "{\"map\":[{\"source\":\"sbus1\",\"channel\":1,\"action\":\"bogus\"}]}",
        "{\"map\":[{\"source\":\"sbus1\",\"channel\":1,\"action\":\"dome_seq\",\"payload\":\"DM:NOPE\"}]}",
        "{\"map\":[{\"source\":\"sbus1\",\"channel\":1,\"action\":\"puppet_part\",\"payload\":\"nope\"}]}",
        "{\"map\":[],\"calibration\":5}",
        "{\"map\":[],\"calibration\":{\"bogus\":{}}}",
        "{\"map\":[],\"calibration\":{\"drive_speed\":{}}}",
        "{\"map\":[{\"source\":\"sbus1\",\"channel\":1,\"action\":\"drive_speed\"}],\"calibration\":{\"drive_speed\":5}}",
        "{\"map\":[{\"source\":\"sbus1\",\"channel\":1,\"action\":\"drive_speed\"}],\"calibration\":{\"drive_speed\":{\"min\":9999}}}",
        "{\"map\":[{\"source\":\"sbus1\",\"channel\":1,\"action\":\"drive_speed\"}],\"calibration\":{\"drive_speed\":{\"reverse\":3}}}",
        "{\"map\":[{\"source\":\"sbus1\",\"channel\":5,\"action\":\"arm1_toggle\"},"
        "{\"source\":\"sbus1\",\"channel\":6,\"action\":\"arm1_toggle\"}]}",
    };
    for (const char* body : bodies) {
        ConfigSnapshot snap = makeDefaultSnap();
        RcMapApplyResult result = applyBody(body, &snap);
        TEST_ASSERT_FALSE_MESSAGE(result.ok, body);
        bool declared = strcmp(result.refusal.field, "plain") == 0;
        for (const char* field : kRcMapRefusalFields) {
            declared = declared || strcmp(field, result.refusal.field) == 0;
        }
        TEST_ASSERT_TRUE_MESSAGE(declared, result.refusal.field);
        TEST_ASSERT_TRUE_MESSAGE(result.refusal.reason != ApplyRefusalReason::None, body);
    }
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
    RUN_TEST(test_rcMapApply_calibration_sets_an_axis_in_both_slots);
    RUN_TEST(test_rcMapApply_calibration_keeps_fields_it_leaves_out);
    RUN_TEST(test_rcMapApply_calibration_refuses_what_it_cannot_store);
    RUN_TEST(test_rcMapApply_calibration_refuses_a_side_inside_the_deadband);
    RUN_TEST(test_rcMapApply_refuses_a_kept_calibration_the_rules_do_not_take);
    RUN_TEST(test_rcMapApply_refuses_a_receiver_the_saved_type_does_not_read);
    RUN_TEST(test_rcMapApply_refuses_drive_split_across_receivers);
    RUN_TEST(test_rcMapApply_refusal_carries_field_reason_and_accepts);
    RUN_TEST(test_rcMapApply_every_refusal_names_a_declared_field);
    RUN_TEST(test_rcMapApply_refuses_drive_on_sbus2);
    RUN_TEST(test_rcMapApply_arm1_toggle_fills_dedicated_slot);
    RUN_TEST(test_rcMapApply_unnamed_trigger_spills_to_first_free_slot);
    return UNITY_END();
}
