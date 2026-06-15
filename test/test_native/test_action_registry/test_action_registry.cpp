// =============================================================================
// test/test_native/test_action_registry/test_action_registry.cpp
//
// Native unit tests for the compile-time ACTION_REGISTRY table.
//
// Tests: size > 0, all entries have non-empty name/display_name, id uniqueness.
// =============================================================================
#include <unity.h>

#include "action_registry.h"
#include "api_actions.h"
#include <ArduinoJson.h>

void setUp() {
}
void tearDown() {
}

// Registry must contain at least one entry.
void test_registry_size_nonzero() {
    TEST_ASSERT_GREATER_THAN(0u, ACTION_REGISTRY_SIZE);
}

// Every entry must have non-null, non-empty name and display_name.
void test_registry_all_fields_nonempty() {
    for (size_t i = 0; i < ACTION_REGISTRY_SIZE; ++i) {
        const ActionEntry& e = ACTION_REGISTRY[i];
        TEST_ASSERT_NOT_NULL_MESSAGE(e.name, "entry name is null");
        TEST_ASSERT_TRUE_MESSAGE(e.name[0] != '\0', "entry name is empty string");
        TEST_ASSERT_NOT_NULL_MESSAGE(e.display_name, "entry display_name is null");
        TEST_ASSERT_TRUE_MESSAGE(e.display_name[0] != '\0', "entry display_name is empty string");
        TEST_ASSERT_NOT_NULL_MESSAGE(e.domain, "entry domain is null");
        TEST_ASSERT_TRUE_MESSAGE(e.domain[0] != '\0', "entry domain is empty string");
        TEST_ASSERT_NOT_NULL_MESSAGE(e.description, "entry description is null");
        TEST_ASSERT_TRUE_MESSAGE(e.description[0] != '\0', "entry description is empty string");
    }
}

// Every entry must have a unique RobotActionId (no duplicates).
void test_registry_ids_unique() {
    for (size_t i = 0; i < ACTION_REGISTRY_SIZE; ++i) {
        for (size_t j = i + 1; j < ACTION_REGISTRY_SIZE; ++j) {
            TEST_ASSERT_NOT_EQUAL_MESSAGE((int)ACTION_REGISTRY[i].id, (int)ACTION_REGISTRY[j].id,
                                         "duplicate RobotActionId in ACTION_REGISTRY");
        }
    }
}

// ROBOT_ACTION_NONE must not appear — the registry covers bindable actions only.
void test_registry_no_none_entry() {
    for (size_t i = 0; i < ACTION_REGISTRY_SIZE; ++i) {
        TEST_ASSERT_NOT_EQUAL_MESSAGE((int)ROBOT_ACTION_NONE, (int)ACTION_REGISTRY[i].id,
                                     "ROBOT_ACTION_NONE must not be in the registry");
    }
}

static bool registryContainsAction(RobotActionId id) {
    for (size_t i = 0; i < ACTION_REGISTRY_SIZE; ++i) {
        if (ACTION_REGISTRY[i].id == id) {
            return true;
        }
    }
    return false;
}

void test_registry_contains_sound_category_actions() {
    const RobotActionId ids[] = {
        SOUND_ACTION_RANDOM_GENERAL,     SOUND_ACTION_RANDOM_CHATTY,
        SOUND_ACTION_RANDOM_HAPPY,       SOUND_ACTION_RANDOM_PROCESSING,
        SOUND_ACTION_RANDOM_SAD,         SOUND_ACTION_RANDOM_SENTIMENTAL,
        SOUND_ACTION_RANDOM_HUMMING,     SOUND_ACTION_RANDOM_SCREAM,
        SOUND_ACTION_RANDOM_SURPRISED,   SOUND_ACTION_RANDOM_ALERT,
        SOUND_ACTION_RANDOM_SNARKY,        SOUND_ACTION_RANDOM_WHISTLE,
    };

    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i) {
        TEST_ASSERT_TRUE(registryContainsAction(ids[i]));
    }
}

void test_registry_contains_droid_sequence_actions() {
    const RobotActionId ids[] = {
        DROID_SEQ_SCREAM,      DROID_SEQ_WAVE,        DROID_SEQ_FAST_WAVE,
        DROID_SEQ_OPEN_WAVE,   DROID_SEQ_BEEP_CANTINA,DROID_SEQ_FAINT,
        DROID_SEQ_CANTINA,     DROID_SEQ_LEIA,        DROID_SEQ_DISCO,
        DROID_SEQ_SCREAMS,     DROID_SEQ_WIGGLE,
    };

    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i) {
        TEST_ASSERT_TRUE(registryContainsAction(ids[i]));
    }
}

void test_registry_contains_speed_preset_cycle_action() {
    TEST_ASSERT_TRUE(registryContainsAction(DRIVE_ACTION_SPEED_PRESET_CYCLE));
}

