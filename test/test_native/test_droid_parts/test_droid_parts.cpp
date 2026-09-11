// =============================================================================
// test/test_native/test_droid_parts/test_droid_parts.cpp
//
// The Droid Parts Catalog as firmware sees it (#301, #356, #357).
//
// Two behaviours, and both are about the line between them. The generated table
// is the vocabulary: which Part ids this build models at all, and it is
// deliberately only the ones the body can drive - a dome panel is a Part the
// browser names and the body never resolves. The availability answer is the
// wiring: which of those Parts an Output on THIS droid actually claims, asked
// fresh every time, so an arm wired after a step was authored starts moving
// without the step being touched.
// =============================================================================
#include <unity.h>

#include "droid_part_availability.h"
#include "droid_part_control.h"
#include "droid_parts.h"
#include "console_record.h"

void setUp() {}
void tearDown() {}

namespace {

// A table with one output, addressed but claiming no Part yet - the state a
// droid is in between plugging a lead in and saying what it moves.
ServoOutputTable oneEmptyOutput() {
    ServoOutputTable table = {};
    table.count = 1;
    servoOutputRowDefaults(&table.rows[0], SERVO_DRIVER_LEDC, LEDC_CH_ARM1,
                           SERVO_COMP_MG996R);
    return table;
}

}  // namespace

// --- the vocabulary ----------------------------------------------------------

void test_the_body_driven_parts_are_the_ones_firmware_knows() {
    // The two utility arms are the only catalog parts the body drives today.
    TEST_ASSERT_TRUE(droidPartIdIsKnown("utilUp"));
    TEST_ASSERT_TRUE(droidPartIdIsKnown("utilLo"));
}

void test_the_escape_hatch_is_nameable_in_firmware() {
    // A spare output is a body output. If these ids did not reach firmware, a
    // real output a builder wired their own hardware to would be unnameable
    // here - which is the one thing the hatch exists to prevent.
    TEST_ASSERT_TRUE(droidPartIdIsKnown("other1"));
    TEST_ASSERT_TRUE(droidPartIdIsKnown("other10"));
    TEST_ASSERT_FALSE(droidPartIdIsKnown("other11"));
}

void test_a_dome_part_is_not_a_part_firmware_resolves() {
    // The dome owns execution of panel intent under Catalog Authority, so a
    // dome-link Part reaches the browser and stops there. An undriven body part
    // is the same answer for the same reason: nothing here can move it.
    TEST_ASSERT_FALSE(droidPartIdIsKnown("pie1"));
    TEST_ASSERT_FALSE(droidPartIdIsKnown("panel14"));
    TEST_ASSERT_FALSE(droidPartIdIsKnown("hp1Pan"));
    TEST_ASSERT_FALSE(droidPartIdIsKnown("doorFL"));
}

void test_a_light_is_named_in_the_browser_and_nowhere_here() {
    // A PSI, a logic display and the Magic Panel are Parts exactly as a pie
    // panel is one (#320, ADR 0045), and nothing on the body drives any of
    // them: the dome renders light intent, and the raw MarcDuino families
    // reach it uninterpreted. So they travel as far as the browser and stop,
    // the same distance a dome panel travels and for the same reason.
    TEST_ASSERT_FALSE(droidPartIdIsKnown("magicPanel"));
    TEST_ASSERT_FALSE(droidPartIdIsKnown("psiFront"));
    TEST_ASSERT_FALSE(droidPartIdIsKnown("psiRear"));
    TEST_ASSERT_FALSE(droidPartIdIsKnown("logicFront"));
    TEST_ASSERT_FALSE(droidPartIdIsKnown("logicRear"));
    TEST_ASSERT_FALSE(droidPartIdIsKnown("upperPanel"));

    // The whole table, so "the lights did not reach firmware" is checked from
    // both ends: the two utility arms plus the ten escape-hatch slots, which
    // is what a body-driven complement is today. This number moves when a Part
    // the BODY drives is declared - never when a dome Part is.
    TEST_ASSERT_EQUAL_size_t(12, DROID_PART_COUNT);
}

