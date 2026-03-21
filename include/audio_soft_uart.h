// =============================================================================
// include/audio_soft_uart.h
//
// Concrete AudioDriver for the DY-SV5W audio module.
//
// Uses software UART TX on PIN_AUDIO_TX (GPIO 26) at 9600 baud 8N1.
// This avoids UART2 contention with SBUS2 and dome serial features.
//
// For compatibility with DY-SV5W firmware variants, this driver sends the
// checksum-frame dialect used by DYPlayer/BetterDuino.
//
// Only compiled when PA_AUDIO_DRIVER == AUDIO_SOFT_UART (platformio.ini).
// =============================================================================
#pragma once

#include <stdint.h>

#include "audio_driver.h"

class AudioDriverSoftUart : public AudioDriver {
public:
    void begin() override;
    void playTrack(uint16_t track) override;
    void stop() override;
    void setVolume(uint8_t vol) override;
    const char* driverName() const override { return "DY-SV5W"; }

private:
    // Send one DY checksum-frame payload.
    // data = [0xAA, CMD, LEN, DATA...]
    void sendFrame(const uint8_t* data, uint8_t len);
};
