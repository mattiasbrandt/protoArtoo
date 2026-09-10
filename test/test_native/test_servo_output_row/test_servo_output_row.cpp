// =============================================================================
// test/test_native/test_servo_output_row/test_servo_output_row.cpp
//
// The Servo Output row model (ADR 0041, ADR 0052) and its storage door.
//
// What these cover is the behaviour the row exists for: reverse is the pair
// rather than a flag, the component type governs the clamp, overshoot switches
// itself off on an unmeasured output, a capture never ticks the boot flag, a
// damaged record loses only the field nobody can read, and every field comes
// back off the wire the way it went on.
// =============================================================================
#include <cstdio>
#include <string>

#include <unity.h>

#include "config_serializer.h"
#include "servo_output_row.h"

#include "../../../test/stubs/config/map_config_io.h"

void setUp() {}
void tearDown() {}

namespace {

ServoOutputRow mg996rRow() {
    ServoOutputRow row = {};
    servoOutputRowDefaults(&row, SERVO_DRIVER_LEDC, LEDC_CH_ARM1, SERVO_COMP_MG996R);
    return row;
}

}  // namespace

// --- defaults ----------------------------------------------------------------

void test_defaults_never_hand_out_a_zero_travel_time() {
    const ServoOutputRow row = mg996rRow();
    // Zero is the dangerous value for a travel time, not the neutral one: an
    // instant move on a panel is a slam.
    TEST_ASSERT_TRUE(row.throw_ms > 0);
    TEST_ASSERT_TRUE(row.accel_ms > 0);
    TEST_ASSERT_EQUAL_UINT16(SERVO_THROW_MS_DEFAULT, row.throw_ms);
    TEST_ASSERT_EQUAL_UINT16(SERVO_ACCEL_MS_DEFAULT, row.accel_ms);
}

void test_defaults_are_limp_and_unmeasured() {
    const ServoOutputRow row = mg996rRow();
    TEST_ASSERT_EQUAL_UINT8(SERVO_BOOT_LIMP, row.boot);
    TEST_ASSERT_FALSE(row.calibrated);
    TEST_ASSERT_EQUAL_STRING("", row.part);
    TEST_ASSERT_EQUAL_UINT16(SERVO_RELEASE_MS_NEVER, row.release_ms);
}

void test_default_table_matches_the_five_fixed_outputs() {
    ServoOutputTable table = {};
    servoOutputTableDefaults(&table);

    TEST_ASSERT_EQUAL_UINT8(5, table.count);
    TEST_ASSERT_EQUAL_UINT8(LEDC_CH_ARM1, table.rows[0].channel);
    TEST_ASSERT_EQUAL_UINT8(LEDC_CH_AUX3, table.rows[4].channel);
    TEST_ASSERT_EQUAL_UINT8(SERVO_COMP_MG996R, table.rows[0].component);
    TEST_ASSERT_EQUAL_UINT8(SERVO_COMP_NONE, table.rows[2].component);

    // The five fixed field sets default to open 2000 / close 1000 on every
    // output; the rows agree with them by construction while both exist.
    for (uint8_t i = 0; i < table.count; ++i) {
        TEST_ASSERT_EQUAL_UINT16(2000, table.rows[i].open_us);
        TEST_ASSERT_EQUAL_UINT16(1500, table.rows[i].centre_us);
        TEST_ASSERT_EQUAL_UINT16(1000, table.rows[i].close_us);
    }
}

void test_rows_past_the_count_are_still_safe() {
    ServoOutputTable table = {};
    servoOutputTableDefaults(&table);
    const ServoOutputRow& spare = table.rows[SERVO_OUTPUT_ROW_MAX - 1];

    TEST_ASSERT_EQUAL_UINT8(SERVO_OUTPUT_CHANNEL_UNSET, spare.channel);
    TEST_ASSERT_FALSE(servoOutputChannelIsValid(spare.driver, spare.channel));
    TEST_ASSERT_TRUE(spare.throw_ms > 0);
    TEST_ASSERT_EQUAL_UINT8(SERVO_BOOT_LIMP, spare.boot);
}

// --- reverse is the pair, never a flag ---------------------------------------

