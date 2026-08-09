// =============================================================================
// src/web/web_admission_psychic.cpp
//
// Connection Admission and Request Admission orchestration for the PsychicHttp
// backend (ADRs 0018, 0022). The socket-level connection-admission callback,
// the URL-aware request-admission middleware, and the state they share --
// admission counters, heap sampling cache, socket census, and request tracing.
//
// Both layers run on the single server task, so no cross-task synchronisation
// is needed for the state they touch. The decision cores they call live in
// web_admission.cpp and compile/test on the host.
// =============================================================================

#include <Arduino.h>
#include <PsychicHttp.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_http_server.h>
#include <esp_timer.h>
#include <stdio.h>
#include <string.h>

#include "../../include/api_admission_trace.h"
#include "../../include/logging.h"
#include "../../include/web_admission.h"
#include "../../include/web_backend_psychic.h"
#include "../../include/web_busy_page.h"
#include "../../include/web_event_stream.h"
#include "../../include/web_response_deadline.h"

static const char* TAG = "WebServer";

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

// An upload needs TWO FILE_CHUNK_SIZE buffers alive at once, and request
// admission only ever guarantees ONE block of PA_ADMISSION_MIN_LARGEST_FREE_BLOCK.
// PsychicHttp's MultipartProcessor takes a receive buffer for the whole transfer,
// then a second buffer of the same size when it reaches the file part. The second
// allocation is carved out of what the first left behind, so the floor has to
// cover both -- and covering only the first is indistinguishable from covering
// both right up until an upload is attempted.
//
// It failed exactly that way: with the library's 8 KB default against a 9000 byte
// floor, the receive buffer took the only qualifying block, the item buffer found
// 6644 bytes left and failed, and the parser abandoned the part WITHOUT reporting
// an error -- it drains the rest of the body and returns ESP_OK, so a 1.5 MB image
// transferred in full and arrived as "no image received". /upload/* is not a
// diagnostic path, so the full floor is the one that applies.
//
// The slack is not decoration. The floor is checked against a reading that may be
// up to PA_ACCEPT_HEAP_SAMPLE_MIN_INTERVAL_MS old, other work allocates between
// admission and the parser, and each block carries allocator overhead of its own.
static constexpr size_t kUploadParserBufferSlack = 2048;
static_assert(2 * (size_t)FILE_CHUNK_SIZE + kUploadParserBufferSlack <=
                  (size_t)PA_ADMISSION_MIN_LARGEST_FREE_BLOCK,
              "An upload holds two FILE_CHUNK_SIZE buffers at once, and the multipart parser "
              "abandons the body silently when the second one fails. Either lower "
              "FILE_CHUNK_SIZE or raise PA_ADMISSION_MIN_LARGEST_FREE_BLOCK; both live in "
              "platformio.ini [flags_base].");

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

// Records a decision that has already been taken, as evidence for re-deriving
// the admission floors (include/web_admission_trace.h). Never consulted by
// either layer: it runs after the outcome is fixed, so the only thing it can
// change is how long the guard took.
//
// The age it stores is the age of the sample the decision USED, which is what
// separates "the heap really was this low" from "the heap was this low once,
// and everything arriving in the same sample interval inherited the reading".
// A rate rejection never calls the sampler at all, so its row carries whatever
// the cache last held and an age to match -- honest rather than tidy.
#if PA_ADMISSION_TRACE

// A second largest-free-block reading, taken fresh straight after the decision
// and never stored back into the cache. This is the control the whole staleness
// question needs: without it, a low `block` is indistinguishable from a low
// reading that has since recovered. It costs one extra heap walk per decision,
// which is why it is separately switchable -- a run that wants the guard's
// undisturbed timing turns it off and keeps the rest of the profile.
#ifndef PA_ADMISSION_TRACE_FRESH
#define PA_ADMISSION_TRACE_FRESH 1
#endif

