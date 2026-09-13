// =============================================================================
// include/audio_mp3trigger.h
//
// Concrete AudioDriver for the SparkFun MP3 Trigger v2.x (WIG-13720).
//
// TX commands are sent over software UART on PIN_AUDIO_TX at 9600 baud  --  the
// same pin and bit-bang rate used by AUDIO_SOFT_UART and AUDIO_CHIRP. RX query
// responses are read via UART_PORT_AUDIO on PIN_AUDIO_RX, opened RX-only
// (TX pin = -1).
//
// Written for the artoo-esp32 posture, where UART_PORT_AUDIO is shared with the
// dome link and there is no spare TX. No P4 environment selects this backend.
// AudioTask, not this driver, holds the claim (audioUartClaim()).
//
// Wire protocol (source-verified: BetterDuino MDuinoSound.cpp, Padawan360,
// SparkFun MP3 Trigger v2.4 Hookup Guide):
//
//   'S'+'0'   --  query firmware version string -> "=MP3 Trigger v2.NN\r\n"
//   'S'+'1'   --  query SD track count          -> "=NNN\r\n"
//   't'+N     --  play track N by filename prefix NNNxxxx.MP3 (N: uint8_t 1-255)
//   'v'+V     --  set volume (VS1063 native: 0=loudest, ascending toward silence)
//   'O'       --  toggle play/pause (not used; stop() plays silent track instead)
//
// Volume mapping (0-30 normalised onto the vendor-audible 0-64 of the inverted
// register; #396). The register itself goes to 255; values much above 64 are
// inaudible (MP3 Trigger v2 User Guide #VOLM).
//   nativeVol = (30 - vol) * MP3TRIGGER_VOL_AUDIBLE / 30
//   vol=0 -> 64 (vendor floor), vol=30 -> 0 (maximum), vol=20 -> 21.
//
// stop() plays track MP3TRIGGER_STOP_TRACK (254)  --  community standard blank
// track used by BetterDuino and SHADOW_MD. Operator SD root must include
// 254XXXX.MP3 (all R2 community packs include it).
//
// Baud rate: 9600 (community standard). Factory default is 38400; configure
// via a baud init file in the SD root. See docs/sound_playback.md #2.3.
//
// Every image carries this driver: sound is a Component Family whose member is
// chosen at runtime and staged at reboot (ADR 0042), so PA_AUDIO_DRIVER now
// only names which module a controller that has never been told starts with.
// =============================================================================
#pragma once

#include <stdint.h>

#include "audio_driver.h"
#include "audio_serial_io.h"
#include "component_registry.h"

// Stop workaround: play the community-standard silent blank track 254.
// Operator must have 254XXXX.MP3 in the SD root (all R2 packs include it).
static constexpr uint8_t MP3TRIGGER_STOP_TRACK = 254;

// Vendor-audible ceiling of the inverted VS1063 register (0 = loudest).
// The register accepts 0-255; the guide's useful range is 0-64 (#396).
static constexpr uint8_t MP3TRIGGER_VOL_AUDIBLE = 64;

class AudioDriverMp3Trigger : public AudioDriver {
   public:
    // Inject a custom I/O seam (call before begin() to override production IO).
    void setIO(const AudioSerialIO& io) { m_io = io; }

    // Configures soft-UART TX and hardware UART RX; sends S0 version query to
    // verify the serial link, S1 track-count query to cache totalTracks, then
    // applies initial volume. Blocking  --  runs inside AudioTask on Core 0.
    bool begin(uint8_t vol) override;

    // Play a track by 1-based filename-prefix index (NNNxxxx.MP3).
    // Track 0 is silently ignored. Track > 255 is logged and dropped: VS1063
    // 't' command is a single uint8_t; casting 256 to uint8_t yields 0x00 and
    // would play the wrong track.
    void playTrack(uint16_t track) override;

    // Stop playback by playing the silent blank track MP3TRIGGER_STOP_TRACK.
    // More reliable than 'O' toggle because it works regardless of current
    // module play state.
    void stop() override;

    // Set volume. vol is 0-30 (clamped by AudioTask before this call).
    // Scaled onto the vendor-audible inverted range:
    // nativeVol = (30 - vol) * MP3TRIGGER_VOL_AUDIBLE / 30.
    void setVolume(uint8_t vol) override;

    const char* driverName() const override {
        return "MP3Trigger";
    }

    // Capabilities bitmask: status query, track count, current track (cached).
    // No device-type concept (0x02 not set).
    // Play-state query not available in this protocol (0x10 not set).
    // The bits are declared on this product's Component Registry row and read
    // from there rather than restated here, so the row and the driver cannot
    // drift apart (ADR 0042).
    uint8_t capabilities() const override {
        return componentPartCapabilities("mp3_trigger");
    }
    static_assert(componentPartExists("mp3_trigger"),
                  "AudioDriverMp3Trigger cites a product id no Component Registry row declares; a typo here would otherwise read as a module that can be asked nothing");

    // Query module state via S0 (link check) and S1 (track count).
    // Assumes the caller holds the audio UART claim; this driver does no
    // contention check of its own. playState and device are always 0xFF (not
    // queryable in this protocol). currentTrack is cached from the last
    // playTrack() call.
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
    // Skips leading unsolicited bytes until '=' (#396).
    uint8_t readLine(char* buf, uint8_t maxLen, uint32_t timeoutMs);

    // Drain RX, send 2-byte query (b0, b1), read and return one response line.
    uint8_t sendQuery(uint8_t b0, uint8_t b1, char* buf, uint8_t maxLen,
                      uint32_t timeoutMs);
};
