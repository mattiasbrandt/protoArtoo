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
#include <http_parser.h>
#include <stdio.h>

#include "../../include/api_upload.h"
#include "../../include/logging.h"
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
