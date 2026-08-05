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

enum class WebMethod : unsigned char { kGet, kPost };

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

    // Send a complete response. A handler sends exactly once; the backend
    // owns status-line/header framing and connection semantics.
    void send(int code, const char* contentType, const char* body);

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

// Register a handler for path+method with the active backend's server.
// Backends require registration to happen inside their server bring-up
// (startHttpServerOnce()'s registration block / initPsychicWebServer()),
// which runs on the WiFi event callback path.
void webRegisterRoute(const char* path, WebMethod method, WebRequestHandler handler);
