// =============================================================================
// test/stubs/include/Preferences.h
//
// In-memory stub for Arduino Preferences (ESP32 NVS wrapper) for native tests.
// Implements the minimal interface needed by config_store.cpp tests.
//
// Data is kept per namespace, the way NVS keeps it, not per handle: every
// Preferences that begin()s the same namespace sees the same keys, for the
// lifetime of the test binary. So a test can open the config namespace itself
// to read what a save that opened its own local handle wrote, or to schedule a
// failed write that save will meet (#424). The flip side is that nothing is
// fresh by default any more: a suite that relied on a new handle being empty
// calls Preferences::eraseFlash() in setUp(), which is what erasing the NVS
// partition is on the device.
// =============================================================================
#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

#include "Arduino.h"  // for String

class Preferences {
private:
    // One NVS namespace: its keys, and how many more putString() calls into it
    // must report a failed write. See failNextStringWrites() at the bottom for
    // what the counter stands in for. It lives with the namespace rather than
    // the handle for the reason the keys do: the handle that meets the failure
    // is usually one the code under test opened, not the test's.
    struct Namespace {
        std::map<std::string, std::string> data;
        unsigned failStringWrites = 0;
    };

    // The flash itself. A function-local static in an inline member, so every
    // translation unit of a test binary shares the one instance.
    static std::map<std::string, Namespace>& flash() {
        static std::map<std::string, Namespace> namespaces;
        return namespaces;
    }

    // The namespace this handle has open, or null between end() and begin().
    Namespace* ns = nullptr;

    bool isOpen() const { return ns != nullptr; }

    // One scheduled failure, consumed. Nothing is stored when it fires: an
    // nvs_set_str that returned an error wrote nothing either.
    bool consumeStringWriteFailure() {
        if (ns->failStringWrites == 0) {
            return false;
        }
        ns->failStringWrites--;
        return true;
    }

    // A test helper used on a handle that has no namespace open is a test
    // bug, and would otherwise read as "nothing landed" or "no failure was
    // scheduled". Said and stopped rather than answered.
    void requireOpen(const char* helper) const {
        if (!isOpen()) {
            std::fprintf(stderr, "Preferences stub: %s() on a handle with no namespace open\n",
                         helper);
            std::abort();
        }
    }

public:
    Preferences() = default;
    ~Preferences() = default;

    bool begin(const char* name, bool readOnly = true) {
        (void)readOnly;
        ns = &flash()[name];
        return true;
    }

    void end() { ns = nullptr; }

    // Getters
    bool getBool(const char* key, bool defaultValue = false) const {
        if (!isOpen()) return defaultValue;
        auto it = ns->data.find(key);
        if (it == ns->data.end()) return defaultValue;
        return it->second == "1" || it->second == "true";
    }

    int8_t getChar(const char* key, int8_t defaultValue = 0) const {
        if (!isOpen()) return defaultValue;
        auto it = ns->data.find(key);
        if (it == ns->data.end()) return defaultValue;
        return (int8_t)std::stoi(it->second);
    }

    uint8_t getUChar(const char* key, uint8_t defaultValue = 0) const {
        if (!isOpen()) return defaultValue;
        auto it = ns->data.find(key);
        if (it == ns->data.end()) return defaultValue;
        return (uint8_t)std::stoul(it->second);
    }

    int16_t getShort(const char* key, int16_t defaultValue = 0) const {
        if (!isOpen()) return defaultValue;
        auto it = ns->data.find(key);
        if (it == ns->data.end()) return defaultValue;
        return (int16_t)std::stoi(it->second);
    }

    uint16_t getUShort(const char* key, uint16_t defaultValue = 0) const {
        if (!isOpen()) return defaultValue;
        auto it = ns->data.find(key);
        if (it == ns->data.end()) return defaultValue;
        return (uint16_t)std::stoul(it->second);
    }

    int32_t getInt(const char* key, int32_t defaultValue = 0) const {
        if (!isOpen()) return defaultValue;
        auto it = ns->data.find(key);
        if (it == ns->data.end()) return defaultValue;
        return (int32_t)std::stol(it->second);
    }

    uint32_t getUInt(const char* key, uint32_t defaultValue = 0) const {
        if (!isOpen()) return defaultValue;
        auto it = ns->data.find(key);
        if (it == ns->data.end()) return defaultValue;
        return (uint32_t)std::stoul(it->second);
    }

    int32_t getLong(const char* key, int32_t defaultValue = 0) const {
        return getInt(key, defaultValue);
    }

    uint32_t getULong(const char* key, uint32_t defaultValue = 0) const {
        return getUInt(key, defaultValue);
    }

    float getFloat(const char* key, float defaultValue = 0.0f) const {
        if (!isOpen()) return defaultValue;
        auto it = ns->data.find(key);
        if (it == ns->data.end()) return defaultValue;
        return std::stof(it->second);
    }

