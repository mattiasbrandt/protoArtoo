// =============================================================================
// include/hoverboard_uart.h
//
// Gen2.x hoverboard UART frame builder interface.
// Protocol: 8-byte frame — [0xABCD start][int16 steer][int16 speed][uint16 XOR checksum]
// Reference: https://github.com/RoboDurden/Hoverboard-Firmware-Hack-Gen2.x
// =============================================================================
#pragma once
#include <stdint.h>

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
