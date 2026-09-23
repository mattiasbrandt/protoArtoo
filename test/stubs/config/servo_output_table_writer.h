// =============================================================================
// test/stubs/config/servo_output_table_writer.h
//
// Writes a whole ServoOutputTable through a ConfigWriter, for a native test
// that needs a stored table to load, compare or fail against.
//
// Firmware has no whole-table serializer and must not grow one:
// configSaveServoOutputs() (src/config_store.cpp) reads the live cache one row
// at a time, so no caller holds a whole table on its stack, and writes each row
// through the same two public halves this helper uses. Keeping the helper here
// keeps a test-only entry point out of include/config_serializer.h.
// Used only in native unit test builds.
// =============================================================================
#pragma once

#include "config_io.h"
#include "config_serializer.h"
#include "servo_output_row.h"

inline bool writeServoOutputTableForTest(const ServoOutputTable& table, ConfigWriter& w) {
    const uint8_t count =
        (table.count <= SERVO_OUTPUT_ROW_MAX) ? table.count : SERVO_OUTPUT_ROW_MAX;
    bool ok = configSerializeServoOutputCount(count, w);
    for (uint8_t i = 0; i < count; ++i) {
        ok = configSerializeServoOutputRow(i, table.rows[i], w) && ok;
    }
    return ok;
}
