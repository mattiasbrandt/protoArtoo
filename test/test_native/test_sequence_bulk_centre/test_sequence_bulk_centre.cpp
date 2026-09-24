// =============================================================================
// test/test_native/test_sequence_bulk_centre/test_sequence_bulk_centre.cpp
//
// Put every Servo Output back to centre, paced by the droid (#318, #365), and
// the boot pass that runs on the same cursor at power-up (#414).
//
// What these hold is the part the browser must never be able to reach: the
// spacing between two Outputs the Coordinator starts, the rows it passes over,
// and the fact that a halt ends the sweep where it has got to. The expansion is
// generated from this droid's own rows, so the tests hand it rows.
// =============================================================================
#include <unity.h>

#include "sequence_bulk_centre.h"

void setUp() {}
void tearDown() {}

// A row on a real LEDC servo channel, measured, with a centre the builder put
// somewhere other than the middle of its travel.
static ServoOutputRow servoRow(uint8_t channel, uint16_t centreUs, uint16_t throwMs) {
    ServoOutputRow row = {};
    row.driver = SERVO_DRIVER_LEDC;
    row.channel = channel;
    row.open_us = 1900;
    row.centre_us = centreUs;
    row.close_us = 1100;
    row.throw_ms = throwMs;
    row.component = SERVO_COMP_MG996R;
    row.calibrated = true;
    return row;
}

// -----------------------------------------------------------------------------
// Where one Output is sent
// -----------------------------------------------------------------------------

// The stored third position, not the middle of the Endpoint Pair. A builder who
// pressed Set CENTER at 1560 gets 1560; half the throw would be 1500, and the
// two are different on purpose (include/servo_output_row.h).
void test_an_output_goes_to_its_recorded_centre_not_to_half_its_throw() {
    const ServoOutputRow row = servoRow(LEDC_CH_ARM1, 1560, 800);
    const SeqBodyStepPlan plan = sequenceBodyCentrePlan(row);

    TEST_ASSERT_TRUE(plan.drive);
    TEST_ASSERT_EQUAL_UINT8(CONSOLE_REASON_NONE, (uint8_t)plan.reason);
    TEST_ASSERT_EQUAL_UINT16(1560, plan.targetUs);
    TEST_ASSERT_EQUAL_UINT16((uint16_t)((row.open_us + row.close_us) / 2), 1500);
}

// A reversed linkage is open < close and nothing else records it. The centre is
// its own number, so reversing changes nothing about where a centre lands.
void test_a_reversed_pair_does_not_move_the_centre() {
    ServoOutputRow row = servoRow(LEDC_CH_ARM2, 1440, 800);
    const uint16_t wasOpen = row.open_us;
    row.open_us = row.close_us;
    row.close_us = wasOpen;

    const SeqBodyStepPlan plan = sequenceBodyCentrePlan(row);
    TEST_ASSERT_TRUE(plan.drive);
    TEST_ASSERT_EQUAL_UINT16(1440, plan.targetUs);
}

// Every door onto a row goes through the component clamp (ADR 0041), this one
// included: an MG996R row cannot be driven to 2400 us by any route, so a stored
// centre outside its band arrives clamped rather than on the pin.
void test_the_component_band_bounds_the_centre() {
    ServoOutputRow row = servoRow(LEDC_CH_AUX1, 2400, 800);
    const SeqBodyStepPlan plan = sequenceBodyCentrePlan(row);

    TEST_ASSERT_TRUE(plan.drive);
    TEST_ASSERT_EQUAL_UINT16(SERVO_BAND_STD.hi, plan.targetUs);
}

// The armId servoCmdQueue speaks, resolved from the Output Address the row
// records - the one bridge between the two vocabularies.
void test_the_output_address_resolves_to_the_arm_the_queue_speaks() {
    TEST_ASSERT_EQUAL_UINT8(0, sequenceBodyCentrePlan(servoRow(LEDC_CH_ARM1, 1500, 800)).armId);
    TEST_ASSERT_EQUAL_UINT8(4, sequenceBodyCentrePlan(servoRow(LEDC_CH_AUX3, 1500, 800)).armId);
}

