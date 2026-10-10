#include <unity.h>

#include <cstring>

#include "api_config_snapshot.h"
#include "rc_map_store.h"  // rcMapStorePlace(), rcMapStoreClear(), the place tables
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
    rcMapStoreClear(&snap.system);
    return snap;
}

// Where a save put a binding, and where a test stores one, read through the
// store's place tables rather than by SystemConfig field.
bool place(const RcMapEntry& entry, const ConfigSnapshot& existing, ConfigSnapshot* working,
           char* err, size_t errSize) {
    return rcMapStorePlace(entry, existing.system, &working->system, err, errSize);
}

RcBindingConfig& axisPlace(ConfigSnapshot* snap, RcMapAxisGroup group, RcMapAxis axis) {
    return const_cast<RcBindingConfig&>(rcMapAxisAt(snap->system, group, axis));
}

RcTriggerBinding& triggerPlace(ConfigSnapshot* snap, size_t i) {
    return snap->system.*RC_MAP_TRIGGER_PLACES[i].place;
}

// The place a toggle owns (RcTriggerPlace::ownAction).
RcTriggerBinding& ownPlace(ConfigSnapshot* snap, RobotActionId toggle) {
    size_t i = 0;
    while (i + 1 < RC_TRIGGER_SLOT_COUNT && RC_MAP_TRIGGER_PLACES[i].ownAction != toggle) {
        ++i;
    }
    TEST_ASSERT_EQUAL_MESSAGE(toggle, RC_MAP_TRIGGER_PLACES[i].ownAction, "a place owns the toggle");
    return triggerPlace(snap, i);
}