void test_reverse_is_read_off_the_pair() {
    ServoOutputRow row = mg996rRow();
    row.open_us = 1900;
    row.close_us = 1100;
    TEST_ASSERT_FALSE(servoOutputIsReversed(row));
    TEST_ASSERT_EQUAL_UINT16(1100, servoOutputLowUs(row));
    TEST_ASSERT_EQUAL_UINT16(1900, servoOutputHighUs(row));

    // A reversed linkage is a swap of the pair and nothing else changes.
    const uint16_t open = row.open_us;
    row.open_us = row.close_us;
    row.close_us = open;
    TEST_ASSERT_TRUE(servoOutputIsReversed(row));
    TEST_ASSERT_EQUAL_UINT16(1100, servoOutputLowUs(row));
    TEST_ASSERT_EQUAL_UINT16(1900, servoOutputHighUs(row));
}

// --- the component type governs the clamp ------------------------------------

void test_mg996r_row_cannot_reach_500us() {
    ServoOutputRow row = mg996rRow();
    TEST_ASSERT_EQUAL_UINT16(1000, servoOutputClampPulse(row, 500));
    TEST_ASSERT_EQUAL_UINT16(2000, servoOutputClampPulse(row, 2500));
    TEST_ASSERT_EQUAL_UINT16(1234, servoOutputClampPulse(row, 1234));
}

void test_mg90s_row_takes_the_full_band() {
    ServoOutputRow row = {};
    servoOutputRowDefaults(&row, SERVO_DRIVER_LEDC, LEDC_CH_AUX1, SERVO_COMP_MG90S);
    TEST_ASSERT_EQUAL_UINT16(500, servoOutputClampPulse(row, 500));
    TEST_ASSERT_EQUAL_UINT16(2500, servoOutputClampPulse(row, 2500));
    TEST_ASSERT_EQUAL_UINT16(500, row.close_us);
    TEST_ASSERT_EQUAL_UINT16(2500, row.open_us);
}

void test_an_unstated_component_gets_the_cautious_band() {
    ServoOutputRow row = {};
    servoOutputRowDefaults(&row, SERVO_DRIVER_LEDC, LEDC_CH_AUX2, SERVO_COMP_NONE);
    TEST_ASSERT_EQUAL_UINT16(1000, servoOutputClampPulse(row, 500));
}

void test_the_stored_door_clamps_to_the_band_too() {
    ServoOutputRow row = mg996rRow();
    ServoOutputRow edited = row;
    edited.open_us = 2500;  // a stored number an MG996R will not take
    const uint16_t repaired = servoOutputRowNormalise(&edited, row);

    TEST_ASSERT_EQUAL_UINT16(2000, edited.open_us);
    TEST_ASSERT_TRUE((repaired & SERVO_FIELD_OPEN) != 0);
}

// --- overshoot degrades until somebody has measured the output ---------------

void test_overshoot_degrades_while_uncalibrated() {
    ServoOutputRow row = mg996rRow();
    row.easing = SERVO_EASE_OVERSHOOT;
    row.calibrated = false;
    TEST_ASSERT_EQUAL_UINT8(SERVO_EASE_NONE, servoOutputEffectiveEasing(row));

    // The stored value survives the degrade, so it comes back the moment the
    // ends exist.
    TEST_ASSERT_EQUAL_UINT8(SERVO_EASE_OVERSHOOT, row.easing);

    row.calibrated = true;
    TEST_ASSERT_EQUAL_UINT8(SERVO_EASE_OVERSHOOT, servoOutputEffectiveEasing(row));
}

void test_soft_easing_is_not_gated_on_calibration() {
    ServoOutputRow row = mg996rRow();
    row.easing = SERVO_EASE_SOFT;
    row.calibrated = false;
    TEST_ASSERT_EQUAL_UINT8(SERVO_EASE_SOFT, servoOutputEffectiveEasing(row));
}

// --- capture records an end, and only an end ---------------------------------

void test_capture_marks_calibrated_and_never_ticks_boot() {
    ServoOutputRow row = mg996rRow();
    row.boot = SERVO_BOOT_LIMP;

    servoOutputCapture(&row, SERVO_END_OPEN, 1850);

    TEST_ASSERT_EQUAL_UINT16(1850, row.open_us);
    TEST_ASSERT_TRUE(row.calibrated);
    // Finding an endpoint must never be the act that makes a panel move at
    // power-up.
    TEST_ASSERT_EQUAL_UINT8(SERVO_BOOT_LIMP, row.boot);
}

void test_capture_is_clamped_by_the_component_type() {
    ServoOutputRow row = mg996rRow();
    servoOutputCapture(&row, SERVO_END_CLOSE, 500);
    TEST_ASSERT_EQUAL_UINT16(1000, row.close_us);
}

// --- one validator at every door ---------------------------------------------

