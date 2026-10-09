#include <unity.h>

#include <cstring>

#include "api_config_snapshot.h"
#include "rc_mapping.h"
#include "web_request_scratch.h"  // RC_MAP_JSON_BODY_BYTES

namespace {

RcMapEntry makeEntry(RcBindingSource source, uint8_t channel, RobotActionId action) {
    RcMapEntry entry = {};
    entry.source = source;
    entry.channel = channel;
    entry.action = action;
    entry.payload[0] = '\0';
    return entry;
}

ConfigSnapshot makeEmptySnapshot(RcInputMode mode = RC_INPUT_DUAL_SBUS) {
    ConfigSnapshot snap = {};
    snap.system.rc_input_mode = mode;
    clearRcMapSlots(&snap);
    return snap;
}

}  // namespace

void setUp(void) {
}

void tearDown(void) {
}

void test_populateRcMapJson_absence_not_sentinel(void) {
    ConfigSnapshot existing = makeEmptySnapshot(RC_INPUT_DUAL_SBUS);
    ConfigSnapshot working = existing;

    char err[96] = {};
    RcMapEntry entry = makeEntry(RC_BINDING_SBUS2, 6, SOUND_ACTION_RANDOM_HUMMING);
    TEST_ASSERT_TRUE(assignRcMapEntryToSnapshot(entry, existing, &working, err, sizeof(err)));

    JsonDocument doc;
    TEST_ASSERT_TRUE(populateRcMapJson(doc, working));

    JsonArrayConst map = doc["map"].as<JsonArrayConst>();
    TEST_ASSERT_EQUAL_UINT(1, map.size());
    TEST_ASSERT_EQUAL_STRING("dual_sbus", doc["mode"] | "");
    TEST_ASSERT_EQUAL_UINT(14, doc["capacity"]["total"].as<unsigned>());
    TEST_ASSERT_EQUAL_UINT(1, doc["capacity"]["used"].as<unsigned>());

    JsonObjectConst item = map[0].as<JsonObjectConst>();
    TEST_ASSERT_EQUAL_STRING("sbus2", item["source"] | "");
    TEST_ASSERT_EQUAL_UINT(6, item["channel"].as<unsigned>());
    TEST_ASSERT_EQUAL_STRING("sound_rand_humming", item["action"] | "");

    char payload[256] = {};
    serializeJson(doc, payload, sizeof(payload));
    TEST_ASSERT_NULL(strstr(payload, "\"none\""));
    TEST_ASSERT_NULL(strstr(payload, "\"disabled\""));
}

void test_assignRcMapEntryToSnapshot_rejects_duplicate_named_slot(void) {
    ConfigSnapshot existing = makeEmptySnapshot();
    ConfigSnapshot working = existing;

    char err[96] = {};
    RcMapEntry first = makeEntry(RC_BINDING_SBUS1, 4, SERVO_ACTION_ARM1_TOGGLE);
    RcMapEntry second = makeEntry(RC_BINDING_SBUS2, 5, SERVO_ACTION_ARM1_TOGGLE);

    TEST_ASSERT_TRUE(assignRcMapEntryToSnapshot(first, existing, &working, err, sizeof(err)));
    TEST_ASSERT_FALSE(assignRcMapEntryToSnapshot(second, existing, &working, err, sizeof(err)));
    TEST_ASSERT_NOT_NULL(strstr(err, "arm1_toggle mapped more than once"));
}

void test_assignRcMapEntryToSnapshot_spill_slots_fill_in_order(void) {
    ConfigSnapshot existing = makeEmptySnapshot();
    ConfigSnapshot working = existing;
    char err[96] = {};

    RcMapEntry e0 = makeEntry(RC_BINDING_SBUS2, 5, SOUND_ACTION_RANDOM_WHISTLE);
    RcMapEntry e1 = makeEntry(RC_BINDING_SBUS2, 6, SOUND_ACTION_RANDOM_HUMMING);
    RcMapEntry e2 = makeEntry(RC_BINDING_SBUS2, 7, SOUND_ACTION_RANDOM_ALERT);
    RcMapEntry e3 = makeEntry(RC_BINDING_SBUS2, 8, SOUND_ACTION_RANDOM_SNARKY);
    RcMapEntry e4 = makeEntry(RC_BINDING_SBUS2, 9, SOUND_ACTION_RANDOM_SAD);
    RcMapEntry e5 = makeEntry(RC_BINDING_SBUS2, 10, SOUND_ACTION_RANDOM_GENERAL);

    TEST_ASSERT_TRUE(assignRcMapEntryToSnapshot(e0, existing, &working, err, sizeof(err)));
    TEST_ASSERT_TRUE(assignRcMapEntryToSnapshot(e1, existing, &working, err, sizeof(err)));
    TEST_ASSERT_TRUE(assignRcMapEntryToSnapshot(e2, existing, &working, err, sizeof(err)));
    TEST_ASSERT_TRUE(assignRcMapEntryToSnapshot(e3, existing, &working, err, sizeof(err)));
    TEST_ASSERT_TRUE(assignRcMapEntryToSnapshot(e4, existing, &working, err, sizeof(err)));

    TEST_ASSERT_EQUAL_UINT8(SOUND_ACTION_RANDOM_WHISTLE, working.system.rc_audio.target);
    TEST_ASSERT_EQUAL_UINT8(SOUND_ACTION_RANDOM_HUMMING, working.system.rc_free0.target);
    TEST_ASSERT_EQUAL_UINT8(SOUND_ACTION_RANDOM_ALERT, working.system.rc_free1.target);
    TEST_ASSERT_EQUAL_UINT8(SOUND_ACTION_RANDOM_SNARKY, working.system.rc_free2.target);
    TEST_ASSERT_EQUAL_UINT8(SOUND_ACTION_RANDOM_SAD, working.system.rc_free3.target);

    TEST_ASSERT_FALSE(assignRcMapEntryToSnapshot(e5, existing, &working, err, sizeof(err)));
    TEST_ASSERT_NOT_NULL(strstr(err, "no trigger slot available"));
}

