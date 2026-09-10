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
#include <cstring>
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
    TEST_ASSERT_EQUAL_UINT8(0, servoOutputPartCount(row));
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
    TEST_ASSERT_EQUAL_UINT8(0, servoOutputPartCount(parsed));
}

void test_one_bad_field_does_not_cost_the_row_its_calibration() {
    const ServoOutputRow defaults = mg996rRow();
    // Everything readable except the ease word.
    const char* record = "ledc:0:doorFL,doorFR:1900:1500:1100:750:200:0:bouncy:home-hold:mg996r:1";
    ServoOutputRow parsed = {};
    const uint16_t repaired = servoOutputRowParse(record, defaults, &parsed);

    TEST_ASSERT_EQUAL_UINT16(SERVO_FIELD_EASING, repaired);
    TEST_ASSERT_EQUAL_UINT8(2, servoOutputPartCount(parsed));
    TEST_ASSERT_EQUAL_STRING("doorFL", servoOutputPartAt(parsed, 0));
    TEST_ASSERT_EQUAL_STRING("doorFR", servoOutputPartAt(parsed, 1));
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

// --- an Output may drive several Parts, a Part only one Output ---------------

void test_a_row_carries_up_to_four_parts_and_refuses_a_fifth() {
    ServoOutputRow row = mg996rRow();
    TEST_ASSERT_TRUE(servoOutputAddPart(&row, "doorFL"));
    TEST_ASSERT_TRUE(servoOutputAddPart(&row, "doorFR"));

    // A Part the row already drives is not a second slot: the same lead. Asked
    // here, with two slots still free, so it is the duplicate that refuses and
    // not the cap.
    TEST_ASSERT_FALSE(servoOutputAddPart(&row, "doorFL"));
    TEST_ASSERT_EQUAL_UINT8(2, servoOutputPartCount(row));

    TEST_ASSERT_TRUE(servoOutputAddPart(&row, "doorRL"));
    TEST_ASSERT_TRUE(servoOutputAddPart(&row, "doorRR"));
    TEST_ASSERT_EQUAL_UINT8(4, servoOutputPartCount(row));

    TEST_ASSERT_FALSE(servoOutputAddPart(&row, "dataport"));
    TEST_ASSERT_EQUAL_UINT8(4, servoOutputPartCount(row));

    TEST_ASSERT_TRUE(servoOutputDrivesPart(row, "doorRR"));
    TEST_ASSERT_FALSE(servoOutputDrivesPart(row, "dataport"));
}

void test_an_empty_part_list_stays_legal() {
    const ServoOutputRow defaults = mg996rRow();
    ServoOutputRow row = mg996rRow();
    TEST_ASSERT_TRUE(servoOutputAddPart(&row, "doorFL"));
    servoOutputClearParts(&row);

    char record[SERVO_OUTPUT_ROW_STR_MAX + 1] = {};
    TEST_ASSERT_TRUE(servoOutputRowFormat(record, sizeof(record), row));

    ServoOutputRow parsed = {};
    const uint16_t repaired = servoOutputRowParse(record, defaults, &parsed);
    TEST_ASSERT_EQUAL_UINT16(0, repaired);
    TEST_ASSERT_EQUAL_UINT8(0, servoOutputPartCount(parsed));
}

void test_a_ganged_pair_survives_the_wire() {
    const ServoOutputRow defaults = mg996rRow();
    ServoOutputRow row = mg996rRow();
    TEST_ASSERT_TRUE(servoOutputAddPart(&row, "doorFL"));
    TEST_ASSERT_TRUE(servoOutputAddPart(&row, "doorFR"));

    char record[SERVO_OUTPUT_ROW_STR_MAX + 1] = {};
    TEST_ASSERT_TRUE(servoOutputRowFormat(record, sizeof(record), row));

    ServoOutputRow parsed = {};
    const uint16_t repaired = servoOutputRowParse(record, defaults, &parsed);
    TEST_ASSERT_EQUAL_UINT16(0, repaired);
    TEST_ASSERT_EQUAL_UINT8(2, servoOutputPartCount(parsed));
    TEST_ASSERT_EQUAL_STRING("doorFL", servoOutputPartAt(parsed, 0));
    TEST_ASSERT_EQUAL_STRING("doorFR", servoOutputPartAt(parsed, 1));
}

void test_one_unreadable_part_costs_only_its_own_slot() {
    const ServoOutputRow defaults = mg996rRow();
    // The middle id carries a character no catalog id can, and the list names
    // one Part twice.
    const char* record =
        "ledc:0:doorFL,door FR,doorRL,doorFL:1900:1500:1100:750:200:0:none:limp:mg996r:1";
    ServoOutputRow parsed = {};
    const uint16_t repaired = servoOutputRowParse(record, defaults, &parsed);

    TEST_ASSERT_EQUAL_UINT16(SERVO_FIELD_PARTS, repaired);
    TEST_ASSERT_EQUAL_UINT8(2, servoOutputPartCount(parsed));
    TEST_ASSERT_EQUAL_STRING("doorFL", servoOutputPartAt(parsed, 0));
    TEST_ASSERT_EQUAL_STRING("doorRL", servoOutputPartAt(parsed, 1));
    // The rest of the row never paid for it.
    TEST_ASSERT_EQUAL_UINT16(1900, parsed.open_us);
    TEST_ASSERT_TRUE(parsed.calibrated);
}

void test_a_part_two_rows_claim_stays_with_the_first() {
    ServoOutputTable table = {};
    servoOutputTableDefaults(&table);
    TEST_ASSERT_TRUE(servoOutputAddPart(&table.rows[1], "doorFL"));
    TEST_ASSERT_TRUE(servoOutputAddPart(&table.rows[3], "doorFL"));
    TEST_ASSERT_TRUE(servoOutputAddPart(&table.rows[3], "doorFR"));

    const uint32_t affected = servoOutputTableEnforcePartOwnership(&table);

    // Which Output drives this Part has exactly one answer, and it does not
    // depend on which row firmware scans first.
    TEST_ASSERT_EQUAL_UINT32((uint32_t)1u << 3, affected);
    TEST_ASSERT_TRUE(servoOutputDrivesPart(table.rows[1], "doorFL"));
    TEST_ASSERT_FALSE(servoOutputDrivesPart(table.rows[3], "doorFL"));
    // The later row keeps the Part nobody contested, and keeps it in slot 0.
    TEST_ASSERT_EQUAL_UINT8(1, servoOutputPartCount(table.rows[3]));
    TEST_ASSERT_EQUAL_STRING("doorFR", servoOutputPartAt(table.rows[3], 0));
}

void test_a_contested_part_is_reported_by_the_loader() {
    MapWriter writer;
    ServoOutputTable saved = {};
    servoOutputTableDefaults(&saved);
    TEST_ASSERT_TRUE(servoOutputAddPart(&saved.rows[0], "utilUp"));
    TEST_ASSERT_TRUE(servoOutputAddPart(&saved.rows[2], "utilUp"));
    TEST_ASSERT_TRUE(configSerializeServoOutputs(saved, writer));

    MapReader reader;
    for (const auto& pair : writer.data()) {
        reader.set(pair.first.c_str(), pair.second);
    }

    ServoOutputTable loaded = {};
    ServoOutputRepairReport report = {};
    configDeserializeServoOutputs(reader, &loaded, &report);

    TEST_ASSERT_EQUAL_UINT8(1, report.rowsRepaired);
    TEST_ASSERT_EQUAL_UINT8(2, report.firstRow);
    TEST_ASSERT_EQUAL_UINT16(SERVO_FIELD_PARTS, report.firstRowMask);
    TEST_ASSERT_TRUE(servoOutputDrivesPart(loaded.rows[0], "utilUp"));
    TEST_ASSERT_FALSE(servoOutputDrivesPart(loaded.rows[2], "utilUp"));

    char note[64] = {};
    servoOutputRepairNote(report.firstRowMask, true, note, sizeof(note));
    TEST_ASSERT_EQUAL_STRING("parts took the safe default", note);
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
    TEST_ASSERT_TRUE(servoOutputAddPart(&row, "chargebay"));
    TEST_ASSERT_TRUE(servoOutputAddPart(&row, "dataport"));
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
    TEST_ASSERT_EQUAL_UINT8(2, servoOutputPartCount(back));
    TEST_ASSERT_EQUAL_STRING("chargebay", servoOutputPartAt(back, 0));
    TEST_ASSERT_EQUAL_STRING("dataport", servoOutputPartAt(back, 1));
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
    TEST_ASSERT_EQUAL_STRING("doorFL", servoOutputPartAt(loaded.rows[2], 0));
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

// --- the bridge from the five fixed field sets (#286, ADR 0041) --------------

void test_a_fixed_pair_arrives_with_its_direction_and_a_midpoint_centre() {
    ServoOutputRow row = mg996rRow();
    // A reversed linkage: the builder's open is the LOWER number.
    const uint16_t repaired = servoOutputAdoptFixedPair(&row, 1200, 1900, SERVO_COMP_MG996R);

    TEST_ASSERT_EQUAL_UINT16(0, repaired);
    TEST_ASSERT_EQUAL_UINT16(1200, row.open_us);
    TEST_ASSERT_EQUAL_UINT16(1900, row.close_us);
    // Halfway between the builder's own two ends, not the middle of the band.
    TEST_ASSERT_EQUAL_UINT16(1550, row.centre_us);
    // Sorting the pair here would be the invert flag ADR 0041 refuses.
    TEST_ASSERT_TRUE(servoOutputIsReversed(row));
}

void test_a_fixed_pair_carries_nothing_it_was_never_told() {
    ServoOutputRow row = mg996rRow();
    row.throw_ms = 2500;
    row.accel_ms = 400;
    row.boot = SERVO_BOOT_HOME_HOLD;
    row.easing = SERVO_EASE_SOFT;
    row.release_ms = 3000;
    TEST_ASSERT_TRUE(servoOutputAddPart(&row, "utilUp"));

    servoOutputAdoptFixedPair(&row, 1900, 1100, SERVO_COMP_MG996R);

    TEST_ASSERT_EQUAL_UINT16(2500, row.throw_ms);
    TEST_ASSERT_EQUAL_UINT16(400, row.accel_ms);
    TEST_ASSERT_EQUAL_UINT16(3000, row.release_ms);
    TEST_ASSERT_EQUAL_UINT8(SERVO_BOOT_HOME_HOLD, row.boot);
    TEST_ASSERT_EQUAL_UINT8(SERVO_EASE_SOFT, row.easing);
    TEST_ASSERT_EQUAL_STRING("utilUp", servoOutputPartAt(row, 0));
    // The old form stored no such bit, and one nobody measured is not one to
    // infer: false is the value that degrades overshoot and warns.
    TEST_ASSERT_FALSE(row.calibrated);
}

void test_a_measured_centre_is_not_recomputed_by_a_later_crossing() {
    ServoOutputRow row = mg996rRow();
    servoOutputCapture(&row, SERVO_END_CENTRE, 1300);
    TEST_ASSERT_TRUE(row.calibrated);

    // The bridge is crossed again on every config write. A centre somebody
    // measured is theirs; only an unmeasured one is a default to re-derive.
    servoOutputAdoptFixedPair(&row, 1900, 1100, SERVO_COMP_MG996R);

    TEST_ASSERT_EQUAL_UINT16(1300, row.centre_us);
    TEST_ASSERT_EQUAL_UINT16(1900, row.open_us);
    TEST_ASSERT_EQUAL_UINT16(1100, row.close_us);
}

void test_a_fixed_pair_the_band_cannot_take_is_reported() {
    ServoOutputRow row = mg996rRow();
    // 500/2500 was legal in the old form; an MG996R row cannot take either.
    const uint16_t repaired = servoOutputAdoptFixedPair(&row, 2500, 500, SERVO_COMP_MG996R);

    TEST_ASSERT_EQUAL_UINT16(2000, row.open_us);
    TEST_ASSERT_EQUAL_UINT16(1000, row.close_us);
    TEST_ASSERT_TRUE((repaired & SERVO_FIELD_OPEN) != 0);
    TEST_ASSERT_TRUE((repaired & SERVO_FIELD_CLOSE) != 0);
    // Nothing is silently clamped away: the note names the fields.
    char note[96] = {};
    servoOutputRepairNote(repaired, true, note, sizeof(note));
    TEST_ASSERT_NOT_NULL(strstr(note, "open"));
    TEST_ASSERT_NOT_NULL(strstr(note, "close"));
}

void test_the_component_is_settled_before_the_pair_is_clamped() {
    ServoOutputRow row = mg996rRow();
    // Naming the component that takes the wider band is the unlock (#286): the
    // same 600 us that an MG996R row refuses lands untouched on an MG90S.
    const uint16_t repaired = servoOutputAdoptFixedPair(&row, 2400, 600, SERVO_COMP_MG90S);

    TEST_ASSERT_EQUAL_UINT16(0, repaired);
    TEST_ASSERT_EQUAL_UINT16(2400, row.open_us);
    TEST_ASSERT_EQUAL_UINT16(600, row.close_us);
    TEST_ASSERT_EQUAL_UINT16(1500, row.centre_us);
}

// --- finding the row behind an Output Address --------------------------------

void test_an_address_finds_its_row_and_an_unclaimed_one_does_not() {
    ServoOutputTable table = {};
    servoOutputTableDefaults(&table);

    TEST_ASSERT_EQUAL_UINT8(0, servoOutputTableFindByAddress(table, SERVO_DRIVER_LEDC, LEDC_CH_ARM1));
    TEST_ASSERT_EQUAL_UINT8(4, servoOutputTableFindByAddress(table, SERVO_DRIVER_LEDC, LEDC_CH_AUX3));
    // The dome channel drives an ESC, so no servo row is addressed there.
    TEST_ASSERT_EQUAL_UINT8(SERVO_OUTPUT_ROW_MAX,
                            servoOutputTableFindByAddress(table, SERVO_DRIVER_LEDC, LEDC_CH_DOME));
    // A row past the live count is not addressed yet, whatever it holds.
    table.count = 2;
    TEST_ASSERT_EQUAL_UINT8(SERVO_OUTPUT_ROW_MAX,
                            servoOutputTableFindByAddress(table, SERVO_DRIVER_LEDC, LEDC_CH_AUX3));
}

// --- the bridge, crossed on first read ---------------------------------------

void test_an_upgrading_controller_finds_its_calibration_on_the_rows() {
    // A controller that calibrated two arms and one aux before ADR 0041: five
    // fixed field sets in NVS, and not one row record.
    MapReader reader;
    reader.set("arm1_op", (uint32_t)1850);
    reader.set("arm1_cl", (uint32_t)1150);
    reader.set("arm1_type", (uint32_t)SERVO_COMP_MG996R);
    reader.set("aux1_op", (uint32_t)1400);
    reader.set("aux1_cl", (uint32_t)1900);
    reader.set("aux1_type", (uint32_t)SERVO_COMP_MG996R);

    ServoOutputTable loaded = {};
    ServoOutputRepairReport report = {};
    configDeserializeServoOutputs(reader, &loaded, &report);

    const uint8_t arm1 = servoOutputTableFindByAddress(loaded, SERVO_DRIVER_LEDC, LEDC_CH_ARM1);
    TEST_ASSERT_EQUAL_UINT16(1850, loaded.rows[arm1].open_us);
    TEST_ASSERT_EQUAL_UINT16(1150, loaded.rows[arm1].close_us);
    TEST_ASSERT_EQUAL_UINT16(1500, loaded.rows[arm1].centre_us);

    // A reversed linkage on aux1 is still reversed on the row.
    const uint8_t aux1 = servoOutputTableFindByAddress(loaded, SERVO_DRIVER_LEDC, LEDC_CH_AUX1);
    TEST_ASSERT_EQUAL_UINT16(1400, loaded.rows[aux1].open_us);
    TEST_ASSERT_EQUAL_UINT16(1900, loaded.rows[aux1].close_us);
    TEST_ASSERT_TRUE(servoOutputIsReversed(loaded.rows[aux1]));
    TEST_ASSERT_EQUAL_UINT8(SERVO_COMP_MG996R, loaded.rows[aux1].component);

    // Nothing was moved, so nothing is reported.
    TEST_ASSERT_EQUAL_UINT8(0, report.rowsRepaired);
    // And the new fields are still the safe values, not something inferred.
    TEST_ASSERT_FALSE(loaded.rows[arm1].calibrated);
    TEST_ASSERT_EQUAL_UINT8(SERVO_BOOT_LIMP, loaded.rows[arm1].boot);
}

void test_a_saved_row_wins_over_the_old_form() {
    // Both forms present, disagreeing: the row is the output from now on.
    MapReader reader;
    reader.set("arm1_op", (uint32_t)1850);
    reader.set("arm1_cl", (uint32_t)1150);
    reader.set("so00", std::string("ledc:0:doorFL:1700:1400:1200:800:200:0:soft:limp:mg996r:1"));

    ServoOutputTable loaded = {};
    ServoOutputRepairReport report = {};
    configDeserializeServoOutputs(reader, &loaded, &report);

    TEST_ASSERT_EQUAL_UINT16(1700, loaded.rows[0].open_us);
    TEST_ASSERT_EQUAL_UINT16(1200, loaded.rows[0].close_us);
    TEST_ASSERT_TRUE(loaded.rows[0].calibrated);
    TEST_ASSERT_EQUAL_STRING("doorFL", servoOutputPartAt(loaded.rows[0], 0));
    TEST_ASSERT_EQUAL_UINT8(0, report.rowsRepaired);

    // The row beside it has no record, so it still crosses the bridge.
    const uint8_t arm2 = servoOutputTableFindByAddress(loaded, SERVO_DRIVER_LEDC, LEDC_CH_ARM2);
    TEST_ASSERT_EQUAL_UINT16(2000, loaded.rows[arm2].open_us);
}

void test_an_old_value_the_band_cannot_take_is_reported_at_load() {
    // 2500 us was legal in the old form on any output. On an MG996R row it is
    // not, so it moves -- and a builder's own number changing under them is
    // said out loud rather than quietly clamped.
    MapReader reader;
    reader.set("arm1_op", (uint32_t)2500);
    reader.set("arm1_type", (uint32_t)SERVO_COMP_MG996R);

    ServoOutputTable loaded = {};
    ServoOutputRepairReport report = {};
    configDeserializeServoOutputs(reader, &loaded, &report);

    TEST_ASSERT_EQUAL_UINT16(2000, loaded.rows[0].open_us);
    TEST_ASSERT_EQUAL_UINT8(1, report.rowsRepaired);
    TEST_ASSERT_EQUAL_UINT8(0, report.firstRow);
    TEST_ASSERT_TRUE((report.firstRowMask & SERVO_FIELD_OPEN) != 0);
}

void test_naming_the_wider_component_carries_the_old_value_across_intact() {
    // The same 2500 us on an output whose builder said what is fitted (#286:
    // the wider band is an unlock, not a default).
    MapReader reader;
    reader.set("aux2_op", (uint32_t)2500);
    reader.set("aux2_cl", (uint32_t)600);
    reader.set("aux2_type", (uint32_t)SERVO_COMP_MG90S);

    ServoOutputTable loaded = {};
    ServoOutputRepairReport report = {};
    configDeserializeServoOutputs(reader, &loaded, &report);

    const uint8_t aux2 = servoOutputTableFindByAddress(loaded, SERVO_DRIVER_LEDC, LEDC_CH_AUX2);
    TEST_ASSERT_EQUAL_UINT16(2500, loaded.rows[aux2].open_us);
    TEST_ASSERT_EQUAL_UINT16(600, loaded.rows[aux2].close_us);
    TEST_ASSERT_EQUAL_UINT16(1550, loaded.rows[aux2].centre_us);
    TEST_ASSERT_EQUAL_UINT8(0, report.rowsRepaired);
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
    RUN_TEST(test_a_row_carries_up_to_four_parts_and_refuses_a_fifth);
    RUN_TEST(test_an_empty_part_list_stays_legal);
    RUN_TEST(test_a_ganged_pair_survives_the_wire);
    RUN_TEST(test_one_unreadable_part_costs_only_its_own_slot);
    RUN_TEST(test_a_part_two_rows_claim_stays_with_the_first);
    RUN_TEST(test_a_contested_part_is_reported_by_the_loader);
    RUN_TEST(test_the_receipt_names_the_field_and_the_door);
    RUN_TEST(test_an_unaddressable_channel_is_repaired);

    RUN_TEST(test_every_field_round_trips_through_storage);
    RUN_TEST(test_a_row_added_without_an_address_is_reported);
    RUN_TEST(test_a_device_that_never_wrote_a_row_reports_nothing);
    RUN_TEST(test_a_damaged_stored_row_is_counted_and_named);
    RUN_TEST(test_an_out_of_range_stored_count_keeps_the_default);

    RUN_TEST(test_a_fixed_pair_arrives_with_its_direction_and_a_midpoint_centre);
    RUN_TEST(test_a_fixed_pair_carries_nothing_it_was_never_told);
    RUN_TEST(test_a_measured_centre_is_not_recomputed_by_a_later_crossing);
    RUN_TEST(test_a_fixed_pair_the_band_cannot_take_is_reported);
    RUN_TEST(test_the_component_is_settled_before_the_pair_is_clamped);
    RUN_TEST(test_an_address_finds_its_row_and_an_unclaimed_one_does_not);

    RUN_TEST(test_an_upgrading_controller_finds_its_calibration_on_the_rows);
    RUN_TEST(test_a_saved_row_wins_over_the_old_form);
    RUN_TEST(test_an_old_value_the_band_cannot_take_is_reported_at_load);
    RUN_TEST(test_naming_the_wider_component_carries_the_old_value_across_intact);

    return UNITY_END();
}
