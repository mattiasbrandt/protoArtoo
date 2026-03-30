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

// Parser helpers and public API: see hoverboard_uart.h for struct layout.

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

// initHoverboardFeedbackParser()
// Zero-initialise the struct and set seekingStart so the parser starts fresh.
// Preserves no state from a prior session.
void initHoverboardFeedbackParser(HoverboardFeedbackParser* p) {
    if (p == nullptr) return;
    memset(p->buf, 0, sizeof(p->buf));
    p->idx          = 0;
    p->seekingStart = true;
    p->formatKnown  = false;
    p->isFoc        = false;
}

#ifdef ARDUINO_ARCH_ESP32
// readHoverboardFeedback()
// Non-blocking UART feedback parser for DriveTask-owned serial port.
bool readHoverboardFeedback(HardwareSerial& uart,
                            HoverboardFeedbackParser* parser,
                            HoverboardFeedback* out) {
    if (parser == nullptr || out == nullptr) return false;

    bool gotFrame = false;

    while (uart.available() > 0) {
        const int raw = uart.read();
        if (raw < 0) break;
        const uint8_t b = (uint8_t)raw;

        if (parser->seekingStart) {
            if (parser->idx == 0) {
                if (b == 0xCD) { parser->buf[0] = b; parser->idx = 1; }
            } else {  // idx == 1, waiting for 0xAB
                if (b == 0xAB) {
                    parser->buf[1] = b; parser->idx = 2; parser->seekingStart = false;
                } else if (b == 0xCD) {
                    parser->buf[0] = b;  // keep idx=1
                } else {
                    parser->idx = 0;
                }
            }
            continue;
        }

        // Guard against accumulator overflow (should not happen in practice)
        if (parser->idx >= kHoverGen2xFrameLen) {
            initHoverboardFeedbackParser(parser);
            continue;
        }

        parser->buf[parser->idx++] = b;

        // Try FOC format at 18 bytes
        if (parser->idx == kHoverFocFrameLen) {
            if (!parser->formatKnown || parser->isFoc) {
                if (validateXorFrame(parser->buf, 9)) {
                    parser->formatKnown = true; parser->isFoc = true;
                    parseFocFrame(parser->buf, out);
                    memset(parser->buf, 0, sizeof(parser->buf));
                    parser->idx = 0; parser->seekingStart = true;
                    gotFrame = true;
                    continue;
                }
                if (parser->formatKnown) {  // known FOC, bad checksum — resync
                    memset(parser->buf, 0, sizeof(parser->buf));
                    parser->idx = 0; parser->seekingStart = true;
                    continue;
                }
                // unknown format, FOC failed — keep accumulating to 26 bytes
            }
            // known Gen2.x — keep accumulating
        }

        // Try Gen2.x format at 26 bytes
        if (parser->idx == kHoverGen2xFrameLen) {
            if (validateXorFrame(parser->buf, 13)) {
                parser->formatKnown = true; parser->isFoc = false;
                parseGen2xFrame(parser->buf, out);
                gotFrame = true;
            }
            memset(parser->buf, 0, sizeof(parser->buf));
            parser->idx = 0; parser->seekingStart = true;
        }
    }

    return gotFrame;
}
#endif
