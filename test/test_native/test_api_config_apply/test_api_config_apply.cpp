// =============================================================================
// test/test_native/test_api_config_apply/test_api_config_apply.cpp
//
// Native unit tests for configApply() (ADR 0011 Apply Core, slice 1).
// Exercises the pure validate/apply logic against a ConfigSnapshot through a
// std::map-backed ConfigParamSource, without FreeRTOS, any web-server type,
// or hardware dependencies.
// =============================================================================
#include <unity.h>

#include <cstdio>
#include <cstring>
#include "board_outputs.h"
#include <map>
#include <string>

#include "api_config_apply.h"
#include "component_registry.h"
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
    TEST_ASSERT_EQUAL_STRING("rcInputMode must be standard_pwm, single_sbus, dual_sbus, or elrs",
                             result.error.message);
}

// The RC Radio is the Radio Controller's Component Member (#369): a registry id
// from that family that the registry calls selectable, and nothing else - a
// sound module and a roadmap radio are both refused, by the rule the picker's
// lineup comes from.
void test_configApply_rcMember_takes_a_radio_and_refuses_anything_else(void) {
    std::map<std::string, std::string> radio = {{"rcMember", "rc_radio"}};
    ConfigSnapshot snap = makeDefaultSnap();
    ConfigApplyResult result;
    configApply(makeSource(&radio), &snap, false, &result);
    TEST_ASSERT_FALSE(result.error.hasError);
    TEST_ASSERT_EQUAL_UINT8(componentPartById("rc_radio")->value, snap.system.rc_member);

    for (const char* refused : {"dy_sv5w", "xbox_controller", "nonsense"}) {
        std::map<std::string, std::string> m = {{"rcMember", refused}};
        ConfigSnapshot other = makeDefaultSnap();
        const uint8_t before = other.system.rc_member;
        ConfigApplyResult refusedResult;
        configApply(makeSource(&m), &other, false, &refusedResult);
        TEST_ASSERT_TRUE_MESSAGE(refusedResult.error.hasError, refused);
        TEST_ASSERT_EQUAL_STRING("rcMember is not a radio this firmware lists", refusedResult.error.message);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(before, other.system.rc_member, refused);
    }
}

// An ELRS receiver is stored like any other receiver type (#369).
void test_configApply_rcInputMode_accepts_elrs(void) {
    std::map<std::string, std::string> m = {{"rcInputMode", "elrs"}};
    ConfigSnapshot snap = makeDefaultSnap();
    ConfigApplyResult result;
    configApply(makeSource(&m), &snap, false, &result);
    TEST_ASSERT_FALSE(result.error.hasError);
    TEST_ASSERT_EQUAL_UINT8(RC_INPUT_ELRS, snap.system.rc_input_mode);
}

void test_configApply_protoR2linkWifiPeerIp_invalid_ipv4_reject(void) {
    std::map<std::string, std::string> m = {{"protoR2linkWifiPeerIp", "not.an.ip"}};
    ConfigSnapshot snap = makeDefaultSnap();
    ConfigApplyResult result;
    configApply(makeSource(&m), &snap, false, &result);
    TEST_ASSERT_TRUE(result.error.hasError);
    TEST_ASSERT_EQUAL_STRING("protoR2linkWifiPeerIp must be empty or a valid IPv4 address",
                             result.error.message);
}

void test_configApply_protoR2linkWifiPeerIp_empty_clears(void) {
    std::map<std::string, std::string> m = {{"protoR2linkWifiPeerIp", ""}};
    ConfigSnapshot snap = makeDefaultSnap();
    strncpy(snap.dome.dome_wifi_peer_ip, "10.0.0.5", sizeof(snap.dome.dome_wifi_peer_ip));
    ConfigApplyResult result;
    configApply(makeSource(&m), &snap, false, &result);
    TEST_ASSERT_FALSE(result.error.hasError);
    TEST_ASSERT_EQUAL_STRING("", snap.dome.dome_wifi_peer_ip);
}

// What is on an Output's wire arrives as its row's `component` (ADR 0068).
void test_configApply_servoType_named_value_updates(void) {
    std::map<std::string, std::string> m = {
        {"plain", "{\"outputs\":[{\"address\":\"ledc:0\",\"component\":\"mg90s\"}]}"}};
    ConfigSnapshot snap = makeDefaultSnap();
    ConfigApplyResult result;
    configApply(makeSource(&m), &snap, false, &result);
    TEST_ASSERT_FALSE(result.error.hasError);
    // Nothing lands on the snapshot: since #345 an endpoint and the component
    // type beside it live on an addressed Servo Output row, and this core is
    // pure, so what it produces is one addressed edit for the Commit Step.
    TEST_ASSERT_EQUAL_size_t(1, result.servoOutputs.count);
    TEST_ASSERT_EQUAL_UINT8(LEDC_CH_ARM1, result.servoOutputs.edits[0].channel);
    TEST_ASSERT_EQUAL_UINT16(SERVO_FIELD_COMPONENT, result.servoOutputs.edits[0].fields);
    TEST_ASSERT_EQUAL_UINT8(SERVO_COMP_MG90S, result.servoOutputs.edits[0].component);
}