// The `n`th open place (one no toggle owns), or else the `n`th place a toggle
// owns, in the table's order.
RcTriggerBinding& nthPlace(ConfigSnapshot* snap, bool owned, size_t n) {
    for (size_t i = 0; i < RC_TRIGGER_SLOT_COUNT; ++i) {
        if ((RC_MAP_TRIGGER_PLACES[i].ownAction != ROBOT_ACTION_NONE) == owned && n-- == 0) {
            return triggerPlace(snap, i);
        }
    }
    TEST_FAIL_MESSAGE("no such place");
    return triggerPlace(snap, 0);
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
    TEST_ASSERT_TRUE(place(entry, existing, &working, err, sizeof(err)));

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

// A toggle on a radio takes its own named place when that is free, so a map
// that fits the older layout is stored as it always was. A Reaction on the
// same toggle takes a general place.
void test_rcMapStorePlace_a_toggle_takes_its_named_place(void) {
    ConfigSnapshot existing = makeEmptySnapshot();
    ConfigSnapshot working = existing;

    char err[96] = {};
    RcMapEntry arm = makeEntry(RC_BINDING_SBUS1, 4, SERVO_ACTION_ARM1_TOGGLE);
    RcMapEntry opMode = makeEntry(RC_BINDING_SBUS1, 5, SYSTEM_ACTION_OP_MODE);
    RcMapEntry reaction = makeEntry(RC_BINDING_DROID_REST, 1, SERVO_ACTION_ARM1_TOGGLE);
    reaction.threshold = kRcMapEntryKeep;
    reaction.quietS = kRcMapEntryKeep;

    TEST_ASSERT_TRUE(place(arm, existing, &working, err, sizeof(err)));
    TEST_ASSERT_TRUE(place(opMode, existing, &working, err, sizeof(err)));
    TEST_ASSERT_TRUE(place(reaction, existing, &working, err, sizeof(err)));
    TEST_ASSERT_EQUAL_UINT8(SERVO_ACTION_ARM1_TOGGLE,
                            ownPlace(&working, SERVO_ACTION_ARM1_TOGGLE).target);
    TEST_ASSERT_EQUAL_UINT8(RC_BINDING_SBUS1, ownPlace(&working, SERVO_ACTION_ARM1_TOGGLE).source);
    TEST_ASSERT_EQUAL_UINT8(SYSTEM_ACTION_OP_MODE,
                            ownPlace(&working, SYSTEM_ACTION_OP_MODE).target);
    TEST_ASSERT_EQUAL_UINT8(RC_BINDING_DROID_REST, nthPlace(&working, false, 0).source);
}

void test_rcMapStorePlace_spill_slots_fill_in_order(void) {
    ConfigSnapshot existing = makeEmptySnapshot();
    ConfigSnapshot working = existing;
    char err[96] = {};

    RcMapEntry e0 = makeEntry(RC_BINDING_SBUS2, 5, SOUND_ACTION_RANDOM_WHISTLE);
    RcMapEntry e1 = makeEntry(RC_BINDING_SBUS2, 6, SOUND_ACTION_RANDOM_HUMMING);
    RcMapEntry e2 = makeEntry(RC_BINDING_SBUS2, 7, SOUND_ACTION_RANDOM_ALERT);
    RcMapEntry e3 = makeEntry(RC_BINDING_SBUS2, 8, SOUND_ACTION_RANDOM_SNARKY);
    RcMapEntry e4 = makeEntry(RC_BINDING_SBUS2, 9, SOUND_ACTION_RANDOM_SAD);
    RcMapEntry e5 = makeEntry(RC_BINDING_SBUS2, 10, SOUND_ACTION_RANDOM_GENERAL);

    TEST_ASSERT_TRUE(place(e0, existing, &working, err, sizeof(err)));
    TEST_ASSERT_TRUE(place(e1, existing, &working, err, sizeof(err)));
    TEST_ASSERT_TRUE(place(e2, existing, &working, err, sizeof(err)));
    TEST_ASSERT_TRUE(place(e3, existing, &working, err, sizeof(err)));
    TEST_ASSERT_TRUE(place(e4, existing, &working, err, sizeof(err)));

    TEST_ASSERT_EQUAL_UINT8(SOUND_ACTION_RANDOM_WHISTLE, nthPlace(&working, false, 0).target);
    TEST_ASSERT_EQUAL_UINT8(SOUND_ACTION_RANDOM_HUMMING, nthPlace(&working, false, 1).target);
    TEST_ASSERT_EQUAL_UINT8(SOUND_ACTION_RANDOM_ALERT, nthPlace(&working, false, 2).target);
    TEST_ASSERT_EQUAL_UINT8(SOUND_ACTION_RANDOM_SNARKY, nthPlace(&working, false, 3).target);
    TEST_ASSERT_EQUAL_UINT8(SOUND_ACTION_RANDOM_SAD, nthPlace(&working, false, 4).target);

    // Past the five general places, a binding takes a named place nothing
    // has claimed (ADR 0070, amended 2026-10-10): eleven in all, whatever
    // they fire.
    TEST_ASSERT_TRUE(place(e5, existing, &working, err, sizeof(err)));
    TEST_ASSERT_EQUAL_UINT8(SOUND_ACTION_RANDOM_GENERAL, nthPlace(&working, true, 0).target);
    for (uint8_t channel = 11; channel <= 15; ++channel) {
        RcMapEntry more = makeEntry(RC_BINDING_SBUS2, channel, SOUND_ACTION_NEXT);
        TEST_ASSERT_TRUE(place(more, existing, &working, err, sizeof(err)));
    }
    TEST_ASSERT_EQUAL_UINT8(SOUND_ACTION_NEXT, nthPlace(&working, true, 5).target);
    RcMapEntry twelfth = makeEntry(RC_BINDING_SBUS2, 16, SOUND_ACTION_NEXT);
    TEST_ASSERT_FALSE(place(twelfth, existing, &working, err, sizeof(err)));
    TEST_ASSERT_NOT_NULL(strstr(err, "map exceeds capacity"));
}

void test_rcMapStorePlace_applies_sbus_button_reverse_default(void) {
    ConfigSnapshot existing = makeEmptySnapshot();
    ConfigSnapshot working = existing;
    char err[96] = {};

    RcMapEntry ch6 = makeEntry(RC_BINDING_SBUS2, 6, SOUND_ACTION_RANDOM_HUMMING);
    RcMapEntry ch7 = makeEntry(RC_BINDING_SBUS2, 7, SOUND_ACTION_RANDOM_ALERT);

    TEST_ASSERT_TRUE(place(ch6, existing, &working, err, sizeof(err)));
    TEST_ASSERT_TRUE(place(ch7, existing, &working, err, sizeof(err)));

    TEST_ASSERT_TRUE(nthPlace(&working, false, 0).reverse);
    TEST_ASSERT_FALSE(nthPlace(&working, false, 1).reverse);
}

void test_rcMapStorePlace_reuses_existing_dome_calibration(void) {
    ConfigSnapshot existing = makeEmptySnapshot();
    axisPlace(&existing, RcMapAxisGroup::Sbus, RcMapAxis::DomeSpeed) =
        makeRcBindingConfig(RC_BINDING_SBUS2, 1, 260, 1180, 1860, 35, false);
    axisPlace(&existing, RcMapAxisGroup::Pwm, RcMapAxis::DomeSpeed) = disabledRcBinding();

    ConfigSnapshot working = existing;
    rcMapStoreClear(&working.system);

    char err[96] = {};
    RcMapEntry dome = makeEntry(RC_BINDING_SBUS2, 1, DOME_ACTION_SPEED);
    TEST_ASSERT_TRUE(place(dome, existing, &working, err, sizeof(err)));

    const RcBindingConfig& sbus = axisPlace(&working, RcMapAxisGroup::Sbus, RcMapAxis::DomeSpeed);
    TEST_ASSERT_EQUAL_UINT8(RC_BINDING_SBUS2, sbus.source);
    TEST_ASSERT_EQUAL_UINT8(1, sbus.channel);
    TEST_ASSERT_EQUAL_UINT16(260, sbus.min);
    TEST_ASSERT_EQUAL_UINT16(1180, sbus.center);
    TEST_ASSERT_EQUAL_UINT16(1860, sbus.max);
    TEST_ASSERT_EQUAL_UINT16(35, sbus.deadband);
    TEST_ASSERT_FALSE(sbus.reverse);

    const RcBindingConfig& pwm = axisPlace(&working, RcMapAxisGroup::Pwm, RcMapAxis::DomeSpeed);
    TEST_ASSERT_EQUAL_UINT16(1180, pwm.center);
    TEST_ASSERT_EQUAL_UINT16(35, pwm.deadband);
}

// An entry the droid would not read says so: a single SBUS droid still holds
// the factory dome on SBUS2, which a save for its receiver type refuses, so a
// page must not post it back (ADR 0070, #486 review).
void test_populateRcMapJson_marks_an_entry_the_saved_type_does_not_read(void) {
    ConfigSnapshot snap = makeEmptySnapshot(RC_INPUT_SINGLE_SBUS);
    axisPlace(&snap, RcMapAxisGroup::Sbus, RcMapAxis::DriveSpeed) =
        defaultSbusBinding(RC_BINDING_SBUS1, 1);
    axisPlace(&snap, RcMapAxisGroup::Sbus, RcMapAxis::DriveSteer) =
        defaultSbusBinding(RC_BINDING_SBUS1, 2);
    axisPlace(&snap, RcMapAxisGroup::Sbus, RcMapAxis::DomeSpeed) =
        defaultSbusBinding(RC_BINDING_SBUS2, 1);
    ownPlace(&snap, SERVO_ACTION_ARM1_TOGGLE) = makeRcTriggerBinding(RC_BINDING_SBUS2, 5, SERVO_ACTION_ARM1_TOGGLE, nullptr,
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
            // The refusal a save would give it, as data.
            TEST_ASSERT_EQUAL_STRING("map.source", item["field"] | "");
            TEST_ASSERT_EQUAL_STRING("out-of-range", item["reason"] | "");
            TEST_ASSERT_EQUAL_STRING("sbus1", item["accepts"] | "");
        } else {
            TEST_ASSERT_TRUE(item["field"].isNull());
        }
    }
}

