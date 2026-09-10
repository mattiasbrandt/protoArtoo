// =============================================================================
// include/config_serializer.h
//
// Pure serialization functions for config persistence.
// configDeserialize and configSerialize operate against ConfigReader/ConfigWriter
// abstract interfaces, making them testable without NVS or Arduino dependencies.
//
// Used by configLoad/configSave in production (via PrefsReader/PrefsWriter).
// Tested by native unit tests (via MapReader/MapWriter).
// =============================================================================
#pragma once

#include "config_store.h"
#include "config_io.h"
#include "servo_output_row.h"

// configDeserialize: Load a ConfigSnapshot from a ConfigReader.
// Applies defaults from configSnapshotDefaults(), then overwrites with stored values.
// If reader.schemaVersion() < CONFIG_SCHEMA_VERSION, fills missing fields with defaults.
// Pure function: no logging, no FreeRTOS calls, no side effects.
bool configDeserialize(const ConfigReader& reader, ConfigSnapshot* out);

// configSerialize: Save a ConfigSnapshot to a ConfigWriter.
// Writes all fields to the ConfigWriter, then schema version.
// Pure function: no logging, no FreeRTOS calls, no side effects.
bool configSerialize(const ConfigSnapshot& snap, ConfigWriter& writer);

// Domain-level serializers (used by domain-specific save functions in config_store)
bool configSerializeDrive(const DriveConfig& cfg, ConfigWriter& w);
bool configSerializeAudio(const AudioConfig& cfg, ConfigWriter& w);
bool configSerializeServo(const ServoConfig& cfg, ConfigWriter& w);
bool configSerializeDome(const DomeConfig& cfg, ConfigWriter& w);
bool configSerializeSystem(const SystemConfig& cfg, ConfigWriter& w);
bool configSerializeWifi(const WifiConfig& cfg, ConfigWriter& w);

// Domain-level deserializers (used by domain-specific load functions in config_store)
// Each fills *out with defaults then overwrites with stored values.
void configDeserializeDrive(const ConfigReader& r, DriveConfig* out);
void configDeserializeAudio(const ConfigReader& r, AudioConfig* out);
void configDeserializeServo(const ConfigReader& r, ServoConfig* out);
void configDeserializeDome(const ConfigReader& r, DomeConfig* out);
void configDeserializeSystem(const ConfigReader& r, SystemConfig* out);
void configDeserializeWifi(const ConfigReader& r, WifiConfig* out);

// -----------------------------------------------------------------------------
// Addressed Servo Output rows (ADR 0041)
//
// Stored beside the five fixed servo field sets, on their own keys, and
// deliberately NOT part of ConfigSnapshot: the snapshot crosses three nested
// stack frames on the serial config-write path and the table is far larger than
// any field this schema has added before (see the static_assert in
// config_store.h for why that number is load-bearing).
//
// One key holds the row count and one string key holds each row. A save writes
// the count and rows 0..count-1, so a record left above the count by an earlier
// larger table is never read; the save that raises the count again writes those
// rows in the same pass.
// -----------------------------------------------------------------------------
// configAdoptFixedServoFields: the bridge from the old form onto ONE row, and
// the only statement anywhere of which Output Address each fixed servo field
// set was ever about -- the five sets carry their channel in their names and
// nowhere else. Both directions of the migrate phase come through here so the
// mapping has one home: the loader crosses a row that has no stored record, and
// the config write path crosses a row whose fields a builder has just changed.
//
// Returns the repair mask (0 when no fixed set is addressed to this row, which
// is what an expander's row gets -- untouched, and reported as nothing).
// Deleted with the fields it names.
uint16_t configAdoptFixedServoFields(ServoOutputRow* row, const ServoConfig& fixed);

// configProjectServoRowIntoFixedFields: the same bridge, read direction. What
// the row holds, said in the old form's names, so a surface still asking for
// arm1OpenUs is answered with the number the droid will actually drive to. The
// mapping is the one above's, stated once. Deleted with the fields it fills.
void configProjectServoRowIntoFixedFields(const ServoOutputRow& row, ServoConfig* fixed);

bool configSerializeServoOutputCount(uint8_t count, ConfigWriter& w);
bool configSerializeServoOutputRow(uint8_t index, const ServoOutputRow& row, ConfigWriter& w);
bool configSerializeServoOutputs(const ServoOutputTable& table, ConfigWriter& w);

// Fills *out with servoOutputTableDefaults() then overwrites with stored rows.
// A row whose record exists but cannot be read is repaired field by field and
// counted in *report.
//
// A row whose key is absent crosses the bridge instead (#286): it adopts the
// fixed field set addressed to its channel, so a builder's existing calibration
// arrives on the rows on first read, with no migration marker to keep and no
// write on the boot path. A stored row always wins over the old form, which is
// what makes the adoption idempotent -- it stops mattering for a row the moment
// that row is saved. The only repair an adoption can report is a pulse width the
// component band had to move, and it is counted like any other.
void configDeserializeServoOutputs(const ConfigReader& r, ServoOutputTable* out,
                                   ServoOutputRepairReport* report);
