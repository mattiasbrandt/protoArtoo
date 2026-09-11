// =============================================================================
// include/audio_chirp.h
//
// Concrete AudioDriver implementation for the CHIRP Audio Trigger board.
//
// CHIRP is an RP2350-based multi-stream audio board with an ASCII UART command
// protocol. TX commands are sent over software UART on PIN_AUDIO_TX at 9600
// baud. RX status/manifest responses are read via UART_PORT_AUDIO on
// PIN_AUDIO_RX.
//
// Written for the artoo-esp32 posture, where UART_PORT_AUDIO is shared with the
// dome link; see the file header of src/drivers/audio_chirp.cpp.
//
// NOTE: CHIRP defaults to 115200 baud. Before using this driver, set the board's
// baud rate to 9600 by placing the following in CHIRP.INI on the SD card root:
//   #BAUD_RATE 9600
//
// Reference: https://github.com/joymonkey/CHIRP
// See docs/sound_playback.md #2.2 for full protocol and file layout details.
//
// Every image carries this driver: sound is a Component Family whose member is
// chosen at runtime and staged at reboot (ADR 0042), so PA_AUDIO_DRIVER now
// only names which module a controller that has never been told starts with.
// =============================================================================
#pragma once

#include <new>
#include <stdint.h>

#include "audio_driver.h"
#include "audio_serial_io.h"
#include "component_registry.h"

// CHIRP native volume range (0 = silent, 99 = maximum)
static constexpr uint8_t CHIRP_VOL_MAX = 99;

class AudioDriverChirp : public AudioDriver {
   public:
    // Inject a custom I/O seam (call before begin() to override production IO).
    void setIO(const AudioSerialIO& io) { m_io = io; }

    // Configures soft-UART TX and sends initial volume. Reads GMAN bank
    // descriptors only when the shared controller's RX is available to audio.
    bool begin(uint8_t vol) override;

    // Play track by 1-based index in Bank 1, Page A.
    // Track 0 is silently ignored.
    void playTrack(uint16_t track) override;
    void playTrackBanked(uint16_t index, uint8_t bank, char page) override;

    // Stop all active streams.
    void stop() override;

    // Set volume 0-30 (clamped by AudioTask). Scaled to CHIRP 0-99 range.
    void setVolume(uint8_t vol) override;
    const char* driverName() const override {
        return "CHIRP";
    }

    // Read from this product's Component Registry row rather than restated
    // here, so the row and the driver cannot drift apart (ADR 0042).
    uint8_t capabilities() const override {
        return componentPartCapabilities("chirp");
    }
    static_assert(componentPartExists("chirp"),
                  "AudioDriverChirp cites a product id no Component Registry row declares; a typo here would otherwise read as a module that can be asked nothing");

    AudioRxStatus classifyRxStatus(bool linkOk) const override;

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
    uint16_t m_catalogCapacity = 0;  // allocated m_catalog entry count (right-sized)
    uint8_t m_catalogBankCount = 0;
    // Catalog storage is heap-allocated on first discovery and reused after.
    // When CHIRP RX is unavailable (e.g. the dome link owns the shared
    // controller) discovery
    // never runs, so these stay null and the ~16 KB they would hold statically
    // stays as free, contiguous heap  --  easing DRAM fragmentation.
    AudioCatalogEntry* m_catalog = nullptr;
    AudioCatalogBank* m_catalogBanks = nullptr;

    bool loadManifestBanks(uint32_t timeoutMs, bool keepTotalTracks);

    // Split, lazy catalog allocation (heap-exhaustion fix). The bank summary
    // array (~2.3 KB) is needed by the boot/link path; the per-track entry array
    // is needed only when the full catalog is filled (refreshCatalog, a UI action
    // with a live module). Keeping it out of the boot/link path is what stops the
    // heap collapsing during audio_play / discovery on a tight heap.
    // ensureEntryStorage right-sizes the entry array to `needed` (the module's
    // actual track count, capped at AUDIO_CATALOG_MAX_ENTRIES) and only grows.
    // Each returns false if its allocation fails.
    bool ensureBankStorage();
    bool ensureEntryStorage(uint16_t needed);

    // Send a null-terminated ASCII command string followed by '\n' via m_io.
    void sendCommand(const char* cmd);

    // Yield Core 0 during long catalog walks so WiFi/OTA/SSE and IDLE0 run.
    void cooperativeCatalogYield();

    // Read one \r\n-terminated ASCII line via m_io.
    uint8_t readLine(char* buf, uint8_t maxLen, uint32_t timeoutMs);
};
