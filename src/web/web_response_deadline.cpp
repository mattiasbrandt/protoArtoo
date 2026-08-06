// =============================================================================
// src/web/web_response_deadline.cpp
//
// Decision core for the response-phase deadline, plus the counters it
// publishes. No Arduino, no vendor type, no clock of its own: every input
// arrives as a parameter, so this whole file compiles and is exercised on the
// host. The device hookup -- the send override the deadline is enforced from --
// lives with the backend that owns the sockets (src/web/web_request_psychic.cpp).
//
// See include/web_response_deadline.h for why this is one record rather than a
// per-socket table, and docs/adr/0024-response-phase-deadline-send-override.md
// for why the send override is the only seam on this stack that can enforce it.
// =============================================================================

#include "../../include/web_response_deadline.h"

volatile uint32_t g_webResponseDeadlineClosures = 0;
volatile uint32_t g_webResponseDeadlineLastMs = 0;
volatile uint32_t g_webResponseLastMs = 0;
volatile uint32_t g_webResponseMaxMs = 0;

void webResponseDeadlineInit(WebResponseDeadline* deadline) {
    deadline->fd = -1;
    deadline->startMs = 0;
    deadline->started = false;
    deadline->exempt = false;
    deadline->breached = false;
}

void webResponseDeadlineArm(WebResponseDeadline* deadline, int fd) {
    // Cleared unconditionally, including the latch. A phase that breached is
    // over the moment the next request is admitted, and carrying its latch
    // forward would fail the first write of an innocent response on the same
    // socket -- which on a keep-alive connection is the very next request.
    webResponseDeadlineInit(deadline);
    if (fd >= 0) {
        deadline->fd = fd;
    }
}

void webResponseDeadlineExempt(WebResponseDeadline* deadline, int fd) {
    if (fd >= 0 && deadline->fd == fd) {
        deadline->exempt = true;
    }
}

int32_t webResponseDeadlineDisarm(WebResponseDeadline* deadline, uint32_t nowMs) {
    int32_t elapsed = -1;
    if (deadline->fd >= 0 && deadline->started && !deadline->breached) {
        elapsed = (int32_t)(uint32_t)(nowMs - deadline->startMs);
    }
    webResponseDeadlineInit(deadline);
    return elapsed;
}

bool webResponseDeadlineGuards(const WebResponseDeadline* deadline, int fd) {
    return fd >= 0 && deadline->fd == fd && !deadline->exempt;
}

WebResponseDeadlineVerdict webResponseDeadlineCheck(WebResponseDeadline* deadline, int fd,
                                                    uint32_t nowMs, uint32_t deadlineMs) {
    // Not the request under deadline. This is the common case for an
    // event-stream broadcast, which is written from another task against a
    // socket this record knows nothing about.
    if (fd < 0 || deadline->fd != fd || deadline->exempt) {
        return WebResponseDeadlineVerdict::kProceed;
    }

    if (deadline->breached) {
        return WebResponseDeadlineVerdict::kBreach;
    }

    // First write of the phase: the clock starts here rather than at admission,
    // so neither a slow body build nor a long upload receive is charged to the
    // response. It cannot already be late.
    if (!deadline->started) {
        deadline->started = true;
        deadline->startMs = nowMs;
        return WebResponseDeadlineVerdict::kProceed;
    }

    // A zero deadline disables the guard rather than breaching everything on
    // its second write. Making "off" spell itself as 0 keeps the build flag
    // able to turn the feature off without a second flag to mean it.
    if (deadlineMs == 0) {
        return WebResponseDeadlineVerdict::kProceed;
    }

    if ((uint32_t)(nowMs - deadline->startMs) >= deadlineMs) {
        deadline->breached = true;
        return WebResponseDeadlineVerdict::kBreach;
    }

    return WebResponseDeadlineVerdict::kProceed;
}
