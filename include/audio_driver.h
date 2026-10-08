// =============================================================================
// include/audio_driver.h
//
// Abstract AudioDriver interface for protoR2 body audio system.
//
// The body controller is the sole audio source for the droid. All audio
// commands  --  from RC, web API, or dome serial '$' RX  --  route through the
// AudioTask queue and are dispatched via this interface. No other task writes
// to the audio serial GPIO directly.
//
// Design:
//   - Sound is a Component Family: the image carries a driver for every
//     supported module and one of them is the Component Member, a runtime
//     setting staged at reboot (ADR 0042). AudioTask binds it once at startup.
//   - Volume range is normalised 0-30 at the interface level; concrete drivers
//     scale to their module's native range if different.
//   - Drivers expose capability bits so AudioTask can choose safe query strategy
//     per backend (for example polling only when stopped vs safe-during-play).
//     The bits themselves are declared once, on the module's Component Registry
//     row (include/component_registry.inc), and each driver returns its own
//     row's word rather than restating it.
//   - Any ACK/status RX path is driver-internal and optional.
//
// Adding a new driver:
//   1. Create include/audio_<name>.h and src/drivers/audio_<name>.cpp.
//   2. Subclass AudioDriver and implement required interface methods, returning
//      componentPartCapabilities("<registry id>") from capabilities().
//   3. Give the product a supported PA_COMPONENT_PART row in
//      include/component_registry.inc, or flip an existing roadmap row.
//   4. Add the instance to kSoundMemberDrivers in
//      src/tasks/audio_sound_member.cpp -- a static_assert there fails the
//      build until you do.
// =============================================================================
#pragma once

#include <stdint.h>

#include "audio_rx_status.h"

// -----------------------------------------------------------------------------
// Build-flag constants  --  match values used in platformio.ini build_flags.
// PA_AUDIO_DRIVER must be set to one of these at compile time. It no longer
// decides what the image can drive -- every image carries every supported
// module -- only which one a controller that has never been told starts with
// (src/component_registry.cpp).
// -----------------------------------------------------------------------------
#define AUDIO_SOFT_UART 1  // Software UART TX binary-frame driver (default)
#define AUDIO_DFPLAYER 2
#define AUDIO_MP3TRIGGER 3
#define AUDIO_CHIRP 4  // CHIRP Audio Trigger  --  ASCII UART commands

// Audio catalog constants  --  used by drivers and callers that support banked playback.
// These are moved to the base interface so AudioTask can work with catalogs without
// backend-specific downcasts.
static constexpr uint8_t AUDIO_CATALOG_MAX_BANKS = 64;
static constexpr uint16_t AUDIO_CATALOG_MAX_ENTRIES = 300;

// Catalog entry  --  one track descriptor in an audio catalog.
struct AudioCatalogEntry {
    uint8_t bank = 0;
    char page = 'A';
    uint16_t index = 0;
    char name[48] = {0};
};

// Where a flat track number plays on a module that numbers its sounds by bank,
// page and index: Bank 1, Page A, the CHIRP Audio Trigger's own vocal bank.
// AudioDriverChirp::playTrack() sends a flat track there, and a Background
// Track named by a flat number or an unbound Named Track goes to the same
// address (src/tasks/audio_task_step.cpp), so the two cannot disagree.
constexpr uint8_t AUDIO_FLAT_BANK = 1;
constexpr char AUDIO_FLAT_PAGE = 'A';

// Catalog bank descriptor  --  one bank/page combination in an audio catalog.
//
// dirName is empty when the module reported no directory for the bank. That is
// an observation ("the module did not name one"), never a placeholder to fill
// in: the CHIRP module sends "BANK:1,,<count>" for a card with no 1x_
// directory, and a Bank 1 row recovered from LIST carries a count and nothing
// else. Anything deriving a page or a path from dirName must check it first.
struct AudioCatalogBank {
    uint8_t bank = 0;
    char page = 'A';
    char dirName[32] = {0};
    uint16_t count = 0;
};

// Whether the fitted module reported a bank numbered `bank`, and the page of
// the first row it reported for it - the address a bank-and-sound request
// plays at. False for a module that reported no such bank, or no catalog.
inline bool audioCatalogBankPage(const AudioCatalogBank* banks, uint8_t count, uint8_t bank,
                                 char* pageOut) {
    if (banks == nullptr) {
        return false;
    }
    for (uint8_t i = 0; i < count; ++i) {
        if (banks[i].bank == bank) {
            if (pageOut != nullptr) {
                *pageOut = banks[i].page;
            }
            return true;
        }
    }
    return false;
}

// How many sounds the fitted module reported on one bank and page, or 0 where
// it reported no such page, or no catalog. Where next and previous sound wrap.
inline uint16_t audioCatalogPageCount(const AudioCatalogBank* banks, uint8_t count, uint8_t bank,
                                      char page) {
    if (banks == nullptr) {
        return 0;
    }
    for (uint8_t i = 0; i < count; ++i) {
        if (banks[i].bank == bank && banks[i].page == page) {
            return banks[i].count;
        }
    }
    return 0;
}

