// =============================================================================
// include/audio_soft_uart.h
//
// Concrete AudioDriver implementation using software UART TX.
//
// Implements a binary frame protocol compatible with common UART-controlled
// audio modules (e.g. DY-SV5W, DFPlayer-family). The specific command bytes
// are defined in audio_soft_uart.cpp and must be verified against the installed
// module during hardware validation (T09).
//
// Communication: software bit-bang UART TX-only, 9600 baud 8N1, on
// PIN_AUDIO_TX (GPIO 26). All three hardware UARTs are reserved for debug,
// hoverboard, and dome serial; software TX is used here instead.
//
// Wire protocol: binary frames — 0xAA [CMD] [LEN] [DATA...] 0xAB
//
// Only compiled when PA_AUDIO_DRIVER == AUDIO_SOFT_UART (platformio.ini).
// =============================================================================
#pragma once

#include <stdint.h>

#include "audio_driver.h"

class AudioDriverSoftUart : public AudioDriver {
public:
    // Initialise PIN_AUDIO_TX as output and set idle HIGH.
    void begin() override;

    // Play track by 1-based index. Track 0 is silently ignored.
    void playTrack(uint16_t track) override;

    // Stop current playback.
    void stop() override;

    // Set volume 0–30. Value is assumed in-range (clamped by AudioTask).
    void setVolume(uint8_t vol) override;

private:
    // Transmit one byte via software UART (9600 baud, 8N1, LSB-first).
    void softTxByte(uint8_t b);

    // Send a complete binary command frame byte-by-byte.
    void sendFrame(const uint8_t* data, uint8_t len);
};
