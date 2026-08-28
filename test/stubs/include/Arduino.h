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
    String& operator=(const char* s) {
        _s = s ? s : "";
        return *this;
    }
    String& operator=(const String& s) {
        _s = s._s;
        return *this;
    }
private:
    std::string _s;
};

// Serial stub — used by code compiled in native host tests.
struct SerialStub {
    template<typename... Args>
    static void printf(const char* /*fmt*/, Args... /*args*/) {}
    static void println(const char* /*s*/) {}
    static void print(const char* /*s*/) {}
};
extern SerialStub Serial;

// millis() stub — used by failsafe gate and other timing code
unsigned long millis();

// Blocking delay stubs — no-ops in native builds
inline void delay(unsigned long) {}
inline void delayMicroseconds(unsigned int) {}

// GPIO stubs — no-ops; used by audio_soft_uart_tx.h inline functions
static constexpr uint8_t OUTPUT = 1;
static constexpr uint8_t INPUT  = 0;
static constexpr uint8_t HIGH   = 1;
static constexpr uint8_t LOW    = 0;
inline void pinMode(uint8_t, uint8_t) {}
inline void digitalWrite(uint8_t, uint8_t) {}

// SERIAL_8N1 constant stub
static constexpr uint8_t SERIAL_8N1 = 0x06;

// HardwareSerial stub — used by audio driver sources in native builds.
// RX methods return no-data defaults; begin() is a no-op.
class HardwareSerial {
   public:
    explicit HardwareSerial(int /*uart_nr*/) {}
    void begin(unsigned long /*baud*/, uint8_t /*config*/ = 0,
               int /*rx*/ = -1, int /*tx*/ = -1) {}
    int available() { return 0; }
    int read() { return -1; }
    void flush() {}
};

// constrain() — Arduino helper function
template<typename T>
inline T constrain(T value, T min_val, T max_val) {
    if (value < min_val) return min_val;
    if (value > max_val) return max_val;
    return value;
}

// ESP stub — provides heap info methods used by console_module.cpp
// Minimal stub with default zero values to avoid affecting other tests.
struct ESPClass {
    unsigned long getFreeHeap() const { return 0; }
    unsigned long getMinFreeHeap() const { return 0; }
    unsigned long getMaxAllocHeap() const { return 0; }
};
extern ESPClass ESP;