// -----------------------------------------------------------------------------
// AudioCatalogCompleteness  --  what the last catalog discovery could NOT see.
//
// A ready catalog is not the same thing as a whole one: the module's reply
// queue can drop bank rows, individual names can go unanswered, and the entry
// array has a fixed ceiling. Every one of those leaves a usable catalog that is
// missing something, and describing it as complete is the lie #397 removes.
// -----------------------------------------------------------------------------
struct AudioCatalogCompleteness {
    bool manifestComplete = false;   // every bank row the module announced arrived
    uint16_t missingNameCount = 0;   // entries left as index_N because no name came back
    bool entryCapReached = false;    // the walk stopped at the entry array's ceiling
};

// -----------------------------------------------------------------------------
// AudioCatalogRefreshOutcome  --  why the last refreshCatalog() ended.
//
// Kept beside refreshCatalog()'s bool rather than replacing it: every caller
// already reads "did the catalog get refreshed" correctly, and the reason is a
// second question only the one caller that has to report it needs to ask.
// -----------------------------------------------------------------------------
enum class AudioCatalogRefreshOutcome : uint8_t {
    Complete = 0,  // the walk ran to the end of the last bank
    Failed,        // no manifest came back, or entry storage could not be had
    Interrupted,   // a stop or sleep entry cut the walk short
};

// Asked periodically during a long catalog walk. Returning true stops the walk
// at the next bounded increment. Runs on the task that called refreshCatalog(),
// so it must not write the audio TX path and must not block.
using AudioCatalogInterruptFn = bool (*)(void* ctx);

// -----------------------------------------------------------------------------
// AudioModuleState  --  live state returned by queryModuleState().
// Populated from live UART RX queries; reflects what the module actually reports.
// -----------------------------------------------------------------------------
struct AudioModuleState {
    bool linkOk;            // true if the module responded to at least one query
    uint8_t playState;      // 0=stop  1=playing  2=paused  0xFF=unknown
    uint8_t device;         // 0=USB 1=SD/TF 2=FLASH 3=Flash+SD (CHIRP) 0xFF=unknown/none
    uint16_t totalTracks;   // 0 if unknown
    uint16_t currentTrack;  // 0 if unknown
    uint16_t missingTrack;  // last track the module said was not on the card; 0 if none
};

