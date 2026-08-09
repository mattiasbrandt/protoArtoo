// =============================================================================
// src/web/web_request_psychic.cpp
//
// PsychicHttp backend for the WebRequest seam (ADR 0021). Implements the seam's
// method contract and the server bring-up/route registration. The only backend
// on the device since PsychicHttp migration; the host-test backend in
// src/native_test_stubs.cpp is the sole other definition of these methods.
//
// backend_ holds a WebRequestPsychicCtx (request + response + the esp_err_t
// the vendor callback must return). The session escape hatch reaches the
// underlying esp_http_server request: sess_ctx / free_ctx assignment and
// httpd_sess_trigger_close() -- the capabilities this migration exists to
// obtain (ADR 0020).
//
// Admission orchestration and the response-phase deadline's send override live
// in separate units (web_admission_psychic.cpp, web_response_deadline_psychic.cpp)
// beside their policy cores, so this file stays focused on the seam's contract.
// =============================================================================

#include <Arduino.h>
#include <LittleFS.h>
#include <PsychicHttp.h>
#include <esp_err.h>
#include <esp_http_server.h>
#include <lwip/sockets.h>
#include <sys/socket.h>
#include <stdio.h>
#include <string.h>

#include "../../include/api_upload.h"
#include "../../include/logging.h"
#include "../../include/web_admission.h"
#include "../../include/web_backend_psychic.h"
#include "../../include/web_event_stream.h"
#include "../../include/web_request.h"
#include "../../include/web_response_deadline.h"
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
// capability not available in the prior async backend -- the reason this
// migration to PsychicHttp exists.
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

    // Exempt this connection from the response-phase deadline before anything
    // is written on it. A stream is a response that never ends by construction,
    // so a deadline on it would be measuring the design rather than a stall --
    // the stream's own bound is PA_SSE_SEND_DEADLINE_MS, applied per event.
    webResponseDeadlineExempt(&s_responseDeadline, socket);

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
    // Configuration: nothing binds or listens until begin();
    // on()/serveStatic() only record configuration.
    s_server.config.max_open_sockets = 10;
    s_server.config.stack_size = 8192;

    // Connection Admission callbacks. Installed in the documented pre-begin()
    // window where server.config is an ordinary ESP-IDF httpd_config.
    webAdmissionRegisterCallbacks(s_server);

    // Response-phase deadline initialization. Idle until a request is admitted.
    // The send override installed per socket from the admission callback reads
    // this record, so it has to be sane before the first connection rather than
    // merely before the first request.
    webDeadlineInitialize();

    // Admission trace configuration (must be before middleware registration).
    webAdmissionTraceInit();

    // Request admission middleware (registered last, after all guards are ready).
    webAdmissionRegisterMiddleware(s_server);

    // Event stream infrastructure. The server task learns a subscribed socket
    // has gone through the close callback chain, which is why the registry
    // needs capturing and chaining here.
    s_vendorCloseFn = s_server.config.close_fn;
    s_server.config.close_fn = streamCloseCallback;
    webEventStreamRegistryInit(&s_streams);

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