void test_assignRcMapEntryToSnapshot_applies_sbus_button_reverse_default(void) {
    ConfigSnapshot existing = makeEmptySnapshot();
    ConfigSnapshot working = existing;
    char err[96] = {};

    RcMapEntry ch6 = makeEntry(RC_BINDING_SBUS2, 6, SOUND_ACTION_RANDOM_HUMMING);
    RcMapEntry ch7 = makeEntry(RC_BINDING_SBUS2, 7, SOUND_ACTION_RANDOM_ALERT);

    TEST_ASSERT_TRUE(assignRcMapEntryToSnapshot(ch6, existing, &working, err, sizeof(err)));
    TEST_ASSERT_TRUE(assignRcMapEntryToSnapshot(ch7, existing, &working, err, sizeof(err)));

    TEST_ASSERT_TRUE(working.system.rc_audio.reverse);
    TEST_ASSERT_FALSE(working.system.rc_free0.reverse);
}

void test_assignRcMapEntryToSnapshot_reuses_existing_dome_calibration(void) {
    ConfigSnapshot existing = makeEmptySnapshot();
    existing.system.rc_sbus_dome_speed = makeRcBindingConfig(RC_BINDING_SBUS2, 1, 260, 1180, 1860, 35, false);
    existing.system.rc_pwm_dome_speed = disabledRcBinding();

    ConfigSnapshot working = existing;
    clearRcMapSlots(&working);

    char err[96] = {};
    RcMapEntry dome = makeEntry(RC_BINDING_SBUS2, 1, DOME_ACTION_SPEED);
    TEST_ASSERT_TRUE(assignRcMapEntryToSnapshot(dome, existing, &working, err, sizeof(err)));

    TEST_ASSERT_EQUAL_UINT8(RC_BINDING_SBUS2, working.system.rc_sbus_dome_speed.source);
    TEST_ASSERT_EQUAL_UINT8(1, working.system.rc_sbus_dome_speed.channel);
    TEST_ASSERT_EQUAL_UINT16(260, working.system.rc_sbus_dome_speed.min);
    TEST_ASSERT_EQUAL_UINT16(1180, working.system.rc_sbus_dome_speed.center);
    TEST_ASSERT_EQUAL_UINT16(1860, working.system.rc_sbus_dome_speed.max);
    TEST_ASSERT_EQUAL_UINT16(35, working.system.rc_sbus_dome_speed.deadband);
    TEST_ASSERT_FALSE(working.system.rc_sbus_dome_speed.reverse);

    TEST_ASSERT_EQUAL_UINT16(1180, working.system.rc_pwm_dome_speed.center);
    TEST_ASSERT_EQUAL_UINT16(35, working.system.rc_pwm_dome_speed.deadband);
}

// An entry the droid would not read says so: a single SBUS droid still holds
// the factory dome on SBUS2, which a save for its receiver type refuses, so a
// page must not post it back (ADR 0070, #486 review).
void test_populateRcMapJson_marks_an_entry_the_saved_type_does_not_read(void) {
    ConfigSnapshot snap = makeEmptySnapshot(RC_INPUT_SINGLE_SBUS);
    snap.system.rc_sbus_drive_speed = defaultSbusBinding(RC_BINDING_SBUS1, 1);
    snap.system.rc_sbus_drive_steer = defaultSbusBinding(RC_BINDING_SBUS1, 2);
    snap.system.rc_sbus_dome_speed = defaultSbusBinding(RC_BINDING_SBUS2, 1);
    snap.system.rc_arm1 = makeRcTriggerBinding(RC_BINDING_SBUS2, 5, SERVO_ACTION_ARM1_TOGGLE, nullptr,
                                               172, 992, 1811, 0, false);
    JsonDocument doc;
    TEST_ASSERT_TRUE(populateRcMapJson(doc, snap));
    JsonArrayConst map = doc["map"].as<JsonArrayConst>();
    TEST_ASSERT_EQUAL_UINT(4, map.size());
    for (JsonObjectConst item : map) {
        const bool sbus2 = strcmp(item["source"] | "", "sbus2") == 0;
        TEST_ASSERT_EQUAL_MESSAGE(sbus2, item["read"].is<bool>(), item["action"] | "");
        if (sbus2) {
            TEST_ASSERT_FALSE(item["read"].as<bool>());
        }
    }
}