void test_an_unreadable_record_takes_the_safe_defaults_and_reports() {
    const ServoOutputRow defaults = mg996rRow();
    ServoOutputRow parsed = {};
    const uint16_t repaired = servoOutputRowParse("not a record at all", defaults, &parsed);

    TEST_ASSERT_EQUAL_UINT16((uint16_t)((1u << SERVO_OUTPUT_FIELD_COUNT) - 1u), repaired);
    TEST_ASSERT_EQUAL_UINT16(defaults.open_us, parsed.open_us);
    TEST_ASSERT_EQUAL_UINT16(defaults.throw_ms, parsed.throw_ms);
    TEST_ASSERT_FALSE(parsed.calibrated);
    TEST_ASSERT_EQUAL_UINT8(SERVO_BOOT_LIMP, parsed.boot);
}

void test_one_bad_field_does_not_cost_the_row_its_calibration() {
    const ServoOutputRow defaults = mg996rRow();
    // Everything readable except the ease word.
    const char* record = "ledc:0:doorFL:1900:1500:1100:750:200:0:bouncy:home-hold:mg996r:1";
    ServoOutputRow parsed = {};
    const uint16_t repaired = servoOutputRowParse(record, defaults, &parsed);

    TEST_ASSERT_EQUAL_UINT16(SERVO_FIELD_EASING, repaired);
    TEST_ASSERT_EQUAL_STRING("doorFL", parsed.part);
    TEST_ASSERT_EQUAL_UINT16(1900, parsed.open_us);
    TEST_ASSERT_EQUAL_UINT16(1100, parsed.close_us);
    TEST_ASSERT_EQUAL_UINT16(750, parsed.throw_ms);
    TEST_ASSERT_TRUE(parsed.calibrated);
    TEST_ASSERT_EQUAL_UINT8(SERVO_BOOT_HOME_HOLD, parsed.boot);
    TEST_ASSERT_EQUAL_UINT8(SERVO_EASE_NONE, parsed.easing);
}

void test_an_empty_field_is_not_zero() {
    const ServoOutputRow defaults = mg996rRow();
    // An empty travel time is not zero, and " 750" is not 750: either would
    // switch off the comparisons every clamp downstream is made of.
    const char* record = "ledc:0:doorFL:1900:1500:1100::200:0:none:limp:mg996r:1";
    ServoOutputRow parsed = {};
    const uint16_t repaired = servoOutputRowParse(record, defaults, &parsed);

    TEST_ASSERT_EQUAL_UINT16(SERVO_FIELD_THROW_MS, repaired);
    TEST_ASSERT_EQUAL_UINT16(defaults.throw_ms, parsed.throw_ms);
    TEST_ASSERT_TRUE(parsed.throw_ms > 0);
}

void test_a_partial_edit_keeps_what_it_could_not_read() {
    ServoOutputRow stored = mg996rRow();
    stored.open_us = 1850;
    stored.throw_ms = 640;
    stored.calibrated = true;

    // One edit applied over what is already there: a good field lands, a bad
    // one keeps the number the builder calibrated.
    ServoOutputRow edited = stored;
    edited.open_us = 1700;
    edited.throw_ms = 0;
    const uint16_t repaired = servoOutputRowNormalise(&edited, stored);

    TEST_ASSERT_EQUAL_UINT16(SERVO_FIELD_THROW_MS, repaired);
    TEST_ASSERT_EQUAL_UINT16(1700, edited.open_us);
    TEST_ASSERT_EQUAL_UINT16(640, edited.throw_ms);
    TEST_ASSERT_TRUE(edited.calibrated);
}

void test_the_receipt_names_the_field_and_the_door() {
    char note[64] = {};
    servoOutputRepairNote(SERVO_FIELD_OPEN | SERVO_FIELD_EASING, true, note, sizeof(note));
    TEST_ASSERT_EQUAL_STRING("open, ease took the safe default", note);

    servoOutputRepairNote(SERVO_FIELD_THROW_MS, false, note, sizeof(note));
    TEST_ASSERT_EQUAL_STRING("throw kept what was there", note);
}

void test_an_unaddressable_channel_is_repaired() {
    const ServoOutputRow defaults = mg996rRow();
    // LEDC channel 2 is the dome ESC, not a servo output.
    char record[SERVO_OUTPUT_ROW_STR_MAX + 1] = {};
    ServoOutputRow row = mg996rRow();
    row.channel = LEDC_CH_DOME;
    TEST_ASSERT_TRUE(servoOutputRowFormat(record, sizeof(record), row));

    ServoOutputRow parsed = {};
    const uint16_t repaired = servoOutputRowParse(record, defaults, &parsed);
    TEST_ASSERT_TRUE((repaired & SERVO_FIELD_CHANNEL) != 0);
    TEST_ASSERT_EQUAL_UINT8(defaults.channel, parsed.channel);
}

