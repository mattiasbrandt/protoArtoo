// =============================================================================
// test/test_native/test_droid_parts/test_droid_parts.cpp
//
// The Droid Parts Catalog as firmware sees it (#301, #356, #357, #358).
//
// Two behaviours, and both are about the line between them. The generated table
// is the vocabulary: which Part ids this build models at all, and since #358 it
// is every Part the catalog declares - a dome panel and a breadpan door are
// Parts a builder can name whether or not anything on the body drives them. The
// availability answer is the wiring: which of those Parts an Output on THIS
// droid actually claims, asked fresh every time, so an arm wired after a step
// was authored starts moving without the step being touched.
//
// Keeping them apart is the point. Collapsing them is what made a door
// unnameable and sent a builder to an `otherN` slot for hardware the catalog
// already had a word for.
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

void test_every_declared_part_is_one_firmware_names() {
    // One from each section the catalog declares, whatever drives it: a dome
    // pie and a dome panel the dome drives, a holoprojector axis and a dome
    // fixture nothing drives, a body door and a Common Addition arm nothing
    // drives yet, and the two utility arms the body already drives.
    TEST_ASSERT_TRUE(droidPartIdIsKnown("pie1"));
    TEST_ASSERT_TRUE(droidPartIdIsKnown("panel14"));
    TEST_ASSERT_TRUE(droidPartIdIsKnown("hp1Pan"));
    TEST_ASSERT_TRUE(droidPartIdIsKnown("domeBtn1"));
    TEST_ASSERT_TRUE(droidPartIdIsKnown("doorFL"));
    TEST_ASSERT_TRUE(droidPartIdIsKnown("gripArm"));
    TEST_ASSERT_TRUE(droidPartIdIsKnown("utilUp"));
    TEST_ASSERT_TRUE(droidPartIdIsKnown("utilLo"));
}

void test_a_light_is_a_part_here_exactly_as_a_panel_is() {
    // A PSI, a logic display and the Magic Panel are Parts exactly as a pie
    // panel is one (#320, ADR 0045). Nothing on the body drives any of them,
    // which is a fact about wiring: it decides what they report when a step
    // names them, never whether they have a name here.
    TEST_ASSERT_TRUE(droidPartIdIsKnown("magicPanel"));
    TEST_ASSERT_TRUE(droidPartIdIsKnown("psiFront"));
    TEST_ASSERT_TRUE(droidPartIdIsKnown("psiRear"));
    TEST_ASSERT_TRUE(droidPartIdIsKnown("logicFront"));
    TEST_ASSERT_TRUE(droidPartIdIsKnown("logicRear"));
    TEST_ASSERT_TRUE(droidPartIdIsKnown("upperPanel"));
}

void test_the_escape_hatch_is_nameable_in_firmware() {
    // A spare output is a body output. If these ids did not reach firmware, a
    // real output a builder wired their own hardware to would be unnameable
    // here - which is the one thing the hatch exists to prevent.
    TEST_ASSERT_TRUE(droidPartIdIsKnown("other1"));
    TEST_ASSERT_TRUE(droidPartIdIsKnown("other10"));
    TEST_ASSERT_FALSE(droidPartIdIsKnown("other11"));
}

void test_the_vocabulary_is_the_whole_catalog_and_fits_a_row() {
    // 48 declared Parts plus the ten escape-hatch slots. This number moves when
    // ANY Part is declared, not only one the body drives - which is the whole
    // change #358 made.
    TEST_ASSERT_EQUAL_size_t(58, DROID_PART_COUNT);

    // Every id in it is storable on a Servo Output row's Part field. The
    // static_assert in droid_part_availability.h is the compile-time half; this
    // is the half that says what the number actually is today.
    TEST_ASSERT_EQUAL_size_t(10, DROID_PART_ID_MAX_LEN);
    TEST_ASSERT_TRUE(DROID_PART_ID_MAX_LEN <= SERVO_OUTPUT_PART_ID_MAX);
}

void test_the_vocabulary_refuses_what_is_not_an_id() {
    TEST_ASSERT_FALSE(droidPartIdIsKnown(nullptr));
    TEST_ASSERT_FALSE(droidPartIdIsKnown(""));
    TEST_ASSERT_FALSE(droidPartIdIsKnown("utilup"));  // ids are case-sensitive
    // A perfectly formed identifier that no catalog row declares. The
    // vocabulary got wider, not open: a step that outlived its catalog still
    // has to be told apart from one naming hardware nobody wired.
    TEST_ASSERT_FALSE(droidPartIdIsKnown("armOfTheFuture"));
}

