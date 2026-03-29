// =============================================================================
// test/stubs/include/ESPAsyncWebServer.h
// Minimal ESPAsyncWebServer type stubs for native host builds.
// Only compiled types/symbols used by src/web/api_config.cpp.
// =============================================================================
#pragma once
#include "Arduino.h"

enum WebRequestMethod { HTTP_GET = 0, HTTP_POST = 1 };

class AsyncWebParameter {
public:
    String value() const { return String(""); }
};

class AsyncResponseStream {
public:
    size_t write(uint8_t /*c*/) { return 1; }
    size_t write(const uint8_t* /*buf*/, size_t size) { return size; }
};

class AsyncWebServerRequest {
public:
    bool hasParam(const char* /*name*/, bool /*post*/) const { return false; }
    AsyncWebParameter* getParam(const char* /*name*/, bool /*post*/) const { return nullptr; }
    void send(int /*code*/, const char* /*type*/, const char* /*body*/) {}
    void send(AsyncResponseStream* /*stream*/) {}
    AsyncResponseStream* beginResponseStream(const char* /*type*/) { return nullptr; }
};

class AsyncWebServer {
public:
    explicit AsyncWebServer(int /*port*/) {}
    template<typename Fn>
    void on(const char* /*uri*/, WebRequestMethod /*method*/, Fn /*handler*/) {}
};
