// =============================================================================
// test/stubs/include/Preferences.h
//
// In-memory stub for Arduino Preferences (ESP32 NVS wrapper) for native tests.
// Implements the minimal interface needed by config_store.cpp tests.
// =============================================================================
#pragma once

#include <cstring>
#include <map>
#include <string>

#include "Arduino.h"  // for String

class Preferences {
private:
    std::map<std::string, std::string> data;
    bool isOpen = false;

public:
    Preferences() = default;
    ~Preferences() = default;

    bool begin(const char* ns, bool readOnly = true) {
        (void)ns;
        (void)readOnly;
        isOpen = true;
        return true;
    }

    void end() { isOpen = false; }

    // Getters
    bool getBool(const char* key, bool defaultValue = false) const {
        if (!isOpen) return defaultValue;
        auto it = data.find(key);
        if (it == data.end()) return defaultValue;
        return it->second == "1" || it->second == "true";
    }

    int8_t getChar(const char* key, int8_t defaultValue = 0) const {
        if (!isOpen) return defaultValue;
        auto it = data.find(key);
        if (it == data.end()) return defaultValue;
        return (int8_t)std::stoi(it->second);
    }

    uint8_t getUChar(const char* key, uint8_t defaultValue = 0) const {
        if (!isOpen) return defaultValue;
        auto it = data.find(key);
        if (it == data.end()) return defaultValue;
        return (uint8_t)std::stoul(it->second);
    }

    int16_t getShort(const char* key, int16_t defaultValue = 0) const {
        if (!isOpen) return defaultValue;
        auto it = data.find(key);
        if (it == data.end()) return defaultValue;
        return (int16_t)std::stoi(it->second);
    }

    uint16_t getUShort(const char* key, uint16_t defaultValue = 0) const {
        if (!isOpen) return defaultValue;
        auto it = data.find(key);
        if (it == data.end()) return defaultValue;
        return (uint16_t)std::stoul(it->second);
    }

    int32_t getInt(const char* key, int32_t defaultValue = 0) const {
        if (!isOpen) return defaultValue;
        auto it = data.find(key);
        if (it == data.end()) return defaultValue;
        return (int32_t)std::stol(it->second);
    }

    uint32_t getUInt(const char* key, uint32_t defaultValue = 0) const {
        if (!isOpen) return defaultValue;
        auto it = data.find(key);
        if (it == data.end()) return defaultValue;
        return (uint32_t)std::stoul(it->second);
    }

    int32_t getLong(const char* key, int32_t defaultValue = 0) const {
        return getInt(key, defaultValue);
    }

    uint32_t getULong(const char* key, uint32_t defaultValue = 0) const {
        return getUInt(key, defaultValue);
    }

    float getFloat(const char* key, float defaultValue = 0.0f) const {
        if (!isOpen) return defaultValue;
        auto it = data.find(key);
        if (it == data.end()) return defaultValue;
        return std::stof(it->second);
    }

    double getDouble(const char* key, double defaultValue = 0.0) const {
        if (!isOpen) return defaultValue;
        auto it = data.find(key);
        if (it == data.end()) return defaultValue;
        return std::stod(it->second);
    }

    String getString(const char* key, const String& defaultValue = String()) const {
        if (!isOpen) return defaultValue;
        auto it = data.find(key);
        if (it == data.end()) return defaultValue;
        return String(it->second.c_str());
    }

    String getString(const char* key, const char* defaultValue) const {
        if (!isOpen) return String(defaultValue);
        auto it = data.find(key);
        if (it == data.end()) return String(defaultValue);
        return String(it->second.c_str());
    }

    // Setters — return size written (or 0 for failure for API compatibility)
    size_t putBool(const char* key, bool value) {
        if (!isOpen) return 0;
        data[key] = value ? "1" : "0";
        return 1;
    }

    size_t putChar(const char* key, int8_t value) {
        if (!isOpen) return 0;
        data[key] = std::to_string(value);
        return 1;
    }

    size_t putUChar(const char* key, uint8_t value) {
        if (!isOpen) return 0;
        data[key] = std::to_string(value);
        return 1;
    }

    size_t putShort(const char* key, int16_t value) {
        if (!isOpen) return 0;
        data[key] = std::to_string(value);
        return 2;
    }

    size_t putUShort(const char* key, uint16_t value) {
        if (!isOpen) return 0;
        data[key] = std::to_string(value);
        return 2;
    }

    size_t putInt(const char* key, int32_t value) {
        if (!isOpen) return 0;
        data[key] = std::to_string(value);
        return 4;
    }

    size_t putUInt(const char* key, uint32_t value) {
        if (!isOpen) return 0;
        data[key] = std::to_string(value);
        return 4;
    }

    size_t putLong(const char* key, int32_t value) {
        return putInt(key, value);
    }

    size_t putULong(const char* key, uint32_t value) {
        return putUInt(key, value);
    }

    size_t putFloat(const char* key, float value) {
        if (!isOpen) return 0;
        data[key] = std::to_string(value);
        return 4;
    }

    size_t putDouble(const char* key, double value) {
        if (!isOpen) return 0;
        data[key] = std::to_string(value);
        return 8;
    }

    size_t putString(const char* key, const char* value) {
        if (!isOpen) return 0;
        data[key] = value ? value : "";
        return data[key].length();
    }

    size_t putString(const char* key, const String& value) {
        if (!isOpen) return 0;
        data[key] = std::string(value.c_str());
        return data[key].length();
    }

    // Key management
    bool isKey(const char* key) const {
        if (!isOpen) return false;
        return data.find(key) != data.end();
    }

    bool remove(const char* key) {
        if (!isOpen) return false;
        return data.erase(key) > 0;
    }

    void clear() { data.clear(); }

    // Test helpers
    const std::map<std::string, std::string>& getData() const { return data; }
};
