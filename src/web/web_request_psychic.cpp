// =============================================================================
// src/web/web_request_psychic.cpp
//
// PsychicHttp backend for the WebRequest seam (ADR 0021), plus the psychic
// server bring-up. Becomes the only backend at the #91 cutover.
//
// backend_ holds a WebRequestPsychicCtx (request + response + the esp_err_t
// the vendor callback must return). The session escape hatch reaches the
// underlying esp_http_server request: sess_ctx / free_ctx assignment and
// httpd_sess_trigger_close() -- the capabilities this migration exists to
// obtain (#53, ADR 0020).
//
// Scope while the migration is in flight: only ported route groups are
// registered here (gap 4, #79 onward), and the pre-HTTP admission guard is
// #81's slice -- this is a development target, not the release configuration,
// until epic #75 completes.
// =============================================================================
#ifdef PA_WEB_BACKEND_PSYCHIC

#include <Arduino.h>
#include <LittleFS.h>
#include <PsychicHttp.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <http_parser.h>
#include <stdio.h>

#include "../../include/api_upload.h"
#include "../../include/logging.h"
#include "../../include/web_admission.h"
#include "../../include/web_busy_page.h"
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

    // Estop is admitted but never counted, matching the async stack: a safety
    // command must not be able to fill the cap it is exempt from.
    InflightSlot slot(!in.estop && !in.longLived);
    return next();
}

}  // namespace

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

void webRegisterRoute(const char* path, WebMethod method, WebRequestHandler handler) {
    const http_method vendorMethod = (method == WebMethod::kPost) ? HTTP_POST : HTTP_GET;
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

#endif  // PA_WEB_BACKEND_PSYCHIC
