// =============================================================================
// test/stubs/config/map_config_io.h
//
// In-memory test doubles for ConfigReader/ConfigWriter.
// All values are stored as strings in a std::map and converted on access,
// mirroring the NVS text-serialization path without any hardware dependency.
// Used only in native unit test builds.
// =============================================================================
#pragma once

#include <map>
#include <string>
#include <stdexcept>
#include <sstream>
#include <cstdlib>

#include "config_io.h"

namespace {

// MapReader: implements ConfigReader using an in-memory string map.
class MapReader : public ConfigReader {
public:
    MapReader() : schema_version_(0) {}

    uint8_t  readU8 (const char* key, uint8_t  def) const override {
        return static_cast<uint8_t>(readInt_(key, static_cast<int>(def)));
    }

    uint16_t readU16(const char* key, uint16_t def) const override {
        return static_cast<uint16_t>(readInt_(key, static_cast<int>(def)));
    }

    int16_t  readI16(const char* key, int16_t  def) const override {
        return static_cast<int16_t>(readInt_(key, static_cast<int>(def)));
    }

    uint32_t readU32(const char* key, uint32_t def) const override {
        auto it = data_.find(key);
        if (it == data_.end()) {
            return def;
        }
        try {
            return static_cast<uint32_t>(std::stoul(it->second));
        } catch (...) {
            return def;
        }
    }

    bool     readBool(const char* key, bool def) const override {
        auto it = data_.find(key);
        if (it == data_.end()) {
            return def;
        }
        const std::string& s = it->second;
        return s == "1" || s == "true" || s == "True" || s == "TRUE";
    }

    float    readF32(const char* key, float def) const override {
        auto it = data_.find(key);
        if (it == data_.end()) {
            return def;
        }
        try {
            return std::stof(it->second);
        } catch (...) {
            return def;
        }
    }

    String   readStr(const char* key, const char* def) const override {
        auto it = data_.find(key);
        if (it == data_.end()) {
            return String(def);
        }
        return String(it->second.c_str());
    }

    uint8_t  schemaVersion() const override {
        return schema_version_;
    }

    // Test API: set or update a value
    void set(const char* key, const std::string& value) {
        data_[key] = value;
    }

    void set(const char* key, uint32_t value) {
        data_[key] = std::to_string(value);
    }

    void set(const char* key, float value) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.6f", value);
        data_[key] = buf;
    }

    void set(const char* key, bool value) {
        data_[key] = value ? "1" : "0";
    }

    void setSchemaVersion(uint8_t v) {
        schema_version_ = v;
    }

    // Test API: access underlying data for verification
    const std::map<std::string, std::string>& data() const {
        return data_;
    }

private:
    std::map<std::string, std::string> data_;
    uint8_t schema_version_;

    // Shared narrow-int reader; callers cast to the correct unsigned type
    int readInt_(const char* key, int def) const {
        auto it = data_.find(key);
        if (it == data_.end()) {
            return def;
        }
        try {
            return std::stoi(it->second);
        } catch (...) {
            return def;
        }
    }
};

// MapWriter: implements ConfigWriter using an in-memory string map.
class MapWriter : public ConfigWriter {
public:
    MapWriter() : schema_version_(0) {}

    bool writeU8 (const char* key, uint8_t  value) override {
        data_[key] = std::to_string(value);
        return true;
    }

    bool writeU16(const char* key, uint16_t value) override {
        data_[key] = std::to_string(value);
        return true;
    }

    bool writeI16(const char* key, int16_t  value) override {
        data_[key] = std::to_string(value);
        return true;
    }

    bool writeU32(const char* key, uint32_t value) override {
        data_[key] = std::to_string(value);
        return true;
    }

    bool writeBool(const char* key, bool value) override {
        data_[key] = value ? "1" : "0";
        return true;
    }

    bool writeF32(const char* key, float value) override {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.6f", value);
        data_[key] = buf;
        return true;
    }

    bool writeStr(const char* key, const char* value) override {
        // Match PrefsWriter contract: nullptr is an error, not an empty string
        if (value == nullptr) return false;
        data_[key] = value;
        return true;
    }

    bool writeSchemaVersion(uint8_t v) override {
        schema_version_ = v;
        return true;
    }

    // Test API: access underlying data for verification
    const std::map<std::string, std::string>& data() const {
        return data_;
    }

    uint8_t schemaVersion() const {
        return schema_version_;
    }

private:
    std::map<std::string, std::string> data_;
    uint8_t schema_version_;
};

}  // anonymous namespace
