// =============================================================================
// src/drivers/drive_backend_hoverboard.cpp
//
// The hoverboard Foot Drive backend: the seam in include/drive_backend.h wired
// to the Gen2.x frame builder and feedback parser in hoverboard_uart.h.
//
// This translation unit is the only place in the drive path that knows what a
// hoverboard is. DriveTask above it decides what to send and when; the four
// safety invariants are settled before it calls in here.
// =============================================================================

#include "drive_backend.h"

#include "hoverboard_uart.h"

#ifdef ARDUINO_ARCH_ESP32
#include <HardwareSerial.h>
#endif

#if !PA_CAP_DRIVE_BACKEND_HOVERBOARD
  #error "this backend is compiled where PA_CAP_DRIVE_BACKEND_HOVERBOARD is 0"
#endif

namespace {

// Feedback decode state. Owned by whichever task calls driveBackendBegin() and
// driveBackendPollFeedback() -- DriveTask, and only DriveTask, the same
// single-owner rule hoverboard_uart.h states for the parser itself. It lives
// here rather than in the caller so a second backend can keep a different
// shape of state without DriveTask learning about it.
HoverboardFeedbackParser g_feedbackParser;

// Static zero-initialisation is NOT the parser's initialised state -- it leaves
// seekingStart false, which is mid-frame. Only driveBackendBegin() puts the
// parser in a state that can decode, so poll refuses until it has run rather
// than parsing whatever the zeroes happen to mean.
bool g_begun = false;

}  // namespace

// buildHoverboardFrame() takes (steer, speed); the seam takes (speed, steer).
// That transposition is the whole reason encoding is its own step -- swapping
// them turns a throttle command into a spin and no build would notice.
size_t driveBackendEncode(uint8_t* buf, size_t bufSize, int16_t speed, int16_t steer) {
    constexpr size_t kFrameBytes = 8;  // Gen2.x: start, steer, speed, checksum
    static_assert(kFrameBytes <= DRIVE_BACKEND_FRAME_MAX_BYTES,
        "the hoverboard frame does not fit the catalogue's frame ceiling");
    if (buf == nullptr || bufSize < kFrameBytes) {
        return 0;
    }
    buildHoverboardFrame(buf, steer, speed);
    return kFrameBytes;
}

#ifdef ARDUINO_ARCH_ESP32
void driveBackendBegin(HardwareSerial& uart) {
    uart.begin(kDriveBackend.baud, SERIAL_8N1, PIN_DRIVE_RX, PIN_DRIVE_TX);
    // After begin(), never before: a mid-stream accumulator left over from a
    // prior UART session would otherwise corrupt the first new frame.
    initHoverboardFeedbackParser(&g_feedbackParser);
    g_begun = true;
}

void driveBackendSend(HardwareSerial& uart, int16_t speed, int16_t steer) {
    uint8_t frame[DRIVE_BACKEND_FRAME_MAX_BYTES];
    const size_t len = driveBackendEncode(frame, sizeof(frame), speed, steer);
    // Unreachable by construction: encode refuses only a buffer shorter than
    // one frame, and this one is the catalogue's frame ceiling, which the
    // static_assert above ties to the backend's own frame length. Kept as a
    // hard guard rather than a log or a partial write, because a truncated
    // frame would make the far end resync mid-command while the tick that
    // follows is 20 ms away.
    if (len == 0) {
        return;
    }
    uart.write(frame, len);
}

bool driveBackendPollFeedback(HardwareSerial& uart, DriveFeedback* out) {
    if (out == nullptr || !g_begun) {
        return false;
    }

    HoverboardFeedback raw = {};
    if (!readHoverboardFeedback(uart, &g_feedbackParser, &raw)) {
        return false;
    }

    out->batteryRaw = raw.batteryRaw;
    out->boardTempRaw = raw.boardTempRaw;
    out->speedR = raw.speedR;
    out->speedL = raw.speedL;
    out->currentL = raw.currentL;
    out->currentR = raw.currentR;
    return true;
}
#endif  // ARDUINO_ARCH_ESP32
