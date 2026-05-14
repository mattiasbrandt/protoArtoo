// =============================================================================
// include/config_io.h
//
// Abstract I/O interfaces for config persistence seam.
// ConfigReader: read-only typed access to stored config (for deserialization).
// ConfigWriter: write-only typed access for storage (for serialization).
//
// Production implementations: PrefsReader/PrefsWriter (config_nvsio.h).
// Test implementations: MapReader/MapWriter (test/stubs/config/map_config_io.h).
// =============================================================================
#pragma once

#include <Arduino.h>

// ConfigReader: abstract interface for reading typed config values.
// Used by configDeserialize to load from storage.
struct ConfigReader {
    virtual uint8_t  readU8 (const char* key, uint8_t  def) const = 0;
    virtual uint16_t readU16(const char* key, uint16_t def) const = 0;
    virtual int16_t  readI16(const char* key, int16_t  def) const = 0;
    virtual uint32_t readU32(const char* key, uint32_t def) const = 0;
    virtual bool     readBool(const char* key, bool    def) const = 0;
    virtual float    readF32(const char* key, float    def) const = 0;
    virtual String   readStr(const char* key, const char* def) const = 0;
    virtual uint8_t  schemaVersion() const = 0;
    virtual ~ConfigReader() = default;
};

// ConfigWriter: abstract interface for writing typed config values.
// Used by configSerialize to persist to storage.
struct ConfigWriter {
    virtual bool writeU8 (const char* key, uint8_t  value) = 0;
    virtual bool writeU16(const char* key, uint16_t value) = 0;
    virtual bool writeI16(const char* key, int16_t  value) = 0;
    virtual bool writeU32(const char* key, uint32_t value) = 0;
    virtual bool writeBool(const char* key, bool    value) = 0;
    virtual bool writeF32(const char* key, float    value) = 0;
    virtual bool writeStr(const char* key, const char* value) = 0;
    virtual bool writeSchemaVersion(uint8_t v) = 0;
    virtual ~ConfigWriter() = default;
};
