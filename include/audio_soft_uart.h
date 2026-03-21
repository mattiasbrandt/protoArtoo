// =============================================================================
// include/audio_soft_uart.h
//
// Concrete AudioDriver for the DY-SV5W audio module.
//
// Uses HardwareSerial(2) remapped to PIN_AUDIO_TX (GPIO 26) / PIN_AUDIO_RX
// (GPIO 35) at 9600 baud 8N1. UART2 is shared with the dome serial link;
// only one may be active at a time. See T65 for contention resolution plan.
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
    const char* driverName() const override {
        return "DY-SV5W";
    }

    // Query live module state via UART RX: device, play state, current track.
    // Sends three query frames and waits up to 300 ms each for a response.
    // Returns true if at least one query received a valid response (link alive).
    // Only call from AudioTask (Core 0).
    bool queryModuleState(AudioModuleState& out) override;

   private:
    // Last known total tracks and device — populated during begin() and carried
    // forward when queryModuleState() is called (total-tracks query runs in begin
    // only, not on every periodic poll to keep poll overhead low).
    uint16_t m_totalTracks = 0;
    uint8_t m_device = 0xFF;  // 0xFF = unknown until first successful query

    // Send one DY-SV5W checksum frame via UART2.
    // data = [0xAA, CMD, LEN, DATA...]
    void sendFrame(const uint8_t* data, uint8_t len);
};
