// =============================================================================
// include/audio_chirp.h
//
// Concrete AudioDriver implementation for the CHIRP Audio Trigger board.
//
// The CHIRP Audio Trigger is an RP2350-based multi-stream audio board with an
// ASCII UART command protocol. This driver maps the AudioDriver interface to
// CHIRP ASCII commands sent over software UART TX on PIN_AUDIO_TX (GPIO 26)
// at 9600 baud.
//
// ⚠ CHIRP defaults to 115200 baud. Before using this driver, set the board's
// baud rate to 9600 by placing the following in CHIRP.INI on the SD card root:
//   #BAUD_RATE 9600
//
// Protocol: ASCII commands, '\n' terminated — fire-and-forget TX.
// Responses from CHIRP (PACK:*, S:*, etc.) are not read in this driver.
// GPIO 35 (PIN_AUDIO_RX) is available for a future RX extension if needed.
//
// AudioDriver interface → CHIRP command mapping:
//   begin()          → configure GPIO 26 output, idle HIGH
//   playTrack(n)     → "PLAY:n,1,A\n"  (Bank 1, Page A, index n, stream 0)
//   stop()           → "STOP\n"
//   setVolume(v)     → "VOL:N\n"  where N = v * CHIRP_VOL_MAX / 30 (scale 0–30 → 0–99)
//
// Reference: https://github.com/joymonkey/CHIRP
// See docs/sound_playback.md §2.2 for full protocol and file layout details.
//
// Only compiled when PA_AUDIO_DRIVER == AUDIO_CHIRP (platformio.ini).
// =============================================================================
#pragma once

#include <stdint.h>

#include "audio_driver.h"

// CHIRP native volume range (0 = silent, 99 = maximum)
static constexpr uint8_t CHIRP_VOL_MAX = 99;

class AudioDriverChirp : public AudioDriver {
public:
    // Initialise PIN_AUDIO_TX as output and set idle HIGH.
    void begin() override;

    // Play track by 1-based index in Bank 1, Page A.
    // Track 0 is silently ignored.
    void playTrack(uint16_t track) override;

    // Stop all active streams.
    void stop() override;

    // Set volume 0–30 (clamped by AudioTask). Scaled to CHIRP 0–99 range.
    void setVolume(uint8_t vol) override;

private:
    // Send a null-terminated ASCII command string followed by '\n'.
    void sendCommand(const char* cmd);
};
