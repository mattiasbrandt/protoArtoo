// =============================================================================
// test/test_native/test_board_outputs/test_board_outputs.cpp
//
// An Output is called by what its board prints beside its pin, and that word is
// what the Console and POST /api/servo take (CONTEXT.md "Output Address", ADR
// 0033 Amendment 2026-09-19; include/board_outputs.h).
//
// The native image is built for the Artoo PCB, so the running board's answers
// are the Artoo's. The lookup takes the board as a parameter, which is how the
// FireBeetle 2's words - the ones with a space in them - are proven here
// without a FireBeetle 2 image.
// =============================================================================
#include <unity.h>

#include <cstring>

#include "action_registry.h"
#include "board_outputs.h"
#include "config_store.h"
#include "rc_action_types.h"

void setUp() {
}

void tearDown() {
}

// The FireBeetle 2's shield prints a bare GPIO number, so its words carry a
// space. Typed as printed, typed without the space, or in any case, each names
// its Output - and GPIO 4 is not a prefix match for GPIO 49.
void test_a_label_with_a_space_is_matched_without_regard_to_case_or_spaces() {
    const BoardOutput* gpio49 = boardOutputForWord("firebeetle2", "GPIO 49");
    TEST_ASSERT_NOT_NULL(gpio49);
    TEST_ASSERT_EQUAL_STRING("arm1", gpio49->id);
    TEST_ASSERT_EQUAL_PTR(gpio49, boardOutputForWord("firebeetle2", "gpio49"));
    TEST_ASSERT_EQUAL_PTR(gpio49, boardOutputForWord("firebeetle2", "Gpio 49"));

    const BoardOutput* gpio4 = boardOutputForWord("firebeetle2", "gpio4");
    TEST_ASSERT_NOT_NULL(gpio4);
    TEST_ASSERT_EQUAL_STRING("aux1", gpio4->id);
    TEST_ASSERT_NULL(boardOutputForWord("firebeetle2", "gpio"));
    TEST_ASSERT_NULL(boardOutputForWord("firebeetle2", "gpio 499"));
}

// protoArtoo's old words are nobody's alias. On the Artoo, ARM1 and ARM2 are
// still what the board prints; the three it printed ARM3..ARM5 all along are no
// longer reachable as aux1..aux3. On the FireBeetle 2 none of them is a word.
void test_the_old_words_name_only_what_a_board_prints() {
    TEST_ASSERT_EQUAL_STRING("arm1", boardOutputForWord("artoo_esp32", "arm1")->id);
    TEST_ASSERT_EQUAL_STRING("aux1", boardOutputForWord("artoo_esp32", "arm3")->id);
    TEST_ASSERT_EQUAL_STRING("aux3", boardOutputForWord("artoo_esp32", "ARM5")->id);

    const char* const kOld[] = {"arm1", "arm2", "aux1", "aux2", "aux3"};
    for (const char* word : kOld) {
        TEST_ASSERT_NULL_MESSAGE(boardOutputForWord("firebeetle2", word), word);
    }
    TEST_ASSERT_NULL(boardOutputForWord("artoo_esp32", "aux1"));
    TEST_ASSERT_NULL(boardOutputForWord("artoo_esp32", "aux3"));
}

// What a refusal names: each board's own words, in the order the pages draw
// the Outputs.
void test_each_board_lists_its_own_words() {
    char words[64];
    TEST_ASSERT_TRUE(boardOutputWordList("firebeetle2", ", ", words, sizeof(words)));
    TEST_ASSERT_EQUAL_STRING("GPIO 49, GPIO 50, GPIO 4, GPIO 5, GPIO 51", words);
    TEST_ASSERT_TRUE(boardOutputWordList("artoo_esp32", ",", words, sizeof(words)));
    TEST_ASSERT_EQUAL_STRING("ARM1,ARM2,ARM3,ARM4,ARM5", words);
}

// A binding a controller saved before Outputs took their board's name still
// loads: the RC token is an id and did not change (#412 answer 6), and the
// action it names is now called by the board - ARM3 on the Artoo for the
// Output stored as aux1.
void test_a_saved_rc_binding_still_loads_and_is_named_by_the_board() {
    Preferences prefs;
    prefs.begin("proto", false);
    prefs.clear();
    // Byte for byte what formatRcTriggerBinding() wrote for an SBUS channel 5
    // switch bound to the third Output's toggle.
    prefs.putString("rc_aux1", "sbus1:5:aux1_toggle::172:992:1811:0:0");

    ConfigSnapshot snap = {};
    TEST_ASSERT_TRUE(configLoad(prefs, &snap));
    prefs.clear();
    prefs.end();

    TEST_ASSERT_EQUAL(SERVO_ACTION_AUX1_TOGGLE, snap.system.rc_aux1.target);
    TEST_ASSERT_EQUAL_UINT8(5, snap.system.rc_aux1.channel);

    const ActionEntry* entry = nullptr;
    for (size_t i = 0; i < ACTION_REGISTRY_SIZE; ++i) {
        if (ACTION_REGISTRY[i].id == snap.system.rc_aux1.target) entry = &ACTION_REGISTRY[i];
    }
    TEST_ASSERT_NOT_NULL(entry);
    char name[32];
    boardOutputComposeText(entry->display_name, strlen(entry->display_name), entry->output, name,
                           sizeof(name));
    TEST_ASSERT_EQUAL_STRING("ARM3 Toggle", name);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_a_label_with_a_space_is_matched_without_regard_to_case_or_spaces);
    RUN_TEST(test_the_old_words_name_only_what_a_board_prints);
    RUN_TEST(test_each_board_lists_its_own_words);
    RUN_TEST(test_a_saved_rc_binding_still_loads_and_is_named_by_the_board);
    return UNITY_END();
}
