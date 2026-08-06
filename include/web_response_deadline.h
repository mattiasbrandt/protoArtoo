// =============================================================================
// include/web_response_deadline.h
//
// Response-phase deadline (ADR 0020, built on the seam ADR 0024 records).
//
// A request that has begun writing its response and does not finish within a
// bounded time gets its socket dropped, rather than holding the one task that
// serves every connection for as long as the client cares to stall it.
//
// The decision is pure: no Arduino, no vendor type, no clock of its own. The
// clock arrives as a parameter and the deadline arrives as a parameter, so a
// host test pins the behaviour without pinning the calibration -- the
// calibrated value lives in platformio.ini [flags_base] under its rationale,
// beside the SSE send deadline it is the request-side counterpart to.
//
// Why one record and not a table: esp_http_server services every connection
// from a single task and the library's optional per-request worker threads are
// off (docs/adr/0022-connection-admission-on-esp-http-server.md), so at most one
// request is in its response phase at any instant. Enabling those workers would
// make responses overlap and would require this to become a per-socket table --
// which is why the socket the deadline is armed for is checked on every call
// rather than assumed.
// =============================================================================
#pragma once

#include <stdint.h>

enum class WebResponseDeadlineVerdict : unsigned char {
    // Carry on writing.
    kProceed,
    // The response phase has outlived its deadline. The caller must stop
    // writing, count the closure, and drop the connection.
    kBreach,
};

// State for the one response phase that can be in flight. Owned by the backend
// that serves the sockets; every field is written from the server task alone.
struct WebResponseDeadline {
    // Socket of the request currently admitted, or -1 when idle. Every check
    // names its own socket and is ignored unless the two match, so a send on
    // any other connection -- an event-stream broadcast from the event task,
    // most importantly -- passes through untouched.
    int fd;
    // When the response phase began, which is the first byte sent and not the
    // moment the request was admitted. A handler that spends time building a
    // body, or an upload that spends time receiving one, has not entered its
    // response phase and must not be charged for it.
    uint32_t startMs;
    bool started;
    // Long-lived stream: a response that never ends by construction, so a
    // deadline on it would measure the design rather than a stall.
    bool exempt;
    // Latched for the rest of the phase. A single breach verdict is not enough:
    // the caller returns an error into a vendor send path, and nothing in this
    // module can promise that path will not attempt one more write. Latching
    // makes every later write in the same phase fail the same way.
    bool breached;
};

void webResponseDeadlineInit(WebResponseDeadline* deadline);

// Take ownership of the response phase for fd. Called once per admitted
// request, before the handler runs. A negative fd leaves the deadline idle.
void webResponseDeadlineArm(WebResponseDeadline* deadline, int fd);

// Exempt the armed phase, for a request that has just turned into a live event
// stream. Ignored when fd is not the armed socket.
void webResponseDeadlineExempt(WebResponseDeadline* deadline, int fd);

// Release the response phase and report how long it took, in milliseconds, or
// -1 when there is nothing to report.
//
// -1 covers three cases that must not enter the statistic: no request was
// armed, the handler never sent a byte, and the phase breached. A breached
// phase is excluded deliberately -- the maximum this feeds is the evidence for
// "the slowest legitimate response", and folding a deliberately stalled client
// into it would let the stall raise the very number the margin is measured
// against.
int32_t webResponseDeadlineDisarm(WebResponseDeadline* deadline, uint32_t nowMs);

// The whole decision, called immediately before every write attempt on fd.
//
// The first call of a phase starts the clock and always proceeds: a response
// cannot be late before it has begun. Wraparound of the millisecond clock is
// handled by unsigned subtraction, so a 49-day uptime neither breaches a fresh
// response nor grants a stalled one an extra 49 days.
WebResponseDeadlineVerdict webResponseDeadlineCheck(WebResponseDeadline* deadline, int fd,
                                                    uint32_t nowMs, uint32_t deadlineMs);

// -----------------------------------------------------------------------------
// Counters
// -----------------------------------------------------------------------------
//
// Published through /api/status. Project-owned, like the admission counters
// they sit beside; see buildStatusJson() in src/web/web_server.cpp for the
// field names.

// Responses dropped by the deadline, and when the last one went. Rare by
// design, which is exactly why it needs a counter: a run that never triggers it
// is otherwise indistinguishable from one where it silently stopped working.
extern volatile uint32_t g_webResponseDeadlineClosures;
extern volatile uint32_t g_webResponseDeadlineLastMs;

// Duration of the most recent completed response phase, and the longest one
// seen this boot. The maximum is the margin evidence: it is what the calibrated
// deadline has to clear, and publishing it continuously means the margin can be
// re-checked on any run rather than resting on one calibration session.
extern volatile uint32_t g_webResponseLastMs;
extern volatile uint32_t g_webResponseMaxMs;
