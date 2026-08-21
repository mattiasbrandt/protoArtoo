// =============================================================================
// test/stubs/include/web_request_test_backend.h
//
// Host-test backend for the WebRequest seam (ADR 0021). A test builds one of
// these, points params at a name/value table, wraps it in WebRequest, and
// asserts on the captured response after calling a handler. The WebRequest
// method definitions live in src/native_test_stubs.cpp.
// =============================================================================
#pragma once

#include <stddef.h>

struct WebRequestTestParam {
    const char* name;
    const char* value;
};

// One response header staged through WebRequest::addHeader(). Sized to the
// same limits the async backend's staging buffer uses, so a header that would
// be truncated on the device is truncated here too.
struct WebRequestTestHeader {
    char name[32] = {};
    char value[64] = {};
};

struct WebRequestTestBackend {
    const WebRequestTestParam* params = nullptr;
    size_t paramCount = 0;

    // What contentLength() reports. Upload tests set this to stand in for the
    // Content-Length of a multipart body.
    size_t contentLength = 0;

    // What body() returns: a raw (non-form) request body, such as the JSON the
    // apply cores accept under the "plain" name.
    const char* body = nullptr;

    int sentCode = 0;
    char sentContentType[64] = {};
    // Sized for the largest ported payload with headroom: the action registry
    // serializes to ~9 KB. Host-only, so a plain array beats an allocation.
    char sentBody[16384] = {};
    // Bytes the handler produced, which is what a test asserting completeness
    // should check -- sentBody alone cannot distinguish a full body from one
    // that filled the buffer exactly.
    size_t sentBodyLength = 0;
    unsigned sendCalls = 0;
    // True when the body arrived through sendChunked() rather than send().
    bool sentChunked = false;

    // Headers the handler staged, in the order it staged them. Kept after the
    // send rather than cleared with it: the assertion happens once the handler
    // has returned, so clearing at send time would leave nothing to check.
    WebRequestTestHeader headers[2];
    size_t headerCount = 0;

    // True once the handler upgraded this request to an event stream. A test
    // asserting that the client cap rejects *before* the upgrade has to be able
    // to see that the upgrade never happened -- the response code alone reads
    // the same whether the connection was refused early or torn down after.
    bool eventStreamStarted = false;
    // Makes beginEventStream() report failure, standing in for a transport that
    // could not write the stream response head.
    bool eventStreamFails = false;
};