// -----------------------------------------------------------------------------
// AudioDriver  --  abstract interface
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
    // Plays a Background Track under vocals (ADR 0054): see the Streams block
    // below.
    static constexpr uint8_t AUDIO_CAP_MIXES = 0x40;

    // Initialise hardware (GPIO, serial pin) and set initial volume  --  called once
    // during AudioTask init. vol is the NVS-configured volume (0-30).
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

    // Stop current playback immediately, on every stream: the droid-wide stop
    // (Quiet, Sleep Mode entry, Sound switched off). A Background Track stops
    // with it.
    virtual void stop() = 0;

    // Set output volume in the range 0-30 (0 = silent, 30 = maximum), on every
    // stream: the operator's volume. A Background Track's own level is
    // overwritten by it. AudioTask clamps the value before calling; driver may
    // assume it is in range.
    virtual void setVolume(uint8_t vol) = 0;

    // -------------------------------------------------------------------------
    // Streams (ADR 0054).
    //
    // A module that mixes plays several sounds at once, each on a numbered
    // stream; a module that cannot has one stream, stream 0, and the defaults
    // below are that one-stream case. Whether a module mixes is its registry
    // row's AUDIO_CAP_MIXES bit (include/component_registry.inc), never inferred
    // from which of these a driver overrides.
    //
    // stop() and setVolume() above are the droid-wide forms; stopStream() and
    // setStreamVolume() are the single-stream forms.
    // -------------------------------------------------------------------------

    // Stop one stream. On one stream, stream 0 is everything that plays.
    virtual void stopStream(uint8_t stream) {
        if (stream == 0) {
            stop();
        }
    }

    // Set one stream's volume, 0-30 like setVolume(). On one stream, stream 0's
    // volume is the module's volume.
    virtual void setStreamVolume(uint8_t stream, uint8_t vol) {
        if (stream == 0) {
            setVolume(vol);
        }
    }

    // Track Stop (ADR 0010): stop what the droid is saying, never a Background
    // Track playing under it. On one stream nothing plays under a vocal, so a
    // Track Stop is stop().
    virtual void stopVocals() {
        stop();
    }

    // Start a Background Track: the bank/page/index track at its own volume
    // (0-30), on a stream vocals fired afterwards will not land on. Returns
    // false when the Background Track is not playing as far as the driver knows
    // -- always, on a module that cannot mix, which is this default. AudioTask
    // refuses a Background Track on a module without AUDIO_CAP_MIXES before it
    // gets here; the default is the honest answer for a driver reached some
    // other way.
    virtual bool playBackgroundTrack(uint16_t index, uint8_t bank, char page, uint8_t vol) {
        (void)index;
        (void)bank;
        (void)page;
        (void)vol;
        return false;
    }

    // Stop the Background Track, and nothing else. No Background Track, nothing
    // sent.
    virtual void stopBackgroundTrack() {}

    // Whether a Background Track is held as playing: started, and neither
    // stopped nor reported idle since. Never, on a module that cannot mix.
    virtual bool backgroundTrackHeld() const { return false; }

    // Whether any stream may still be playing a vocal as far as the driver
    // knows: one it sent a PLAY to and has neither stopped nor seen idle since.
    // Asked when a Background Track stops, to say whether the droid's sound is
    // still playing. The one-stream default answers false: a module that cannot
    // mix never holds a Background Track to stop.
    virtual bool vocalHeld() const { return false; }

    virtual ~AudioDriver() = default;

    // Returns the operator-visible name of this driver's module (e.g. "DY-SV5W",
    // "CHIRP Audio Trigger"). Used by the status API to expose the active
    // backend to the web UI, so it is product copy: a name that also belongs to
    // another product is qualified here rather than on the page.
    virtual const char* driverName() const = 0;

    // Capability bits describing which query fields this backend can provide and
    // whether status polling is safe during playback. Default: no query support.
    virtual uint8_t capabilities() const {
        return 0;
    }

    // Classifies the RX availability state from a begin() or queryModuleState() outcome.
    // Default: AVAILABLE if linkOk, NO_RESPONSE otherwise.
    // Override in drivers whose RX path shares UART2 with DomeLink to return
    // BLOCKED_BY_DOME_UART when DomeLink owns the bus  --  forgetting to override
    // in such a driver causes false "No module response" errors in the UI.
    virtual AudioRxStatus classifyRxStatus(bool linkOk) const {
        return linkOk ? AUDIO_RX_AVAILABLE : AUDIO_RX_NO_RESPONSE;
    }

    // Query the module for live state. Returns true and populates 'out' if the
    // module responds. Default returns false (driver has no RX path).
    // Must only be called from the AudioTask (Core 0). Blocking for as long as
    // the driver's own query budget: the CHIRP driver asks three streams in
    // turn and can spend ~600 ms doing it.
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
        out.missingTrack = 0;
    }

    // Drain unsolicited RX (finish/cancel/missing-track bytes) without a query.
    // Default is a no-op. Must not take the dome UART. AudioTask calls this
    // every loop so play-state can follow a finish byte (#396).
    virtual void serviceRx() {}

    // Catalog support (AUDIO_CAP_CATALOG backends only).
    // Drivers without catalog support return false/0/nullptr; these defaults apply to all.

    // Refresh the catalog from the hardware module (blocking, Core 0 only).
    // Returns true only when the walk ran to the end; false on timeout, error
    // or interruption. Why it ended is lastCatalogRefreshOutcome().
    // Default returns false (no catalog support).
    virtual bool refreshCatalog() {
        return false;
    }

    // Why the last refreshCatalog() ended. Read straight after the call.
    virtual AudioCatalogRefreshOutcome lastCatalogRefreshOutcome() const {
        return AudioCatalogRefreshOutcome::Failed;
    }

    // Install the predicate a long catalog walk asks whether to stop. Set once,
    // by the task that owns refreshCatalog(); passing nullptr removes it. A
    // full walk is up to 300 names at 450 ms each, which is minutes of a task
    // that also has to answer Stop and enter sleep (#397 work item 13).
    void setCatalogInterrupt(AudioCatalogInterruptFn fn, void* ctx) {
        m_catalogInterrupt = fn;
        m_catalogInterruptCtx = ctx;
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

    // What the last discovery could not see. Default reports an incomplete
    // manifest, which is the honest answer for a backend that never ran one.
    virtual void getCatalogCompleteness(AudioCatalogCompleteness& out) const {
        out = AudioCatalogCompleteness{};
    }

    // The checksum the module reported for its own sound list at the last
    // manifest read, if it reported one. Writes *out and returns true only when
    // a value was actually observed: a module that sent no checksum, or a reply
    // the checksum line was dropped from, is "not observed", which is a
    // different fact from a checksum of zero. Default: no such value exists.
    virtual bool getSoundListChecksum(uint32_t* out) const {
        (void)out;
        return false;
    }

   protected:
    // True when whoever asked for the catalog wants the walk to stop now.
    // Cheap and side-effect free when no predicate is installed, which is every
    // build that never wires one.
    bool catalogInterruptRequested() const {
        return m_catalogInterrupt != nullptr && m_catalogInterrupt(m_catalogInterruptCtx);
    }

   private:
    AudioCatalogInterruptFn m_catalogInterrupt = nullptr;
    void* m_catalogInterruptCtx = nullptr;
};