    double getDouble(const char* key, double defaultValue = 0.0) const {
        if (!isOpen()) return defaultValue;
        auto it = ns->data.find(key);
        if (it == ns->data.end()) return defaultValue;
        return std::stod(it->second);
    }

    String getString(const char* key, const String& defaultValue = String()) const {
        if (!isOpen()) return defaultValue;
        auto it = ns->data.find(key);
        if (it == ns->data.end()) return defaultValue;
        return String(it->second.c_str());
    }

    String getString(const char* key, const char* defaultValue) const {
        if (!isOpen()) return String(defaultValue);
        auto it = ns->data.find(key);
        if (it == ns->data.end()) return String(defaultValue);
        return String(it->second.c_str());
    }

    // Setters — return size written (or 0 for failure for API compatibility)
    size_t putBool(const char* key, bool value) {
        if (!isOpen()) return 0;
        ns->data[key] = value ? "1" : "0";
        return 1;
    }

    size_t putChar(const char* key, int8_t value) {
        if (!isOpen()) return 0;
        ns->data[key] = std::to_string(value);
        return 1;
    }

    size_t putUChar(const char* key, uint8_t value) {
        if (!isOpen()) return 0;
        ns->data[key] = std::to_string(value);
        return 1;
    }

    size_t putShort(const char* key, int16_t value) {
        if (!isOpen()) return 0;
        ns->data[key] = std::to_string(value);
        return 2;
    }

    size_t putUShort(const char* key, uint16_t value) {
        if (!isOpen()) return 0;
        ns->data[key] = std::to_string(value);
        return 2;
    }

    size_t putInt(const char* key, int32_t value) {
        if (!isOpen()) return 0;
        ns->data[key] = std::to_string(value);
        return 4;
    }

    size_t putUInt(const char* key, uint32_t value) {
        if (!isOpen()) return 0;
        ns->data[key] = std::to_string(value);
        return 4;
    }

    size_t putLong(const char* key, int32_t value) {
        return putInt(key, value);
    }

    size_t putULong(const char* key, uint32_t value) {
        return putUInt(key, value);
    }

    size_t putFloat(const char* key, float value) {
        if (!isOpen()) return 0;
        ns->data[key] = std::to_string(value);
        return 4;
    }

    size_t putDouble(const char* key, double value) {
        if (!isOpen()) return 0;
        ns->data[key] = std::to_string(value);
        return 8;
    }

    size_t putString(const char* key, const char* value) {
        if (!isOpen()) return 0;
        if (consumeStringWriteFailure()) return 0;
        ns->data[key] = value ? value : "";
        return ns->data[key].length();
    }

    size_t putString(const char* key, const String& value) {
        if (!isOpen()) return 0;
        if (consumeStringWriteFailure()) return 0;
        ns->data[key] = std::string(value.c_str());
        return ns->data[key].length();
    }

    // Key management
    bool isKey(const char* key) const {
        if (!isOpen()) return false;
        return ns->data.find(key) != ns->data.end();
    }

    bool remove(const char* key) {
        if (!isOpen()) return false;
        return ns->data.erase(key) > 0;
    }

    void clear() {
        if (!isOpen()) return;
        ns->data.clear();
    }

    // Test helpers

    // What this handle's namespace holds. Open the namespace first, as any
    // reader would: a closed handle has nothing to show.
    const std::map<std::string, std::string>& getData() const {
        requireOpen("getData");
        return ns->data;
    }

    // Make the next `count` string writes into this handle's namespace fail,
    // the way a full or fragmented NVS partition does on the device:
    // nvs_set_str() returns an error, the vendor's Preferences::putString()
    // logs it and returns 0, and nothing is stored
    // (framework-arduinoespressif32 libraries/Preferences/src/
    // Preferences.cpp:264-279). Without this the stub can only ever succeed
    // while it is open, which is why #375's guard had never been exercised in
    // the one state it exists for.
    //
    // Strings only, deliberately. It is writeStr() that could not tell a
    // failed write from an empty one, and a counter aimed at putString() lets
    // a test fail one row of a multi-row save and leave the rest landing.
    //
    // Scheduled on the namespace, so it is still there for the handle the
    // code under test opens after this one is closed.
    void failNextStringWrites(unsigned count) {
        requireOpen("failNextStringWrites");
        ns->failStringWrites = count;
    }

    // Every namespace gone, and every scheduled failure with it: a freshly
    // erased NVS partition. For setUp() in a suite whose tests each expect to
    // start from empty storage.
    //
    // Emptied in place rather than removed from the map: a handle a test left
    // open (a file-scope one, say) points at its namespace, and a std::map
    // node outlives any change to the other entries but not its own erase.
    static void eraseFlash() {
        for (auto& entry : flash()) {
            entry.second = Namespace{};
        }
    }
};