// A drive split across receivers moves neither stick, so both are unread
// (ADR 0070, amended 2026-10-10): Steer with the refusal a save gives it,
// Speed waiting on it under map.drive, which no save refuses, so a page posts
// Speed back and mending Steer brings the pair back.
void test_populateRcMapJson_marks_both_axes_of_a_split_drive_unread(void) {
    ConfigSnapshot snap = makeEmptySnapshot(RC_INPUT_DUAL_SBUS);
    axisPlace(&snap, RcMapAxisGroup::Sbus, RcMapAxis::DriveSpeed) =
        defaultSbusBinding(RC_BINDING_SBUS1, 1);
    axisPlace(&snap, RcMapAxisGroup::Sbus, RcMapAxis::DriveSteer) =
        defaultSbusBinding(RC_BINDING_SBUS2, 2);
    JsonDocument doc;
    TEST_ASSERT_TRUE(populateRcMapJson(doc, snap));
    JsonArrayConst map = doc["map"].as<JsonArrayConst>();
    TEST_ASSERT_EQUAL_UINT(2, map.size());
    TEST_ASSERT_EQUAL_STRING("drive_speed", map[0]["action"] | "");
    TEST_ASSERT_FALSE(map[0]["read"] | true);
    TEST_ASSERT_EQUAL_STRING("map.drive", map[0]["field"] | "");
    TEST_ASSERT_EQUAL_STRING("conflict", map[0]["reason"] | "");
    TEST_ASSERT_EQUAL_STRING("drive_steer", map[1]["action"] | "");
    TEST_ASSERT_FALSE(map[1]["read"] | true);
    TEST_ASSERT_EQUAL_STRING("map.source", map[1]["field"] | "");
    TEST_ASSERT_EQUAL_STRING("conflict", map[1]["reason"] | "");
    TEST_ASSERT_TRUE(map[1]["accepts"].isNull());
}

