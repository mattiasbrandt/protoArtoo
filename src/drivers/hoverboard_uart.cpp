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