void test_the_vocabulary_refuses_what_is_not_an_id() {
    TEST_ASSERT_FALSE(droidPartIdIsKnown(nullptr));
    TEST_ASSERT_FALSE(droidPartIdIsKnown(""));
    TEST_ASSERT_FALSE(droidPartIdIsKnown("utilup"));  // ids are case-sensitive
}

void test_every_id_in_the_table_is_reachable_by_index() {
    for (size_t i = 0; i < DROID_PART_COUNT; ++i) {
        TEST_ASSERT_TRUE(droidPartIdIsKnown(droidPartIdAt(i)));
        TEST_ASSERT_TRUE(strlen(droidPartIdAt(i)) <= DROID_PART_ID_MAX_LEN);
    }
    TEST_ASSERT_EQUAL_STRING("", droidPartIdAt(DROID_PART_COUNT));
}

// --- the control paths the firmware declares ---------------------------------

void test_only_the_body_driven_path_reaches_firmware() {
    TEST_ASSERT_TRUE(droidPartControlReachesFirmware(DROID_PART_CONTROL_BODY_LEDC));
    TEST_ASSERT_FALSE(droidPartControlReachesFirmware(DROID_PART_CONTROL_DOME_LINK));
    TEST_ASSERT_FALSE(droidPartControlReachesFirmware(DROID_PART_CONTROL_NONE));
}

void test_the_catalog_spelling_is_the_one_the_firmware_answers_with() {
    // The generator validates the catalog's `control:` against these tokens, so
    // a synonym here would silently start refusing a catalog that is correct.
    TEST_ASSERT_EQUAL_STRING("body-ledc", droidPartControlToken(DROID_PART_CONTROL_BODY_LEDC));
    TEST_ASSERT_EQUAL_STRING("dome-link", droidPartControlToken(DROID_PART_CONTROL_DOME_LINK));
    TEST_ASSERT_EQUAL_STRING("none", droidPartControlToken(DROID_PART_CONTROL_NONE));

    DroidPartControl control = DROID_PART_CONTROL_NONE;
    TEST_ASSERT_TRUE(droidPartControlFromToken("dome-link", &control));
    TEST_ASSERT_EQUAL_INT(DROID_PART_CONTROL_DOME_LINK, control);
    TEST_ASSERT_FALSE(droidPartControlFromToken("i2c-expander", &control));
    TEST_ASSERT_EQUAL_INT(DROID_PART_CONTROL_DOME_LINK, control);  // left alone
}

// --- driveable here ----------------------------------------------------------

void test_a_known_part_no_output_claims_reports_part_not_assigned() {
    const ServoOutputTable table = oneEmptyOutput();
    TEST_ASSERT_EQUAL_INT(CONSOLE_REASON_PART_NOT_ASSIGNED,
                          droidPartAvailabilityReason(table, "utilUp"));
    TEST_ASSERT_EQUAL_STRING("part-not-assigned",
                             consoleReasonString(CONSOLE_REASON_PART_NOT_ASSIGNED));
}

void test_wiring_the_arm_later_is_what_changes_the_answer() {
    // The point of the reason: nothing is cached at discovery. The same Part id
    // that was inert a moment ago moves as soon as an Output records it, with
    // nothing re-authored.
    ServoOutputTable table = oneEmptyOutput();
    TEST_ASSERT_EQUAL_INT(CONSOLE_REASON_PART_NOT_ASSIGNED,
                          droidPartAvailabilityReason(table, "utilUp"));

    TEST_ASSERT_TRUE(servoOutputAddPart(&table.rows[0], "utilUp"));
    TEST_ASSERT_EQUAL_INT(CONSOLE_REASON_NONE,
                          droidPartAvailabilityReason(table, "utilUp"));

    // And back again when the lead moves to something else.
    servoOutputRemovePartAt(&table.rows[0], 0);
    TEST_ASSERT_EQUAL_INT(CONSOLE_REASON_PART_NOT_ASSIGNED,
                          droidPartAvailabilityReason(table, "utilUp"));
}

