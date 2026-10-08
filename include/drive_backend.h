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
//   - the 50 Hz zero-frame continuity guarantee         (the unconditional
//                                                        driveBackendSend() in
//                                                        src/tasks/drive.cpp)
//
// Those four are AGENTS.md safety invariants. A seam that let any of them
// become a backend's choice would be the wrong seam, so what a backend gets is
// a value and a wire, and what it declares about itself is data.
//
// WHY THE SEAM EXISTS AT ALL. Every drive controller has some version of
// "keep talking to me", and they are not the same rule twice. A hoverboard's
// timeout is always on, and starving it keeps the wheels turning on the last
// command before the board acts: EFeru firmware holds it 800 ms and then
// coasts; RoboDurden firmware holds it 500 ms and then soft-brakes and
// releases the bridge. A packet-serial controller's (Sabertooth setTimeout(),
// 100 ms granularity, 100 ms minimum) is opt-in: only a 2x25 V2 or newer on
// which command 14 was sent since its last power-up stops when starved. A V1,
// or one that was never armed or has browned out since, holds its last
// command for as long as it is starved. Same class of rule, different windows
// and different ends, so it can never be one shared constant - and the
// generic emitter cannot pick a cadence without asking the backend. None of
// these far ends can be trusted to stop the wheels in time on its own, which
// is why zero-frame continuity sits above the seam and is unconditional.
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "component_registry.h"  // componentPartCapabilities() -- the row a profile reads
#include "config.h"
#include "drive_capabilities.h"   // DRIVE_CAP_* -- the Foot Drive family's vocabulary

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
    uint16_t continuityDeadlineMs;  // longest gap allowed between frames to the far end;
                                    // can be shorter than the far end's own timeout
    DriveStarvation starvation;     // what that far end does once starved
    bool reportsFeedback;           // false -> pollFeedback() never reports
};

// Whether the Foot Drive with this Component Registry id reports readings
// back. Read from the row's own capability word rather than written into the
// profile, so the firmware's answer and the one GET /api/identity/components
// gives the browser are the same declaration and cannot drift apart (ADR 0042).
constexpr bool driveBackendReportsFeedback(const char* registryId) {
    return (componentPartCapabilities(registryId) & DRIVE_CAP_REPORTS_FEEDBACK) != 0;
}

#if PA_CAP_DRIVE_BACKEND_HOVERBOARD
// The hoverboard's Component Registry id, named once so the profile's id and
// the row its feedback bit is read from cannot be two different spellings.
inline constexpr char kHoverboardRegistryId[] = "hoverboard";
static_assert(componentPartExists(kHoverboardRegistryId),
              "the drive backend cites a product id no Component Registry row declares; a typo"
              " here would otherwise read as a Foot Drive that reports nothing");

// Hoverboard mainboard on EFeru FOC or RoboDurden Gen2.x (REMOTE_ROS2)
// firmware, 8-byte command frames over UART. The 20 ms deadline is the
// droid's, not the board's: starved, the board holds its last command for
// 800 ms (EFeru, then a coast) or 500 ms (RoboDurden, then a soft brake), so a
// frame every tick is what lets a zero command, an estop included, reach the
// wheels in one tick instead of after that window. Drifts is the honest class
// for both firmwares: the held window is motion nobody commanded. That is why
// zero-frame continuity is unconditional above this line.
inline constexpr DriveBackendProfile kDriveBackend = {
    .id = kHoverboardRegistryId,
    .protocol = "hoverboard_gen2x",
    .baud = 115200,
    .continuityDeadlineMs = 20,
    .starvation = DriveStarvation::Drifts,
    .reportsFeedback = driveBackendReportsFeedback(kHoverboardRegistryId),
};
#else
  #error "no Board Capability Gate selects a drive backend: add a row and select it here"
#endif

// The generic tick is fixed and the backend declares what it needs; this is
// the guard that stops the two drifting apart. A backend that declares a
// deadline shorter than the emitter's tick is a build error, not a droid that
// misses its own deadline on the bench.
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

// Whether this backend's feedback frames carry a motor current. A frame that
// does not still fills DriveFeedback.currentL/R - with 0 - so a reader that
// needs to tell "no current drawn" from "no current reported" asks here.
enum class DriveCurrentReport : uint8_t {
    Unknown = 0,  // no feedback frame decoded since begin
    Reported,
    NotReported,
};

// driveBackendCurrentReport()
// thread-safe: any task. It reads decode state DriveTask owns without a lock,
// and may: the answer is two flags, each a single byte the decoder sets on the
// first good frame and then leaves. The worst a reader can see is that one
// frame's answer half-written, for one read, and a current that is not
// reported reads 0 either way. Nothing is added to DriveTask's loop for it.
DriveCurrentReport driveBackendCurrentReport();

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
