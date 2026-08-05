// =============================================================================
// src/web/web_request_async.cpp
//
// ESPAsyncWebServer backend for the WebRequest seam (ADR 0021). The default
// device backend while routes convert group by group; the #91 cutover deletes
// this file together with ESPAsyncWebServer/AsyncTCP.
//
// backend_ holds the AsyncWebServerRequest* directly. The session escape
// hatch is unsupported here by design: the async stack has no sess_ctx /
// free_ctx / trigger-close equivalent (ADR 0020), which is why it is being
// replaced.
// =============================================================================
#if !defined(PA_WEB_BACKEND_PSYCHIC) && !defined(PA_NATIVE_TEST_STUBS)

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <stdio.h>

#include "../../include/web_request.h"
#include "../../include/web_request_async.h"

namespace {

AsyncWebServer* s_server = nullptr;

AsyncWebServerRequest* asyncReq(void* backend) {
    return static_cast<AsyncWebServerRequest*>(backend);
}

// POST handlers read form-body parameters, GET handlers read the query
// string -- the same split the handlers expressed directly against the
// vendor API before this seam existed.
bool isPostParam(AsyncWebServerRequest* req) {
    return req->method() == HTTP_POST;
}

}  // namespace

void webRequestAsyncAttach(AsyncWebServer& server) {
    s_server = &server;
}

bool WebRequest::hasParam(const char* name) const {
    AsyncWebServerRequest* req = asyncReq(backend_);
    return req->hasParam(name, isPostParam(req));
}

bool WebRequest::param(const char* name, char* out, size_t outSize) const {
    AsyncWebServerRequest* req = asyncReq(backend_);
    const AsyncWebParameter* p = req->getParam(name, isPostParam(req));
    if (p == nullptr || out == nullptr || outSize == 0) {
        return false;
    }
    snprintf(out, outSize, "%s", p->value().c_str());
    return true;
}

const char* WebRequest::paramRef(const char* name) const {
    AsyncWebServerRequest* req = asyncReq(backend_);
    const AsyncWebParameter* p = req->getParam(name, isPostParam(req));
    // value() returns a reference to the parameter's own String, which the
    // request owns until it completes -- so this pointer outlives the call.
    return p != nullptr ? p->value().c_str() : nullptr;
}

const char* WebRequest::body() const {
    // ESPAsyncWebServer surfaces a non-form request body as a parameter named
    // "plain" rather than exposing the buffer, so that is where the body is.
    AsyncWebServerRequest* req = asyncReq(backend_);
    const AsyncWebParameter* p = req->getParam("plain", true);
    return p != nullptr ? p->value().c_str() : nullptr;
}

size_t WebRequest::contentLength() const {
    return asyncReq(backend_)->contentLength();
}

void WebRequest::send(int code, const char* contentType, const char* body) {
    asyncReq(backend_)->send(code, contentType, body);
}

bool WebRequest::sendChunked(const char* contentType, WebResponseBodyFiller filler) {
    // AwsResponseFiller has the seam filler's exact shape, so the body is
    // generated straight into AsyncTCP's outgoing chunk with nothing buffered
    // in between. beginChunkedResponse() is 200-only here, which is why the
    // seam declares chunked sends as 200-only rather than inventing a status
    // argument one backend would have to fake.
    AsyncWebServerRequest* req = asyncReq(backend_);
    AsyncWebServerResponse* response = req->beginChunkedResponse(contentType, filler);
    if (response == nullptr) {
        return false;
    }
    req->send(response);
    return true;
}

void* WebRequest::sessionContext() const {
    return nullptr;
}

bool WebRequest::setSessionContext(void* /*ctx*/, void (* /*freeFn*/)(void*)) {
    return false;
}

bool WebRequest::triggerClose() {
    return false;
}

void webRegisterRoute(const char* path, WebMethod method, WebRequestHandler handler) {
    if (s_server == nullptr) {
        return;
    }
    const WebRequestMethod vendorMethod = (method == WebMethod::kPost) ? HTTP_POST : HTTP_GET;
    s_server->on(path, vendorMethod, [handler](AsyncWebServerRequest* vendorReq) {
        WebRequest req(vendorReq);
        handler(req);
    });
}

void webRegisterUploadRoute(const char* path, WebUploadChunkHandler onChunk,
                            WebRequestHandler onDone) {
    if (s_server == nullptr) {
        return;
    }
    s_server->on(
        path, HTTP_POST,
        [onDone](AsyncWebServerRequest* vendorReq) {
            WebRequest req(vendorReq);
            onDone(req);
        },
        [onChunk](AsyncWebServerRequest* vendorReq, const String& filename, size_t index,
                  uint8_t* data, size_t len, bool final) {
            WebRequest req(vendorReq);
            onChunk(req, filename.c_str(), index, data, len, final);
        });
}

#endif  // !PA_WEB_BACKEND_PSYCHIC && !PA_NATIVE_TEST_STUBS
