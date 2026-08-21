// =============================================================================
// src/config_nvsio.cpp
//
// NVS adapter implementations.
// =============================================================================

#include "config_nvsio.h"

#include <cstring>

namespace {

// Helper: Convert float to/from uint32_t bit representation
float floatFromBits(uint32_t value) {
    float result = 0.0f;
    memcpy(&result, &value, sizeof(result));
    return result;
}

uint32_t floatToBits(float value) {
    uint32_t result = 0;
    memcpy(&result, &value, sizeof(result));
    return result;
}

}  // namespace

// PrefsReader implementation
PrefsReader::PrefsReader(Preferences& prefs) : prefs_(prefs) {}

uint8_t PrefsReader::readU8(const char* key, uint8_t def) const {
    return prefs_.getUChar(key, def);
}

uint16_t PrefsReader::readU16(const char* key, uint16_t def) const {
    return prefs_.getUShort(key, def);
}

int16_t PrefsReader::readI16(const char* key, int16_t def) const {
    return prefs_.getShort(key, def);
}

uint32_t PrefsReader::readU32(const char* key, uint32_t def) const {
    return prefs_.getULong(key, def);
}

bool PrefsReader::readBool(const char* key, bool def) const {
    return prefs_.getBool(key, def);
}

float PrefsReader::readF32(const char* key, float def) const {
    return floatFromBits(prefs_.getULong(key, floatToBits(def)));
}

String PrefsReader::readStr(const char* key, const char* def) const {
    return prefs_.getString(key, String(def));
}

uint8_t PrefsReader::schemaVersion() const {
    return prefs_.getUChar("schema_ver", 0);
}

// PrefsWriter implementation
PrefsWriter::PrefsWriter(Preferences& prefs) : prefs_(prefs) {}

bool PrefsWriter::writeU8(const char* key, uint8_t value) {
    return prefs_.putUChar(key, value) > 0;
}

bool PrefsWriter::writeU16(const char* key, uint16_t value) {
    return prefs_.putUShort(key, value) > 0;
}

bool PrefsWriter::writeI16(const char* key, int16_t value) {
    return prefs_.putShort(key, value) > 0;
}

bool PrefsWriter::writeU32(const char* key, uint32_t value) {
    return prefs_.putULong(key, value) > 0;
}

bool PrefsWriter::writeBool(const char* key, bool value) {
    return prefs_.putBool(key, value) > 0;
}

bool PrefsWriter::writeF32(const char* key, float value) {
    return prefs_.putULong(key, floatToBits(value)) > 0;
}

bool PrefsWriter::writeStr(const char* key, const char* value) {
    if (value == nullptr) {
        return false;
    }
    // putString returns length(), which is 0 for empty strings  --  not an error.
    prefs_.putString(key, value);
    return true;
}

bool PrefsWriter::writeSchemaVersion(uint8_t v) {
    return prefs_.putUChar("schema_ver", v) > 0;
}
