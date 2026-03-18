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
//   - Drivers are TX-primary; any ACK/status RX path is driver-internal and
//     optional.
//
// Adding a new driver:
//   1. Create include/audio_<name>.h and src/drivers/audio_<name>.cpp.
//   2. Subclass AudioDriver and implement all four pure virtual methods.
//   3. Add a new AUDIO_<NAME> constant below.
//   4. Instantiate in AudioTask behind #if PA_AUDIO_DRIVER == AUDIO_<NAME>.
// =============================================================================
#pragma once

#include <stdint.h>

// -----------------------------------------------------------------------------
// Build-flag constants — match values used in platformio.ini build_flags.
// PA_AUDIO_DRIVER must be set to one of these at compile time.
// -----------------------------------------------------------------------------
#define AUDIO_SOFT_UART  1  // Software UART TX binary-frame driver (default)
#define AUDIO_DFPLAYER   2
#define AUDIO_MP3TRIGGER 3
#define AUDIO_CHIRP      4  // CHIRP Audio Trigger — ASCII UART commands

// -----------------------------------------------------------------------------
// AudioDriver — abstract interface
// -----------------------------------------------------------------------------
class AudioDriver {
public:
    // Initialise hardware (GPIO, serial pin) — called once during AudioTask init.
    virtual void begin() = 0;

    // Play a specific track by 1-based index (maps directly to SD card file number).
    // Track 0 is invalid; driver should silently ignore it.
    virtual void playTrack(uint16_t track) = 0;

    // Stop current playback immediately.
    virtual void stop() = 0;

    // Set output volume in the range 0–30 (0 = silent, 30 = maximum).
    // AudioTask clamps the value before calling; driver may assume it is in range.
    virtual void setVolume(uint8_t vol) = 0;

    virtual ~AudioDriver() = default;

    // Returns a short human-readable name for this driver (e.g. "DY-SV5W", "CHIRP").
    // Used by the status API to expose the active backend to the web UI.
    virtual const char* driverName() const = 0;
};