// One Output Address, one edit, however many of its fields the row carried -
// the component type has to be settled against the pair it will clamp, not by
// whichever key the row happened to list first.
void test_configApply_servo_endpoints_and_type_become_one_addressed_edit(void) {
    std::map<std::string, std::string> m = {
        {"plain", "{\"outputs\":[{\"address\":\"ledc:3\",\"openUs\":1800,"
                  "\"closeUs\":1200,\"component\":\"mg90s\"}]}"}};
    ConfigSnapshot snap = makeDefaultSnap();
    ConfigApplyResult result;
    configApply(makeSource(&m), &snap, false, &result);
    TEST_ASSERT_FALSE(result.error.hasError);
    TEST_ASSERT_EQUAL_size_t(1, result.servoOutputs.count);

    const ServoOutputEdit& edit = result.servoOutputs.edits[0];
    TEST_ASSERT_EQUAL_UINT8(SERVO_DRIVER_LEDC, edit.driver);
    TEST_ASSERT_EQUAL_UINT8(LEDC_CH_AUX1, edit.channel);
    TEST_ASSERT_EQUAL_UINT16(
        (uint16_t)(SERVO_FIELD_OPEN | SERVO_FIELD_CLOSE | SERVO_FIELD_COMPONENT), edit.fields);
    TEST_ASSERT_EQUAL_UINT16(1800, edit.open_us);
    TEST_ASSERT_EQUAL_UINT16(1200, edit.close_us);
    TEST_ASSERT_EQUAL_UINT8(SERVO_COMP_MG90S, edit.component);
}

// A request that names one end names one field, and the row keeps the other.
// The mask is what says so, so an absent parameter cannot arrive as a zero.
void test_configApply_one_endpoint_edits_only_that_field(void) {
    std::map<std::string, std::string> m = {
        {"plain", "{\"outputs\":[{\"address\":\"ledc:1\",\"openUs\":1750}]}"}};
    ConfigSnapshot snap = makeDefaultSnap();
    ConfigApplyResult result;
    configApply(makeSource(&m), &snap, false, &result);
    TEST_ASSERT_FALSE(result.error.hasError);
    TEST_ASSERT_EQUAL_size_t(1, result.servoOutputs.count);
    TEST_ASSERT_EQUAL_UINT8(LEDC_CH_ARM2, result.servoOutputs.edits[0].channel);
    TEST_ASSERT_EQUAL_UINT16(SERVO_FIELD_OPEN, result.servoOutputs.edits[0].fields);
    TEST_ASSERT_EQUAL_UINT16(1750, result.servoOutputs.edits[0].open_us);
}

// A request that names no servo parameter produces no edit at all, so the
// Commit Step has nothing to push over the rows it just loaded.
void test_configApply_without_servo_params_records_no_edit(void) {
    std::map<std::string, std::string> m = {{"logLevel", "3"}};
    ConfigSnapshot snap = makeDefaultSnap();
    ConfigApplyResult result;
    configApply(makeSource(&m), &snap, false, &result);
    TEST_ASSERT_FALSE(result.error.hasError);
    TEST_ASSERT_EQUAL_size_t(0, result.servoOutputs.count);
}

// A capture (#364): one Output Address, one position on it, and the width the
// dial was standing at. It becomes an addressed edit like any other, so it
// reaches the rows through the door the Commit Step already opens - marked as a
// capture, which is what makes it say a human measured this Output.
void test_configApply_capture_becomes_one_addressed_capture_edit(void) {
    std::map<std::string, std::string> m = {
        {"captureOutput", "ledc:3"}, {"captureEnd", "open"}, {"captureUs", "1850"}};
    ConfigSnapshot snap = makeDefaultSnap();
    ConfigApplyResult result;
    configApply(makeSource(&m), &snap, false, &result);
    TEST_ASSERT_FALSE(result.error.hasError);
    TEST_ASSERT_EQUAL_size_t(1, result.servoOutputs.count);

    const ServoOutputEdit& edit = result.servoOutputs.edits[0];
    TEST_ASSERT_EQUAL_UINT8(SERVO_EDIT_CAPTURE, edit.kind);
    TEST_ASSERT_EQUAL_UINT8(SERVO_DRIVER_LEDC, edit.driver);
    TEST_ASSERT_EQUAL_UINT8(LEDC_CH_AUX1, edit.channel);
    TEST_ASSERT_EQUAL_UINT16(SERVO_FIELD_OPEN, edit.fields);
    TEST_ASSERT_EQUAL_UINT16(1850, edit.open_us);
}

