// =============================================================================
// src/native_test_stubs.cpp
//
// Stub definitions for extern symbols referenced by src/web/api_config.cpp
// when it is compiled as part of the [env:native] test build.
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

// dome_link.cpp is excluded from the native build. Provide a no-op stub so
// audio_chirp.cpp's UART2 ownership guard compiles and links cleanly.
// Dome never owns UART2 in native test builds.
#include "dome_link.h"
bool domeUartOwnedBy(DomeUartOwner /*owner*/) {
    return false;
}

#endif