// A drive split across receivers loses only the axis a save would refuse
// (steer), so posting the rest back saves.
void test_populateRcMapJson_narrows_a_split_drive_to_the_axis_a_save_refuses(void) {
    ConfigSnapshot snap = makeEmptySnapshot(RC_INPUT_DUAL_SBUS);
    snap.system.rc_sbus_drive_speed = defaultSbusBinding(RC_BINDING_SBUS1, 1);
    snap.system.rc_sbus_drive_steer = defaultSbusBinding(RC_BINDING_SBUS2, 2);
    JsonDocument doc;
    TEST_ASSERT_TRUE(populateRcMapJson(doc, snap));
    JsonArrayConst map = doc["map"].as<JsonArrayConst>();
    TEST_ASSERT_EQUAL_UINT(2, map.size());
    TEST_ASSERT_EQUAL_STRING("drive_speed", map[0]["action"] | "");
    TEST_ASSERT_TRUE(map[0]["read"].isNull());
    TEST_ASSERT_EQUAL_STRING("drive_steer", map[1]["action"] | "");
    TEST_ASSERT_FALSE(map[1]["read"] | true);
}

// The widest answer GET /api/rc/map can give still fits the body it is sent
// in: every slot on the longest action and payload, the eight Reactions there
// can be (each carrying its two numbers), and every other slot not read.
void test_populateRcMapJson_widest_map_fits_its_body(void) {
    ConfigSnapshot snap = makeEmptySnapshot(RC_INPUT_NOT_FITTED);
    snap.system.rc_sbus_drive_speed = defaultSbusBinding(RC_BINDING_SBUS1, 16);
    snap.system.rc_sbus_drive_steer = defaultSbusBinding(RC_BINDING_SBUS1, 15);
    snap.system.rc_sbus_dome_speed = defaultSbusBinding(RC_BINDING_SBUS2, 14);
    RcTriggerBinding* const slots[] = {&snap.system.rc_arm1,  &snap.system.rc_arm2,  &snap.system.rc_aux1,
                                       &snap.system.rc_aux2,  &snap.system.rc_aux3,  &snap.system.rc_audio,
                                       &snap.system.rc_opmode, &snap.system.rc_free0, &snap.system.rc_free1,
                                       &snap.system.rc_free2, &snap.system.rc_free3};
    const struct {
        RcBindingSource source;
        uint8_t channel;
    } reactions[] = {{RC_BINDING_DROID_WHEEL_SPEED, 1}, {RC_BINDING_DROID_WHEEL_SPEED, 2},
                     {RC_BINDING_DROID_WHEEL_AMPS, 1},  {RC_BINDING_DROID_WHEEL_AMPS, 2},
                     {RC_BINDING_DROID_HARD_STOP, 1},   {RC_BINDING_DROID_SPEED, 1},
                     {RC_BINDING_DROID_TRACK, 1},       {RC_BINDING_DROID_REST, 1}};
    const char* const payload = ":OP01ABCDEFGHIJ";
    size_t i = 0;
    for (const auto& reaction : reactions) {
        *slots[i++] = makeRcReactionBinding(reaction.source, reaction.channel, DROID_SEQ_BEEP_CANTINA,
                                            payload, 5000, 3600);
    }
    for (uint8_t channel = 16; i < sizeof(slots) / sizeof(slots[0]); ++i, --channel) {
        *slots[i] = makeRcTriggerBinding(RC_BINDING_SBUS2, channel, DROID_SEQ_BEEP_CANTINA, payload,
                                         172, 992, 1811, 0, false);
    }
    JsonDocument doc;
    TEST_ASSERT_TRUE(populateRcMapJson(doc, snap));
    TEST_ASSERT_EQUAL_UINT(14, doc["map"].as<JsonArrayConst>().size());
    TEST_ASSERT_FALSE(doc["map"][13]["read"] | true);
    TEST_ASSERT_EQUAL_UINT(3600, doc["map"][3]["quietS"].as<unsigned>());
    TEST_ASSERT_LESS_THAN_UINT(RC_MAP_JSON_BODY_BYTES, measureJson(doc));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_populateRcMapJson_absence_not_sentinel);
    RUN_TEST(test_populateRcMapJson_marks_an_entry_the_saved_type_does_not_read);
    RUN_TEST(test_populateRcMapJson_narrows_a_split_drive_to_the_axis_a_save_refuses);
    RUN_TEST(test_populateRcMapJson_widest_map_fits_its_body);
    RUN_TEST(test_assignRcMapEntryToSnapshot_rejects_duplicate_named_slot);
    RUN_TEST(test_assignRcMapEntryToSnapshot_spill_slots_fill_in_order);
    RUN_TEST(test_assignRcMapEntryToSnapshot_applies_sbus_button_reverse_default);
    RUN_TEST(test_assignRcMapEntryToSnapshot_reuses_existing_dome_calibration);
    return UNITY_END();
}