void test_every_id_in_the_table_is_reachable_by_index() {
    for (size_t i = 0; i < DROID_PART_COUNT; ++i) {
        TEST_ASSERT_TRUE(droidPartIdIsKnown(droidPartIdAt(i)));
        TEST_ASSERT_TRUE(strlen(droidPartIdAt(i)) <= DROID_PART_ID_MAX_LEN);
    }
    TEST_ASSERT_EQUAL_STRING("", droidPartIdAt(DROID_PART_COUNT));
}

// --- the control paths the firmware declares ---------------------------------

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

void test_a_part_the_body_never_drives_is_unassigned_not_unknown() {
    // The reason the vocabulary is the whole catalog. A dome panel, a light and
    // a breadpan door nobody has wired are all Parts this droid simply has no
    // Output for - which is what part-not-assigned says, and it is the truth a
    // builder can act on. Before #358 all three answered "not a Part at all".
    const ServoOutputTable table = oneEmptyOutput();
    TEST_ASSERT_EQUAL_INT(CONSOLE_REASON_PART_NOT_ASSIGNED,
                          droidPartAvailabilityReason(table, "pie1"));
    TEST_ASSERT_EQUAL_INT(CONSOLE_REASON_PART_NOT_ASSIGNED,
                          droidPartAvailabilityReason(table, "psiFront"));
    TEST_ASSERT_EQUAL_INT(CONSOLE_REASON_PART_NOT_ASSIGNED,
                          droidPartAvailabilityReason(table, "doorFL"));
}

void test_a_breadpan_door_can_be_recorded_on_the_output_that_moves_it() {
    // The capability the decision buys. A builder who Y-harnesses a spare
    // output to the front-left breadpan door records the door's own id, and the
    // droid answers for it by name from then on - no escape-hatch slot, and no
    // screen anywhere reading "Other part 7" for a part the catalog can name.
    ServoOutputTable table = oneEmptyOutput();
    TEST_ASSERT_TRUE(servoOutputAddPart(&table.rows[0], "doorFL"));
    TEST_ASSERT_EQUAL_UINT8(1, servoOutputPartCount(table.rows[0]));
    TEST_ASSERT_EQUAL_STRING("doorFL", servoOutputPartAt(table.rows[0], 0));
    TEST_ASSERT_EQUAL_INT(CONSOLE_REASON_NONE,
                          droidPartAvailabilityReason(table, "doorFL"));
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
    // A step that outlived its catalog is "not a Part here" rather than "a Part
    // nobody wired" - the difference between sending a builder to the bench and
    // telling them the truth. Widening the vocabulary to the whole catalog did
    // not widen it to anything a builder types.
    const ServoOutputTable table = oneEmptyOutput();
    TEST_ASSERT_EQUAL_INT(CONSOLE_REASON_UNKNOWN_ARGUMENT,
                          droidPartAvailabilityReason(table, "armOfTheFuture"));
    TEST_ASSERT_EQUAL_INT(CONSOLE_REASON_UNKNOWN_ARGUMENT,
                          droidPartAvailabilityReason(table, "other11"));
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

    RUN_TEST(test_every_declared_part_is_one_firmware_names);
    RUN_TEST(test_a_light_is_a_part_here_exactly_as_a_panel_is);
    RUN_TEST(test_the_escape_hatch_is_nameable_in_firmware);
    RUN_TEST(test_the_vocabulary_is_the_whole_catalog_and_fits_a_row);
    RUN_TEST(test_the_vocabulary_refuses_what_is_not_an_id);
    RUN_TEST(test_every_id_in_the_table_is_reachable_by_index);

    RUN_TEST(test_the_catalog_spelling_is_the_one_the_firmware_answers_with);

    RUN_TEST(test_a_known_part_no_output_claims_reports_part_not_assigned);
    RUN_TEST(test_a_part_the_body_never_drives_is_unassigned_not_unknown);
    RUN_TEST(test_a_breadpan_door_can_be_recorded_on_the_output_that_moves_it);
    RUN_TEST(test_wiring_the_arm_later_is_what_changes_the_answer);
    RUN_TEST(test_a_ganged_lead_answers_for_every_part_it_moves);
    RUN_TEST(test_an_id_this_build_does_not_model_is_not_reported_as_unwired);
    RUN_TEST(test_a_stored_count_past_the_table_does_not_walk_off_the_end);

    return UNITY_END();
}
