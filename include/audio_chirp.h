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

// What one read window produced. A parser must only ever see Complete: half a
// NAME line read as a whole one is a track called "genera", and half a
// "Sounds: 24" is a count of 2 (#397 work item 12).
enum class ChirpFrame : uint8_t {
    None,       // nothing finished inside the window; any partial is retained
    Complete,   // one whole line, terminator consumed, in the caller's buffer
    Oversized,  // a line longer than the buffer could hold; dropped through its terminator
};

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
    // The Sound page's Driver row is operator-facing, and bare "CHIRP" also
    // names CHIRP Droid Control, a different product by the same author
    // (CONTEXT.md Flagged Ambiguities, 2026-09-08: always qualify in operator
    // copy). Read from this product's Component Registry row rather than
    // restated here, exactly as capabilities() is. The other two sound drivers
    // keep their own short literals: "MP3Trigger" is matched by the Sound page
    // to decide which module-specific rows exist at all, so its spelling is
    // load-bearing in a way this one is not.
    const char* driverName() const override {
        return componentPartDisplayName("chirp");
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
    AudioCatalogRefreshOutcome lastCatalogRefreshOutcome() const override {
        return m_lastRefreshOutcome;
    }
    void getCatalogCompleteness(AudioCatalogCompleteness& out) const override;
    bool getSoundListChecksum(uint32_t* out) const override;
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
    // The catalog entry the module was last OBSERVED playing, 0 when nothing
    // identified it. Never the last index this driver sent: a commanded index
    // echoed back as current playback is the lie #397 exists to remove.
    uint16_t m_currentTrack = 0;
    bool m_catalogReady = false;
    // Whether the last GMAN reply accounted for every bank the module meant to
    // send. False after a truncated manifest, including one this driver
    // recovered Bank 1's count for: a recovered count is not proof that no
    // other BANK row was dropped, and a catalog that cannot be shown complete
    // must not be described as complete (#397 work item 1).
    bool m_manifestComplete = false;
    uint16_t m_catalogCount = 0;
    uint16_t m_catalogCapacity = 0;  // allocated m_catalog entry count (right-sized)
    uint8_t m_catalogBankCount = 0;
    // Entries the walk could not name, left as index_N. Counted rather than
    // only logged: a catalog full of index_N rows is usable and is not whole,
    // and the Sound page has to be able to say so (#397 work item 12).
    uint16_t m_missingNameCount = 0;
    // The walk stopped at m_catalogCapacity with banks still unwalked.
    bool m_entryCapReached = false;
    // GMAN's "MSUM:<n>" -- the module's CRC32 over its variant and file NAMES in
    // scan order (CHIRP_Audio.ino globalFilenameChecksum). It changes when a
    // file is added, removed or renamed, which is exactly what renumbers the
    // Banks 2-6 indexes a saved binding addresses. It does NOT cover
    // directories, pages or file contents, so a same-name move between pages
    // keeps it; that is a limit to state, not a reason to withhold the warning
    // the module does offer (#397 work item 4).
    AudioCatalogRefreshOutcome m_lastRefreshOutcome = AudioCatalogRefreshOutcome::Failed;
    uint32_t m_soundListChecksum = 0;
    // Whether the last manifest read carried a checksum at all. A checksum of
    // zero is a value; an absent one is not, and the two must not be confused.
    bool m_soundListChecksumValid = false;
    // Catalog storage is heap-allocated on first discovery and reused after.
    // When CHIRP RX is unavailable (e.g. the dome link owns the shared
    // controller) discovery
    // never runs, so these stay null and the ~16 KB they would hold statically
    // stays as free, contiguous heap  --  easing DRAM fragmentation.
    AudioCatalogEntry* m_catalog = nullptr;
    AudioCatalogBank* m_catalogBanks = nullptr;

    bool loadManifestBanks(uint32_t timeoutMs, bool keepTotalTracks);

    // Bank 1's sound count read back from the module's LIST dump, or 0 when the
    // dump did not carry it. Only called when GMAN arrived without its BANK:1
    // line, which is what a card with 13 or more Bank 2-6 directories does to
    // it; see loadManifestBanks().
    uint16_t queryBank1CountFromList();

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

    // Read one '\n'-terminated ASCII line via m_io ('\r' discarded). Bytes that
    // arrive without their terminator stay in the assembly buffer below and are
    // completed by a later call, so the caller's own operation deadline -- not
    // one read window -- is what bounds a partial line.
    ChirpFrame readFrame(char* buf, uint8_t maxLen, uint32_t timeoutMs);

    // Forget any partial line. Called wherever the driver drains RX to start a
    // fresh conversation: the bytes before a drain belong to the exchange that
    // is being abandoned, and completing a line across that boundary would
    // splice two replies together.
    void resetFrameAssembly();

    // True while bytes are being assembled into a line that has not ended yet.
    // A read window that expires mid-line is the module still talking, not the
    // module having stopped -- which is the difference between "the LIST dump
    // is over" and "this line is long".
    bool frameInProgress() const { return m_rxLineLen > 0 || m_rxDiscardToTerminator; }

    // The catalog index the module's reported playback path identifies, or 0
    // when it identifies no single entry. Path-aware: see the definition in
    // src/drivers/audio_chirp.cpp for the module-side rules it mirrors.
    uint16_t catalogIndexForPath(const char* path) const;

    // Line assembly for readFrame(). Wide enough for every frame the module
    // prints -- the longest is "STAT:playing," plus a 64-byte path plus ",99"
    // -- so anything that fills it is not a frame at all.
    static constexpr uint8_t CHIRP_RX_LINE_MAX = 128;
    char m_rxLine[CHIRP_RX_LINE_MAX] = {0};
    uint8_t m_rxLineLen = 0;
    // True while a line too long for m_rxLine is being dropped. The rest of it
    // is discarded up to and including its terminator so the NEXT frame starts
    // clean, rather than the tail being handed out as a short line of its own.
    bool m_rxDiscardToTerminator = false;
};