// Two stored bindings on one RC Channel (a save cut short by a power loss can
// leave them) are both unread, with the conflict a save would refuse them for;
// the rest of the map reads on (ADR 0070).
void test_populateRcMapJson_marks_both_bindings_on_one_channel_unread(void) {
    ConfigSnapshot snap = makeEmptySnapshot(RC_INPUT_DUAL_SBUS);
    axisPlace(&snap, RcMapAxisGroup::Sbus, RcMapAxis::DriveSpeed) =
        defaultSbusBinding(RC_BINDING_SBUS1, 4);
    axisPlace(&snap, RcMapAxisGroup::Sbus, RcMapAxis::DriveSteer) =
        defaultSbusBinding(RC_BINDING_SBUS1, 2);
    ownPlace(&snap, SERVO_ACTION_ARM1_TOGGLE) = makeRcTriggerBinding(RC_BINDING_SBUS1, 4, SERVO_ACTION_ARM1_TOGGLE, nullptr,
                                               172, 992, 1811, 0, false);
    JsonDocument doc;
    TEST_ASSERT_TRUE(populateRcMapJson(doc, snap));
    JsonArrayConst map = doc["map"].as<JsonArrayConst>();
    TEST_ASSERT_EQUAL_UINT(3, map.size());
    for (JsonObjectConst item : map) {
        // Every entry is unread: the two on CH4 for the conflict, and Steer
        // because Speed, its partner, is one of them.
        TEST_ASSERT_FALSE_MESSAGE(item["read"] | true, item["action"] | "");
        const bool onCh4 = (item["channel"] | 0) == 4;
        TEST_ASSERT_EQUAL_STRING(onCh4 ? "map.channel" : "map.drive", item["field"] | "");
        TEST_ASSERT_EQUAL_STRING("conflict", item["reason"] | "");
        TEST_ASSERT_TRUE(item["accepts"].isNull());
    }
}

namespace {

RcTriggerBinding* const* triggerSlots(ConfigSnapshot* snap) {
    static RcTriggerBinding* slots[RC_TRIGGER_SLOT_COUNT];
    for (size_t i = 0; i < RC_TRIGGER_SLOT_COUNT; ++i) slots[i] = &triggerPlace(snap, i);
    return slots;
}

const char* const kLongPayload = ":OP01ABCDEFGHIJ";

}  // namespace

// The widest answer GET /api/rc/map can give still fits the body it is sent
// in. Two maps compete: one of eight Reactions (each carrying its two numbers)
// beside unread radio cues, and one of nothing but unread bindings carrying
// the longest refusal - a PWM droid's cues ("map.action", accepts the three
// axes) and axes whose dead zone swallows a side.
void test_populateRcMapJson_widest_map_fits_its_body(void) {
    ConfigSnapshot reactions = makeEmptySnapshot(RC_INPUT_NOT_FITTED);
    axisPlace(&reactions, RcMapAxisGroup::Sbus, RcMapAxis::DriveSpeed) =
        defaultSbusBinding(RC_BINDING_SBUS1, 16);
    axisPlace(&reactions, RcMapAxisGroup::Sbus, RcMapAxis::DriveSteer) =
        defaultSbusBinding(RC_BINDING_SBUS1, 15);
    axisPlace(&reactions, RcMapAxisGroup::Sbus, RcMapAxis::DomeSpeed) =
        defaultSbusBinding(RC_BINDING_SBUS2, 14);
    const struct {
        RcBindingSource source;
        uint8_t channel;
    } conditions[] = {{RC_BINDING_DROID_WHEEL_SPEED, 1}, {RC_BINDING_DROID_WHEEL_SPEED, 2},
                      {RC_BINDING_DROID_WHEEL_AMPS, 1},  {RC_BINDING_DROID_WHEEL_AMPS, 2},
                      {RC_BINDING_DROID_HARD_STOP, 1},   {RC_BINDING_DROID_SPEED, 1},
                      {RC_BINDING_DROID_TRACK, 1},       {RC_BINDING_DROID_REST, 1}};
    RcTriggerBinding* const* slots = triggerSlots(&reactions);
    size_t i = 0;
    for (const auto& condition : conditions) {
        *slots[i++] = makeRcReactionBinding(condition.source, condition.channel, DROID_SEQ_BEEP_CANTINA,
                                            kLongPayload, 5000, 3600);
    }
    for (uint8_t channel = 16; i < 11; ++i, --channel) {
        *slots[i] = makeRcTriggerBinding(RC_BINDING_SBUS2, channel, DROID_SEQ_BEEP_CANTINA, kLongPayload,
                                         172, 992, 1811, 0, false);
    }

    ConfigSnapshot unread = makeEmptySnapshot(RC_INPUT_STANDARD_PWM);
    axisPlace(&unread, RcMapAxisGroup::Pwm, RcMapAxis::DriveSpeed) =
        makeRcBindingConfig(RC_BINDING_PWM, 1, 1000, 1100, 2000, 150, false);
    axisPlace(&unread, RcMapAxisGroup::Pwm, RcMapAxis::DriveSteer) =
        makeRcBindingConfig(RC_BINDING_PWM, 2, 1000, 1100, 2000, 150, false);
    axisPlace(&unread, RcMapAxisGroup::Pwm, RcMapAxis::DomeSpeed) =
        makeRcBindingConfig(RC_BINDING_PWM, 3, 1000, 1100, 2000, 150, false);
    slots = triggerSlots(&unread);
    for (i = 0; i < 11; ++i) {
        *slots[i] = makeRcTriggerBinding(RC_BINDING_PWM, (uint8_t)(i % 6 + 1), DROID_SEQ_BEEP_CANTINA,
                                         kLongPayload, 1000, 1500, 2000, 0, false);
    }

    for (ConfigSnapshot* snap : {&reactions, &unread}) {
        JsonDocument doc;
        TEST_ASSERT_TRUE(populateRcMapJson(doc, *snap));
        TEST_ASSERT_EQUAL_UINT(14, doc["map"].as<JsonArrayConst>().size());
        TEST_ASSERT_FALSE(doc["map"][13]["read"] | true);
        TEST_ASSERT_LESS_THAN_UINT(RC_MAP_JSON_BODY_BYTES, measureJson(doc));
    }
    JsonDocument doc;
    populateRcMapJson(doc, unread);
    TEST_ASSERT_EQUAL_STRING("calibration.deadband", doc["map"][0]["field"] | "");
    TEST_ASSERT_EQUAL_STRING("drive_speed,drive_steer,dome_speed", doc["map"][13]["accepts"] | "");
}

