// =============================================================================
// src/drivers/hoverboard_uart.cpp
//
// Gen2.x hoverboard UART frame builder.
// Protocol: 8-byte frame — [0xABCD start][int16 steer][int16 speed][uint16 XOR checksum]
// Reference: https://github.com/RoboDurden/Hoverboard-Firmware-Hack-Gen2.x
//
// Pure logic — no FreeRTOS, no Serial, no task code.
// DriveTask owns UART1 and calls these functions at 50 Hz.
// =============================================================================

#include "hoverboard_uart.h"

#include <string.h>  // memcpy
#ifdef ARDUINO_ARCH_ESP32
#include <HardwareSerial.h>
#endif

// -----------------------------------------------------------------------------
// calcHoverboardChecksum()
// XOR of start word (0xABCD), steer, and speed — as used by Gen2.x firmware.
// Cast to uint16_t is required: steer/speed are signed, XOR treats as bit patterns.
// -----------------------------------------------------------------------------
uint16_t calcHoverboardChecksum(int16_t steer, int16_t speed) {
    return (uint16_t)(0xABCD ^ (uint16_t)steer ^ (uint16_t)speed);
}

// -----------------------------------------------------------------------------
// buildHoverboardFrame()
// Assembles a complete 8-byte Gen2.x UART frame into buf.
// buf must be exactly sizeof(HoverboardFrame) = 8 bytes.
// Uses memcpy to avoid strict-aliasing UB when writing to uint8_t*.
// -----------------------------------------------------------------------------
void buildHoverboardFrame(uint8_t* buf, int16_t steer, int16_t speed) {
    HoverboardFrame frame;
    frame.start = 0xABCD;
    frame.steer = steer;
    frame.speed = speed;
    frame.checksum = calcHoverboardChecksum(steer, speed);
    memcpy(buf, &frame, sizeof(frame));
}

// -----------------------------------------------------------------------------
// Hoverboard feedback parser state (UART1 RX)
// Auto-detects EFeru FOC (18-byte) vs RoboDurden Gen2.x (26-byte) format
// on first valid frame and sticks to that format until reset.
// -----------------------------------------------------------------------------
static uint8_t s_fbBuf[kHoverGen2xFrameLen];
static int s_fbIdx = 0;
static bool s_fbSeekingStart = true;
static bool s_fbFormatKnown = false;
static bool s_fbIsFoc = false;

// -----------------------------------------------------------------------------
// validateXorFrame()
// XOR all uint16_t words (including checksum). Valid frame XORs to zero.
// -----------------------------------------------------------------------------
static bool validateXorFrame(const uint8_t* buf, int numFields) {
    if (buf == nullptr || numFields <= 0) {
        return false;
    }

    uint16_t x = 0;
    for (int i = 0; i < numFields; ++i) {
        uint16_t field = 0;
        memcpy(&field, buf + (i * (int)sizeof(uint16_t)), sizeof(field));
        x ^= field;
    }
    return x == 0;
}

// -----------------------------------------------------------------------------
// parseFocFrame()
// Parse EFeru FOC 18-byte feedback frame.
// -----------------------------------------------------------------------------
static void parseFocFrame(const uint8_t* buf, HoverboardFeedback* out) {
    memcpy(&out->speedR, buf + 6, sizeof(out->speedR));
    memcpy(&out->speedL, buf + 8, sizeof(out->speedL));
    memcpy(&out->batteryRaw, buf + 10, sizeof(out->batteryRaw));
    memcpy(&out->boardTempRaw, buf + 12, sizeof(out->boardTempRaw));
    out->currentL = 0;
    out->currentR = 0;
}

// -----------------------------------------------------------------------------
// parseGen2xFrame()
// Parse RoboDurden Gen2.x 26-byte feedback frame.
// -----------------------------------------------------------------------------
static void parseGen2xFrame(const uint8_t* buf, HoverboardFeedback* out) {
    memcpy(&out->speedR, buf + 6, sizeof(out->speedR));
    memcpy(&out->speedL, buf + 8, sizeof(out->speedL));
    memcpy(&out->currentL, buf + 14, sizeof(out->currentL));
    memcpy(&out->currentR, buf + 16, sizeof(out->currentR));
    memcpy(&out->batteryRaw, buf + 18, sizeof(out->batteryRaw));
    memcpy(&out->boardTempRaw, buf + 20, sizeof(out->boardTempRaw));
}