// --- every field survives the wire -------------------------------------------

void test_every_field_round_trips_through_storage() {
    ServoOutputTable saved = {};
    servoOutputTableDefaults(&saved);

    ServoOutputRow& row = saved.rows[1];
    row.driver = SERVO_DRIVER_LEDC;
    row.channel = LEDC_CH_AUX2;
    snprintf(row.part, sizeof(row.part), "%s", "chargebay");
    row.component = SERVO_COMP_MG90S;  // set before the endpoints it bounds
    row.open_us = 700;                 // reversed pair, and outside the MG996R band
    row.centre_us = 1500;
    row.close_us = 2300;
    row.throw_ms = 1450;
    row.accel_ms = 310;
    row.release_ms = 4000;
    row.easing = SERVO_EASE_OVERSHOOT;
    row.boot = SERVO_BOOT_HOME_RELEASE;
    row.calibrated = true;

    MapWriter writer;
    TEST_ASSERT_TRUE(configSerializeServoOutputs(saved, writer));

    MapReader reader;
    for (const auto& pair : writer.data()) {
        reader.set(pair.first.c_str(), pair.second);
    }

    ServoOutputTable loaded = {};
    ServoOutputRepairReport report = {};
    configDeserializeServoOutputs(reader, &loaded, &report);

    TEST_ASSERT_EQUAL_UINT8(0, report.rowsRepaired);
    TEST_ASSERT_FALSE(report.countRepaired);
    TEST_ASSERT_EQUAL_UINT8(SERVO_OUTPUT_ROW_DEFAULT_COUNT, loaded.count);

    const ServoOutputRow& back = loaded.rows[1];
    TEST_ASSERT_EQUAL_UINT8(SERVO_DRIVER_LEDC, back.driver);
    TEST_ASSERT_EQUAL_UINT8(LEDC_CH_AUX2, back.channel);
    TEST_ASSERT_EQUAL_STRING("chargebay", back.part);
    TEST_ASSERT_EQUAL_UINT8(SERVO_COMP_MG90S, back.component);
    TEST_ASSERT_EQUAL_UINT16(700, back.open_us);
    TEST_ASSERT_EQUAL_UINT16(1500, back.centre_us);
    TEST_ASSERT_EQUAL_UINT16(2300, back.close_us);
    TEST_ASSERT_EQUAL_UINT16(1450, back.throw_ms);
    TEST_ASSERT_EQUAL_UINT16(310, back.accel_ms);
    TEST_ASSERT_EQUAL_UINT16(4000, back.release_ms);
    TEST_ASSERT_EQUAL_UINT8(SERVO_EASE_OVERSHOOT, back.easing);
    TEST_ASSERT_EQUAL_UINT8(SERVO_BOOT_HOME_RELEASE, back.boot);
    TEST_ASSERT_TRUE(back.calibrated);

    // The reversed pair came back reversed, with no flag to disagree with it.
    TEST_ASSERT_TRUE(servoOutputIsReversed(back));
    TEST_ASSERT_EQUAL_UINT16(700, servoOutputLowUs(back));
    TEST_ASSERT_EQUAL_UINT16(2300, servoOutputHighUs(back));
}

void test_a_row_added_without_an_address_is_reported() {
    ServoOutputTable saved = {};
    servoOutputTableDefaults(&saved);
    saved.count = SERVO_OUTPUT_ROW_DEFAULT_COUNT + 1;  // a sixth row, as an expander adds

    MapWriter writer;
    TEST_ASSERT_TRUE(configSerializeServoOutputs(saved, writer));

    MapReader reader;
    for (const auto& pair : writer.data()) {
        reader.set(pair.first.c_str(), pair.second);
    }

    ServoOutputTable loaded = {};
    ServoOutputRepairReport report = {};
    configDeserializeServoOutputs(reader, &loaded, &report);

    // The row count is stored, so rows can be added without rewriting a field
    // set -- and a row nobody has addressed yet says so rather than reading as
    // channel zero.
    TEST_ASSERT_EQUAL_UINT8(SERVO_OUTPUT_ROW_DEFAULT_COUNT + 1, loaded.count);
    TEST_ASSERT_EQUAL_UINT8(1, report.rowsRepaired);
    TEST_ASSERT_EQUAL_UINT8(SERVO_OUTPUT_ROW_DEFAULT_COUNT, report.firstRow);
    TEST_ASSERT_TRUE((report.firstRowMask & SERVO_FIELD_CHANNEL) != 0);
}

