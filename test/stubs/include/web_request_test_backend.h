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

struct WebRequestTestBackend {
    const WebRequestTestParam* params = nullptr;
    size_t paramCount = 0;

    int sentCode = 0;
    char sentContentType[64] = {};
    char sentBody[256] = {};
    unsigned sendCalls = 0;
};
