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
//
// availableForWrite() / operator bool() (ADR 0036, #265): test-controlled so
// the room-wait seam in src/console/console_serial_output.cpp is provable on
// the host without a board. Defaults ("unlimited room", "connected") match
// what every native test written before #265 already assumed implicitly, so
// this extension is additive — nothing that does not touch these knobs can
// observe a behavior change.
//
// The write() capture exists for the same reason: consoleSerialEmitFramedLine()
// writes the record/log line straight to Serial (not through embedded-cli's
// per-character cli->writeChar, which test_console_serial_output.cpp already
// captures separately) and its own tests need to see exactly what reached
// the wire and in how many calls.
struct SerialStub {
    // Test-controlled knobs — see serialStubReset() below for the one place
    // that owns their defaults.
    static inline int availableForWriteValue = 100000;
    static inline bool connectedValue = true;

    // Bytes handed to write(), across however many calls were made, plus a
    // count of those calls. Deliberately separate from the writeChar capture
    // used elsewhere in this header's neighboring test file: merging them
    // would make "one write call" assertions ambiguous about which sink
    // produced a given byte.
    static inline char capturedBuf[2048] = {};
    static inline size_t capturedLen = 0;
    static inline int writeCallCount = 0;
    static inline int availableForWriteCallCount = 0;

    template<typename... Args>
    static void printf(const char* /*fmt*/, Args... /*args*/) {}
    static void println(const char* /*s*/) {}
    static void print(const char* /*s*/) {}
    static size_t write(uint8_t c) {
        writeCallCount++;
        if (capturedLen + 1 < sizeof(capturedBuf)) {
            capturedBuf[capturedLen++] = (char)c;
        }
        return 1;
    }
    static size_t write(const uint8_t* data, size_t len) {
        writeCallCount++;
        for (size_t i = 0; i < len && capturedLen + 1 < sizeof(capturedBuf); ++i) {
            capturedBuf[capturedLen++] = (char)data[i];
        }
        return len;
    }
    static void flush() {}
    static int available() { return 0; }
    static int read() { return -1; }
    static int availableForWrite() {
        availableForWriteCallCount++;
        return availableForWriteValue;
    }
    operator bool() const { return connectedValue; }
};
extern SerialStub Serial;

// Resets every SerialStub knob and capture to its default (unlimited room,
// connected, empty capture, zeroed counters). Call at the top of any test
// that touches the ADR 0036 room-wait/single-write seam — this is one global
// shared by every native test in the binary, so state left over from a prior
// test would otherwise leak forward.
inline void serialStubReset() {
    SerialStub::availableForWriteValue = 100000;
    SerialStub::connectedValue = true;
    SerialStub::capturedLen = 0;
    SerialStub::capturedBuf[0] = '\0';
    SerialStub::writeCallCount = 0;
    SerialStub::availableForWriteCallCount = 0;
}

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
