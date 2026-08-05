// =============================================================================
// include/web_request.h
//
// Project-owned web request/response seam (ADR 0021). Handlers take
// WebRequest and register through webRegisterRoute(); no handler names a
// vendor request type. Exactly one backend translation unit defines these
// methods per build:
//   src/web/web_request_async.cpp    -- ESPAsyncWebServer (default scaffold,
//                                       deleted by the #91 cutover)
//   src/web/web_request_psychic.cpp  -- PsychicHttp (PA_WEB_BACKEND_PSYCHIC)
//   src/native_test_stubs.cpp        -- host-test backend (PA_NATIVE_TEST_STUBS)
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>

enum class WebMethod : unsigned char { kGet, kPost };

// Produces a chunked response body a piece at a time. Writes at most capacity
// bytes of the body starting at byte offset into out and returns how many it
// wrote; returning 0 ends the body. The backend calls it repeatedly, never
// re-entrantly, and never holds more than one chunk -- so a response larger
// than any single buffer costs only that buffer.
using WebResponseBodyFiller = size_t (*)(uint8_t* out, size_t capacity, size_t offset);

class WebRequest {
   public:
    explicit WebRequest(void* backend) : backend_(backend) {
    }

    // True if the request carries the named parameter (query string for GET,
    // form body for POST).
    bool hasParam(const char* name) const;

    // Copy the named parameter's value into out (always null-terminated,
    // truncated if longer than outSize-1). Returns false if the parameter is
    // absent. Copy-out semantics: no backend string lifetime crosses the seam.
    // Callers validating length-bounded values must size out larger than the
    // longest valid value, so an over-long input still reaches validation as
    // an over-long string instead of a silently valid truncation.
    bool param(const char* name, char* out, size_t outSize) const;

    // Declared body size. For a multipart upload this counts the framing as
    // well as the file, so it is an upper bound on the payload, never its
    // exact size -- the exact size is not knowable until the body is consumed.
    size_t contentLength() const;

    // Send a complete response. A handler sends exactly once; the backend
    // owns status-line/header framing and connection semantics.
    void send(int code, const char* contentType, const char* body);

    // Send a 200 response whose body is produced incrementally, for payloads
    // no backend should have to hold whole (the action registry is ~9 KB and
    // buffering it exhausted fragmented heap on the async stack). Returns
    // false if the backend could not start the response, in which case
    // nothing was sent and the handler still owes the client an error.
    bool sendChunked(const char* contentType, WebResponseBodyFiller filler);

    // -------------------------------------------------------------------------
    // Session escape hatch (ADR 0021): the esp_http_server capabilities this
    // migration exists to obtain -- sess_ctx / free_ctx / trigger-close for
    // the SSE eviction path and the ADR 0020 response-phase deadline. Real on
    // the PsychicHttp backend; documented no-ops (null/false) on the async
    // scaffold, which has no equivalent and is deleted at the #91 cutover.
    // -------------------------------------------------------------------------
    void* sessionContext() const;
    bool setSessionContext(void* ctx, void (*freeFn)(void*));
    bool triggerClose();

   private:
    void* backend_;
};

using WebRequestHandler = void (*)(WebRequest&);

// One chunk of a streamed upload body, delivered as it arrives so no backend
// ever holds a whole image. index is this chunk's byte offset within the file,
// final marks the last chunk, and data holds file bytes only -- both backends
// strip the multipart envelope before calling this.
//
// A chunk handler must not send a response: the response belongs to the
// completion handler, which runs once after the body. It records its own
// outcome (and its own status code and body) instead of signalling through a
// return value, so a rejected upload still answers in the shape the dashboard
// parses rather than whatever the backend would substitute.
using WebUploadChunkHandler = void (*)(WebRequest& req, const char* filename, size_t index,
                                       const uint8_t* data, size_t len, bool final);

// Register a handler for path+method with the active backend's server.
// Backends require registration to happen inside their server bring-up
// (startHttpServerOnce()'s registration block / initPsychicWebServer()),
// which runs on the WiFi event callback path.
void webRegisterRoute(const char* path, WebMethod method, WebRequestHandler handler);

// Register a POST route whose body streams through onChunk, after which onDone
// produces the response. Same registration timing rules as webRegisterRoute().
void webRegisterUploadRoute(const char* path, WebUploadChunkHandler onChunk,
                            WebRequestHandler onDone);

// Every route group already ported to the seam, in one list both backends
// call. A ported route registered per-backend instead would only have to be
// forgotten in one of the two places to go missing on that stack. At the #91
// cutover this becomes the whole route table.
void webRegisterSeamRoutes();
