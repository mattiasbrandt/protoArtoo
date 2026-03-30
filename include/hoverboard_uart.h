// =============================================================================
// include/hoverboard_uart.h
//
// Gen2.x hoverboard UART frame builder interface.
// Protocol: 8-byte frame — [0xABCD start][int16 steer][int16 speed][uint16 XOR checksum]
// Reference: https://github.com/RoboDurden/Hoverboard-Firmware-Hack-Gen2.x
// =============================================================================
#pragma once
#include <stdint.h>
#include <stddef.h>
// -----------------------------------------------------------------------------
// HoverboardFrame
// Packed 8-byte Gen2.x protocol frame layout.
// Little-endian fields as transmitted over UART1 at 115200 baud.
// -----------------------------------------------------------------------------
struct HoverboardFrame {
    uint16_t start;     // Always 0xABCD
    int16_t steer;      // -1000..+1000 (left/right)
    int16_t speed;      // -1000..+1000 (forward/reverse)
    uint16_t checksum;  // start ^ steer ^ speed
};

// -----------------------------------------------------------------------------
// calcHoverboardChecksum()
// XOR checksum for a Gen2.x hoverboard frame.
// params: steer — signed steer value (-1000..+1000)
//         speed — signed speed value (-1000..+1000)
// returns: uint16_t XOR of 0xABCD, steer, speed
// thread-safe: yes (pure function)
// -----------------------------------------------------------------------------
uint16_t calcHoverboardChecksum(int16_t steer, int16_t speed);

// -----------------------------------------------------------------------------
// buildHoverboardFrame()
// Assembles an 8-byte Gen2.x frame into a caller-supplied buffer.
// params: buf   — output buffer, must be exactly 8 bytes
//         steer — signed steer value (-1000..+1000)
//         speed — signed speed value (-1000..+1000)
// thread-safe: yes (no globals, no shared state)
// -----------------------------------------------------------------------------
void buildHoverboardFrame(uint8_t* buf, int16_t steer, int16_t speed);

// -----------------------------------------------------------------------------
// HoverboardFeedback
// Parsed feedback values from hoverboard controller RX status frames.
// batteryRaw  = V × 100; divide by 100.0 for volts.
// boardTempRaw = °C × 10; divide by 10.0 for °C.
// currentL/currentR = A × 100 (Gen2.x only; 0 for FOC firmware).
// -----------------------------------------------------------------------------
struct HoverboardFeedback {
    int16_t batteryRaw;    // V × 100 (divide by 100.0 for volts)
    int16_t boardTempRaw;  // °C × 10 (divide by 10.0 for °C)
    int16_t speedR;        // right motor speed, RPM
    int16_t speedL;        // left motor speed, RPM
    int16_t currentL;      // left motor current × 100 = A (Gen2.x only; 0 for FOC)
    int16_t currentR;      // right motor current × 100 = A (Gen2.x only; 0 for FOC)
};

// Frame length constants (used by parser and tests)
static constexpr int kHoverFocFrameLen = 18;
static constexpr int kHoverGen2xFrameLen = 26;

// parseHoverboardFeedbackFrame()
// Pure logic — no hardware, no FreeRTOS. Testable on native.
// Validates and parses a single complete feedback frame.
// buf: exactly kHoverFocFrameLen (18) or kHoverGen2xFrameLen (26) bytes,
//      starting with start marker 0xABCD (little-endian: 0xCD 0xAB).
// len: must be exactly kHoverFocFrameLen or kHoverGen2xFrameLen.
// Returns true if the XOR checksum is valid and out is populated.
// Returns false if checksum invalid or len is unrecognised.
// thread-safe: yes (no globals — state is in caller's variables)
bool parseHoverboardFeedbackFrame(const uint8_t* buf, int len, HoverboardFeedback* out);

// resetHoverboardFeedbackParser()
// Clear the internal byte accumulator and format-detection state.
// Call after HardwareSerial::begin() to ensure clean parser state.
void resetHoverboardFeedbackParser();

#ifdef ARDUINO_ARCH_ESP32
class HardwareSerial;
// readHoverboardFeedback()
// Non-blocking. Drains available bytes from uart; decodes any complete
// feedback frame. Returns true if a new valid frame was decoded.
// Call from DriveTask after sending each hoverboard drive frame.
// Auto-detects EFeru FOC (18-byte) vs RoboDurden Gen2.x (26-byte) on the
// first valid frame; sticks to detected format until resetHoverboardFeedbackParser().
// thread-safe: must only be called from the task that owns the serial port.
bool readHoverboardFeedback(HardwareSerial& uart, HoverboardFeedback* out);
#endif