void traceDecision(WebAdmissionTraceLayer layer, WebAdmissionTraceOutcome outcome, int inflight,
                   WebAdmissionTraceNavigation navigation) {
    // Read here rather than taken from the caller. Each layer captures its own
    // clock at a different point relative to the sampler -- the connection
    // callback before it, the request middleware after it -- so a caller's
    // stamp can predate the sample it is being compared against, and the
    // unsigned subtraction below would turn a just-refreshed sample into the
    // maximum possible age. That is the one reading this trace cannot afford to
    // get backwards. Sampling has already happened by the time this runs, so a
    // clock read here is never earlier than the sample it measures.
    const uint32_t nowMs = millis();
    const uint32_t ageMs = s_heapSample.primed ? (nowMs - s_heapSample.lastSampleMs)
                                               : kWebAdmissionTraceAgeUnknown;
#if PA_ADMISSION_TRACE_FRESH
    const uint32_t fresh = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
#else
    const uint32_t fresh = 0;
#endif
    webAdmissionTraceRecord(webAdmissionTraceInstance(), nowMs, layer, outcome,
                            (uint32_t)s_heapSample.value, fresh, ageMs, inflight, navigation);
}

#else

// Trace off: the call sites stay, and cost nothing.
inline void traceDecision(WebAdmissionTraceLayer, WebAdmissionTraceOutcome, int,
                          WebAdmissionTraceNavigation) {
}

#endif  // PA_ADMISSION_TRACE

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

    // The connection layer is blind to the URL by construction, so every row it
    // records carries no path and an unknown navigation class.
    if (decision == WebAcceptDecision::kRejectRate) {
        g_webAcceptRejectRate = g_webAcceptRejectRate + 1u;
        g_webAcceptRejectLastMs = nowMs;
        traceDecision(WebAdmissionTraceLayer::kConnection,
                      WebAdmissionTraceOutcome::kRejectRate, 0,
                      WebAdmissionTraceNavigation::kUnknown);
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
        traceDecision(WebAdmissionTraceLayer::kConnection,
                      WebAdmissionTraceOutcome::kRejectHeap, 0,
                      WebAdmissionTraceNavigation::kUnknown);
        return ESP_FAIL;
    }

    traceDecision(WebAdmissionTraceLayer::kConnection, WebAdmissionTraceOutcome::kAdmit, 0,
                  WebAdmissionTraceNavigation::kUnknown);

    // Admitted. Counted before the vendor chain so the census and the server's
    // own session table agree on what exists: httpd_sess_new() has already
    // created the session by the time open_fn runs, and a vendor callback that
    // failed would take it down through the same close_fn the census listens to.
    webSocketCensusOpen(&s_census, sockfd);
    publishCensus();

    const esp_err_t vendorResult = s_vendorOpenFn != nullptr ? s_vendorOpenFn(hd, sockfd) : ESP_OK;
    if (vendorResult != ESP_OK) {
        return vendorResult;
    }

    // Response-phase deadline, installed on the session the moment it exists so
    // no response on it can ever leave through the unguarded default. Nothing
    // is chained: PsychicHttpServer installs no send override of its own on the
    // plain HTTP server (only its HTTPS sibling does, through esp_https_server),
    // and there is no getter to recover one if it did.
    if (httpd_sess_set_send_override(hd, sockfd, webResponseDeadlineSendOverride) != ESP_OK) {
        // Not fatal -- the connection works, it is simply unguarded -- but it
        // must not pass silently, because the whole guard would then be absent
        // with every counter reading zero, which looks exactly like a quiet run.
        PA_LOG_WARN(TAG, "no response deadline on socket %d: send override refused", sockfd);
    }

    return ESP_OK;
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
    // Bench-only: the induced-pressure env raises the floor above the resting
    // heap so ordinary navigations genuinely refuse and serve the ADR 0016
    // Busy Recovery Page. Compiled to a constant zero everywhere else.
#ifdef PA_ADMISSION_OVERRIDE_HEAP_FLOOR
    in.minLargestFreeBlockOverride = PA_ADMISSION_OVERRIDE_HEAP_FLOOR;
#else
    in.minLargestFreeBlockOverride = 0;