// LEDC's DOME channel drives a brushless ESC, so no row should be addressed
// there and none can be driven there. Reported, not driven, and not silent.
void test_a_row_on_a_channel_that_is_not_a_servo_is_not_driven() {
    const SeqBodyStepPlan plan = sequenceBodyCentrePlan(servoRow(LEDC_CH_DOME, 1500, 800));

    TEST_ASSERT_FALSE(plan.drive);
    TEST_ASSERT_EQUAL_UINT8(CONSOLE_REASON_NOT_IN_THIS_BUILD, (uint8_t)plan.reason);
}

// -----------------------------------------------------------------------------
// Which rows the sweep covers
// -----------------------------------------------------------------------------

// A light has no centre to go back to. The row's own record of what is on the
// end of the wire is what says so.
void test_a_row_recorded_as_an_led_strip_has_nothing_to_centre() {
    ServoOutputRow row = servoRow(LEDC_CH_AUX2, 1500, 800);
    TEST_ASSERT_TRUE(sequenceBulkCentreHasTravel(row));

    row.component = SERVO_COMP_RGB;
    TEST_ASSERT_FALSE(sequenceBulkCentreHasTravel(row));
}

// A row nobody has said anything about is not a light: an unstated component
// gets the cautious band, never an assumption that the wire does not move.
void test_a_row_with_no_component_stated_still_has_travel() {
    ServoOutputRow row = servoRow(LEDC_CH_AUX2, 1500, 800);
    row.component = SERVO_COMP_NONE;
    TEST_ASSERT_TRUE(sequenceBulkCentreHasTravel(row));
}

// -----------------------------------------------------------------------------
// The Cadence Floor
// -----------------------------------------------------------------------------

// The number, and whose it is. 450 ms is the DOME's, measured on dome hardware
// on 2026-06-17/-18; the body's is unmeasured and this stands in for it (#355).
void test_the_cadence_floor_is_the_dome_figure_standing_in() {
    TEST_ASSERT_EQUAL_UINT32(450, SEQ_CADENCE_FLOOR_MS);
}

// Floored, never refused: a throw faster than the rail allows gets the rail's
// pace and the sweep carries on, with no error anywhere.
void test_a_throw_faster_than_the_floor_is_floored() {
    TEST_ASSERT_EQUAL_UINT32(SEQ_CADENCE_FLOOR_MS, sequenceCadenceSpacingMs(200));
    TEST_ASSERT_EQUAL_UINT32(SEQ_CADENCE_FLOOR_MS, sequenceCadenceSpacingMs(0));
    TEST_ASSERT_EQUAL_UINT32(SEQ_CADENCE_FLOOR_MS, sequenceCadenceSpacingMs(450));
}

// An Output slower than the floor keeps its own pace: one servo actuating at a
// time is the rail rule, so a two-second door holds the next Output off for two
// seconds rather than for the floor.
void test_a_throw_slower_than_the_floor_keeps_its_own_pace() {
    TEST_ASSERT_EQUAL_UINT32(451, sequenceCadenceSpacingMs(451));
    TEST_ASSERT_EQUAL_UINT32(2000, sequenceCadenceSpacingMs(2000));
}

// -----------------------------------------------------------------------------
// The run
// -----------------------------------------------------------------------------

void test_a_fresh_run_is_not_running() {
    SeqBulkCentreRun run = {};
    TEST_ASSERT_FALSE(sequenceBulkCentreRowDue(run, 1000));
}

// The first Output starts at once: the Floor spaces Outputs from each other,
// and there is nothing yet to space this one from.
void test_the_first_output_is_due_at_once() {
    SeqBulkCentreRun run = {};
    sequenceBulkCentreStart(&run, 5000, SRC_WEB_API);

    TEST_ASSERT_TRUE(run.active);
    TEST_ASSERT_EQUAL_UINT8(0, run.nextRow);
    TEST_ASSERT_TRUE(sequenceBulkCentreRowDue(run, 5000));
}

