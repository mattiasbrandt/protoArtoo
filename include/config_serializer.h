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
