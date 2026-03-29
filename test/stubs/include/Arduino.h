// =============================================================================
// test/stubs/include/Arduino.h
// Minimal Arduino type stubs for native host builds.
// Only included when test/stubs/include is on the include path ([env:native]).
// =============================================================================
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string>

// Tell servo_component_helpers.h not to redefine ServoComponentType
// (robot_state.h always defines it; this guard prevents the duplicate)
#ifndef ARDUINO_ARCH_ESP32
#define ARDUINO_ARCH_ESP32
#endif

// Minimal String stub — c_str() access only
class String {
public:
    String() {}
    explicit String(const char* s) : _s(s ? s : "") {}
    const char* c_str() const { return _s.c_str(); }
    size_t length() const { return _s.length(); }
private:
    std::string _s;
};

// Serial stub — PA_LOG_* macros call Serial.printf(...)
struct SerialStub {
    template<typename... Args>
    static void printf(const char* /*fmt*/, Args... /*args*/) {}
    static void println(const char* /*s*/) {}
    static void print(const char* /*s*/) {}
};
extern SerialStub Serial;
