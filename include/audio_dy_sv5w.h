// =============================================================================
// include/audio_dy_sv5w.h
//
// Concrete AudioDriver for the DY-SV5W audio module.
//
// TX uses interrupt-protected software UART on GPIO26 (PIN_AUDIO_TX) via
// audio_soft_uart_tx.h.
// RX queries use HardwareSerial(2) on GPIO35 (PIN_AUDIO_RX) when UART2 is not
// contended by dome link or SBUS2; last-cached AudioModuleState is returned
// otherwise.
//
// AudioModuleState interface, queryModuleState(), and getCachedState() remain
// fully intact. See T66 for UART2 contention handling.
//
// For compatibility with DY-SV5W firmware variants, this driver sends the
// checksum-frame dialect used by DYPlayer/BetterDuino.
//
// Only compiled when PA_AUDIO_DRIVER == AUDIO_SOFT_UART (platformio.ini).
// =============================================================================
#pragma once

#include <stdint.h>

#include "audio_driver.h"

class AudioDriverDySv5w : public AudioDriver {
   public:
    void begin(uint8_t vol) override;
    void playTrack(uint16_t track) override;
    void stop() override;
    void setVolume(uint8_t vol) override;
    const char* driverName() const override {
        return "DY-SV5W";
    }

    // DY-SV5W: supports query, device type, track count, current track; not safe to poll during playback
    uint8_t capabilities() const override {
        return AUDIO_CAP_STATUS_QUERY | AUDIO_CAP_DEVICE_TYPE | AUDIO_CAP_TRACK_COUNT | AUDIO_CAP_CURRENT_TRACK;
    }

    // Query live module state via UART RX: device, play state, current track.
    // Sends three query frames and waits up to 300 ms each for a response.
    // Returns true if at least one query received a valid response (link alive).
    // Only call from AudioTask (Core 0).
    bool queryModuleState(AudioModuleState& out) override;

    // Returns last-known cached state from begin()-time queries with no UART traffic.
    // Safe to call at any time including during playback.
    void getCachedState(AudioModuleState& out) const override;

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
