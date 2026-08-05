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

#include "../../include/api_identity.h"
#include "../../include/logging.h"
#include "../../include/web_request.h"
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

void WebRequest::send(int code, const char* contentType, const char* body) {
    WebRequestPsychicCtx* ctx = psychicCtx(backend_);
    ctx->result = ctx->resp->send(code, contentType, body);
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

void initPsychicWebServer() {
    // Verified live on the #72 prototype: nothing binds or listens until
    // begin(); on()/serveStatic() only record configuration.
    s_server.config.max_open_sockets = 10;
    s_server.config.stack_size = 8192;

    registerIdentityRoutes();

    if (!LittleFS.begin(true)) {
        PA_LOG_WARN(TAG, "LittleFS mount failed; static serving unavailable");
    }
    s_server.serveStatic("/", LittleFS, "/")->setDefaultFile("index.html")->setCacheControl("no-cache");

    esp_err_t err = s_server.begin();
    if (err != ESP_OK) {
        PA_LOG_WARN(TAG, "PsychicHttp server.begin() failed: %s", esp_err_to_name(err));
        return;
    }
    PA_LOG_INFO(TAG, "PsychicHttp server listening on port 80");
}

#endif  // PA_WEB_BACKEND_PSYCHIC
