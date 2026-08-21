// =============================================================================
// include/config_nvsio.h
//
// NVS adapter implementations of ConfigReader and ConfigWriter.
// PrefsReader wraps Preferences for reading; PrefsWriter for writing.
// Production implementations used by configLoad/configSave.
// =============================================================================
#pragma once

#include <Preferences.h>
#include "config_io.h"

// PrefsReader: wraps Preferences, implements ConfigReader
class PrefsReader : public ConfigReader {
public:
    explicit PrefsReader(Preferences& prefs);

    uint8_t  readU8 (const char* key, uint8_t  def) const override;
    uint16_t readU16(const char* key, uint16_t def) const override;
    int16_t  readI16(const char* key, int16_t  def) const override;
    uint32_t readU32(const char* key, uint32_t def) const override;
    bool     readBool(const char* key, bool    def) const override;
    float    readF32(const char* key, float    def) const override;
    String   readStr(const char* key, const char* def) const override;
    uint8_t  schemaVersion() const override;

private:
    Preferences& prefs_;
};

// PrefsWriter: wraps Preferences, implements ConfigWriter
class PrefsWriter : public ConfigWriter {
public:
    explicit PrefsWriter(Preferences& prefs);

    bool writeU8 (const char* key, uint8_t  value) override;
    bool writeU16(const char* key, uint16_t value) override;
    bool writeI16(const char* key, int16_t  value) override;
    bool writeU32(const char* key, uint32_t value) override;
    bool writeBool(const char* key, bool    value) override;
    bool writeF32(const char* key, float    value) override;
    bool writeStr(const char* key, const char* value) override;
    bool writeSchemaVersion(uint8_t v) override;

private:
    Preferences& prefs_;
};