void test_a_ganged_lead_answers_for_every_part_it_moves() {
    ServoOutputTable table = oneEmptyOutput();
    TEST_ASSERT_TRUE(servoOutputAddPart(&table.rows[0], "other3"));
    TEST_ASSERT_TRUE(servoOutputAddPart(&table.rows[0], "other4"));

    TEST_ASSERT_EQUAL_INT(CONSOLE_REASON_NONE, droidPartAvailabilityReason(table, "other3"));
    TEST_ASSERT_EQUAL_INT(CONSOLE_REASON_NONE, droidPartAvailabilityReason(table, "other4"));
    TEST_ASSERT_EQUAL_INT(CONSOLE_REASON_PART_NOT_ASSIGNED,
                          droidPartAvailabilityReason(table, "other5"));
}

void test_an_id_this_build_does_not_model_is_not_reported_as_unwired() {
    // A dome panel, and a step that outlived its catalog, are both "not a Part
    // here" rather than "a Part nobody wired" - the difference between sending
    // a builder to the bench and telling them the truth.
    const ServoOutputTable table = oneEmptyOutput();
    TEST_ASSERT_EQUAL_INT(CONSOLE_REASON_UNKNOWN_ARGUMENT,
                          droidPartAvailabilityReason(table, "pie1"));
    TEST_ASSERT_EQUAL_INT(CONSOLE_REASON_UNKNOWN_ARGUMENT,
                          droidPartAvailabilityReason(table, "armOfTheFuture"));
    // A light included: a PSI nobody wired to a body output is not a wiring
    // fault a builder can go and fix at the bench.
    TEST_ASSERT_EQUAL_INT(CONSOLE_REASON_UNKNOWN_ARGUMENT,
                          droidPartAvailabilityReason(table, "psiFront"));
    TEST_ASSERT_EQUAL_INT(CONSOLE_REASON_UNKNOWN_ARGUMENT,
                          droidPartAvailabilityReason(table, nullptr));
}

void test_a_stored_count_past_the_table_does_not_walk_off_the_end() {
    ServoOutputTable table = oneEmptyOutput();
    TEST_ASSERT_TRUE(servoOutputAddPart(&table.rows[0], "utilLo"));
    table.count = 250;  // a count no loader should have produced

    TEST_ASSERT_EQUAL_INT(CONSOLE_REASON_NONE, droidPartAvailabilityReason(table, "utilLo"));
    TEST_ASSERT_EQUAL_INT(CONSOLE_REASON_PART_NOT_ASSIGNED,
                          droidPartAvailabilityReason(table, "utilUp"));
}

int main(int, char**) {
    UNITY_BEGIN();

    RUN_TEST(test_the_body_driven_parts_are_the_ones_firmware_knows);
    RUN_TEST(test_the_escape_hatch_is_nameable_in_firmware);
    RUN_TEST(test_a_dome_part_is_not_a_part_firmware_resolves);
    RUN_TEST(test_a_light_is_named_in_the_browser_and_nowhere_here);
    RUN_TEST(test_the_vocabulary_refuses_what_is_not_an_id);
    RUN_TEST(test_every_id_in_the_table_is_reachable_by_index);

    RUN_TEST(test_only_the_body_driven_path_reaches_firmware);
    RUN_TEST(test_the_catalog_spelling_is_the_one_the_firmware_answers_with);

    RUN_TEST(test_a_known_part_no_output_claims_reports_part_not_assigned);
    RUN_TEST(test_wiring_the_arm_later_is_what_changes_the_answer);
    RUN_TEST(test_a_ganged_lead_answers_for_every_part_it_moves);
    RUN_TEST(test_an_id_this_build_does_not_model_is_not_reported_as_unwired);
    RUN_TEST(test_a_stored_count_past_the_table_does_not_walk_off_the_end);

    return UNITY_END();
}
