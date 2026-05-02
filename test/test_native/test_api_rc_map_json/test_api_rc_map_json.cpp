#include <unity.h>

#include <cstring>

#include "api_config_snapshot.h"
#include "rc_mapping.h"

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
    snap.rc_input_mode = mode;
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

    TEST_ASSERT_EQUAL_UINT8(SOUND_ACTION_RANDOM_WHISTLE, working.rc_sound.target);
    TEST_ASSERT_EQUAL_UINT8(SOUND_ACTION_RANDOM_HUMMING, working.rc_free0.target);
    TEST_ASSERT_EQUAL_UINT8(SOUND_ACTION_RANDOM_ALERT, working.rc_free1.target);
    TEST_ASSERT_EQUAL_UINT8(SOUND_ACTION_RANDOM_SNARKY, working.rc_free2.target);
    TEST_ASSERT_EQUAL_UINT8(SOUND_ACTION_RANDOM_SAD, working.rc_free3.target);

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

    TEST_ASSERT_TRUE(working.rc_sound.reverse);
    TEST_ASSERT_FALSE(working.rc_free0.reverse);
}

void test_assignRcMapEntryToSnapshot_reuses_existing_dome_calibration(void) {
    ConfigSnapshot existing = makeEmptySnapshot();
    existing.rc_sbus_dome_speed = makeRcBindingConfig(RC_BINDING_SBUS2, 1, 260, 1180, 1860, 35, false);
    existing.rc_pwm_dome_speed = disabledRcBinding();

    ConfigSnapshot working = existing;
    clearRcMapSlots(&working);

    char err[96] = {};
    RcMapEntry dome = makeEntry(RC_BINDING_SBUS2, 1, DOME_ACTION_SPEED);
    TEST_ASSERT_TRUE(assignRcMapEntryToSnapshot(dome, existing, &working, err, sizeof(err)));

    TEST_ASSERT_EQUAL_UINT8(RC_BINDING_SBUS2, working.rc_sbus_dome_speed.source);
    TEST_ASSERT_EQUAL_UINT8(1, working.rc_sbus_dome_speed.channel);
    TEST_ASSERT_EQUAL_UINT16(260, working.rc_sbus_dome_speed.min);
    TEST_ASSERT_EQUAL_UINT16(1180, working.rc_sbus_dome_speed.center);
    TEST_ASSERT_EQUAL_UINT16(1860, working.rc_sbus_dome_speed.max);
    TEST_ASSERT_EQUAL_UINT16(35, working.rc_sbus_dome_speed.deadband);
    TEST_ASSERT_FALSE(working.rc_sbus_dome_speed.reverse);

    TEST_ASSERT_EQUAL_UINT16(1180, working.rc_pwm_dome_speed.center);
    TEST_ASSERT_EQUAL_UINT16(35, working.rc_pwm_dome_speed.deadband);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_populateRcMapJson_absence_not_sentinel);
    RUN_TEST(test_assignRcMapEntryToSnapshot_rejects_duplicate_named_slot);
    RUN_TEST(test_assignRcMapEntryToSnapshot_spill_slots_fill_in_order);
    RUN_TEST(test_assignRcMapEntryToSnapshot_applies_sbus_button_reverse_default);
    RUN_TEST(test_assignRcMapEntryToSnapshot_reuses_existing_dome_calibration);
    return UNITY_END();
}
