// =============================================================================
// include/audio_chirp.h
//
// Concrete AudioDriver implementation for the CHIRP Audio Trigger board.
//
// CHIRP is an RP2350-based multi-stream audio board with an ASCII UART command
// protocol. TX commands are sent over software UART on PIN_AUDIO_TX (GPIO 26) at
// 9600 baud. RX status/manifest responses are read via HardwareSerial(2) on
// PIN_AUDIO_RX (GPIO 35).
//
// ⚠ CHIRP defaults to 115200 baud. Before using this driver, set the board's
// baud rate to 9600 by placing the following in CHIRP.INI on the SD card root:
//   #BAUD_RATE 9600
//
// Reference: https://github.com/joymonkey/CHIRP
// See docs/sound_playback.md §2.2 for full protocol and file layout details.
//
// Only compiled when PA_AUDIO_DRIVER == AUDIO_CHIRP (platformio.ini).
// =============================================================================
#pragma once

#include <stdint.h>

#include "audio_driver.h"
#include "audio_serial_io.h"

// CHIRP native volume range (0 = silent, 99 = maximum)
static constexpr uint8_t CHIRP_VOL_MAX = 99;

class AudioDriverChirp : public AudioDriver {
   public:
    // Inject a custom I/O seam (call before begin() to override production IO).
    void setIO(const AudioSerialIO& io) { m_io = io; }

    // Configures soft-UART TX and sends initial volume. Reads GMAN bank
    // descriptors only when shared UART2 RX is available to audio.
    bool begin(uint8_t vol) override;

    // Play track by 1-based index in Bank 1, Page A.
    // Track 0 is silently ignored.
    void playTrack(uint16_t track) override;
    void playTrackBanked(uint16_t index, uint8_t bank, char page) override;

    // Stop all active streams.
    void stop() override;

    // Set volume 0–30 (clamped by AudioTask). Scaled to CHIRP 0–99 range.
    void setVolume(uint8_t vol) override;
    const char* driverName() const override {
        return "CHIRP";
    }

    uint8_t capabilities() const override {
        return AUDIO_CAP_STATUS_QUERY | AUDIO_CAP_DEVICE_TYPE | AUDIO_CAP_TRACK_COUNT |
               AUDIO_CAP_CURRENT_TRACK | AUDIO_CAP_QUERY_SAFE_PLAYING | AUDIO_CAP_CATALOG;  // 0x3F
    }

    bool queryModuleState(AudioModuleState& out) override;
    void getCachedState(AudioModuleState& out) const override;

    // Catalog interface implementations (overrides).
    bool refreshCatalog() override;
    uint16_t getCatalogEntryCount() const override;
    const AudioCatalogEntry* getCatalogEntries() const override;
    uint8_t getCatalogBankCount() const override;
    const AudioCatalogBank* getCatalogBanks() const override;
    bool isCatalogReady() const override;

   private:
    AudioSerialIO m_io{};

    uint16_t m_totalTracks = 0;
    uint8_t m_playState = 0xFF;
    bool m_linkOk = false;
    uint16_t m_lastTrack = 0;   // last track index sent to playTrack(); reported as currentTrack
    bool m_catalogReady = false;
    uint16_t m_catalogCount = 0;
    uint8_t m_catalogBankCount = 0;
    AudioCatalogEntry m_catalog[AUDIO_CATALOG_MAX_ENTRIES] = {};
    AudioCatalogBank m_catalogBanks[AUDIO_CATALOG_MAX_BANKS] = {};

    bool loadManifestBanks(uint32_t timeoutMs, bool keepTotalTracks);

    // Send a null-terminated ASCII command string followed by '\n' via m_io.
    void sendCommand(const char* cmd);

    // Read one \r\n-terminated ASCII line via m_io.
    uint8_t readLine(char* buf, uint8_t maxLen, uint32_t timeoutMs);
};
