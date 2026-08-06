// =============================================================================
// include/web_request.h
//
// Project-owned web request/response seam (ADR 0021). Handlers take
// WebRequest and register through webRegisterRoute(); no handler names a
// vendor request type. Exactly one backend translation unit defines these
// methods per build:
//   src/web/web_request_psychic.cpp  -- PsychicHttp, the device backend
//   src/native_test_stubs.cpp        -- host-test backend (PA_NATIVE_TEST_STUBS)
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>

// kDelete carries no body and reads its parameters from the query string, the
// same as kGet. It exists because Memory Wipe is DELETE /api/seq?name= in
// data/seq.js, and re-spelling that as a POST to fit a two-value enum would
// change a shipped client contract to save one enumerator.
enum class WebMethod : unsigned char { kGet, kPost, kDelete };

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

    // Borrow the named parameter's value for as long as the request lives,
    // without copying it. Returns nullptr when the parameter is absent.
    //
    // param() copies out and is what most handlers want: a value they can
    // validate and normalize without depending on any backend's string
    // lifetime. paramRef() exists for the apply cores (ADR 0011), whose
    // ConfigParamSource hands a borrowed const char* straight to a parser --
    // and whose raw-body parameter can carry a whole JSON document that no
    // handler-side buffer should have to be sized for.
    const char* paramRef(const char* name) const;

    // Borrow the raw request body, or nullptr when there is none. Same
    // lifetime as paramRef(). Backends differ in where a non-form body lives,
    // and this is where that difference stops.
    const char* body() const;

    // Declared body size. For a multipart upload this counts the framing as
    // well as the file, so it is an upper bound on the payload, never its
    // exact size -- the exact size is not knowable until the body is consumed.
    size_t contentLength() const;

    // Stage one response header for the send that follows, and only that send:
    // the staging clears as the response goes out, so a header can never leak
    // onto the next request. Call before send() or sendChunked().
    //
    // Status line, Content-Type and framing headers stay the backend's to set;
    // this is for the handler's own metadata, of which the group ported so far
    // has exactly one -- the dome layout's cache age. Silently more than
    // kMaxStagedHeaders staged headers would be a header lost at runtime, so
    // the overflow is logged and dropped rather than swallowed.
    static constexpr size_t kMaxStagedHeaders = 2;
    void addHeader(const char* name, const char* value);

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
    // the PsychicHttp backend; no-ops (null/false) on the host-test backend.
    // -------------------------------------------------------------------------
    void* sessionContext() const;
    bool setSessionContext(void* ctx, void (*freeFn)(void*));
    bool triggerClose();

    // Turn this request into a live event stream: write the stream response
    // head and hand the connection to the broadcaster, which owns it from then
    // on (include/web_event_stream.h). Returns false when the head could not be
    // written, or on a backend with no per-request upgrade point at all.
    //
    // A handler that gets true must not send anything else: the response is
    // open-ended by construction and there is no second reply to make.
    bool beginEventStream();

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

// Ceiling on a buffered raw request body when a route does not name its own.
// Above the largest an ordinary seam POST carries -- GET /api/config
// serializes to 1341 bytes on the device and its POST counterpart is the same
// shape -- and low enough that a hostile Content-Length cannot turn into a
// large allocation.
constexpr size_t kDefaultMaxBodyBytes = 4096;

// Register a handler for path+method with the backend's server. Registration
// has to happen inside the server bring-up (initPsychicWebServer()), which runs
// on the WiFi event callback path.
//
// maxBodyBytes bounds the raw body this route will buffer. It is per-route
// rather than global because one route legitimately carries far more than the
// rest -- POST /api/seq saves up to SEQ_FILE_MAX_BYTES (12 KB) -- and raising
// the default for everything to accommodate it would hand every other route
// the same allocation ceiling for nothing. A body over the limit is not
// buffered, so body() reports null and the handler answers 413 from
// contentLength(); see the parity rules in the ported handlers.
void webRegisterRoute(const char* path, WebMethod method, WebRequestHandler handler,
                      size_t maxBodyBytes = kDefaultMaxBodyBytes);

// Register a POST route whose body streams through onChunk, after which onDone
// produces the response. Same registration timing rules as webRegisterRoute().
void webRegisterUploadRoute(const char* path, WebUploadChunkHandler onChunk,
                            WebRequestHandler onDone);

// The whole route table, in one list the backend calls from its bring-up.
void webRegisterSeamRoutes();