// The centre is a position a dial captures into, not one a form types.
void test_configApply_capture_records_a_centre_on_its_own_field(void) {
    std::map<std::string, std::string> m = {
        {"captureOutput", "ledc:0"}, {"captureEnd", "centre"}, {"captureUs", "1490"}};
    ConfigSnapshot snap = makeDefaultSnap();
    ConfigApplyResult result;
    configApply(makeSource(&m), &snap, false, &result);
    TEST_ASSERT_FALSE(result.error.hasError);
    TEST_ASSERT_EQUAL_UINT16(SERVO_FIELD_CENTRE, result.servoOutputs.edits[0].fields);
    TEST_ASSERT_EQUAL_UINT16(1490, result.servoOutputs.edits[0].centre_us);
}

// All three fields or none, the shape a Part move already uses: they mean
// nothing apart. An address with no position is not a capture.
void test_configApply_a_half_stated_capture_is_refused(void) {
    std::map<std::string, std::string> m = {{"captureOutput", "ledc:0"}, {"captureUs", "1500"}};
    ConfigSnapshot snap = makeDefaultSnap();
    ConfigApplyResult result;
    configApply(makeSource(&m), &snap, false, &result);
    TEST_ASSERT_TRUE(result.error.hasError);
    TEST_ASSERT_NOT_NULL(strstr(result.error.message, "captureEnd"));
    TEST_ASSERT_EQUAL_size_t(0, result.servoOutputs.count);
}

// An address the driver does not have is not an Output, however well spelled.
// ledc:2 is the dome ESC, which is not addressable as a servo.
void test_configApply_a_capture_at_a_non_servo_address_is_refused(void) {
    std::map<std::string, std::string> m = {
        {"captureOutput", "ledc:2"}, {"captureEnd", "close"}, {"captureUs", "1500"}};
    ConfigSnapshot snap = makeDefaultSnap();
    ConfigApplyResult result;
    configApply(makeSource(&m), &snap, false, &result);
    TEST_ASSERT_TRUE(result.error.hasError);
    TEST_ASSERT_EQUAL_size_t(0, result.servoOutputs.count);
}

// A width no servo takes is refused here; what an Output KEEPS is then bounded
// again by the component fitted to it, on the row, where a moved number can be
// reported.
void test_configApply_a_capture_outside_what_a_servo_takes_is_refused(void) {
    std::map<std::string, std::string> m = {
        {"captureOutput", "ledc:0"}, {"captureEnd", "open"}, {"captureUs", "2600"}};
    ConfigSnapshot snap = makeDefaultSnap();
    ConfigApplyResult result;
    configApply(makeSource(&m), &snap, false, &result);
    TEST_ASSERT_TRUE(result.error.hasError);
}

// An end this model does not have is not a position. "min" and "max" are what
// the buttons say; open and close are what the row records.
void test_configApply_an_unknown_captured_end_is_refused(void) {
    std::map<std::string, std::string> m = {
        {"captureOutput", "ledc:0"}, {"captureEnd", "middle"}, {"captureUs", "1500"}};
    ConfigSnapshot snap = makeDefaultSnap();
    ConfigApplyResult result;
    configApply(makeSource(&m), &snap, false, &result);
    TEST_ASSERT_TRUE(result.error.hasError);
}

// A capture and a row can ride one request without colliding: the capture is
// an edit of its own, addressed at any Output.
void test_configApply_a_capture_rides_beside_a_row_edit(void) {
    std::map<std::string, std::string> m = {
        {"plain", "{\"outputs\":[{\"address\":\"ledc:0\",\"openUs\":1900}]}"},
                                            {"captureOutput", "ledc:5"},
                                            {"captureEnd", "close"},
                                            {"captureUs", "1050"}};
    ConfigSnapshot snap = makeDefaultSnap();
    ConfigApplyResult result;
    configApply(makeSource(&m), &snap, false, &result);
    TEST_ASSERT_FALSE(result.error.hasError);
    TEST_ASSERT_EQUAL_size_t(2, result.servoOutputs.count);
    TEST_ASSERT_EQUAL_UINT8(SERVO_EDIT_TYPED, result.servoOutputs.edits[0].kind);
    TEST_ASSERT_EQUAL_UINT8(LEDC_CH_ARM1, result.servoOutputs.edits[0].channel);
    TEST_ASSERT_EQUAL_UINT8(SERVO_EDIT_CAPTURE, result.servoOutputs.edits[1].kind);
    TEST_ASSERT_EQUAL_UINT8(LEDC_CH_AUX3, result.servoOutputs.edits[1].channel);
}

// Reverse (#364): one Output Address, no widths. A page cannot write a stale
// pair back through it, and it cannot be used to type a number.
void test_configApply_reverse_is_one_addressed_act_with_no_widths(void) {
    std::map<std::string, std::string> m = {{"reverseOutput", "ledc:1"}};
    ConfigSnapshot snap = makeDefaultSnap();
    ConfigApplyResult result;
    configApply(makeSource(&m), &snap, false, &result);
    TEST_ASSERT_FALSE(result.error.hasError);
    TEST_ASSERT_EQUAL_size_t(1, result.servoOutputs.count);
    TEST_ASSERT_EQUAL_UINT8(SERVO_EDIT_REVERSE, result.servoOutputs.edits[0].kind);
    TEST_ASSERT_EQUAL_UINT8(LEDC_CH_ARM2, result.servoOutputs.edits[0].channel);
    TEST_ASSERT_EQUAL_UINT16(0, result.servoOutputs.edits[0].fields);
}

