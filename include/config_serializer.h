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
#include "droid_build.h"
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
// The only place an endpoint is stored (#345), on their own keys, and
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
// A row whose record exists but cannot be read is repaired field by field and
// counted in *report.
//
// A row whose key is absent crosses the bridge instead (#286): it adopts
// whatever the fixed key set addressed to its channel still holds in NVS, so a
// builder's existing calibration arrives on the rows on first read, with no
// migration marker to keep and no write on the boot path. A stored row always
// wins over the old form, which is what makes the adoption idempotent -- it
// stops mattering for a row the moment that row is saved, and
// configSaveServoOutputs() then removes the old keys. The key names are
// include/servo_legacy_field_sets.h's. The only repair an adoption can report
// is a pulse width the component band had to move, and it is counted like any
// other.
void configDeserializeServoOutputs(const ConfigReader& r, ServoOutputTable* out,
                                   ServoOutputRepairReport* report);

// -----------------------------------------------------------------------------
// The Droid Build (ADR 0047)
//
// Which droid a builder says they built, and which Parts are on it. On its own
// keys and outside ConfigSnapshot for the same reason the Servo Output rows
// above are: the snapshot crosses three nested frames on the serial
// config-write path, and nothing on a real-time path reads a Droid Build.
//
// Five keys: a design and a variant for each half, and one holding the Fitted
// Parts as a comma-separated Part id list. The Parts are stored as IDS rather
// than as the bitmap they are held in, because the bitmap's indices are
// emission order and the catalog grows - a Part added in the middle would
// re-point every bit after it at a different Part, silently.
//
// An ABSENT Fitted Parts record is a controller that has never been answered,
// and it takes the default complement. An EMPTY one is a real answer - a droid
// with nothing fitted yet - and is kept. Those two must not read alike: a
// builder who cleared their droid would otherwise have the pre-selected design
// re-fitted under them on the next boot.
// -----------------------------------------------------------------------------
bool configSerializeDroidBuild(const DroidBuildConfig& cfg, ConfigWriter& w);

// Fills *out with droidBuildDefaults() then overwrites with stored values. A
// stored half this image's catalog no longer declares is repaired to the
// default and counted in *report, the way a damaged Servo Output row is; a
// stored Part id it no longer declares is dropped and counted.
void configDeserializeDroidBuild(const ConfigReader& r, DroidBuildConfig* out,
                                 DroidBuildRepairReport* report);