// -----------------------------------------------------------------------------
// parseHoverboardFeedbackFrame()
// Pure parser for one complete feedback frame.
// -----------------------------------------------------------------------------
bool parseHoverboardFeedbackFrame(const uint8_t* buf, int len, HoverboardFeedback* out) {
    if (buf == nullptr || out == nullptr) {
        return false;
    }

    if (len == kHoverFocFrameLen && validateXorFrame(buf, 9)) {
        parseFocFrame(buf, out);
        return true;
    }

    if (len == kHoverGen2xFrameLen && validateXorFrame(buf, 13)) {
        parseGen2xFrame(buf, out);
        return true;
    }

    return false;
}

// -----------------------------------------------------------------------------
// resetFeedbackAccumulator()
// Clear only framing accumulator; preserve detected frame format.
// -----------------------------------------------------------------------------
static void resetFeedbackAccumulator() {
    memset(s_fbBuf, 0, sizeof(s_fbBuf));
    s_fbIdx = 0;
    s_fbSeekingStart = true;
}

// -----------------------------------------------------------------------------
// resetHoverboardFeedbackParser()
// Clear parser accumulator and auto-detection state.
// -----------------------------------------------------------------------------
void resetHoverboardFeedbackParser() {
    resetFeedbackAccumulator();
    s_fbFormatKnown = false;
    s_fbIsFoc = false;
}

#ifdef ARDUINO_ARCH_ESP32
// -----------------------------------------------------------------------------
// readHoverboardFeedback()
// Non-blocking UART feedback parser for DriveTask-owned serial port.
// -----------------------------------------------------------------------------
bool readHoverboardFeedback(HardwareSerial& uart, HoverboardFeedback* out) {
    if (out == nullptr) {
        return false;
    }

    bool gotFrame = false;

    while (uart.available() > 0) {
        const int raw = uart.read();
        if (raw < 0) {
            break;
        }

        const uint8_t byte = (uint8_t)raw;

        if (s_fbSeekingStart) {
            if (s_fbIdx == 0) {
                if (byte == 0xCD) {
                    s_fbBuf[0] = byte;
                    s_fbIdx = 1;
                }
                continue;
            }

            if (s_fbIdx == 1) {
                if (byte == 0xAB) {
                    s_fbBuf[1] = byte;
                    s_fbIdx = 2;
                    s_fbSeekingStart = false;
                } else if (byte == 0xCD) {
                    s_fbBuf[0] = byte;
                    s_fbIdx = 1;
                } else {
                    s_fbIdx = 0;
                }
            }
            continue;
        }

        if (s_fbIdx >= kHoverGen2xFrameLen) {
            resetFeedbackAccumulator();
            continue;
        }

        s_fbBuf[s_fbIdx++] = byte;

        if (s_fbIdx == kHoverFocFrameLen) {
            if (!s_fbFormatKnown || s_fbIsFoc) {
                if (validateXorFrame(s_fbBuf, 9)) {
                    s_fbFormatKnown = true;
                    s_fbIsFoc = true;
                    parseFocFrame(s_fbBuf, out);
                    resetFeedbackAccumulator();
                    gotFrame = true;
                    continue;
                }

                if (s_fbFormatKnown) {
                    resetFeedbackAccumulator();
                    continue;
                }
            }
        }

        if (s_fbIdx == kHoverGen2xFrameLen) {
            if (validateXorFrame(s_fbBuf, 13)) {
                s_fbFormatKnown = true;
                s_fbIsFoc = false;
                parseGen2xFrame(s_fbBuf, out);
                gotFrame = true;
            }
            resetFeedbackAccumulator();
        }
    }

    return gotFrame;
}
#endif