// A started Output holds the next one off by the spacing its own throw asks
// for, floored. Nothing is due in between.
void test_a_started_output_spaces_the_next_one() {
    SeqBulkCentreRun run = {};
    sequenceBulkCentreStart(&run, 5000, SRC_WEB_API);
    sequenceBulkCentreAdvance(&run, 5, 5000, true, 300);

    TEST_ASSERT_EQUAL_UINT8(1, run.centred);
    TEST_ASSERT_FALSE(sequenceBulkCentreRowDue(run, 5000 + SEQ_CADENCE_FLOOR_MS - 1));
    TEST_ASSERT_TRUE(sequenceBulkCentreRowDue(run, 5000 + SEQ_CADENCE_FLOOR_MS));
}

// A skipped row spaces nothing: nothing moved, so there is no inrush to hold
// apart and the sweep does not stall on a light.
void test_a_skipped_row_costs_the_sweep_no_time() {
    SeqBulkCentreRun run = {};
    sequenceBulkCentreStart(&run, 5000, SRC_WEB_API);
    sequenceBulkCentreAdvance(&run, 5, 5000, false, 0);

    TEST_ASSERT_EQUAL_UINT8(1, run.skipped);
    TEST_ASSERT_EQUAL_UINT8(0, run.centred);
    TEST_ASSERT_TRUE(sequenceBulkCentreRowDue(run, 5000));
}

// The sweep ends after the last row, and the counts are what the Coordinator
// reports.
void test_the_run_ends_after_the_last_row() {
    SeqBulkCentreRun run = {};
    sequenceBulkCentreStart(&run, 0, SRC_WEB_API);
    sequenceBulkCentreAdvance(&run, 3, 0, true, 100);
    sequenceBulkCentreAdvance(&run, 3, 450, false, 0);
    TEST_ASSERT_TRUE(run.active);
    sequenceBulkCentreAdvance(&run, 3, 450, true, 100);

    TEST_ASSERT_FALSE(run.active);
    TEST_ASSERT_EQUAL_UINT8(2, run.centred);
    TEST_ASSERT_EQUAL_UINT8(1, run.skipped);
}

// A table that shrank under the run - a save between two steps - ends it at the
// new count rather than walking past the end of it.
void test_a_table_that_shrank_ends_the_run() {
    SeqBulkCentreRun run = {};
    sequenceBulkCentreStart(&run, 0, SRC_WEB_API);
    sequenceBulkCentreAdvance(&run, 5, 0, true, 100);
    TEST_ASSERT_TRUE(run.active);

    sequenceBulkCentreAdvance(&run, 1, 450, true, 100);
    TEST_ASSERT_FALSE(run.active);
}

// ANY ESTOP ENDS THE RUN (#365 criterion 6). Nothing further is commanded, and
// what the sweep had reached is still reportable.
void test_a_halt_ends_the_run_where_it_got_to() {
    SeqBulkCentreRun run = {};
    sequenceBulkCentreStart(&run, 0, SRC_WEB_API);
    sequenceBulkCentreAdvance(&run, 9, 0, true, 100);
    sequenceBulkCentreAdvance(&run, 9, 450, true, 100);

    sequenceBulkCentreEnd(&run);

    TEST_ASSERT_FALSE(run.active);
    TEST_ASSERT_EQUAL_UINT8(2, run.centred);
    // Ended means ended: no later moment brings a row round again.
    TEST_ASSERT_FALSE(sequenceBulkCentreRowDue(run, 900));
    TEST_ASSERT_FALSE(sequenceBulkCentreRowDue(run, 90000));
    // And nothing advances a run that is over.
    sequenceBulkCentreAdvance(&run, 9, 900, true, 100);
    TEST_ASSERT_EQUAL_UINT8(2, run.centred);
}

