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
bool configSerializeServoOutputCount(uint8_t count, ConfigWriter& w);
bool configSerializeServoOutputRow(uint8_t index, const ServoOutputRow& row, ConfigWriter& w);
bool configSerializeServoOutputs(const ServoOutputTable& table, ConfigWriter& w);

// Fills *out with servoOutputTableDefaults() then overwrites with stored rows.
// A row whose key is absent keeps its default silently -- that is a device that
// has never written it, not a damaged record. A row whose record exists but
// cannot be read is repaired field by field and counted in *report.
void configDeserializeServoOutputs(const ConfigReader& r, ServoOutputTable* out,
                                   ServoOutputRepairReport* report);