// GET /api/rc/map says which RC Receivers the saved type reads and which the
// drive may use, so the RC page offers exactly what a save takes (ADR 0070).
void test_populateRcMapJson_says_which_receivers_a_map_may_bind(void) {
    JsonDocument dual;
    TEST_ASSERT_TRUE(populateRcMapJson(dual, makeEmptySnapshot(RC_INPUT_DUAL_SBUS)));
    TEST_ASSERT_EQUAL_UINT(2, dual["receivers"]["read"].size());
    TEST_ASSERT_EQUAL_STRING("sbus2", dual["receivers"]["read"][1] | "");
    TEST_ASSERT_EQUAL_UINT(1, dual["receivers"]["drive"].size());
    TEST_ASSERT_EQUAL_STRING("sbus1", dual["receivers"]["drive"][0] | "");
    TEST_ASSERT_EQUAL_UINT(2, dual["receivers"]["cues"].size());
    JsonDocument pwm;
    TEST_ASSERT_TRUE(populateRcMapJson(pwm, makeEmptySnapshot(RC_INPUT_STANDARD_PWM)));
    TEST_ASSERT_EQUAL_STRING("pwm", pwm["receivers"]["drive"][0] | "");
    TEST_ASSERT_EQUAL_UINT(0, pwm["receivers"]["cues"].size());
    JsonDocument none;
    TEST_ASSERT_TRUE(populateRcMapJson(none, makeEmptySnapshot(RC_INPUT_NOT_FITTED)));
    TEST_ASSERT_TRUE(none["receivers"]["read"].is<JsonArray>());
    TEST_ASSERT_EQUAL_UINT(0, none["receivers"]["read"].size());
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_populateRcMapJson_absence_not_sentinel);
    RUN_TEST(test_populateRcMapJson_says_which_receivers_a_map_may_bind);
    RUN_TEST(test_populateRcMapJson_marks_an_entry_the_saved_type_does_not_read);
    RUN_TEST(test_populateRcMapJson_marks_both_bindings_on_one_channel_unread);
    RUN_TEST(test_populateRcMapJson_marks_both_axes_of_a_split_drive_unread);
    RUN_TEST(test_populateRcMapJson_widest_map_fits_its_body);
    RUN_TEST(test_rcMapStorePlace_a_toggle_takes_its_named_place);
    RUN_TEST(test_rcMapStorePlace_spill_slots_fill_in_order);
    RUN_TEST(test_rcMapStorePlace_applies_sbus_button_reverse_default);
    RUN_TEST(test_rcMapStorePlace_reuses_existing_dome_calibration);
    return UNITY_END();
}
