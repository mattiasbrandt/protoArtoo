// =============================================================================
// include/audio_driver.h
//
// Abstract AudioDriver interface for protoArtoo body audio system.
//
// The body controller is the sole audio source for the droid. All audio
// commands — from RC, web API, or dome serial '$' RX — route through the
// AudioTask queue and are dispatched via this interface. No other task writes
// to the audio serial GPIO directly.
//
// Design:
//   - One concrete driver is compiled in per build, selected by PA_AUDIO_DRIVER.
//   - Volume range is normalised 0–30 at the interface level; concrete drivers
//     scale to their module's native range if different.
//   - Drivers expose capability bits so AudioTask can choose safe query strategy
//     per backend (for example polling only when stopped vs safe-during-play).
//   - Any ACK/status RX path is driver-internal and optional.
//
// Adding a new driver:
//   1. Create include/audio_<name>.h and src/drivers/audio_<name>.cpp.
//   2. Subclass AudioDriver and implement required interface methods.
//   3. Add a new AUDIO_<NAME> constant below.
//   4. Instantiate in AudioTask behind #if PA_AUDIO_DRIVER == AUDIO_<NAME>.
// =============================================================================
#pragma once

#include <stdint.h>

// -----------------------------------------------------------------------------
// Build-flag constants — match values used in platformio.ini build_flags.
// PA_AUDIO_DRIVER must be set to one of these at compile time.
// -----------------------------------------------------------------------------
#define AUDIO_SOFT_UART 1  // Software UART TX binary-frame driver (default)
#define AUDIO_DFPLAYER 2
#define AUDIO_MP3TRIGGER 3
#define AUDIO_CHIRP 4  // CHIRP Audio Trigger — ASCII UART commands

// Audio catalog constants — used by drivers and callers that support banked playback.
// These are moved to the base interface so AudioTask can work with catalogs without
// backend-specific downcasts.
static constexpr uint8_t AUDIO_CATALOG_MAX_BANKS = 64;
static constexpr uint16_t AUDIO_CATALOG_MAX_ENTRIES = 300;

// Catalog entry — one track descriptor in an audio catalog.
struct AudioCatalogEntry {
    uint8_t bank = 0;
    char page = 'A';
    uint16_t index = 0;
    char name[48] = {0};
};

// Catalog bank descriptor — one bank/page combination in an audio catalog.
struct AudioCatalogBank {
    uint8_t bank = 0;
    char page = 'A';
    char dirName[32] = {0};
    uint16_t count = 0;
};

// -----------------------------------------------------------------------------
// AudioModuleState — live state returned by queryModuleState().
// Populated from live UART RX queries; reflects what the module actually reports.
// -----------------------------------------------------------------------------
struct AudioModuleState {
    bool linkOk;            // true if the module responded to at least one query
    uint8_t playState;      // 0=stop  1=playing  2=paused  0xFF=unknown
    uint8_t device;         // 0=USB   1=SD/TF    2=FLASH   0xFF=unknown/none
    uint16_t totalTracks;   // 0 if unknown
    uint16_t currentTrack;  // 0 if unknown
};

// -----------------------------------------------------------------------------
// AudioDriver — abstract interface
// -----------------------------------------------------------------------------
class AudioDriver {
   public:
    // Capability bitmask returned by capabilities().
    static constexpr uint8_t AUDIO_CAP_STATUS_QUERY = 0x01;
    static constexpr uint8_t AUDIO_CAP_DEVICE_TYPE = 0x02;
    static constexpr uint8_t AUDIO_CAP_TRACK_COUNT = 0x04;
    static constexpr uint8_t AUDIO_CAP_CURRENT_TRACK = 0x08;
    static constexpr uint8_t AUDIO_CAP_QUERY_SAFE_PLAYING = 0x10;
    static constexpr uint8_t AUDIO_CAP_CATALOG = 0x20;

    // Initialise hardware (GPIO, serial pin) and set initial volume — called once
    // during AudioTask init. vol is the NVS-configured volume (0–30).
    // Returns true when command-side initialisation completed; false only for a
    // transient failure that should be retried by AudioTask.
    virtual bool begin(uint8_t vol) = 0;

    // Play a specific track by 1-based index (maps directly to SD card file number).
    // Track 0 is invalid; driver should silently ignore it.
    virtual void playTrack(uint16_t track) = 0;

    // Play a specific bank/page/index tuple. Default maps to flat track playback
    // so non-banked backends do not need an override.
    virtual void playTrackBanked(uint16_t index, uint8_t bank, char page) {
        (void)bank;
        (void)page;
        playTrack(index);
    }

    // Stop current playback immediately.
    virtual void stop() = 0;

    // Set output volume in the range 0–30 (0 = silent, 30 = maximum).
    // AudioTask clamps the value before calling; driver may assume it is in range.
    virtual void setVolume(uint8_t vol) = 0;

    virtual ~AudioDriver() = default;

    // Returns a short human-readable name for this driver (e.g. "DY-SV5W", "CHIRP").
    // Used by the status API to expose the active backend to the web UI.
    virtual const char* driverName() const = 0;

    // Capability bits describing which query fields this backend can provide and
    // whether status polling is safe during playback. Default: no query support.
    virtual uint8_t capabilities() const {
        return 0;
    }

    // Query the module for live state. Returns true and populates 'out' if the
    // module responds. Default returns false (driver has no RX path).
    // Must only be called from the AudioTask (Core 0). Blocking up to ~300 ms.
    virtual bool queryModuleState(AudioModuleState& out) {
        (void)out;
        return false;
    }

    // Returns last-known cached state without issuing any UART traffic.
    // Safe to call at any time including during playback.
    // Default implementation returns all-unknown values.
    virtual void getCachedState(AudioModuleState& out) const {
        out = AudioModuleState{};
        out.playState = 0xFF;
        out.device = 0xFF;
    }

    // Catalog support (AUDIO_CAP_CATALOG backends only).
    // Drivers without catalog support return false/0/nullptr; these defaults apply to all.

    // Refresh the catalog from the hardware module (blocking, Core 0 only).
    // Returns true on success, false on timeout or error.
    // Default returns false (no catalog support).
    virtual bool refreshCatalog() {
        return false;
    }

    // Query whether the catalog is ready (has been loaded and populated).
    // Returns true if loaded, false otherwise or if catalog not supported.
    // Safe to call from any context.
    virtual bool isCatalogReady() const {
        return false;
    }

    // Get the number of catalog entries currently loaded.
    // Returns 0 if no catalog is loaded or if catalog not supported.
    virtual uint16_t getCatalogEntryCount() const {
        return 0;
    }

    // Get the catalog entry array (read-only).
    // Returns nullptr if no catalog is loaded or if catalog not supported.
    // Caller should iterate [0, getCatalogEntryCount()) if non-nullptr.
    virtual const AudioCatalogEntry* getCatalogEntries() const {
        return nullptr;
    }

    // Get the number of catalog banks currently loaded.
    // Returns 0 if no catalog is loaded or if catalog not supported.
    virtual uint8_t getCatalogBankCount() const {
        return 0;
    }

    // Get the catalog bank descriptor array (read-only).
    // Returns nullptr if no catalog is loaded or if catalog not supported.
    // Caller should iterate [0, getCatalogBankCount()) if non-nullptr.
    virtual const AudioCatalogBank* getCatalogBanks() const {
        return nullptr;
    }
};
