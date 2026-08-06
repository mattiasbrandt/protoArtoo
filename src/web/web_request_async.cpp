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
#include <stdlib.h>
#include <string.h>

#include "../../include/logging.h"
#include "../../include/web_request.h"
#include "../../include/web_request_async.h"

namespace {

AsyncWebServer* s_server = nullptr;

AsyncWebServerRequest* asyncReq(void* backend) {
    return static_cast<AsyncWebServerRequest*>(backend);
}

// POST handlers read form-body parameters, GET and DELETE handlers read the
// query string -- the same split the handlers expressed directly against the
// vendor API before this seam existed.
bool isPostParam(AsyncWebServerRequest* req) {
    return req->method() == HTTP_POST;
}

// Buffers a non-form request body so WebRequest::body() has something to hand
// back, because this library will not do it for us: an application/json body
// leaves _isPlainPost false and is delivered here, to the route's body handler,
// and nowhere else. The buffer hangs on the request's own _tempObject slot,
// which the vendor frees in ~AsyncWebServerRequest (WebRequest.cpp:114-115), so
// nothing here owns it past the request.
//
// Form-urlencoded and multipart bodies never reach this function -- the library
// parses them into parameters instead -- so body() reports nullptr for them,
// which is exactly what the PsychicHttp backend does for the same two cases.
// The two backends agree on "a raw body is one nobody parsed" without either
// having to special-case a content type here.
//
// Every giving-up path leaves _tempObject null rather than sending a response:
// the response belongs to the handler that runs after the body, and it reads a
// null body as a malformed one. Sending here would answer the request twice.
void accumulateRawBody(AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index,
                       size_t total, size_t maxBytes) {
    if (index == 0) {
        req->_tempObject = nullptr;
        if (total == 0 || total > maxBytes) {
            if (total > maxBytes) {
                PA_LOG_WARN("WebServer", "raw body of %u bytes exceeds %u; dropped",
                            (unsigned)total, (unsigned)maxBytes);
            }
            return;
        }
        char* buf = (char*)malloc(total + 1);
        if (buf == nullptr) {
            PA_LOG_ERROR("WebServer", "raw body buffer alloc failed (%u bytes)",
                         (unsigned)(total + 1));
            return;
        }
        // Terminated up front, so a body that never completes reads as empty
        // rather than as whatever the allocation happened to contain.
        buf[0] = '\0';
        req->_tempObject = buf;
    }

    char* buf = (char*)req->_tempObject;
    if (buf == nullptr) {
        return;
    }

    if ((index + len) > total) {
        free(buf);
        req->_tempObject = nullptr;
        return;
    }

    if (len > 0) {
        memcpy(buf + index, data, len);
    }
    if ((index + len) == total) {
        buf[total] = '\0';
    }
}

// Headers staged by addHeader() until there is a response object to hang them
// on. ESPAsyncWebServer has no header slot on the request, and send(code, type,
// body) builds and dispatches its response in one call, so the only place a
// staged header can be applied is between beginResponse() and send() below.
//
// File-scope for the same reason api_system.cpp's coredump statics are: the
// server task runs handlers one at a time under both backends, so exactly one
// request can be staging headers. Cleared as each response goes out, which is
// what keeps a header from reaching the next request.
struct StagedHeader {
    char name[32];
    char value[64];
};

StagedHeader s_stagedHeaders[WebRequest::kMaxStagedHeaders];
size_t s_stagedHeaderCount = 0;

void applyStagedHeaders(AsyncWebServerResponse* response) {
    for (size_t i = 0; i < s_stagedHeaderCount; i++) {
        response->addHeader(s_stagedHeaders[i].name, s_stagedHeaders[i].value);
    }
    s_stagedHeaderCount = 0;
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
    // The buffer accumulateRawBody() built, hung on the request's own
    // _tempObject slot (see webRegisterRoute below).
    //
    // Not getParam("plain"): that is the original me-no-dev library's
    // behaviour, and this project builds against the ESP32Async fork 3.11.2,
    // which never creates such a parameter. Checked in the vendored source
    // rather than assumed -- every _params.emplace_back() in WebRequest.cpp
    // (lines 304, 716, 845-846, 915, 921) is a query-string, form key=value,
    // or multipart parameter. An application/json body leaves _isPlainPost
    // false and goes to the route's body handler instead, so reading a "plain"
    // parameter returned nullptr for every JSON POST on this stack.
    AsyncWebServerRequest* req = asyncReq(backend_);
    const char* raw = static_cast<const char*>(req->_tempObject);
    return (raw != nullptr && raw[0] != '\0') ? raw : nullptr;
}

size_t WebRequest::contentLength() const {
    return asyncReq(backend_)->contentLength();
}

void WebRequest::addHeader(const char* name, const char* value) {
    if (s_stagedHeaderCount >= kMaxStagedHeaders) {
        PA_LOG_ERROR("WebServer", "staged header limit reached; dropping %s", name);
        return;
    }
    StagedHeader& staged = s_stagedHeaders[s_stagedHeaderCount++];
    snprintf(staged.name, sizeof(staged.name), "%s", name);
    snprintf(staged.value, sizeof(staged.value), "%s", value);
}

void WebRequest::send(int code, const char* contentType, const char* body) {
    AsyncWebServerRequest* req = asyncReq(backend_);
    if (s_stagedHeaderCount == 0) {
        // The overwhelmingly common path, kept exactly as it was: no response
        // object handled here, no extra allocation, nothing to undo.
        req->send(code, contentType, body);
        return;
    }
    AsyncWebServerResponse* response = req->beginResponse(code, contentType, body);
    if (response == nullptr) {
        // Out of heap for the response object. Drop the staging so it cannot
        // reach the next request, and fall back to the plain send, which is
        // the vendor's own allocation-failure path either way.
        s_stagedHeaderCount = 0;
        req->send(code, contentType, body);
        return;
    }
    applyStagedHeaders(response);
    req->send(response);
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
        // Nothing was sent, so the handler still owes the client an error --
        // and must not carry this response's headers into it.
        s_stagedHeaderCount = 0;
        return false;
    }
    applyStagedHeaders(response);
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

bool WebRequest::beginEventStream() {
    // AsyncEventSource claims /api/events as a whole handler and drives the
    // upgrade from inside the vendor's own request dispatch, so there is no
    // point at which a seam handler could take the connection over. This stack
    // keeps serving its stream through that handler (see startHttpServerOnce()
    // in web_server.cpp); the route is therefore not in the shared seam table,
    // and this is never reached on this build.
    return false;
}

void webRegisterRoute(const char* path, WebMethod method, WebRequestHandler handler,
                      size_t maxBodyBytes) {
    if (s_server == nullptr) {
        return;
    }
    if (method != WebMethod::kPost) {
        // GET and DELETE carry no body worth buffering, so they need no body
        // handler and their parameters come from the query string.
        const WebRequestMethod vendorMethod =
            (method == WebMethod::kDelete) ? HTTP_DELETE : HTTP_GET;
        s_server->on(path, vendorMethod, [handler](AsyncWebServerRequest* vendorReq) {
            WebRequest req(vendorReq);
            handler(req);
        });
        return;
    }

    // A POST also gets a body handler, or the library drops any body it did not
    // parse into parameters and body() has nothing to return. Registered for
    // every seam POST rather than the ones known to read a body: a route that
    // starts reading one later would otherwise fail in a way that looks like a
    // client bug.
    s_server->on(
        path, HTTP_POST,
        [handler](AsyncWebServerRequest* vendorReq) {
            WebRequest req(vendorReq);
            handler(req);
        },
        nullptr,
        [maxBodyBytes](AsyncWebServerRequest* vendorReq, uint8_t* data, size_t len, size_t index,
                       size_t total) {
            accumulateRawBody(vendorReq, data, len, index, total, maxBodyBytes);
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
