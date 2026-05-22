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
#include "logging.h"      // paLogLineRaw declaration
#include "robot_state.h"  // RobotState, portMUX_TYPE, QueueHandle_t

// Zero-initialised global state. Test cases populate cfg_* fields as needed
// before calling captureConfigSnapshot() or populateConfigJson().
RobotState robotState = {};
portMUX_TYPE robotStateMux = 0;

// Arduino Serial instance (referenced by PA_LOG_* macros via logging.h)
SerialStub Serial;

// Logging sink — no-op in native test builds
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

#endif
