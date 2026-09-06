// =============================================================================
// include/audio_dy_sv5w.h
//
// Concrete AudioDriver for the DY-SV5W audio module.
//
// Transport is per Board Variant; the full contract is the Transport section of
// src/drivers/audio_dy_sv5w.cpp. In short:
//   - PA_CAP_DEDICATED_AUDIO_UART == 1: one hardware UART (UART_PORT_AUDIO) on
//     PIN_AUDIO_TX / PIN_AUDIO_RX, contended by nothing.
//   - PA_CAP_DEDICATED_AUDIO_UART == 0: interrupt-protected software UART TX on
//     PIN_AUDIO_TX (audio_soft_uart_tx.h) plus RX-only use of the dome link's
//     controller on PIN_AUDIO_RX. AudioTask claims it through audioUartClaim()
//     and reports a refusal as AUDIO_RX_BLOCKED_BY_DOME_UART; the last cached
//     AudioModuleState is what the operator sees meanwhile.
//
// The class itself is transport-agnostic: AudioModuleState, queryModuleState()
// and getCachedState() behave identically on both boards, and all hardware
// access goes through the injectable AudioSerialIO seam.
//
// For compatibility with DY-SV5W firmware variants, this driver sends the
// checksum-frame dialect used by DYPlayer/BetterDuino.
//
// Only compiled when PA_AUDIO_DRIVER == AUDIO_SOFT_UART (platformio.ini).
// =============================================================================
#pragma once

#include <stdint.h>

#include "audio_driver.h"
#include "audio_serial_io.h"

class AudioDriverDySv5w : public AudioDriver {
   public:
    // Inject a custom I/O seam (call before begin() to override production IO).
    // If not called, begin() initialises production hardware adapters.
    void setIO(const AudioSerialIO& io) { m_io = io; }

    bool begin(uint8_t vol) override;
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
    AudioSerialIO m_io{};

    // Last known total tracks and device  --  populated during begin() and carried
    // forward when queryModuleState() is called (total-tracks query runs in begin
    // only, not on every periodic poll to keep poll overhead low).
    uint16_t m_totalTracks = 0;
    uint8_t m_device = 0xFF;  // 0xFF = unknown until first successful query

    // Send one DY-SV5W checksum frame: [payload...][SM] via m_io.writeByte.
    // SM = low 8 bits of sum of all payload bytes. Posts 100 ms delay after frame.
    void sendCommand(const uint8_t* payload, uint8_t len);

    // Send a 4-byte query frame then poll for a response via m_io.
    // Returns number of bytes read (up to maxLen).
    uint8_t sendQuery(const uint8_t* query, uint8_t* buf, uint8_t maxLen,
                      uint8_t expectedLen, uint32_t timeoutMs);
};