// A second press supersedes the sweep in flight rather than queueing behind it:
// two overlapping sweeps is the many-at-once shape the Floor exists to prevent.
void test_pressing_again_starts_over_rather_than_queueing() {
    SeqBulkCentreRun run = {};
    sequenceBulkCentreStart(&run, 0, SRC_WEB_API);
    sequenceBulkCentreAdvance(&run, 9, 0, true, 2000);
    TEST_ASSERT_FALSE(sequenceBulkCentreRowDue(run, 100));

    sequenceBulkCentreStart(&run, 100, SRC_WEB_CONSOLE);

    TEST_ASSERT_EQUAL_UINT8(0, run.nextRow);
    TEST_ASSERT_EQUAL_UINT8(0, run.centred);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)SRC_WEB_CONSOLE, run.src);
    TEST_ASSERT_TRUE(sequenceBulkCentreRowDue(run, 100));
}

// millis() wraps every 49 days; a sweep across the wrap is judged on elapsed
// time, the way every other due-time test in this firmware is.
void test_a_run_across_a_millis_wrap_is_judged_on_elapsed_time() {
    SeqBulkCentreRun run = {};
    const uint32_t nearWrap = 0xFFFFFF00u;
    sequenceBulkCentreStart(&run, nearWrap, SRC_WEB_API);
    sequenceBulkCentreAdvance(&run, 5, nearWrap, true, 100);

    TEST_ASSERT_FALSE(sequenceBulkCentreRowDue(run, (uint32_t)(nearWrap + 449)));
    TEST_ASSERT_TRUE(sequenceBulkCentreRowDue(run, (uint32_t)(nearWrap + 450)));
}

// -----------------------------------------------------------------------------
// The boot pass (ADR 0052, #414): the same cursor, each row asked its boot mode
// -----------------------------------------------------------------------------

static ServoOutputRow bootRow(uint8_t channel, ServoBootBehaviour boot, uint16_t throwMs) {
    ServoOutputRow row = servoRow(channel, 1560, throwMs);
    row.boot = boot;
    return row;
}

static SeqBulkCentreRun bootPass(uint32_t nowMs) {
    SeqBulkCentreRun run = {};
    TEST_ASSERT_TRUE(sequenceBootPassStart(&run, nowMs, /*estopLatched=*/false, /*sleepMode=*/false));
    return run;
}

// A limp row commands nothing and costs the pass no time: the Part stays where
// it was left. Limp is every row's default, so a droid nobody has configured
// does not move at power-up at all.
void test_limp_commands_nothing_at_boot() {
    SeqBulkCentreRun run = bootPass(1000);
    const SeqBulkCentreRowStep step = sequenceBulkCentreRowStep(run, bootRow(LEDC_CH_ARM1, SERVO_BOOT_LIMP, 800));
    TEST_ASSERT_FALSE(step.centre);
    TEST_ASSERT_FALSE(step.releaseAfter);

    ServoOutputRow fresh = {};
    servoOutputRowDefaults(&fresh, SERVO_DRIVER_LEDC, LEDC_CH_ARM2, SERVO_COMP_MG996R);
    TEST_ASSERT_FALSE(sequenceBulkCentreRowStep(run, fresh).centre);

    sequenceBulkCentreAdvance(&run, 5, 1000, step.centre, 800);
    TEST_ASSERT_EQUAL_UINT8(0, run.centred);
    TEST_ASSERT_TRUE(sequenceBulkCentreRowDue(run, 1000));
}

// The two home modes go to the recorded centre, the same plan a press makes;
// only "go home and release" owes a release afterwards.
void test_the_two_home_modes_go_to_the_recorded_centre() {
    const SeqBulkCentreRun run = bootPass(0);
    const ServoOutputRow hold = bootRow(LEDC_CH_ARM1, SERVO_BOOT_HOME_HOLD, 800);
    const ServoOutputRow release = bootRow(LEDC_CH_AUX1, SERVO_BOOT_HOME_RELEASE, 800);

    const SeqBulkCentreRowStep holdStep = sequenceBulkCentreRowStep(run, hold);
    TEST_ASSERT_TRUE(holdStep.centre);
    TEST_ASSERT_FALSE(holdStep.releaseAfter);
    const SeqBulkCentreRowStep releaseStep = sequenceBulkCentreRowStep(run, release);
    TEST_ASSERT_TRUE(releaseStep.centre);
    TEST_ASSERT_TRUE(releaseStep.releaseAfter);

    TEST_ASSERT_EQUAL_UINT16(1560, sequenceBodyCentrePlan(hold).targetUs);
    TEST_ASSERT_EQUAL_UINT16(1560, sequenceBodyCentrePlan(release).targetUs);
}

