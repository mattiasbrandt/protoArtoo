// =============================================================================
// include/drive_backend.h
//
// The Foot Drive backend seam (#339, from #304).
//
// WHERE THE LINE IS. Everything a drive command passes through before it gets
// here is generic and no backend can reach it:
//
//   - the DriveTask speed cap and the failsafe zeroing  (driveArbiterResolve())
//   - the latching estop                                (include/failsafe_gate.h)
//   - the 50 Hz zero-frame continuity guarantee         (driveTickDecide())
//
// Those four are AGENTS.md safety invariants. A seam that let any of them
// become a backend's choice would be the wrong seam, so what a backend gets is
// a value and a wire, and what it declares about itself is data.
//
// WHY THE SEAM EXISTS AT ALL. Every drive controller has some version of
// "keep talking to me", and they are not the same rule twice. A hoverboard's
// is mandatory, 20 ms, and starving it makes the wheels DRIFT. A packet-serial
// controller's (Sabertooth setTimeout(), 100 ms granularity, 100 ms minimum)
// is opt-in and starving it makes the wheels STOP. Same class of rule,
// opposite failure mode, so it can never be one shared constant - and the
// generic emitter cannot pick a cadence without asking the backend.
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "config.h"

#ifdef ARDUINO_ARCH_ESP32
class HardwareSerial;
#endif

// What the far end does when it stops being fed.
enum class DriveStarvation : uint8_t {
    Stops = 0,   // safe: the controller cuts its own motors
    Drifts = 1,  // hazard: the controller holds the last command it was given
};

// One catalogue row per drive backend, carrying its own wire settings.
// Adding a foot drive is adding a row: baud, the protocol spoken over it and
// the continuity deadline live here, never in DriveTask.
struct DriveBackendProfile {
    const char* id;                 // Component Member identifier
    const char* protocol;           // Component Protocol spoken on the wire
    uint32_t baud;
    uint16_t continuityDeadlineMs;  // longest gap the far end tolerates
    DriveStarvation starvation;     // what that far end does once starved
    bool reportsFeedback;           // false -> pollFeedback() never reports
};

#if PA_CAP_DRIVE_BACKEND_HOVERBOARD
// RoboDurden Gen2.x hoverboard mainboard, 8-byte command frames over UART.
// The 20 ms deadline is the protocol's, not a preference: the mainboard holds
// its last command when the stream stops, which is why zero-frame continuity
// is unconditional above this line.
inline constexpr DriveBackendProfile kDriveBackend = {
    .id = "hoverboard",
    .protocol = "hoverboard_gen2x",
    .baud = 115200,
    .continuityDeadlineMs = 20,
    .starvation = DriveStarvation::Drifts,
    .reportsFeedback = true,
};
#else
  #error "no Board Capability Gate selects a drive backend: add a row and select it here"
#endif

// The generic tick is fixed and the backend declares what it needs; this is
// the guard that stops the two drifting apart. A backend whose far end wants
// to hear from us more often than the emitter speaks is a build error, not a
// droid that coasts on the bench.
static_assert(DRIVE_FRAME_PERIOD_MS <= kDriveBackend.continuityDeadlineMs,
    "the generic drive tick is slower than this backend's continuity deadline:"
    " raise DRIVE_FREQ_HZ or the backend cannot be fed in time");

// Longest wire frame any backend in the catalogue emits. Sized here so the
// caller's buffer is a fixed stack allocation on the Core 1 real-time path.
constexpr size_t DRIVE_BACKEND_FRAME_MAX_BYTES = 8;

// Drive telemetry a backend can report back. Not universal: a backend whose
// profile says reportsFeedback == false never fills one in.
struct DriveFeedback {
    int16_t batteryRaw;    // V x 100
    int16_t boardTempRaw;  // degC x 10
    int16_t speedR;        // right wheel, RPM
    int16_t speedL;        // left wheel, RPM
    int16_t currentL;      // left wheel current x 100 = A (0 where unreported)
    int16_t currentR;      // right wheel current x 100 = A (0 where unreported)
};

// -----------------------------------------------------------------------------
// driveBackendEncode()
// Pure step: encode one resolved command into the backend's wire frame.
// This is where the seam's (speed, steer) argument order meets whatever order
// the wire uses, and it is separated from the write for exactly that reason --
// a transposition here swaps throttle and steering on a real droid, and it is
// invisible in a build.
// Returns the frame length, or 0 if bufSize cannot hold a whole frame. Never
// writes a partial frame.
// thread-safe: yes (pure function)
// -----------------------------------------------------------------------------
size_t driveBackendEncode(uint8_t* buf, size_t bufSize, int16_t speed, int16_t steer);

#ifdef ARDUINO_ARCH_ESP32
// driveBackendBegin()
// Opens the drive lane at the backend's own baud and resets whatever decode
// state it keeps. Call once, before the first send. A backend that programs a
// hardware timeout of its own does it here.
void driveBackendBegin(HardwareSerial& uart);

// driveBackendSend()
// Emit one command frame. Called every tick by DriveTask, including the zero
// frames -- suppressing a frame is not a backend's decision to make.
// thread-safe: must only be called from the task that owns the serial port.
void driveBackendSend(HardwareSerial& uart, int16_t speed, int16_t steer);

// driveBackendPollFeedback()
// Non-blocking; drains whatever the backend has to say and reports true when a
// complete reading was decoded. Always false where the profile declares no
// feedback.
// thread-safe: must only be called from the task that owns the serial port.
bool driveBackendPollFeedback(HardwareSerial& uart, DriveFeedback* out);
#endif
