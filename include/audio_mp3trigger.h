// =============================================================================
// include/audio_mp3trigger.h
//
// Concrete AudioDriver for the SparkFun MP3 Trigger v2.x (DEV-13720).
//
// TX commands are sent over software UART on PIN_AUDIO_TX (GPIO 26) at 9600
// baud — the same pin and bit-bang rate used by AUDIO_SOFT_UART and AUDIO_CHIRP.
// RX query responses are read via HardwareSerial(2) on PIN_AUDIO_RX (GPIO 35),
// opened RX-only (TX pin = -1). UART2 is shared with dome link (S3) and SBUS2;
// queryModuleState() returns cached state when UART2 is contended (the dome link shares UART2).
//
// Wire protocol (source-verified: BetterDuino MDuinoSound.cpp, Padawan360,
// SparkFun MP3 Trigger v2.4 Hookup Guide):
//
//   'S'+'0'  — query firmware version string → "=MP3 Trigger v2.NN\r\n"
//   'S'+'1'  — query SD track count          → "=NNN\r\n"
//   't'+N    — play track N by filename prefix NNNxxxx.MP3 (N: uint8_t 1–255)
//   'v'+V    — set volume (VS1053 native: 0=loudest, 255=silent — inverted)
//   'O'      — toggle play/pause (not used; stop() plays silent track instead)
//
// Volume mapping (0–30 normalised → VS1053 inverted register):
//   nativeVol = (30 - vol) * MP3TRIGGER_VOL_MAX / 30
//   vol=0 → 255 (silent), vol=30 → 0 (maximum), vol=15 → 127.
//
// stop() plays track MP3TRIGGER_STOP_TRACK (254) — community standard blank
// track used by BetterDuino and SHADOW_MD. Operator SD root must include
// 254XXXX.MP3 (all R2 community packs include it).
//
// Baud rate: 9600 (community standard). Factory default is 38400; configure
// via a baud init file in the SD root. See docs/sound_playback.md §2.3.
//
// Only compiled when PA_AUDIO_DRIVER == AUDIO_MP3TRIGGER (platformio.ini).
// =============================================================================
#pragma once

#include <stdint.h>

#include "audio_driver.h"
#include "audio_serial_io.h"

// Stop workaround: play the community-standard silent blank track 254.
// Operator must have 254XXXX.MP3 in the SD root (all R2 packs include it).
static constexpr uint8_t MP3TRIGGER_STOP_TRACK = 254;

// VS1053 native volume range: 0 = maximum loudness, 255 = silent.
static constexpr uint8_t MP3TRIGGER_VOL_MAX = 255;

class AudioDriverMp3Trigger : public AudioDriver {
   public:
    // Inject a custom I/O seam (call before begin() to override production IO).
    void setIO(const AudioSerialIO& io) { m_io = io; }

    // Configures soft-UART TX and hardware UART RX; sends S0 version query to
    // verify the serial link, S1 track-count query to cache totalTracks, then
    // applies initial volume. Blocking — runs inside AudioTask on Core 0.
    bool begin(uint8_t vol) override;

    // Play a track by 1-based filename-prefix index (NNNxxxx.MP3).
    // Track 0 is silently ignored. Track > 255 is logged and dropped: VS1053
    // 't' command is a single uint8_t; casting 256 to uint8_t yields 0x00 and
    // would play the wrong track.
    void playTrack(uint16_t track) override;

    // Stop playback by playing the silent blank track MP3TRIGGER_STOP_TRACK.
    // More reliable than 'O' toggle because it works regardless of current
    // module play state.
    void stop() override;

    // Set volume. vol is 0–30 (clamped by AudioTask before this call).
    // Scaled to VS1053 inverted range: nativeVol = (30 - vol) * 255 / 30.
    void setVolume(uint8_t vol) override;

    const char* driverName() const override {
        return "MP3Trigger";
    }

    // Capabilities bitmask: status query, track count, current track (cached).
    // No device-type concept (0x02 not set).
    // Play-state query not available in this protocol (0x10 not set).
    uint8_t capabilities() const override {
        return AUDIO_CAP_STATUS_QUERY | AUDIO_CAP_TRACK_COUNT | AUDIO_CAP_CURRENT_TRACK;  // 0x0D
    }

    // Query module state via S0 (link check) and S1 (track count).
    // UART2 contention-aware — returns cached state when dome ctrl or
    // dual_sbus is active. playState and device are always 0xFF (not queryable
    // in this protocol). currentTrack is cached from the last playTrack() call.
    // Only call from AudioTask (Core 0).
    bool queryModuleState(AudioModuleState& out) override;

    // Returns last-known cached state with no UART traffic. Safe to call at
    // any time including during playback.
    void getCachedState(AudioModuleState& out) const override;

   private:
    AudioSerialIO m_io{};

    uint16_t m_totalTracks = 0;      // populated from S1 query in begin()
    uint16_t m_lastTrack   = 0;      // last track index sent to playTrack()
    bool     m_linkOk      = false;  // true if S0 response received in begin()

    // Read one \r\n-terminated ASCII response line via m_io.
    uint8_t readLine(char* buf, uint8_t maxLen, uint32_t timeoutMs);

    // Drain RX, send 2-byte query (b0, b1), read and return one response line.
    uint8_t sendQuery(uint8_t b0, uint8_t b1, char* buf, uint8_t maxLen,
                      uint32_t timeoutMs);
};