// A press is not asked the boot mode: the operator asked for every Output, and
// a limp row goes back to centre like any other.
void test_a_press_centres_a_limp_row_the_boot_pass_leaves_alone() {
    SeqBulkCentreRun run = {};
    sequenceBulkCentreStart(&run, 0, SRC_WEB_API);
    const SeqBulkCentreRowStep step = sequenceBulkCentreRowStep(run, bootRow(LEDC_CH_ARM1, SERVO_BOOT_LIMP, 800));
    TEST_ASSERT_TRUE(step.centre);
    TEST_ASSERT_FALSE(step.releaseAfter);
    TEST_ASSERT_FALSE(sequenceBulkCentreRowStep(run, bootRow(LEDC_CH_ARM1, SERVO_BOOT_HOME_RELEASE, 800)).releaseAfter);
}

// Outputs never start together: each home row holds the next off by the
// Cadence Floor, exactly as a press does.
void test_the_boot_pass_is_spaced_by_the_cadence_floor() {
    SeqBulkCentreRun run = bootPass(2000);
    sequenceBulkCentreAdvance(&run, 5, 2000, true, 200);

    TEST_ASSERT_FALSE(sequenceBulkCentreRowDue(run, 2000 + SEQ_CADENCE_FLOOR_MS - 1));
    TEST_ASSERT_TRUE(sequenceBulkCentreRowDue(run, 2000 + SEQ_CADENCE_FLOOR_MS));
}

// A droid that powers up with the estop latched - a TWDT reset latches it on
// the way up - runs no boot pass: nothing is due at boot, and nothing becomes
// due later, whenever the estop is cleared. Sleep Mode the same.
void test_nothing_moves_at_boot_when_estop_is_latched() {
    SeqBulkCentreRun run = {};
    TEST_ASSERT_FALSE(sequenceBootPassStart(&run, 0, /*estopLatched=*/true, /*sleepMode=*/false));
    TEST_ASSERT_FALSE(run.active);
    TEST_ASSERT_FALSE(sequenceBulkCentreRowDue(run, 0));
    TEST_ASSERT_FALSE(sequenceBulkCentreRowDue(run, 600000));

    TEST_ASSERT_FALSE(sequenceBootPassStart(&run, 0, /*estopLatched=*/false, /*sleepMode=*/true));
    TEST_ASSERT_FALSE(run.active);
}

// An estop that latches during the pass ends it where it got to, and a release
// the pass owed goes with it: the estop has already let go of every Output.
void test_an_estop_during_the_boot_pass_ends_it_and_owes_nothing() {
    SeqBulkCentreRun run = bootPass(0);
    sequenceBulkCentreAwait(&run, 2, /*release=*/true);
    sequenceBulkCentreAdvance(&run, 5, 0, true, 800);

    sequenceBulkCentreEnd(&run);

    TEST_ASSERT_FALSE(run.active);
    ServoCommandedPosition settled = {};
    settled.pulsing = true;
    TEST_ASSERT_EQUAL_UINT8(SEQ_AWAIT_WAIT, sequenceBulkCentreAwaitCheck(run, 90000, settled));
}