#endif

    bool refused = false;
    WebAdmissionTraceOutcome outcome = WebAdmissionTraceOutcome::kAdmit;
    switch (webRequestAdmissionDecide(in)) {
        case WebRequestAdmission::kRejectInflightCap:
            g_webRefusedInflightCap = g_webRefusedInflightCap + 1u;
            outcome = WebAdmissionTraceOutcome::kRejectInflight;
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
            outcome = WebAdmissionTraceOutcome::kRejectHeap;
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

        const bool navigation = webIsMainFrameNavigation(secFetchMode, accept);
        if (navigation) {
            g_webBusyRecoveryPagesServed = g_webBusyRecoveryPagesServed + 1u;
            webBusyRecoveryPageSend(raw);
        }

        // Recorded here rather than beside the counters above because this is
        // the only point where a refusal's navigation class is known, and that
        // class is the whole question: a refused navigation is the one this
        // ticket has to see either completed or answered with the Busy page.
        traceDecision(WebAdmissionTraceLayer::kRequest, outcome, in.inflightRequests,
                      navigation ? WebAdmissionTraceNavigation::kNavigation
                                 : WebAdmissionTraceNavigation::kAsset);

        // Either way the connection goes: returning non-ESP_OK is what makes
        // esp_http_server close it, and the response above already declared
        // Connection: close.
        return ESP_FAIL;
    }

    // Admitted: the navigation class stays unknown, because determining it
    // costs two header reads and an admitted request must not pay for a
    // distinction only a refusal acts on.
    traceDecision(WebAdmissionTraceLayer::kRequest, WebAdmissionTraceOutcome::kAdmit,
                  in.inflightRequests, WebAdmissionTraceNavigation::kUnknown);

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

    // Arm the response-phase deadline for this request's socket. Armed for
    // every admitted request including estop: the safety path is exempt from
    // being *refused*, which is a memory policy, but a safety response that
    // cannot finish is holding the task every other request needs and is not
    // made safer by being allowed to hold it forever.
    //
    // The clock does not start here. It starts at the first byte the handler
    // sends, so a slow body build and an upload's whole receive phase are
    // outside it -- see webResponseDeadlineCheck().
    httpd_req_t* rawForDeadline = request->request();
    const int deadlineSocket = httpd_req_to_sockfd(rawForDeadline);
    // webResponseDeadlineArm is declared in web_response_deadline.h (policy core)
    // s_responseDeadline is declared in web_backend_psychic.h (cross-unit interface)
    webResponseDeadlineArm(&s_responseDeadline, deadlineSocket);

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

    // Release the phase and fold its duration into the published maximum. This
    // is the margin evidence the calibrated deadline is set against, so it is
    // taken on every response rather than in a one-off measurement session.
    // Disarm reports -1 for a response that sent nothing or that breached,
    // neither of which is a legitimate response time.
    // webResponseDeadlineDisarm is declared in web_response_deadline.h (policy core)
    const int32_t responseMs = webResponseDeadlineDisarm(&s_responseDeadline, millis());
    if (responseMs >= 0) {
        g_webResponseLastMs = (uint32_t)responseMs;
        if ((uint32_t)responseMs > g_webResponseMaxMs) {
            g_webResponseMaxMs = (uint32_t)responseMs;
            // The route that set a new maximum, which is what a calibration
            // run needs and what a bare number cannot supply.
            PA_LOG_INFO(TAG, "slowest response phase now %ld ms (%s)", (long)responseMs, path);
        }
    }

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

// Functions to register the middleware callbacks with the server.
// (Called from web_request_psychic.cpp during server initialization.)

void webAdmissionRegisterCallbacks(PsychicHttpServer& server) {
    // Capture and chain the vendor's open callback
    s_vendorOpenFn = server.config.open_fn;
    server.config.open_fn = admissionOpenCallback;

    // Initialize admission state
    webAcceptRateLimiterInit(&s_acceptLimiter, millis(), PA_ACCEPT_BURST);
    webSocketCensusInit(&s_census);
}

void webAdmissionRegisterMiddleware(PsychicHttpServer& server) {
    // Register the request admission middleware
    server.addMiddleware(admissionMiddleware);
}

void webAdmissionTraceInit() {
#if PA_ADMISSION_TRACE
    WebAdmissionTraceConfig traceConfig = {};
    traceConfig.connectionFloor = PA_ACCEPT_MIN_LARGEST_FREE_BLOCK;
    traceConfig.requestFloor = PA_ADMISSION_MIN_LARGEST_FREE_BLOCK;
    traceConfig.requestFloorDiagnostic = PA_ADMISSION_MIN_LARGEST_FREE_BLOCK_DIAG;
    traceConfig.sampleIntervalMs = PA_ACCEPT_HEAP_SAMPLE_MIN_INTERVAL_MS;
    traceConfig.maxInflightRequests = PA_ADMISSION_MAX_INFLIGHT_REQUESTS;
    webAdmissionTraceConfigure(webAdmissionTraceInstance(), traceConfig);
#endif
}