void test_web_testable_action_logic() {
    TEST_ASSERT_FALSE(robotActionIsWebTestable(DRIVE_ACTION_SPEED));
    TEST_ASSERT_FALSE(robotActionIsWebTestable(DRIVE_ACTION_STEER));
    TEST_ASSERT_FALSE(robotActionIsWebTestable(DOME_ACTION_SPEED));
    TEST_ASSERT_TRUE(robotActionIsWebTestable(DRIVE_ACTION_SPEED_PRESET_CYCLE));
    TEST_ASSERT_FALSE(robotActionIsWebTestable(SYSTEM_ACTION_ESTOP));
    TEST_ASSERT_FALSE(robotActionIsWebTestable(DOME_ACTION_MARCDUINO_SEQ));
    TEST_ASSERT_FALSE(robotActionIsWebTestable(DOME_ACTION_MARCDUINO_CMD));

    TEST_ASSERT_TRUE(robotActionIsWebTestable(SERVO_ACTION_ARM1_TOGGLE));
    TEST_ASSERT_TRUE(robotActionIsWebTestable(SOUND_ACTION_RANDOM_GENERAL));
    TEST_ASSERT_TRUE(robotActionIsWebTestable(SYSTEM_ACTION_SLEEP_TOGGLE));
    TEST_ASSERT_TRUE(robotActionIsWebTestable(DROID_SEQ_SCREAM));
}

void test_one_shot_action_logic() {
    TEST_ASSERT_FALSE(robotActionIsOneShotButton(DRIVE_ACTION_SPEED));
    TEST_ASSERT_FALSE(robotActionIsOneShotButton(SYSTEM_ACTION_OP_MODE));
    TEST_ASSERT_FALSE(robotActionIsOneShotButton(SERVO_ACTION_ARM1_TOGGLE));

    TEST_ASSERT_TRUE(robotActionIsOneShotButton(SOUND_ACTION_RANDOM_GENERAL));
    TEST_ASSERT_TRUE(robotActionIsOneShotButton(SYSTEM_ACTION_SLEEP_TOGGLE));
    TEST_ASSERT_TRUE(robotActionIsOneShotButton(DRIVE_ACTION_SPEED_PRESET_CYCLE));
}

void test_action_test_guard_logic() {
    TEST_ASSERT_EQUAL(ACTION_TEST_SAFETY_CRITICAL_BLOCKED,
                      evaluateActionTestGuard(SYSTEM_ACTION_ESTOP, false));
    TEST_ASSERT_EQUAL(ACTION_TEST_WEB_CONTROL_DISABLED,
                      evaluateActionTestGuard(SERVO_ACTION_ARM1_TOGGLE, false));
    TEST_ASSERT_EQUAL(ACTION_TEST_ACTION_NOT_TESTABLE,
                      evaluateActionTestGuard(DRIVE_ACTION_SPEED, true));
    TEST_ASSERT_EQUAL(ACTION_TEST_ALLOWED,
                      evaluateActionTestGuard(SERVO_ACTION_ARM1_TOGGLE, true));
}

void test_registry_json_payload_fits_budget() {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();

    for (size_t i = 0; i < ACTION_REGISTRY_SIZE; ++i) {
        const ActionEntry& e = ACTION_REGISTRY[i];
        JsonObject obj = arr.add<JsonObject>();
        obj["id"] = static_cast<int>(e.id);
        obj["name"] = e.name;
        obj["display_name"] = e.display_name;
        obj["domain"] = e.domain;
        obj["description"] = e.description;
        obj["safety_critical"] = e.safety_critical;
        obj["testable"] = robotActionIsWebTestable(e.id);
        obj["one_shot"] = robotActionIsOneShotButton(e.id);
        obj["token"] = robotActionIdToString(e.id);
    }

    const size_t bytes = measureJson(doc);
    TEST_ASSERT_GREATER_THAN(0u, bytes);
    TEST_ASSERT_LESS_THAN_MESSAGE(4096u, bytes,
                                  "GET /api/actions JSON payload exceeded expected budget");
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_registry_size_nonzero);
    RUN_TEST(test_registry_all_fields_nonempty);
    RUN_TEST(test_registry_ids_unique);
    RUN_TEST(test_registry_no_none_entry);
    RUN_TEST(test_registry_contains_sound_category_actions);
    RUN_TEST(test_registry_contains_droid_sequence_actions);
    RUN_TEST(test_web_testable_action_logic);
    RUN_TEST(test_one_shot_action_logic);
    RUN_TEST(test_registry_contains_speed_preset_cycle_action);
    RUN_TEST(test_action_test_guard_logic);
    RUN_TEST(test_registry_json_payload_fits_budget);

    return UNITY_END();
}