// "Go home and release" on an overshoot row: the move outlasts one full throw,
// because it goes out to its aim and settles back. The release must not come
// before it settles, however long past the throw time that is.
void test_a_release_waits_for_an_overshoot_to_settle() {
    SeqBulkCentreRun run = bootPass(0);
    ServoOutputRow row = bootRow(LEDC_CH_AUX1, SERVO_BOOT_HOME_RELEASE, 800);
    row.easing = SERVO_EASE_OVERSHOOT;
    const SeqBulkCentreRowStep step = sequenceBulkCentreRowStep(run, row);
    TEST_ASSERT_TRUE(step.releaseAfter);
    sequenceBulkCentreAwait(&run, 2, /*release=*/true);
    sequenceBulkCentreAdvance(&run, 1, 0, true, row.throw_ms);
    TEST_ASSERT_TRUE(run.active);  // the last row, but a release is still owed

    ServoCommandedPosition at = {};
    at.pulsing = true;
    at.moving = true;
    at.nowUs = 1560;  // passing through its target on the way out to the aim
    at.targetUs = 1560;
    TEST_ASSERT_EQUAL_UINT8(SEQ_AWAIT_WAIT, sequenceBulkCentreAwaitCheck(run, 799, at));
    TEST_ASSERT_EQUAL_UINT8(SEQ_AWAIT_WAIT, sequenceBulkCentreAwaitCheck(run, 800, at));
    TEST_ASSERT_EQUAL_UINT8(SEQ_AWAIT_WAIT, sequenceBulkCentreAwaitCheck(run, 1400, at));

    at.moving = false;  // settled back onto the centre
    TEST_ASSERT_EQUAL_UINT8(SEQ_AWAIT_RELEASE, sequenceBulkCentreAwaitCheck(run, 1420, at));
    sequenceBulkCentreAwaitOver(&run, 1);
    TEST_ASSERT_FALSE(run.active);
}

// Even settled, the release is never looked at before the Output's floored
// full-throw time: that is the earliest check, a snap included.
void test_a_release_is_never_earlier_than_the_floored_throw() {
    SeqBulkCentreRun run = bootPass(0);
    sequenceBulkCentreAwait(&run, 0, /*release=*/true);
    sequenceBulkCentreAdvance(&run, 5, 0, true, 200);

    ServoCommandedPosition settled = {};
    settled.pulsing = true;
    TEST_ASSERT_EQUAL_UINT8(SEQ_AWAIT_WAIT, sequenceBulkCentreAwaitCheck(run, SEQ_CADENCE_FLOOR_MS - 1, settled));
    TEST_ASSERT_EQUAL_UINT8(SEQ_AWAIT_RELEASE, sequenceBulkCentreAwaitCheck(run, SEQ_CADENCE_FLOOR_MS, settled));
}

// An Output ServoTask never drove - switched off, or carrying a light - has no
// pulse to take off: the release is dropped, not sent, and the pass goes on.
void test_a_release_on_an_output_with_no_pulse_is_dropped() {
    SeqBulkCentreRun run = bootPass(0);
    sequenceBulkCentreAwait(&run, 3, /*release=*/true);
    sequenceBulkCentreAdvance(&run, 5, 0, true, 500);

    const ServoCommandedPosition limp = {};
    TEST_ASSERT_EQUAL_UINT8(SEQ_AWAIT_DROP, sequenceBulkCentreAwaitCheck(run, 500, limp));
    sequenceBulkCentreAwaitOver(&run, 5);
    TEST_ASSERT_TRUE(run.active);
    TEST_ASSERT_EQUAL_UINT8(SEQ_BULK_CENTRE_NO_AWAIT, run.awaitArm);
}