void test_a_device_that_never_wrote_a_row_reports_nothing() {
    MapReader reader;  // nothing stored at all
    ServoOutputTable loaded = {};
    ServoOutputRepairReport report = {};
    configDeserializeServoOutputs(reader, &loaded, &report);

    TEST_ASSERT_EQUAL_UINT8(SERVO_OUTPUT_ROW_DEFAULT_COUNT, loaded.count);
    TEST_ASSERT_EQUAL_UINT8(0, report.rowsRepaired);
    TEST_ASSERT_EQUAL_UINT16(0, report.fieldsRepaired);
    TEST_ASSERT_EQUAL_UINT16(2000, loaded.rows[0].open_us);
}

void test_a_damaged_stored_row_is_counted_and_named() {
    MapWriter writer;
    ServoOutputTable saved = {};
    servoOutputTableDefaults(&saved);
    TEST_ASSERT_TRUE(configSerializeServoOutputs(saved, writer));

    MapReader reader;
    for (const auto& pair : writer.data()) {
        reader.set(pair.first.c_str(), pair.second);
    }
    reader.set("so02", std::string("ledc:3:doorFL:1900:1500:1100:750:200:0:none:limp:mg996r:yes"));

    ServoOutputTable loaded = {};
    ServoOutputRepairReport report = {};
    configDeserializeServoOutputs(reader, &loaded, &report);

    TEST_ASSERT_EQUAL_UINT8(1, report.rowsRepaired);
    TEST_ASSERT_EQUAL_UINT8(2, report.firstRow);
    TEST_ASSERT_EQUAL_UINT16(1, report.fieldsRepaired);
    TEST_ASSERT_EQUAL_UINT16(SERVO_FIELD_CALIBRATED, report.firstRowMask);
    // An unreadable calibrated bit never reads as measured.
    TEST_ASSERT_FALSE(loaded.rows[2].calibrated);
    // ...and the rest of the row survived it.
    TEST_ASSERT_EQUAL_STRING("doorFL", loaded.rows[2].part);
    TEST_ASSERT_EQUAL_UINT16(750, loaded.rows[2].throw_ms);
}

void test_an_out_of_range_stored_count_keeps_the_default() {
    MapReader reader;
    reader.set("so_cnt", std::string("200"));

    ServoOutputTable loaded = {};
    ServoOutputRepairReport report = {};
    configDeserializeServoOutputs(reader, &loaded, &report);

    TEST_ASSERT_TRUE(report.countRepaired);
    TEST_ASSERT_EQUAL_UINT8(SERVO_OUTPUT_ROW_DEFAULT_COUNT, loaded.count);
}

int main(int, char**) {
    UNITY_BEGIN();

    RUN_TEST(test_defaults_never_hand_out_a_zero_travel_time);
    RUN_TEST(test_defaults_are_limp_and_unmeasured);
    RUN_TEST(test_default_table_matches_the_five_fixed_outputs);
    RUN_TEST(test_rows_past_the_count_are_still_safe);

    RUN_TEST(test_reverse_is_read_off_the_pair);

    RUN_TEST(test_mg996r_row_cannot_reach_500us);
    RUN_TEST(test_mg90s_row_takes_the_full_band);
    RUN_TEST(test_an_unstated_component_gets_the_cautious_band);
    RUN_TEST(test_the_stored_door_clamps_to_the_band_too);

    RUN_TEST(test_overshoot_degrades_while_uncalibrated);
    RUN_TEST(test_soft_easing_is_not_gated_on_calibration);

    RUN_TEST(test_capture_marks_calibrated_and_never_ticks_boot);
    RUN_TEST(test_capture_is_clamped_by_the_component_type);

    RUN_TEST(test_an_unreadable_record_takes_the_safe_defaults_and_reports);
    RUN_TEST(test_one_bad_field_does_not_cost_the_row_its_calibration);
    RUN_TEST(test_an_empty_field_is_not_zero);
    RUN_TEST(test_a_partial_edit_keeps_what_it_could_not_read);
    RUN_TEST(test_the_receipt_names_the_field_and_the_door);
    RUN_TEST(test_an_unaddressable_channel_is_repaired);

    RUN_TEST(test_every_field_round_trips_through_storage);
    RUN_TEST(test_a_row_added_without_an_address_is_reported);
    RUN_TEST(test_a_device_that_never_wrote_a_row_reports_nothing);
    RUN_TEST(test_a_damaged_stored_row_is_counted_and_named);
    RUN_TEST(test_an_out_of_range_stored_count_keeps_the_default);

    return UNITY_END();
}