void test_configApply_reverse_at_a_non_address_is_refused(void) {
    std::map<std::string, std::string> m = {{"reverseOutput", "none"}};
    ConfigSnapshot snap = makeDefaultSnap();
    ConfigApplyResult result;
    configApply(makeSource(&m), &snap, false, &result);
    TEST_ASSERT_TRUE(result.error.hasError);
    TEST_ASSERT_EQUAL_size_t(0, result.servoOutputs.count);
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
    std::map<std::string, std::string> m = {{"enableDomeEsc", "1"}};
    ConfigSnapshot snap = makeDefaultSnap();
    snap.system.enable_dome_esc = false;
    ConfigApplyResult result;
    configApply(makeSource(&m), &snap, /*domeEnabledBefore=*/false, &result);
    TEST_ASSERT_FALSE(result.error.hasError);
    TEST_ASSERT_TRUE(result.actions.playDomeOnCue);
}

void test_configApply_dome_already_enabled_no_cue(void) {
    std::map<std::string, std::string> m = {{"enableDomeEsc", "1"}};
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

// A value in the GET shape reaches the same check as its form name, so it is
// refused in the same words, about the same field (ADR 0068): there is one range
// per field, not one per door.
void test_configApply_json_body_is_refused_by_the_form_fields_own_check(void) {
    std::map<std::string, std::string> m = {{"plain", "{\"rc\":{\"sbusTimeoutMs\":10}}"}};
    ConfigSnapshot snap = makeDefaultSnap();
    const uint32_t before = snap.drive.sbusTimeoutMs;
    ConfigApplyResult result;
    configApply(makeSource(&m), &snap, false, &result);
    TEST_ASSERT_TRUE(result.error.hasError);
    TEST_ASSERT_EQUAL_STRING("sbusTimeoutMs must be 50..5000", result.error.message);
    TEST_ASSERT_EQUAL_STRING("sbusTimeoutMs", result.error.refusal.field);
    TEST_ASSERT_EQUAL_STRING("50..5000", result.error.refusal.accepts);
    TEST_ASSERT_EQUAL_UINT32(before, snap.drive.sbusTimeoutMs);
}

// A value no form field could hold - an object where a peer IP belongs - is
// refused, never read as the empty string that would clear the stored address.
void test_configApply_json_body_object_where_a_value_belongs_is_refused(void) {
    std::map<std::string, std::string> m = {
        {"plain", "{\"protoR2link\":{\"wifiPeerIp\":{\"ip\":\"10.0.0.2\"}}}"}};
    ConfigSnapshot snap = makeDefaultSnap();
    snprintf(snap.dome.dome_wifi_peer_ip, sizeof(snap.dome.dome_wifi_peer_ip), "%s", "10.0.0.9");
    ConfigApplyResult result;
    configApply(makeSource(&m), &snap, false, &result);
    TEST_ASSERT_TRUE(result.error.hasError);
    TEST_ASSERT_EQUAL_STRING("protoR2linkWifiPeerIp", result.error.refusal.field);
    TEST_ASSERT_EQUAL_STRING("10.0.0.9", snap.dome.dome_wifi_peer_ip);
}

void test_configApply_json_body_invalid_json_rejected(void) {
    std::map<std::string, std::string> m = {{"plain", "not json"}};
    ConfigSnapshot snap = makeDefaultSnap();
    ConfigApplyResult result;
    configApply(makeSource(&m), &snap, false, &result);
    TEST_ASSERT_TRUE(result.error.hasError);
    TEST_ASSERT_EQUAL_STRING("invalid json body", result.error.message);
}

// An empty list is an answer, not an absence (#351, #371): a run that has shown
// nothing yet, or a droid with nothing fitted. In the GET shape it arrives as
// `[]`, and a restore that read it as "not sent" would leave the droid claiming
// steps it was never shown and Parts nobody fitted.
void test_configApply_an_empty_get_shape_list_is_an_answer(void) {
    std::map<std::string, std::string> m = {
        {"plain", "{\"guidedSetup\":{\"visited\":[]},\"droidBuild\":{\"fitted\":[]}}"}};
    ConfigSnapshot snap = makeDefaultSnap();
    ConfigApplyResult result;
    configApply(makeSource(&m), &snap, false, &result);
    TEST_ASSERT_FALSE(result.error.hasError);
    TEST_ASSERT_TRUE(result.guidedSetup.visitedChanged);
    TEST_ASSERT_TRUE(result.guidedSetup.visited.recorded);
    TEST_ASSERT_EQUAL_STRING("", result.guidedSetup.visited.visited);
    TEST_ASSERT_TRUE(result.droidBuild.fittedChanged);
    TEST_ASSERT_EQUAL_size_t(0, droidFittedPartsCount(result.droidBuild.fitted));
}

// A light's LED count is one per Output (#413), a field of its row. A value
// outside the band says which Output it was about, because a restore carrying
// three of them needs to know which one it got wrong.
void test_configApply_led_count_out_of_range_names_its_output(void) {
    std::map<std::string, std::string> m = {
        {"plain", "{\"outputs\":[{\"address\":\"ledc:4\",\"ledCount\":0}]}"}};
    ConfigSnapshot snap = makeDefaultSnap();
    ConfigApplyResult result;
    configApply(makeSource(&m), &snap, false, &result);
    TEST_ASSERT_TRUE(result.error.hasError);
    TEST_ASSERT_EQUAL_STRING("ledc:4.ledCount", result.error.refusal.field);
    TEST_ASSERT_EQUAL_STRING("1..255", result.error.refusal.accepts);
}

// A word that is not one of the four is refused, not read as `none`: a typo
// must never quietly take a servo off a wire.
void test_configApply_an_unknown_component_word_is_refused(void) {
    std::map<std::string, std::string> m = {
        {"plain", "{\"outputs\":[{\"address\":\"ledc:0\",\"component\":\"mg995\"}]}"}};
    ConfigSnapshot snap = makeDefaultSnap();
    ConfigApplyResult result;
    configApply(makeSource(&m), &snap, false, &result);
    TEST_ASSERT_TRUE(result.error.hasError);
    TEST_ASSERT_EQUAL_STRING("ledc:0.component", result.error.refusal.field);
    TEST_ASSERT_EQUAL_size_t(0, result.servoOutputs.count);
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

// Every scalar a request sets leaves a line, the dome ESC set and the peer IP
// included - they once changed the stored config without one.
void test_configApply_every_scalar_records_an_applied_line(void) {
    std::map<std::string, std::string> m = {
        {"sbusRecvCh2", "true"},          {"domeEscNeutralUs", "1500"},
        {"domeEscMinPulseUs", "1100"},    {"domeEscMaxPulseUs", "1900"},
        {"domeEscSpeedLimitPct", "40"},   {"protoR2linkWifiPeerIp", "192.168.4.2"},
    };
    ConfigSnapshot snap = makeDefaultSnap();
    ConfigApplyResult result;
    configApply(makeSource(&m), &snap, false, &result);
    TEST_ASSERT_FALSE(result.error.hasError);
    TEST_ASSERT_EQUAL(6, result.applied.count);
    TEST_ASSERT_EQUAL_STRING("[CFG] sbusRecvCh2 updated to true", result.applied.lines[0]);
    TEST_ASSERT_EQUAL_STRING("[CFG] domeEscNeutralUs updated to 1500", result.applied.lines[1]);
    TEST_ASSERT_EQUAL_STRING("[CFG] domeEscMinPulseUs updated to 1100", result.applied.lines[2]);
    TEST_ASSERT_EQUAL_STRING("[CFG] domeEscMaxPulseUs updated to 1900", result.applied.lines[3]);
    TEST_ASSERT_EQUAL_STRING("[CFG] domeEscSpeedLimitPct updated to 40", result.applied.lines[4]);
    TEST_ASSERT_EQUAL_STRING("[CFG] protoR2linkWifiPeerIp updated to 192.168.4.2", result.applied.lines[5]);
}

void test_configApply_cleared_peer_ip_records_none(void) {
    std::map<std::string, std::string> m = {{"protoR2linkWifiPeerIp", ""}};
    ConfigSnapshot snap = makeDefaultSnap();
    ConfigApplyResult result;
    configApply(makeSource(&m), &snap, false, &result);
    TEST_ASSERT_FALSE(result.error.hasError);
    TEST_ASSERT_EQUAL(1, result.applied.count);
    TEST_ASSERT_EQUAL_STRING("[CFG] protoR2linkWifiPeerIp updated to (none)", result.applied.lines[0]);
}

// --- the Droid Build (ADR 0047) ---
//
// The Apply Core is the door a stated Droid Build comes through, and there are
// exactly three ways it could undo the decision it implements: fence the Parts,
// refuse a mixed droid, or take half an answer.

void test_configApply_droid_build_records_both_halves_and_the_parts(void) {
    std::map<std::string, std::string> m;
    m["domeDesign"] = "mk4";
    m["domeVariant"] = "complex";
    m["bodyDesign"] = "own";
    m["bodyVariant"] = "";
    m["fittedParts"] = "utilUp,utilLo,gripArm";
    ConfigSnapshot snap = makeDefaultSnap();
    ConfigApplyResult result;
    configApply(makeSource(&m), &snap, false, &result);

    TEST_ASSERT_FALSE(result.error.hasError);
    TEST_ASSERT_TRUE(result.changed);
    TEST_ASSERT_TRUE(result.droidBuild.domeChanged);
    TEST_ASSERT_TRUE(result.droidBuild.bodyChanged);
    TEST_ASSERT_TRUE(result.droidBuild.fittedChanged);
    TEST_ASSERT_EQUAL_STRING("mk4", result.droidBuild.dome.design);
    TEST_ASSERT_EQUAL_STRING("complex", result.droidBuild.dome.variant);
    TEST_ASSERT_EQUAL_STRING("own", result.droidBuild.body.design);
    TEST_ASSERT_EQUAL_STRING("", result.droidBuild.body.variant);
    TEST_ASSERT_EQUAL_UINT32(3u, (uint32_t)droidFittedPartsCount(result.droidBuild.fitted));
    TEST_ASSERT_TRUE(
        droidFittedPartsHasIndex(result.droidBuild.fitted, droidPartIndexOf("gripArm")));
}

void test_configApply_a_mixed_droid_saves_without_complaint(void) {
    // A dome from one design and a body from another. Neither half gates the
    // other, and a real droid is a mixture.
    std::map<std::string, std::string> m;
    m["domeDesign"] = "mk4";
    m["domeVariant"] = "complex";
    m["bodyDesign"] = "own";
    m["bodyVariant"] = "";
    ConfigSnapshot snap = makeDefaultSnap();
    ConfigApplyResult result;
    configApply(makeSource(&m), &snap, false, &result);

    TEST_ASSERT_FALSE(result.error.hasError);
    TEST_ASSERT_TRUE(result.droidBuild.domeChanged);
    TEST_ASSERT_TRUE(result.droidBuild.bodyChanged);
}

void test_configApply_a_design_without_its_variant_is_refused(void) {
    std::map<std::string, std::string> m;
    m["domeDesign"] = "mk4";  // and no domeVariant
    ConfigSnapshot snap = makeDefaultSnap();
    ConfigApplyResult result;
    configApply(makeSource(&m), &snap, false, &result);

    TEST_ASSERT_TRUE(result.error.hasError);
    TEST_ASSERT_FALSE(result.droidBuild.domeChanged);
}

void test_configApply_a_variant_that_is_not_that_design_s_is_refused(void) {
    std::map<std::string, std::string> m;
    m["domeDesign"] = "own";      // declares no variants
    m["domeVariant"] = "complex";
    ConfigSnapshot snap = makeDefaultSnap();
    ConfigApplyResult result;
    configApply(makeSource(&m), &snap, false, &result);

    TEST_ASSERT_TRUE(result.error.hasError);
    TEST_ASSERT_FALSE(result.droidBuild.domeChanged);
}

void test_configApply_a_design_this_build_does_not_declare_is_refused(void) {
    std::map<std::string, std::string> m;
    m["domeDesign"] = "mk9";
    m["domeVariant"] = "complex";
    ConfigSnapshot snap = makeDefaultSnap();
    ConfigApplyResult result;
    configApply(makeSource(&m), &snap, false, &result);

    TEST_ASSERT_TRUE(result.error.hasError);
}

void test_configApply_an_empty_fitted_list_is_an_answer(void) {
    // A builder saying their droid carries nothing yet is different from a
    // request that said nothing about the Fitted Parts at all.
    std::map<std::string, std::string> m;
    m["fittedParts"] = "";
    ConfigSnapshot snap = makeDefaultSnap();
    ConfigApplyResult result;
    configApply(makeSource(&m), &snap, false, &result);

    TEST_ASSERT_FALSE(result.error.hasError);
    TEST_ASSERT_TRUE(result.droidBuild.fittedChanged);
    TEST_ASSERT_EQUAL_UINT32(0u, (uint32_t)droidFittedPartsCount(result.droidBuild.fitted));
}

void test_configApply_a_part_this_build_cannot_name_is_refused(void) {
    std::map<std::string, std::string> m;
    m["fittedParts"] = "utilUp,periscope";
    ConfigSnapshot snap = makeDefaultSnap();
    ConfigApplyResult result;
    configApply(makeSource(&m), &snap, false, &result);

    TEST_ASSERT_TRUE(result.error.hasError);
    TEST_ASSERT_FALSE(result.droidBuild.fittedChanged);
}

void test_configApply_without_droid_build_params_records_no_edit(void) {
    std::map<std::string, std::string> m;
    m["logLevel"] = "3";
    ConfigSnapshot snap = makeDefaultSnap();
    ConfigApplyResult result;
    configApply(makeSource(&m), &snap, false, &result);

    TEST_ASSERT_FALSE(result.error.hasError);
    TEST_ASSERT_FALSE(result.droidBuild.domeChanged);
    TEST_ASSERT_FALSE(result.droidBuild.bodyChanged);
    TEST_ASSERT_FALSE(result.droidBuild.fittedChanged);
}

// --- Part moves (ADR 0050, #347) ---

// The core records a move for the Commit Step and applies nothing itself: it
// cannot see where the Part is, so it cannot decide whether the move lands.
void test_configApply_a_part_move_is_recorded_for_the_commit_step(void) {
    std::map<std::string, std::string> m = {
        {"movePart", "doorFL"}, {"movePartFrom", "none"}, {"movePartTo", "ledc:3"}};
    ConfigSnapshot snap = makeDefaultSnap();
    ConfigApplyResult result;
    configApply(makeSource(&m), &snap, false, &result);

    TEST_ASSERT_FALSE(result.error.hasError);
    TEST_ASSERT_TRUE(result.changed);
    TEST_ASSERT_TRUE(result.partMove.requested);
    TEST_ASSERT_EQUAL_STRING("doorFL", result.partMove.move.part);
    TEST_ASSERT_FALSE(result.partMove.move.fromOutput);
    TEST_ASSERT_TRUE(result.partMove.move.toOutput);
    TEST_ASSERT_EQUAL_UINT8(SERVO_DRIVER_LEDC, result.partMove.move.toDriver);
    TEST_ASSERT_EQUAL_UINT8(LEDC_CH_AUX1, result.partMove.move.toChannel);
    TEST_ASSERT_EQUAL_STRING("[CFG] movePart doorFL to ledc:3", result.applied.lines[0]);
}

// Without its origin a move cannot say what it takes a Part away from, and an
// end that is not an Output this driver has is not an end.
void test_configApply_a_part_move_missing_or_misspelling_an_end_is_refused(void) {
    const std::map<std::string, std::string> kRefused[] = {
        {{"movePart", "doorFL"}, {"movePartTo", "ledc:3"}},
        {{"movePart", "doorFL"}, {"movePartFrom", "none"}},
        {{"movePartFrom", "none"}, {"movePartTo", "ledc:3"}},
        {{"movePart", "doorFL"}, {"movePartFrom", "none"}, {"movePartTo", "ledc:2"}},
        {{"movePart", "doorFL"}, {"movePartFrom", "AUX1"}, {"movePartTo", "none"}},
        {{"movePart", "banana"}, {"movePartFrom", "none"}, {"movePartTo", "ledc:3"}},
        {{"movePart", ""}, {"movePartFrom", "none"}, {"movePartTo", "ledc:3"}},
    };
    for (const auto& fields : kRefused) {
        std::map<std::string, std::string> m = fields;
        ConfigSnapshot snap = makeDefaultSnap();
        ConfigApplyResult result;
        configApply(makeSource(&m), &snap, false, &result);
        TEST_ASSERT_TRUE(result.error.hasError);
        TEST_ASSERT_FALSE(result.partMove.requested);
        TEST_ASSERT_EQUAL_STRING("movePart, movePartFrom and movePartTo must be sent together: a "
                                 "Part this build models, and each end an Output Address or none",
                                 result.error.message);
    }
}

// --- the dome ESC pulse set is judged as a set (#417) ---
//
// Each width passing 1000..2000 on its own is not enough: out of order, speed 0
// is not a stop (include/dome_math.h). A partial POST is judged against the two
// it will be stored beside, which is the case the Dome page's own check cannot
// reach - a direct POST, or a restore.
void test_configApply_an_out_of_order_dome_pulse_set_is_refused(void) {
    std::map<std::string, std::string> full = {
        {"domeEscMinPulseUs", "1800"}, {"domeEscNeutralUs", "1500"}, {"domeEscMaxPulseUs", "1200"}};
    ConfigSnapshot snap = makeDefaultSnap();
    snap.dome.dome_min_pulse_us = 1000;
    snap.dome.dome_neutral_us = 1500;
    snap.dome.dome_max_pulse_us = 2000;
    ConfigApplyResult result;
    configApply(makeSource(&full), &snap, false, &result);
    TEST_ASSERT_TRUE(result.error.hasError);
    TEST_ASSERT_EQUAL_STRING(
        "domeEscMinPulseUs 1800, domeEscNeutralUs 1500, domeEscMaxPulseUs 1200: "
        "must be min <= neutral <= max",
        result.error.message);

    // One field, legal alone, that puts neutral above the stored max.
    std::map<std::string, std::string> partial = {{"domeEscMaxPulseUs", "1400"}};
    ConfigSnapshot stored = makeDefaultSnap();
    stored.dome.dome_min_pulse_us = 1000;
    stored.dome.dome_neutral_us = 1500;
    stored.dome.dome_max_pulse_us = 2000;
    ConfigApplyResult partialResult;
    configApply(makeSource(&partial), &stored, false, &partialResult);
    TEST_ASSERT_TRUE(partialResult.error.hasError);
    TEST_ASSERT_EQUAL_STRING(
        "domeEscMinPulseUs 1000, domeEscNeutralUs 1500, domeEscMaxPulseUs 1400: "
        "must be min <= neutral <= max",
        partialResult.error.message);

    // An ordered set, neutral on an end, still goes through.
    std::map<std::string, std::string> ordered = {{"domeEscNeutralUs", "2000"}};
    ConfigSnapshot fine = makeDefaultSnap();
    fine.dome.dome_min_pulse_us = 1000;
    fine.dome.dome_neutral_us = 1500;
    fine.dome.dome_max_pulse_us = 2000;
    ConfigApplyResult okResult;
    configApply(makeSource(&ordered), &fine, false, &okResult);
    TEST_ASSERT_FALSE(okResult.error.hasError);
    TEST_ASSERT_EQUAL_UINT16(2000, fine.dome.dome_neutral_us);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_configApply_a_part_move_is_recorded_for_the_commit_step);
    RUN_TEST(test_configApply_a_part_move_missing_or_misspelling_an_end_is_refused);
    RUN_TEST(test_configApply_no_fields_supplied_returns_error);
    RUN_TEST(test_configApply_speedLimitMax_updates_and_logs);
    RUN_TEST(test_configApply_speedLimitMax_out_of_range_rejected);
    RUN_TEST(test_configApply_stationary_bool_reject);
    RUN_TEST(test_configApply_rcInputMode_enum_reject);
    RUN_TEST(test_configApply_rcMember_takes_a_radio_and_refuses_anything_else);
    RUN_TEST(test_configApply_rcInputMode_accepts_elrs);
    RUN_TEST(test_configApply_protoR2linkWifiPeerIp_invalid_ipv4_reject);
    RUN_TEST(test_configApply_protoR2linkWifiPeerIp_empty_clears);
    RUN_TEST(test_configApply_servoType_named_value_updates);
    RUN_TEST(test_configApply_servo_endpoints_and_type_become_one_addressed_edit);
    RUN_TEST(test_configApply_one_endpoint_edits_only_that_field);
    RUN_TEST(test_configApply_without_servo_params_records_no_edit);
    RUN_TEST(test_configApply_capture_becomes_one_addressed_capture_edit);
    RUN_TEST(test_configApply_capture_records_a_centre_on_its_own_field);
    RUN_TEST(test_configApply_a_half_stated_capture_is_refused);
    RUN_TEST(test_configApply_a_capture_at_a_non_servo_address_is_refused);
    RUN_TEST(test_configApply_a_capture_outside_what_a_servo_takes_is_refused);
    RUN_TEST(test_configApply_an_unknown_captured_end_is_refused);
    RUN_TEST(test_configApply_a_capture_rides_beside_a_row_edit);
    RUN_TEST(test_configApply_reverse_is_one_addressed_act_with_no_widths);
    RUN_TEST(test_configApply_reverse_at_a_non_address_is_refused);
    RUN_TEST(test_configApply_speed_presets_must_be_distinct);
    RUN_TEST(test_configApply_speedLimitMax_derives_from_active_preset_when_omitted);
    RUN_TEST(test_configApply_speedLimitMax_resolves_matching_preset);
    RUN_TEST(test_configApply_speedLimitMax_falls_back_to_normal_when_unmatched);
    RUN_TEST(test_configApply_dome_enable_transition_queues_dome_on_cue);
    RUN_TEST(test_configApply_dome_already_enabled_no_cue);
    RUN_TEST(test_configApply_json_body_sbusTimeoutMs_updates);
    RUN_TEST(test_configApply_json_body_is_refused_by_the_form_fields_own_check);
    RUN_TEST(test_configApply_json_body_object_where_a_value_belongs_is_refused);
    RUN_TEST(test_configApply_json_body_invalid_json_rejected);
    RUN_TEST(test_configApply_an_empty_get_shape_list_is_an_answer);
    RUN_TEST(test_configApply_led_count_out_of_range_names_its_output);
    RUN_TEST(test_configApply_an_unknown_component_word_is_refused);
    RUN_TEST(test_configApply_multiple_fields_record_applied_lines_in_order);
    RUN_TEST(test_configApply_every_scalar_records_an_applied_line);
    RUN_TEST(test_configApply_cleared_peer_ip_records_none);
    RUN_TEST(test_configApply_droid_build_records_both_halves_and_the_parts);
    RUN_TEST(test_configApply_a_mixed_droid_saves_without_complaint);
    RUN_TEST(test_configApply_a_design_without_its_variant_is_refused);
    RUN_TEST(test_configApply_a_variant_that_is_not_that_design_s_is_refused);
    RUN_TEST(test_configApply_a_design_this_build_does_not_declare_is_refused);
    RUN_TEST(test_configApply_an_empty_fitted_list_is_an_answer);
    RUN_TEST(test_configApply_a_part_this_build_cannot_name_is_refused);
    RUN_TEST(test_configApply_without_droid_build_params_records_no_edit);
    RUN_TEST(test_configApply_an_out_of_order_dome_pulse_set_is_refused);
    return UNITY_END();
}