// One servo actuating at a time (#417). An overshoot row outlasts its floored
// throw - it goes out to its aim and settles back - and the next row waits for
// it to settle rather than starting beside it. A row that owes no release is
// waited on exactly as one that does.
void test_the_next_row_waits_for_an_output_still_moving() {
    SeqBulkCentreRun run = {};
    sequenceBulkCentreStart(&run, 0, SRC_WEB_API);
    ServoOutputRow row = servoRow(LEDC_CH_ARM1, 1500, 800);
    row.easing = SERVO_EASE_OVERSHOOT;
    const SeqBulkCentreRowStep step = sequenceBulkCentreRowStep(run, row);
    TEST_ASSERT_TRUE(step.centre);
    TEST_ASSERT_FALSE(step.releaseAfter);
    sequenceBulkCentreAwait(&run, sequenceBodyCentrePlan(row).armId, step.releaseAfter);
    sequenceBulkCentreAdvance(&run, 5, 0, true, row.throw_ms);

    ServoCommandedPosition at = {};
    at.pulsing = true;
    at.moving = true;
    TEST_ASSERT_EQUAL_UINT8(SEQ_AWAIT_WAIT, sequenceBulkCentreAwaitCheck(run, 799, at));
    TEST_ASSERT_EQUAL_UINT8(SEQ_AWAIT_WAIT, sequenceBulkCentreAwaitCheck(run, 800, at));
    TEST_ASSERT_EQUAL_UINT8(SEQ_AWAIT_WAIT, sequenceBulkCentreAwaitCheck(run, 1400, at));

    at.moving = false;  // settled back onto its centre
    TEST_ASSERT_EQUAL_UINT8(SEQ_AWAIT_DONE, sequenceBulkCentreAwaitCheck(run, 1420, at));
    sequenceBulkCentreAwaitOver(&run, 5);
    TEST_ASSERT_TRUE(run.active);
    TEST_ASSERT_EQUAL_UINT8(SEQ_BULK_CENTRE_NO_AWAIT, run.awaitArm);
    TEST_ASSERT_TRUE(sequenceBulkCentreRowDue(run, 1420));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_an_output_goes_to_its_recorded_centre_not_to_half_its_throw);
    RUN_TEST(test_a_reversed_pair_does_not_move_the_centre);
    RUN_TEST(test_the_component_band_bounds_the_centre);
    RUN_TEST(test_the_output_address_resolves_to_the_arm_the_queue_speaks);
    RUN_TEST(test_a_row_on_a_channel_that_is_not_a_servo_is_not_driven);
    RUN_TEST(test_a_row_recorded_as_an_led_strip_has_nothing_to_centre);
    RUN_TEST(test_a_row_with_no_component_stated_still_has_travel);
    RUN_TEST(test_the_cadence_floor_is_the_dome_figure_standing_in);
    RUN_TEST(test_a_throw_faster_than_the_floor_is_floored);
    RUN_TEST(test_a_throw_slower_than_the_floor_keeps_its_own_pace);
    RUN_TEST(test_a_fresh_run_is_not_running);
    RUN_TEST(test_the_first_output_is_due_at_once);
    RUN_TEST(test_a_started_output_spaces_the_next_one);
    RUN_TEST(test_a_skipped_row_costs_the_sweep_no_time);
    RUN_TEST(test_the_run_ends_after_the_last_row);
    RUN_TEST(test_a_table_that_shrank_ends_the_run);
    RUN_TEST(test_a_halt_ends_the_run_where_it_got_to);
    RUN_TEST(test_pressing_again_starts_over_rather_than_queueing);
    RUN_TEST(test_a_run_across_a_millis_wrap_is_judged_on_elapsed_time);
    RUN_TEST(test_limp_commands_nothing_at_boot);
    RUN_TEST(test_the_two_home_modes_go_to_the_recorded_centre);
    RUN_TEST(test_a_press_centres_a_limp_row_the_boot_pass_leaves_alone);
    RUN_TEST(test_the_boot_pass_is_spaced_by_the_cadence_floor);
    RUN_TEST(test_nothing_moves_at_boot_when_estop_is_latched);
    RUN_TEST(test_an_estop_during_the_boot_pass_ends_it_and_owes_nothing);
    RUN_TEST(test_a_release_waits_for_an_overshoot_to_settle);
    RUN_TEST(test_a_release_is_never_earlier_than_the_floored_throw);
    RUN_TEST(test_a_release_on_an_output_with_no_pulse_is_dropped);
    RUN_TEST(test_the_next_row_waits_for_an_output_still_moving);
    return UNITY_END();
}
