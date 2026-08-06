// =============================================================================
// src/web/web_request_psychic.cpp
//
// PsychicHttp backend for the WebRequest seam (ADR 0021), plus the server
// bring-up. The only backend on the device since the #91 cutover; the host-test
// backend in src/native_test_stubs.cpp is the sole other definition of these
// methods.
//
// backend_ holds a WebRequestPsychicCtx (request + response + the esp_err_t
// the vendor callback must return). The session escape hatch reaches the
// underlying esp_http_server request: sess_ctx / free_ctx assignment and
// httpd_sess_trigger_close() -- the capabilities this migration exists to
// obtain (#53, ADR 0020).
// =============================================================================

#include <Arduino.h>
#include <LittleFS.h>
#include <PsychicHttp.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <http_parser.h>
#include <lwip/sockets.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "../../include/api_events.h"
#include "../../include/api_upload.h"
#include "../../include/logging.h"
#include "../../include/web_admission.h"
#include "../../include/web_busy_page.h"
#include "../../include/web_event_stream.h"
#include "../../include/web_request.h"
#include "../../include/web_server.h"
#include "../../include/web_server_psychic.h"

static const char* TAG = "WebServer";

namespace {

struct WebRequestPsychicCtx {
    PsychicRequest* req;
    PsychicResponse* resp;
    esp_err_t result;
};

WebRequestPsychicCtx* psychicCtx(void* backend) {
    return static_cast<WebRequestPsychicCtx*>(backend);
}

PsychicHttpServer s_server;

// =============================================================================
// Admission (include/web_admission.h)
//
// Two layers. The socket-open callback runs before any HTTP byte is parsed and
// is blind to the URL; the global middleware runs once the request head is
// read, before route matching and before a static file is opened, and is where
// the estop bypass lives. Both run on the single server task, which is why the
// heap sample is cached rather than taken per connection.
//
// All state here is touched only from that task, so no synchronisation is
// needed -- the same single-writer property the async stack's admission
// counters relied on.
// =============================================================================

WebAcceptRateLimiter s_acceptLimiter;
WebHeapSampleCache s_heapSample;

// Connection lifetime evidence. Lives here rather than beside the event stream
// registry because it is written from the same two admission callbacks, on the
// same single task, and therefore needs none of that registry's locking.
WebSocketCensus s_census;

// Copies the census into the globals /api/status publishes. Called from the
// same task that mutates it, so the read is consistent without a lock; the
// globals are volatile only because the status builder reads them from another.
void publishCensus() {
    g_webSocketsAccepted = s_census.accepted;
    g_webSocketsOpen = s_census.open;
    g_webSocketsOpenPeak = s_census.openPeak;
    g_webSocketsUntracked = s_census.untracked;
    g_webRequestsServed = s_census.requests;
}

// The PsychicHttp constructor installs its own open_fn; this holds it so the
// admit path can chain to it. Without that chain no PsychicClient is created,
// which breaks getClient() and the close path.
esp_err_t (*s_vendorOpenFn)(httpd_handle_t, int) = nullptr;

#ifndef PA_ACCEPT_BURST
#define PA_ACCEPT_BURST 6
#endif
#ifndef PA_ACCEPT_PER_SECOND
#define PA_ACCEPT_PER_SECOND 8
#endif
#ifndef PA_ACCEPT_MIN_LARGEST_FREE_BLOCK
#define PA_ACCEPT_MIN_LARGEST_FREE_BLOCK 8500
#endif
#ifndef PA_ACCEPT_HEAP_SAMPLE_MIN_INTERVAL_MS
#define PA_ACCEPT_HEAP_SAMPLE_MIN_INTERVAL_MS 100
#endif
#ifndef PA_ADMISSION_MIN_LARGEST_FREE_BLOCK
#define PA_ADMISSION_MIN_LARGEST_FREE_BLOCK 9000
#endif
#ifndef PA_ADMISSION_MIN_LARGEST_FREE_BLOCK_DIAG
#define PA_ADMISSION_MIN_LARGEST_FREE_BLOCK_DIAG 7500
#endif
#ifndef PA_ADMISSION_MAX_INFLIGHT_REQUESTS
#define PA_ADMISSION_MAX_INFLIGHT_REQUESTS 6
#endif

#if PA_HEAP_PROFILE
// Bounded request-lifecycle trace (issue #54 evidence, profiler-gated so it
// costs nothing in normal builds). Read after an experiment via /api/profiler,
// not polled during the workload. Covers only admitted requests that count
// against the inflight cap -- a long-lived stream's lifetime is already visible
// through the sseClients/sseClientsPeak counters, and it is not a per-request
// event this ring is meant to capture.
//
// handlerDoneMs marks when next() returned, i.e. when the matched handler's
// call into send() returned control to this middleware. esp_http_server writes
// the response synchronously from that call, so unlike the async stack this is
// "response written", not just "response ready".
//
// There is deliberately no disconnect timestamp. Connections are kept alive
// across requests (ADR 0023), so a request has no close of its own to record --
// the socket-level view lives in the httpSockets* counters instead.
//
// Single-writer: both the initial record and the handlerDoneMs update run on
// the single server task, which is also where /api/profiler reads it. A slot
// may be overwritten by a newer entry before a very long request's update
// reaches it -- acceptable for a bounded evidence trace, not a
// correctness-bearing structure.
//
// RequestLifecycleEntry and PA_REQUEST_TRACE_MAX are declared in web_server.h
// so api_profiler.cpp can size its copy buffer identically.
RequestLifecycleEntry s_requestTrace[PA_REQUEST_TRACE_MAX];
uint8_t s_requestTraceHead = 0;
uint8_t s_requestTraceCount = 0;

// Opens a new lifecycle-trace entry and returns its ring index, so the caller
// can fill in handlerDoneMs without a second lookup. Overwrites the oldest slot
// once full.
uint8_t pushRequestTraceEntry(const char* path, uint32_t startMs) {
    const uint8_t idx = s_requestTraceHead;
    RequestLifecycleEntry& e = s_requestTrace[idx];
    strncpy(e.requestPath, path, sizeof(e.requestPath) - 1);
    e.requestPath[sizeof(e.requestPath) - 1] = '\0';
    e.startMs = startMs;
    e.handlerDoneMs = 0;
    s_requestTraceHead = (uint8_t)((s_requestTraceHead + 1U) % PA_REQUEST_TRACE_MAX);
    if (s_requestTraceCount < PA_REQUEST_TRACE_MAX) {
        s_requestTraceCount++;
    }
    return idx;
}
#endif  // PA_HEAP_PROFILE

// Refreshes the cached largest-free-block reading at most once per interval.
// heap_caps_get_largest_free_block() walks the heap, so charging every
// connection for one would put that walk on the task servicing all the others
// -- the cost that failed 1 of 2 concurrent requests on the prototype.
size_t sampleLargestFreeBlock(void*) {
    const uint32_t nowMs = millis();
    if (webHeapSampleDue(&s_heapSample, nowMs, PA_ACCEPT_HEAP_SAMPLE_MIN_INTERVAL_MS)) {
        const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
        webHeapSampleStore(&s_heapSample, nowMs, largest);
        // The low-water mark the guard itself observed. /api/status reports
        // the resting value, which by definition is never the one that caused
        // a refusal, so without this the depth of a transient dip is invisible.
        if ((uint32_t)largest < g_webAcceptMinLargestBlockSeen) {
            g_webAcceptMinLargestBlockSeen = (uint32_t)largest;
        }
    }
    return s_heapSample.value;
}

// Connection Admission. Returning non-ESP_OK makes httpd_sess_new() delete the
// session and the accept loop close the socket, before any HTTP parsing --
// and, because the vendor open_fn is not chained on this path, before any
// PsychicClient is allocated. Rejection therefore costs no heap, which is the
// whole point at the moment there is none.
esp_err_t admissionOpenCallback(httpd_handle_t hd, int sockfd) {
    const int64_t startUs = esp_timer_get_time();
    const uint32_t nowMs = millis();

    const WebAcceptDecision decision =
        webAcceptDecide(&s_acceptLimiter, nowMs, PA_ACCEPT_BURST, PA_ACCEPT_PER_SECOND,
                        sampleLargestFreeBlock, nullptr, PA_ACCEPT_MIN_LARGEST_FREE_BLOCK);

    const uint32_t elapsedUs = (uint32_t)(esp_timer_get_time() - startUs);
    g_webAcceptGuardLastUs = elapsedUs;
    if (elapsedUs > g_webAcceptGuardMaxUs) {
        g_webAcceptGuardMaxUs = elapsedUs;
    }

    if (decision == WebAcceptDecision::kRejectRate) {
        g_webAcceptRejectRate = g_webAcceptRejectRate + 1u;
        g_webAcceptRejectLastMs = nowMs;
        return ESP_FAIL;
    }
    if (decision == WebAcceptDecision::kRejectHeap) {
        g_webAcceptRejectHeap = g_webAcceptRejectHeap + 1u;
        g_webAcceptRejectLastMs = nowMs;
        // The reading the decision actually used, which is the cached sample
        // and may be up to one interval old -- so a burst can be shed on one
        // pessimistic sample. Publishing it makes that visible rather than
        // leaving a bare refusal count to be argued over.
        g_webAcceptRejectLargestBlock = (uint32_t)s_heapSample.value;
        return ESP_FAIL;
    }

    // Admitted. Counted before the vendor chain so the census and the server's
    // own session table agree on what exists: httpd_sess_new() has already
    // created the session by the time open_fn runs, and a vendor callback that
    // failed would take it down through the same close_fn the census listens to.
    webSocketCensusOpen(&s_census, sockfd);
    publishCensus();

    return s_vendorOpenFn != nullptr ? s_vendorOpenFn(hd, sockfd) : ESP_OK;
}

// Releases an admitted request's in-flight slot however the handler leaves --
// including by exception, which the async stack proved can escape from deep
// inside response handling.
struct InflightSlot {
    bool held;

