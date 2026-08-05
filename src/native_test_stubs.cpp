// =============================================================================
// src/native_test_stubs.cpp
//
// Stub definitions for extern symbols referenced by native-build source files
// (src/web/api_config.cpp, src/drivers/audio_chirp.cpp, etc.) that depend on
// hardware or task-level facilities not available in [env:native].
//
// This file is only listed in [env:native] build_src_filter.
// It is never compiled for device (ESP32) firmware builds.
// =============================================================================
#ifdef PA_NATIVE_TEST_STUBS

#include "Arduino.h"      // SerialStub (from test/stubs/include)
#include "logging.h"      // logging sink declarations
#include "robot_state.h"  // RobotState, portMUX_TYPE, QueueHandle_t

// Zero-initialised global state. Test cases populate cfg_* fields as needed
// before calling captureConfigSnapshot() or populateConfigJson().
RobotState robotState = {};
portMUX_TYPE robotStateMux = 0;

// Arduino Serial instance (referenced by code compiled in native tests)
SerialStub Serial;

// Logging sinks — no-op in native test builds
void paLogInit() {
}

void paLogLine(const char* /*line*/) {
}

void paLogLineRaw(const char* /*line*/) {
}

// millis() stub — used by failsafe gate for diagnostics
unsigned long millis() {
    return 0;
}

// NVS save stub — not under test; POST handler calls it but tests call
// populateConfigJson() directly without going through registerConfigRoutes().
bool saveConfigToNvs() {
    return true;
}

// dome_link.cpp is excluded from the native build. Provide a controllable stub
// so audio_chirp.cpp's UART2 ownership guard can be exercised in tests.
// Default: DOME_UART_NONE (no owner). Tests set g_test_dome_uart_owner in
// setUp() and reset it in tearDown().
#include "dome_link.h"
DomeUartOwner g_test_dome_uart_owner = DOME_UART_NONE;
bool domeUartOwnedBy(DomeUartOwner owner) {
    return g_test_dome_uart_owner == owner;
}
bool domeUartAcquire(DomeUartOwner requester) {
    if (requester == DOME_UART_NONE) {
        return false;
    }
    if (g_test_dome_uart_owner != DOME_UART_NONE && g_test_dome_uart_owner != requester) {
        return false;
    }
    g_test_dome_uart_owner = requester;
    return true;
}
void domeUartRelease(DomeUartOwner requester) {
    if (g_test_dome_uart_owner == requester) {
        g_test_dome_uart_owner = DOME_UART_NONE;
    }
}
bool domeConnected() { return true; }

// sequence_dispatcher.cpp needs domeQueueTx and audioQueueDollar.
// No-op stubs: routing tests use sequenceLookup() directly and do not need
// side-effect capture from these functions.
bool domeQueueTx(const char* /*cmd*/) { return true; }

#include "audio_task.h"
bool audioQueueDollar(const char* /*cmd*/, CommandSource /*src*/) { return true; }

// sequenceQueue — defined here so sequence_dispatcher.cpp can reference the
// extern without main.cpp being in the native build.
#include "sequence_dispatcher.h"
QueueHandle_t sequenceQueue = nullptr;

// -----------------------------------------------------------------------------
// WebRequest host-test backend (ADR 0021). backend_ holds a
// WebRequestTestBackend (test/stubs/include/web_request_test_backend.h):
// params come from the test's name/value table, send() captures the response
// for assertions. The session escape hatch mirrors the async scaffold's
// unsupported behavior (null/false).
// -----------------------------------------------------------------------------
#include <stdio.h>
#include <string.h>

#include "web_request.h"
#include "web_request_test_backend.h"

static const char* testParamLookup(const WebRequestTestBackend* b, const char* name) {
    for (size_t i = 0; i < b->paramCount; i++) {
        if (strcmp(b->params[i].name, name) == 0) {
            return b->params[i].value;
        }
    }
    return nullptr;
}

bool WebRequest::hasParam(const char* name) const {
    return testParamLookup(static_cast<const WebRequestTestBackend*>(backend_), name) != nullptr;
}

bool WebRequest::param(const char* name, char* out, size_t outSize) const {
    const char* value = testParamLookup(static_cast<const WebRequestTestBackend*>(backend_), name);
    if (value == nullptr || out == nullptr || outSize == 0) {
        return false;
    }
    snprintf(out, outSize, "%s", value);
    return true;
}

void WebRequest::send(int code, const char* contentType, const char* body) {
    WebRequestTestBackend* b = static_cast<WebRequestTestBackend*>(backend_);
    b->sentCode = code;
    snprintf(b->sentContentType, sizeof(b->sentContentType), "%s", contentType);
    snprintf(b->sentBody, sizeof(b->sentBody), "%s", body);
    b->sendCalls++;
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

// Route registration is a no-op on the host: native tests call the exposed
// handlers directly instead of dispatching through a server.
void webRegisterRoute(const char* /*path*/, WebMethod /*method*/, WebRequestHandler /*handler*/) {
}

#endif
