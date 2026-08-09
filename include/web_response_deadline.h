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

// True when fd is the socket this deadline is currently guarding: armed, and
// not exempted as a stream.
//
// The caller needs this as a separate question from the verdict below, because
// the two answers lead to different code and not merely to a different result.
// A guarded write is issued non-blocking and retried under the deadline; an
// unguarded one -- a refusal answered before any request was armed, a stream,
// an idle server -- must keep the plain blocking write it always had. Deciding
// that from the verdict alone would put an unguarded write into a retry loop
// with no deadline to end it.
bool webResponseDeadlineGuards(const WebResponseDeadline* deadline, int fd);

// The whole decision, called immediately before every write attempt on fd.
//
// The first call of a phase starts the clock and always proceeds: a response
// cannot be late before it has begun. Wraparound of the millisecond clock is
// handled by unsigned subtraction, so a 49-day uptime neither breaches a fresh
// response nor grants a stalled one an extra 49 days.
WebResponseDeadlineVerdict webResponseDeadlineCheck(WebResponseDeadline* deadline, int fd,
                                                    uint32_t nowMs, uint32_t deadlineMs);

// -----------------------------------------------------------------------------
// Write outcomes
// -----------------------------------------------------------------------------

enum class WebSendOutcome : unsigned char {
    // Bytes were accepted. The count is the caller's to loop over.
    kWritten,
    // Nothing was accepted this instant, but the connection is alive and the
    // same write can succeed later: the peer's receive window is full, or the
    // stack could not find memory to queue the segment. Retry under the
    // deadline.
    kTransient,
    // The socket cannot carry this response at all.
    kFatal,
};

// Classifies one non-blocking send() return. Pure, so the table below is pinned
// by a host test rather than by reasoning about a device under memory pressure.
//
// The distinction this draws is the whole point. A non-blocking write on this
// stack fails for two reasons that look identical to the caller and are not:
// EAGAIN when the peer's window is full, and ENOMEM/ENOBUFS when lwIP cannot
// allocate a segment for the write. Both are transient and both clear on their
// own, but only the first was ever retried -- so a momentary shortage of
// contiguous heap, which a concurrent page load produces routinely, aborted the
// response mid-body. For a static file that meant a well-formed 200 with a
// truncated or empty body: the browser accepted it silently, and the script that
// never arrived simply never ran.
//
// A zero return is fatal rather than transient. send() returning 0 on a stream
// socket means it will not make progress, and retrying it is an unbounded spin
// against a socket that has nothing to say.
WebSendOutcome webSendClassify(int sendResult, int errnoValue);

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

// Write attempts that were retried rather than failed, split by what made them
// wait, plus the longest a single write spent waiting.
//
// Separated because they carry different news. A full receive window is the
// ordinary cost of serving a client slower than the link; a memory-starved
// write means the heap ran out of contiguous space mid-response, which is the
// condition that used to truncate the body silently. A run where the second
// counter climbs is a run whose heap headroom wants looking at, and that has to
// be visible without reproducing the failure it now prevents.
extern volatile uint32_t g_webSendRetriesWindow;
extern volatile uint32_t g_webSendRetriesMemory;
extern volatile uint32_t g_webSendRetryMaxMs;