    explicit InflightSlot(bool takeSlot) : held(takeSlot) {
        if (!held) {
            return;
        }
        g_webInflightRequests = g_webInflightRequests + 1;
        if (g_webInflightRequests > g_webInflightRequestsPeak) {
            g_webInflightRequestsPeak = g_webInflightRequests;
        }
    }

    ~InflightSlot() {
        if (held) {
            g_webInflightRequests = g_webInflightRequests - 1;
        }
    }

    InflightSlot(const InflightSlot&) = delete;
    InflightSlot& operator=(const InflightSlot&) = delete;
};

// Emits the Busy Recovery Page straight onto the socket, bypassing the
// response object entirely. The buffer is compile-time constant, so this
// answers a refusal without allocating a single byte -- which is the whole
// reason a refused navigation can be answered at all.
//
// send() is not obliged to take the whole buffer at once, so partial writes
// are looped. A failure mid-response just ends the attempt: the connection is
// being closed either way, and the browser treats a truncated response the
// same as the bare close it would otherwise have received.
bool sendBusyRecoveryPage(httpd_req_t* raw) {
    const int sockfd = httpd_req_to_sockfd(raw);
    if (sockfd < 0) {
        return false;
    }

    size_t sent = 0;
    while (sent < kBusyRecoveryResponseLength) {
        const int written = httpd_socket_send(raw->handle, sockfd, kBusyRecoveryResponse + sent,
                                              kBusyRecoveryResponseLength - sent, 0);
        if (written <= 0) {
            return false;
        }
        sent += (size_t)written;
    }
    return true;
}

// Reads one request header into a caller-owned buffer. Deliberately not
// PsychicRequest::header(), which resizes an internal std::string -- that
// allocates on the one path that must not, and it shares that string with
// pathCStr(), so it would also invalidate a path pointer taken earlier in the
// same call.
void copyHeader(httpd_req_t* raw, const char* name, char* out, size_t outSize) {
    out[0] = '\0';
    const size_t len = httpd_req_get_hdr_value_len(raw, name);
    if (len == 0) {
        return;
    }
    // Truncation is fine for both headers read here: one is a short enum-like
    // token, the other is only examined for its leading media type.
    httpd_req_get_hdr_value_str(raw, name, out, outSize);
}

// Request admission. Registered as a global middleware rather than a global
// filter: a filter's only rejection path is the vendor's bodyless send(400),
// whereas this rejects by returning non-ESP_OK, which esp_http_server answers
// by closing the socket -- matching what the async stack's abort() did, and
// allocating nothing. Where a rejected main-frame navigation should instead
// receive the Busy Recovery Page, this return is the seam that carries it;
// see docs/adr/0016-busy-recovery-page-wire-contract.md.
esp_err_t admissionMiddleware(PsychicRequest* request, PsychicResponse* response,
                              PsychicMiddlewareNext next) {
    (void)response;
    const char* path = request->pathCStr();

    WebRequestAdmissionInputs in = {};
    in.estop = webPathIsEstop(path);
    in.diagnostic = webPathIsDiagnostic(path);
    in.longLived = webPathIsLongLived(path);
    in.inflightRequests = g_webInflightRequests;
    in.maxInflightRequests = PA_ADMISSION_MAX_INFLIGHT_REQUESTS;
    in.largestFreeBlock = sampleLargestFreeBlock(nullptr);
    in.minLargestFreeBlock = PA_ADMISSION_MIN_LARGEST_FREE_BLOCK;
    in.minLargestFreeBlockDiagnostic = PA_ADMISSION_MIN_LARGEST_FREE_BLOCK_DIAG;

    bool refused = false;
    switch (webRequestAdmissionDecide(in)) {
        case WebRequestAdmission::kRejectInflightCap:
            g_webRefusedInflightCap = g_webRefusedInflightCap + 1u;
            refused = true;
            break;
        case WebRequestAdmission::kRejectHeapFloor:
            if (in.diagnostic) {
                // Diagnostic rejections stay silent: they only happen during a
                // pressure storm, exactly when log volume is least welcome.
                g_webRefusedHeapFloorDiag = g_webRefusedHeapFloorDiag + 1u;
            } else {
                g_webRefusedHeapFloor = g_webRefusedHeapFloor + 1u;
                PA_LOG_WARN(TAG, "rejecting %s: largest free block %u < %u", path,
                            (unsigned)in.largestFreeBlock,
                            (unsigned)in.minLargestFreeBlock);
            }
            refused = true;
            break;
        case WebRequestAdmission::kAdmit:
            break;
    }

    if (refused) {
        // Only a navigation is answered. A refused asset gets the bare close:
        // its caller never renders a body, and spending bytes on one during a
        // pressure window is what this whole layer exists to avoid. The
        // headers are read only here, on the path already committed to
        // refusing, so an admitted request pays nothing for this decision.
        httpd_req_t* raw = request->request();
        char secFetchMode[16];
        char accept[32];
        copyHeader(raw, "Sec-Fetch-Mode", secFetchMode, sizeof(secFetchMode));
        copyHeader(raw, "Accept", accept, sizeof(accept));

        if (webIsMainFrameNavigation(secFetchMode, accept)) {
            g_webBusyRecoveryPagesServed = g_webBusyRecoveryPagesServed + 1u;
            sendBusyRecoveryPage(raw);
        }
        // Either way the connection goes: returning non-ESP_OK is what makes
        // esp_http_server close it, and the response above already declared
        // Connection: close.
        return ESP_FAIL;
    }

    // Counted here rather than per route, because this is the only point every
    // request passes through -- the global middleware chain wraps the static
    // file handler as well as the endpoints, and assets are most of a page load.
    // Only admitted requests count: a refusal never reached a handler, and
    // folding refusals in would hide the very shedding the counters above exist
    // to expose.
    webSocketCensusRequest(&s_census);
    publishCensus();

#if PA_WEB_CLOSE_PER_RESPONSE
    // The close-per-response arm of the keep-alive comparison. The event stream
    // is exempt by definition: it is a response that never ends, and closing it
    // after its head would make the arm measure a broken stream rather than a
    // connection policy.
    //
    // The header is staged before the handler runs because it has to travel
    // with the response, and PsychicResponse::sendHeaders() appends to this
    // list rather than replacing it. esp_http_server stores the pointers as
    // given, so both strings are literals with static storage.
    const bool closeAfterResponse = !in.longLived;
    httpd_req_t* rawForClose = request->request();
    const httpd_handle_t serverForClose = rawForClose->handle;
    const int sockForClose = httpd_req_to_sockfd(rawForClose);
    if (closeAfterResponse && httpd_resp_set_hdr(rawForClose, "Connection", "close") != ESP_OK) {
        // The header list is full (max_resp_headers). Say so rather than
        // closing anyway: a socket that dies without having announced it is a
        // different experiment from the one being run.
        PA_LOG_WARN(TAG, "no header slot for Connection: close on %s", path);
    }
#endif

    // Estop is admitted but never counted: a safety command must not be able to
    // fill the cap it is exempt from.
    const bool counted = !in.estop && !in.longLived;
    InflightSlot slot(counted);

#if PA_HEAP_PROFILE
    // Full path (not just a broad class) so a specific slow request (issue #67)
    // can be matched against the browser's own per-request timestamps after the
    // fact. Traced for exactly the requests the inflight cap counts, so the
    // trace and the depth counters describe the same population.
    uint8_t traceIdx = 0;
    if (counted) {
        traceIdx = pushRequestTraceEntry(path, millis());
    }
#endif

    const esp_err_t result = next();

#if PA_HEAP_PROFILE
    if (counted) {
        s_requestTrace[traceIdx].handlerDoneMs = millis();
    }
#endif

#if PA_WEB_CLOSE_PER_RESPONSE
    // Queued, not immediate: the server task processes the close from its own
    // loop once this request has finished flushing, so the response the browser
    // is still reading is not cut off underneath it.
    if (closeAfterResponse && sockForClose >= 0) {
        httpd_sess_trigger_close(serverForClose, sockForClose);
    }
#endif

    return result;
}

// =============================================================================
// Event stream transport (include/web_event_stream.h)
//
// PsychicEventSource is deliberately not used. Its send retries
// httpd_socket_send() in an unbounded loop against a socket whose send timeout
// defaults to five seconds, so one client that stops reading stops the
// broadcast for everyone -- the defect this slice exists to remove -- and it
// builds every message through a std::string plus an Arduino String copy, two
// allocations per client per event on a stack whose whole problem is heap.
//
// What replaces it is small: a fixed registry of subscribed sockets, an
// upgrade that writes the stream head, and a send bounded by a deadline that
// drops the socket instead of waiting on it.
// =============================================================================

// The registry is written from the server task (a stream opening, a socket
// closing) and from the event task (an eviction), so unlike the admission
// counters above it is genuinely cross-task and needs a lock. The critical
// sections only ever cover a scan of three ints, never a send.
WebEventStreamRegistry s_streams;
portMUX_TYPE s_streamMux = portMUX_INITIALIZER_UNLOCKED;

// Captured when the first stream opens. The broadcaster runs long after any
// request object is gone, so it needs the server handle from somewhere that
// outlives one.
httpd_handle_t s_streamServer = nullptr;

// PsychicHttpServer's constructor claims close_fn the same way it claims
// open_fn; this holds it so a closing socket still reaches the vendor's own
// client teardown after the registry has learned about it.
void (*s_vendorCloseFn)(httpd_handle_t, int) = nullptr;

// How long to wait between attempts once a client's receive window is full.
// Yielding matters more than the exact value: the window can only reopen when
// the client reads, which it cannot do while this task spins on the CPU.
constexpr uint32_t kStreamRetryDelayMs = 5;

// The stream response head, byte for byte what PsychicEventSourceResponse
// assembles, as a compile-time constant so the upgrade allocates nothing.
const char kEventStreamHead[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/event-stream\r\n"
    "Cache-Control: no-cache\r\n"
    "Connection: keep-alive\r\n"
    "\r\n";
constexpr size_t kEventStreamHeadLength = sizeof(kEventStreamHead) - 1;

struct EventSegment {
    const char* data;
    size_t len;
};

enum class StreamSendOutcome : unsigned char { kSent, kEvictDeadline, kEvictError };

// Pushes one event at one socket without ever blocking longer than the
// deadline.
//
// Called from two tasks: eventStreamTask (core 0) for every broadcast, and the
// psychic server task for the stream head at upgrade time. Both may block for
// up to the deadline, which is affordable on either -- eventStreamTask is a 1 Hz
// housekeeping task, and the server task is core-0 web work. Neither is a core-1
// real-time loop (AGENTS.md).
//
// MSG_DONTWAIT is what makes the deadline mean anything. Without it the
// socket's own send timeout governs -- five seconds by default -- so a single
// attempt against a stalled client would overrun any shorter deadline before
// this loop got to look at the clock again. That is precisely how the vendor's
// unbounded retry manages to hold the broadcaster for tens of seconds.
StreamSendOutcome sendEventBounded(int socket, const EventSegment* segments, size_t segmentCount) {
    const uint32_t startMs = millis();
    size_t segment = 0;
    size_t offset = 0;

    for (;;) {
        while (segment < segmentCount && offset >= segments[segment].len) {
            segment++;
            offset = 0;
        }
        if (segment >= segmentCount) {
            return StreamSendOutcome::kSent;
        }

        const int written = httpd_socket_send(s_streamServer, socket,
                                              segments[segment].data + offset,
                                              segments[segment].len - offset, MSG_DONTWAIT);
        WebEventWriteResult result;
        if (written > 0) {
            offset += (size_t)written;
            result = WebEventWriteResult::kWrote;
        } else if (written == HTTPD_SOCK_ERR_TIMEOUT) {
            result = WebEventWriteResult::kWouldBlock;
        } else {
            result = WebEventWriteResult::kFailed;
        }

        switch (webEventSendDecide(result, millis() - startMs, PA_SSE_SEND_DEADLINE_MS)) {
            case WebEventSendVerdict::kEvictError:
                return StreamSendOutcome::kEvictError;
            case WebEventSendVerdict::kEvictDeadline:
                return StreamSendOutcome::kEvictDeadline;
            case WebEventSendVerdict::kContinue:
                break;
        }

        if (result == WebEventWriteResult::kWouldBlock) {
            vTaskDelay(pdMS_TO_TICKS(kStreamRetryDelayMs));
        }
    }
}

// Unregisters a stream and drops its connection. Called from eventStreamTask,
// which is the only place that can find a client unresponsive.
//
// httpd_sess_trigger_close() posts the close to the server's own control
// socket rather than touching the session from here, which is what makes it
// safe to call from the event task. That is exactly the cross-task close
// ADR 0020 went looking for on AsyncTCP and could not find -- the capability
// this migration exists to obtain.
void evictStream(int socket, StreamSendOutcome outcome) {
    taskENTER_CRITICAL(&s_streamMux);
    webEventStreamRegistryRemove(&s_streams, socket);
    taskEXIT_CRITICAL(&s_streamMux);

    if (outcome == StreamSendOutcome::kEvictDeadline) {
        g_webSseEvicted = g_webSseEvicted + 1u;
        g_webSseEvictLastMs = millis();
        // Warn, not debug: this is the whole point of the slice and it is rare
        // by design, so a run that hit it must say so without anyone having to
        // have raised the log level in advance.
        PA_LOG_WARN(TAG, "event stream client %d missed the %u ms send deadline; dropped", socket,
                    (unsigned)PA_SSE_SEND_DEADLINE_MS);
    } else {
        // An ordinary disconnect: the tab closed between one tick and the next.
        PA_LOG_DEBUG(TAG, "event stream client %d disconnected", socket);
    }

    // Abandon the connection abruptly rather than gracefully. Without this the
    // eviction frees the broadcaster but not the memory: close() on a socket
    // whose send queue is full leaves lwIP holding the pcb and the whole queue
    // while it retransmits into a peer that has already stopped answering.
    // Measured live on this controller -- the largest free block stayed
    // depressed for 71 s after the client was evicted, and with one other
    // stream open that sat below the Connection Admission floor for the whole
    // period, refusing every new connection including the /api/status that
    // would have explained it.
    //
    // linger{on, 0} makes the close an RST, which drops the queue immediately.
    // It only ever touches a socket already being abandoned, so no ordinary
    // connection loses its graceful shutdown. Requires CONFIG_LWIP_SO_LINGER,
    // set in platformio.ini's custom_sdkconfig with the same rationale.
    struct linger abandon = {};
    abandon.l_onoff = 1;
    abandon.l_linger = 0;
    if (setsockopt(socket, SOL_SOCKET, SO_LINGER, &abandon, sizeof(abandon)) != 0) {
        // Not fatal: the client still goes, the heap just recovers slowly. Worth
        // saying out loud though, because "evicted but the block stayed low" is
        // otherwise a very confusing thing to read in a run.
        PA_LOG_WARN(TAG, "could not set SO_LINGER on client %d; close will be graceful", socket);
    }

    httpd_sess_trigger_close(s_streamServer, socket);
}

// Keeps the registry honest about which sockets are still open. Runs on the
// server task for every closing connection, streams and ordinary requests
// alike, so it stays a three-slot scan and nothing more.
void streamCloseCallback(httpd_handle_t hd, int sockfd) {
    taskENTER_CRITICAL(&s_streamMux);
    const bool wasStream = webEventStreamRegistryRemove(&s_streams, sockfd);
    taskEXIT_CRITICAL(&s_streamMux);
    if (wasStream) {
        PA_LOG_DEBUG(TAG, "event stream client %d closed", sockfd);
    }

    // Returns false for a socket Connection Admission refused, which reaches
    // this callback through httpd_sess_delete() having never been admitted.
    // The census ignores it; the occupancy reading only ever counts sockets
    // that actually got to serve something.
    webSocketCensusClose(&s_census, sockfd);
    publishCensus();

    if (s_vendorCloseFn != nullptr) {
        // The vendor callback closes the descriptor itself once it has torn
        // down its PsychicClient.
        s_vendorCloseFn(hd, sockfd);
    } else {
        PA_LOG_ERROR(TAG, "no vendor close callback captured; closing socket %d directly", sockfd);
        close(sockfd);
    }
}

}  // namespace

#if PA_HEAP_PROFILE
// Copies the trace ring oldest-first into out, for the /api/profiler handler
// (api_profiler.cpp) to read once after an experiment. Read-only; does not
// clear or rotate the ring, so repeated reads during a warm-up are safe.
size_t copyRequestLifecycleTrace(RequestLifecycleEntry* out, size_t maxEntries) {
    uint8_t count = s_requestTraceCount;
    if (count > maxEntries) {
        count = (uint8_t)maxEntries;
    }
    const uint8_t oldest =
        (uint8_t)((s_requestTraceHead + PA_REQUEST_TRACE_MAX - s_requestTraceCount) %
                  PA_REQUEST_TRACE_MAX);
    for (uint8_t i = 0; i < count; i++) {
        out[i] = s_requestTrace[(uint8_t)((oldest + i) % PA_REQUEST_TRACE_MAX)];
    }
    return count;
}
#endif  // PA_HEAP_PROFILE

bool WebRequest::hasParam(const char* name) const {
    return psychicCtx(backend_)->req->hasParam(name);
}

bool WebRequest::param(const char* name, char* out, size_t outSize) const {
    PsychicWebParameter* p = psychicCtx(backend_)->req->getParam(name);
    if (p == nullptr || out == nullptr || outSize == 0) {
        return false;
    }
    // Copy-out before the temporary String from value() goes away.
    snprintf(out, outSize, "%s", p->value().c_str());
    return true;
}

const char* WebRequest::paramRef(const char* name) const {
    PsychicWebParameter* p = psychicCtx(backend_)->req->getParam(name);
    // valueCStr(), not value(): value() returns a String by value under
    // Arduino, so its c_str() would dangle the moment the temporary died.
    // valueCStr() points into the parameter's own storage, which the request
    // owns until it completes.
    return p != nullptr ? p->valueCStr() : nullptr;
}

const char* WebRequest::body() const {
    PsychicRequest* req = psychicCtx(backend_)->req;

    // Only a body that was NOT turned into parameters counts as a raw body.
    // PsychicHttp parses form-urlencoded and multipart bodies into params, and
    // the async stack likewise only exposes its "plain" parameter for bodies
    // it did not parse. Returning a form body here made every form POST look
    // to an apply core like a malformed JSON document -- proven on the device,
    // where POST /api/config with an ordinary form field answered 400
    // "invalid json body".
    if (req->isMultipart()) {
        return nullptr;
    }
    // contentType() returns a String by value under Arduino, so it is consumed
    // here rather than held.
    if (req->contentType().startsWith("application/x-www-form-urlencoded")) {
        return nullptr;
    }

    // bodyCStr() rather than body(), for the same lifetime reason as paramRef().
    const char* raw = req->bodyCStr();
    return (raw != nullptr && raw[0] != '\0') ? raw : nullptr;
}

size_t WebRequest::contentLength() const {
    return (size_t)psychicCtx(backend_)->req->contentLength();
}

void WebRequest::addHeader(const char* name, const char* value) {
    WebRequestPsychicCtx* ctx = psychicCtx(backend_);
    if (ctx->resp == nullptr) {
        PA_LOG_ERROR(TAG, "addHeader() during upload chunk phase ignored (%s)", name);
        return;
    }
    // No staging buffer needed: the response object already exists and belongs
    // to this request alone, so the header is scoped exactly as the seam
    // documents it. PsychicResponse copies both strings into its own storage.
    ctx->resp->addHeader(name, value);
}

void WebRequest::send(int code, const char* contentType, const char* body) {
    WebRequestPsychicCtx* ctx = psychicCtx(backend_);
    if (ctx->resp == nullptr) {
        // Upload chunk phase: PsychicHttp owns the response until the body has
        // been consumed, so there is nothing to send through yet. Refusing here
        // turns a handler that sends too early into a logged bug instead of a
        // null dereference on a device that is mid-firmware-write.
        PA_LOG_ERROR(TAG, "send() during upload chunk phase ignored (code=%d)", code);
        return;
    }
    ctx->result = ctx->resp->send(code, contentType, body);
}

bool WebRequest::sendChunked(const char* contentType, WebResponseBodyFiller filler) {
    WebRequestPsychicCtx* ctx = psychicCtx(backend_);
    if (ctx->resp == nullptr) {
        PA_LOG_ERROR(TAG, "sendChunked() during upload chunk phase ignored");
        return false;
    }

    // Header/chunk/terminator sequence taken from PsychicFileResponse: the
    // staged headers go out with the first chunk, and the empty terminating
    // chunk closes the body. One chunk is in memory at a time.
    ctx->resp->setCode(200);
    ctx->resp->setContentType(contentType);
    ctx->resp->sendHeaders();

    // Stack, not static: the psychic server task is configured with an 8 KB
    // stack and one chunk buffer is the whole cost of an arbitrarily large
    // body. 1 KB keeps the chunk count low without crowding that stack.
    uint8_t chunk[1024];
    size_t offset = 0;
    esp_err_t err = ESP_OK;
    for (;;) {
        const size_t written = filler(chunk, sizeof(chunk), offset);
        if (written == 0) {
            break;
        }
        err = ctx->resp->sendChunk(chunk, written);
        if (err != ESP_OK) {
            break;
        }
        offset += written;
    }
    if (err == ESP_OK) {
        err = ctx->resp->finishChunking();
    }

    // Always true: staging headers cannot fail, so the response is committed
    // from here on and the handler must not send an error on top of a body
    // already on the wire. A transport failure travels out through result,
    // the esp_err_t the vendor callback returns -- the same path send() uses.
    ctx->result = err;
    return true;
}

void* WebRequest::sessionContext() const {
    return psychicCtx(backend_)->req->request()->sess_ctx;
}

bool WebRequest::setSessionContext(void* ctx, void (*freeFn)(void*)) {
    httpd_req_t* raw = psychicCtx(backend_)->req->request();
    raw->sess_ctx = ctx;
    raw->free_ctx = freeFn;
    return true;
}

bool WebRequest::triggerClose() {
    httpd_req_t* raw = psychicCtx(backend_)->req->request();
    return httpd_sess_trigger_close(raw->handle, httpd_req_to_sockfd(raw)) == ESP_OK;
}

// Called from the psychic server task, inside handleEventsGet()'s dispatch, so
// the registry mutation here races the eviction path on eventStreamTask -- which
// is why the registry has a lock and the admission counters above do not.
bool WebRequest::beginEventStream() {
    WebRequestPsychicCtx* ctx = psychicCtx(backend_);
    httpd_req_t* raw = ctx->req->request();
    const int socket = httpd_req_to_sockfd(raw);
    if (socket < 0) {
        return false;
    }
    s_streamServer = raw->handle;

    // Register before writing the head. If the head fails the registration is
    // undone below, whereas registering afterwards would leave a window in
    // which a broadcast could reach a socket the caller still believes it owns.
    taskENTER_CRITICAL(&s_streamMux);
    const bool registered = webEventStreamRegistryAdd(&s_streams, socket);
    const uint32_t open = (uint32_t)s_streams.count;
    taskEXIT_CRITICAL(&s_streamMux);
    if (!registered) {
        // The handler already checked the cap; losing the race to a
        // simultaneous upgrade is the only way to arrive here.
        return false;
    }
    if (open > g_webSseClientsPeak) {
        g_webSseClientsPeak = open;
    }

    const EventSegment head[1] = {{kEventStreamHead, kEventStreamHeadLength}};
    if (sendEventBounded(socket, head, 1) != StreamSendOutcome::kSent) {
        // Unwind rather than hand a half-written stream to the broadcaster.
        // The registry entry goes, but the close does not come from here: this
        // runs on the server task, so returning ESP_FAIL is already the
        // shortest path to dropping the socket, and triggering a close on top
        // of it would queue teardown work for a session esp_http_server is
        // about to tear down itself. This is also not counted as an eviction --
        // a handshake that never completed is not a stalled client, and that
        // counter has to keep meaning what it says.
        taskENTER_CRITICAL(&s_streamMux);
        webEventStreamRegistryRemove(&s_streams, socket);
        taskEXIT_CRITICAL(&s_streamMux);
        PA_LOG_WARN(TAG, "event stream head to client %d failed; dropping connection", socket);
        ctx->result = ESP_FAIL;
        // True even though no stream survived: the connection is going, so
        // there is nobody left for the handler to answer. Returning false would
        // put a 503 body on top of a partially written stream head.
        return true;
    }

    PA_LOG_DEBUG(TAG, "event stream client %d subscribed (%u open)", socket, (unsigned)open);
    ctx->result = ESP_OK;
    return true;
}

// Read from eventStreamTask (to decide whether a tick has anywhere to go), from
// the psychic server task (handleEventsGet()'s cap check and buildStatusJson()),
// hence the lock on what is otherwise a plain field read.
size_t webEventStreamClientCount() {
    taskENTER_CRITICAL(&s_streamMux);
    const size_t count = s_streams.count;
    taskEXIT_CRITICAL(&s_streamMux);
    return count;
}

// Called only from eventStreamTask (core 0, 1 Hz), which owns the scheduling
// this reaches the wire for -- the on-demand status flag, the rc snapshot and
// the log batch. Nothing else may broadcast: the segment buffers below borrow
// that task's own static payload buffers.
void webEventStreamBroadcast(const char* event, const char* data, uint32_t id) {
    if (s_streamServer == nullptr || data == nullptr) {
        return;
    }

    int sockets[PA_ADMISSION_MAX_SSE_CLIENTS];
    taskENTER_CRITICAL(&s_streamMux);
    const size_t count =
        webEventStreamRegistrySnapshot(&s_streams, sockets, PA_ADMISSION_MAX_SSE_CLIENTS);
    taskEXIT_CRITICAL(&s_streamMux);
    if (count == 0) {
        return;
    }

    // Three segments, nothing concatenated: the payload goes out straight from
    // the caller's own buffer, so a 3 KB rc event costs no second buffer and no
    // allocation. Both vendor implementations build the whole frame into a
    // fresh string first, once per client.
    char prefix[kWebEventStreamPrefixMax];
    const size_t prefixLength = webEventStreamFormatPrefix(prefix, sizeof(prefix), event, id);
    const EventSegment segments[3] = {
        {prefix, prefixLength},
        {data, strlen(data)},
        {kWebEventStreamTerminator, kWebEventStreamTerminatorLength},
    };

    for (size_t i = 0; i < count; i++) {
        const int socket = sockets[i];

        // Re-check membership per client rather than trusting the snapshot: a
        // socket that closed since it was taken may already have been reissued
        // to a new connection, which must not be handed event-stream bytes.
        // The residual window is one client's send wide and cannot be closed
        // entirely without doing the sends under the lock, which would put a
        // deadline-long critical section on the path of every other connection.
        taskENTER_CRITICAL(&s_streamMux);
        const bool stillOpen = webEventStreamRegistryHas(&s_streams, socket);
        taskEXIT_CRITICAL(&s_streamMux);
        if (!stillOpen) {
            continue;
        }

        const StreamSendOutcome outcome = sendEventBounded(socket, segments, 3);
        if (outcome != StreamSendOutcome::kSent) {
            // Evict and carry on to the next client. Bounding the send is only
            // half the fix; the other half is that one bad client costs this
            // broadcast one deadline and then stops costing anything at all.
            evictStream(socket, outcome);
        }
    }
}

void webRegisterRoute(const char* path, WebMethod method, WebRequestHandler handler,
                      size_t maxBodyBytes) {
    // maxBodyBytes is the async backend's buffering bound; here the library
    // does the buffering and enforces one server-wide ceiling
    // (PsychicHttpServer::maxRequestBodySize), so the per-route value only has
    // to fit under it. Raising the server ceiling to the largest route's need
    // keeps the two backends agreeing on which bodies arrive at all; the
    // matching 413 comes from the handler reading contentLength(), which is
    // where both backends already agree.
    if (maxBodyBytes > s_server.maxRequestBodySize) {
        s_server.maxRequestBodySize = maxBodyBytes;
    }

    http_method vendorMethod = HTTP_GET;
    if (method == WebMethod::kPost) {
        vendorMethod = HTTP_POST;
    } else if (method == WebMethod::kDelete) {
        vendorMethod = HTTP_DELETE;
    }
    s_server.on(path, vendorMethod,
                [handler](PsychicRequest* vendorReq, PsychicResponse* vendorResp) -> esp_err_t {
                    WebRequestPsychicCtx ctx = {vendorReq, vendorResp, ESP_OK};
                    WebRequest req(&ctx);
                    handler(req);
                    return ctx.result;
                });
}

void webRegisterUploadRoute(const char* path, WebUploadChunkHandler onChunk,
                            WebRequestHandler onDone) {
    // Heap-allocated and never freed on purpose: PsychicUploadHandler must
    // outlive registration for the life of the server, and registration only
    // happens once during bring-up. There is no path that unregisters a route.
    PsychicUploadHandler* handler = new PsychicUploadHandler();

    handler->onUpload([onChunk](PsychicRequest* vendorReq, const String& filename, uint64_t index,
                                uint8_t* data, size_t len, bool final) -> esp_err_t {
        // No response object during the body phase; the seam's null-response
        // guard turns a handler that sends here into a logged error.
        WebRequestPsychicCtx ctx = {vendorReq, nullptr, ESP_OK};
        WebRequest req(&ctx);
        onChunk(req, filename.c_str(), (size_t)index, data, len, final);

        // Always ESP_OK, even for an upload the handler is rejecting. A
        // non-OK return makes PsychicUploadHandler::handleRequest() send its
        // own text/html error and never call onRequest, which would replace
        // the JSON error body data/firmware.js reads its message from. The
        // handler records its own outcome and answers from onDone instead.
        return ESP_OK;
    });

    handler->onRequest([onDone](PsychicRequest* vendorReq,
                                PsychicResponse* vendorResp) -> esp_err_t {
        WebRequestPsychicCtx ctx = {vendorReq, vendorResp, ESP_OK};
        WebRequest req(&ctx);
        onDone(req);
        return ctx.result;
    });

    s_server.on(path, HTTP_POST, handler);
}

void initPsychicWebServer() {
    // Verified live on the #72 prototype: nothing binds or listens until
    // begin(); on()/serveStatic() only record configuration.
    s_server.config.max_open_sockets = 10;
    s_server.config.stack_size = 8192;

    // Connection Admission, installed in the documented pre-begin() window
    // where server.config is an ordinary ESP-IDF httpd_config. The constructor
    // has already pointed open_fn at the library's own callback, so capture it
    // first and chain to it whenever the guard admits -- dropping it would
    // leave every admitted connection without a PsychicClient.
    s_vendorOpenFn = s_server.config.open_fn;
    s_server.config.open_fn = admissionOpenCallback;
    webAcceptRateLimiterInit(&s_acceptLimiter, millis(), PA_ACCEPT_BURST);
    webSocketCensusInit(&s_census);

    // Same capture-and-chain for the close side, which is how the event stream
    // registry learns that a subscribed socket has gone. Without it sseClients
    // would only ever fall when a broadcast happened to fail against a dead
    // socket -- a count that lags reality by up to a tick and never falls at
    // all once the last client leaves, which is the one number ADR 0017 scores.
    s_vendorCloseFn = s_server.config.close_fn;
    s_server.config.close_fn = streamCloseCallback;
    webEventStreamRegistryInit(&s_streams);

    // Request admission, ahead of route matching and the static-file open.
    s_server.addMiddleware(admissionMiddleware);

    // PsychicHttp leaves HTTPD_DEFAULT_CONFIG()'s core_id at tskNO_AFFINITY,
    // which would let the server task -- and with it every handler's JSON
    // serialization and LittleFS read -- be scheduled onto core 1, where the
    // 50 Hz drive and RC loops run. Web work belongs on core 0 (AGENTS.md:
    // core 1 real-time loops avoid heap allocation, core 0 web handlers may
    // allocate bounded per-request documents), so pin it.
    s_server.config.core_id = 0;

    // PsychicUploadHandler refuses a request whose contentLength() exceeds this
    // before any of our callbacks run, and answers with its own text/html 400.
    // The library's 2 MB default happens to clear the 1.625 MB app partition,
    // but "happens to" is not a contract: set it above our own per-target guard
    // so the guard is what rejects an oversize image, in the JSON shape
    // data/firmware.js reads. See uploadContentLengthFits() in api_upload.h.
    s_server.maxUploadSize = kUploadTransportCeiling;

    webRegisterSeamRoutes();

    // The live update stream, registered here rather than in the shared seam
    // route table. Its handler takes WebRequest like any other, but the async
    // scaffold serves /api/events through a whole vendor handler
    // (server.addHandler(&events)) and has no per-request upgrade point for a
    // seam handler to use -- so listing the route in the table both backends
    // read would register a handler one of them cannot honour. The #91 cutover
    // deletes that scaffold and moves this line into the table.
    webRegisterRoute("/api/events", WebMethod::kGet, handleEventsGet);

    // Endpoints registered above win: serveStatic() installs a global handler,
    // and the server only reaches global handlers after no endpoint matched.
    //
    // webServerInit() already mounted LittleFS and owns the littleFsReady flag
    // /api/status reports, so gate on that rather than mounting a second time
    // -- otherwise what is served and what is reported could disagree.
    //
    // The filesystem image ships only the gzipped copy of each text asset
    // (tools/gzip_fsdata.py), which PsychicStaticFileHandler serves off its
    // "<path>.gz" fallback with Content-Encoding: gzip, matching what the
    // async stack did. Default file and cache-control are the async settings
    // from web_server.cpp verbatim.
    if (webLittleFsMounted()) {
        s_server.serveStatic("/", LittleFS, "/")->setDefaultFile("index.html")->setCacheControl("no-cache");
    } else {
        PA_LOG_WARN(TAG, "LittleFS not mounted; static serving unavailable");
    }

    esp_err_t err = s_server.begin();
    if (err != ESP_OK) {
        PA_LOG_WARN(TAG, "PsychicHttp server.begin() failed: %s", esp_err_to_name(err));
        return;
    }
    PA_LOG_INFO(TAG, "PsychicHttp server listening on port 80");
}
